// ============================================================================
// ファイルの役割: 作業報告用の「自動撮影モード」と「処理の重さの計測モード」を実装している（説明はCaptureMode.hを参照）。
// 主な技術: Media Foundation（Sink WriterでH.264へ符号化）、WIC（PNG保存）、
//           バックバッファの読み戻し（ステージングテクスチャ）、キーフレームの補間
// ============================================================================

#include "CaptureMode.h"

#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "GpuTimer.h"
#include "Locker.h"
#include "Player.h"

#include <SimpleMath.h>
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <dxgi.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX::SimpleMath;

namespace
{
    // ------------------------------------------------------------------------
    // 道順（キーフレーム）。時刻と時刻の間は、位置と向きを直線で補間している。
    // Shotに名前があるキーフレームの時刻で、スクリーンショットを1枚保存している。
    // ------------------------------------------------------------------------
    struct Keyframe
    {
        // 道順の時刻（シーンが始まってからの秒数）
        float Time;
        Vector3 Position;   // 足元の位置（yは床の高さ）
        float Yaw;          // 水平の向き（+Zが0、+Xがπ/2）
        float Pitch;        // 上下の向き（負で下を向く）
        const wchar_t* Shot;  // スクリーンショットのファイル名（nullptrなら撮らない）
        bool Hide;          // 2面のロッカーに隠れている区間
    };

    // 円周率と、床の高さ（プレイヤーの足元のy）
    constexpr float Pi = 3.14159265f;
    constexpr float FloorY = -99.0f;

    // (fromX, fromZ)から(toX, toZ)の方を向くヨー角を求めている
    float YawToward(float fromX, float fromZ, float toX, float toZ)
    {
        return std::atan2(toX - fromX, toZ - fromZ);
    }

    // 1面: 開始地点 → 中央ホール → 左の倉庫 → 暗証番号の扉 → ループ廊下 → 西棟（浸水した機械室）
    const std::vector<Keyframe> Stage1Tour =
    {
        { 0.0f, Vector3(0.0f, FloorY, -120.0f), Pi, -0.12f, nullptr, false },
        { 4.5f, Vector3(0.0f, FloorY, -128.0f), Pi, -0.12f, L"01_1面_開始地点_照らすと浮かぶ壁の文字", false },
        { 8.0f, Vector3(0.0f, FloorY, -90.0f), 0.0f, 0.0f, nullptr, false },
        { 13.0f, Vector3(0.0f, FloorY, -10.0f), YawToward(0.0f, -10.0f, -180.0f, 35.0f), 0.0f, nullptr, false },
        { 15.5f, Vector3(-10.0f, FloorY, 0.0f), YawToward(-10.0f, 0.0f, -180.0f, 35.0f), -0.05f, L"02_1面_中央ホールと配電盤", false },
        { 17.5f, Vector3(-10.0f, FloorY, 0.0f), YawToward(-10.0f, 0.0f, -180.0f, 35.0f) + 0.6f, 0.95f, L"02b_1面_中央ホールの天井", false },
        { 19.5f, Vector3(-60.0f, FloorY, -88.0f), YawToward(-60.0f, -88.0f, -150.0f, -140.0f), 0.0f, nullptr, false },
        { 24.0f, Vector3(-150.0f, FloorY, -110.0f), 0.0f, -0.26f, L"03_1面_左の倉庫の壁の文字", false },
        { 28.0f, Vector3(-60.0f, FloorY, -30.0f), YawToward(-60.0f, -30.0f, 0.0f, 40.0f), 0.0f, nullptr, false },
        { 32.0f, Vector3(0.0f, FloorY, 70.0f), 0.0f, 0.0f, nullptr, false },
        { 36.5f, Vector3(12.0f, FloorY, 112.0f), Pi * 0.5f, -0.05f, L"04_1面_暗証番号の扉", false },
        { 41.0f, Vector3(0.0f, FloorY, 185.0f), 0.0f, 0.0f, nullptr, false },
        { 46.5f, Vector3(0.0f, FloorY, 240.0f), 0.0f, -0.10f, L"05_1面_ループ廊下の突き当たり", false },
        { 50.0f, Vector3(0.0f, FloorY, 245.0f), 0.0f, -0.10f, nullptr, false },
        // 西棟（浸水した機械室）。入口の内側へ場面を切り替え、仕切りの切れ目を通って奥のポンプ室まで進んでいる。
        // 50.0秒と50.05秒の間はほぼ0秒なので、ループ廊下から西棟へ一瞬で移っている（場面の切り替え）
        { 50.05f, Vector3(-235.0f, FloorY, -85.0f), YawToward(-235.0f, -85.0f, -370.0f, -20.0f), -0.28f, nullptr, false },
        { 52.5f, Vector3(-237.0f, FloorY, -82.0f), YawToward(-237.0f, -82.0f, -370.0f, -20.0f), -0.28f, L"05b_1面_西棟の浸水した通路", false },
        { 54.0f, Vector3(-240.0f, FloorY, 25.0f), 0.0f, -0.10f, nullptr, false },
        { 56.5f, Vector3(-245.0f, FloorY, 40.0f), -Pi * 0.5f, -0.10f, nullptr, false },
        { 60.0f, Vector3(-355.0f, FloorY, 40.0f), -Pi * 0.5f, -0.05f, nullptr, false },
        { 62.0f, Vector3(-360.0f, FloorY, 60.0f), 0.0f, -0.05f, nullptr, false },
        { 64.5f, Vector3(-360.0f, FloorY, 115.0f), Pi * 0.5f, -0.05f, nullptr, false },
        { 68.0f, Vector3(-250.0f, FloorY, 120.0f), Pi * 0.5f, -0.05f, nullptr, false },
        { 70.0f, Vector3(-240.0f, FloorY, 140.0f), 0.0f, -0.05f, nullptr, false },
        { 72.5f, Vector3(-240.0f, FloorY, 195.0f), -Pi * 0.4f, -0.10f, nullptr, false },
        { 75.0f, Vector3(-280.0f, FloorY, 210.0f), YawToward(-280.0f, 210.0f, -345.0f, 235.0f), -0.15f, L"05c_1面_西棟のポンプ室", false },
        { 77.0f, Vector3(-285.0f, FloorY, 212.0f), YawToward(-285.0f, 212.0f, -345.0f, 235.0f), -0.15f, nullptr, false },
    };

    // 2面: ループ廊下 → 時計 → 肖像画 → ロッカー（中に隠れる）→ 奥の扉
    const std::vector<Keyframe> Stage2Tour =
    {
        { 0.0f, Vector3(0.0f, FloorY, -125.0f), 0.0f, 0.0f, nullptr, false },
        { 4.5f, Vector3(0.0f, FloorY, -118.0f), 0.0f, 0.0f, L"06_2面_ループ廊下", false },
        { 9.0f, Vector3(0.0f, FloorY, -45.0f), 0.0f, 0.0f, nullptr, false },
        { 12.0f, Vector3(0.0f, FloorY, -25.0f), -Pi * 0.5f, -0.10f, L"07_2面_時計", false },
        { 15.5f, Vector3(0.0f, FloorY, -25.0f), Pi * 0.5f, -0.10f, L"08_2面_肖像画", false },
        { 19.5f, Vector3(14.0f, FloorY, 40.0f), Pi * 0.5f, -0.12f, L"09_2面_ロッカー", false },
        { 20.5f, Vector3(14.0f, FloorY, 40.0f), Pi * 0.5f, -0.12f, nullptr, true },
        { 24.5f, Vector3(14.0f, FloorY, 40.0f), -Pi * 0.5f, 0.0f, L"10_2面_ロッカーの中から覗く", true },
        { 27.0f, Vector3(14.0f, FloorY, 40.0f), -Pi * 0.5f, 0.0f, nullptr, false },
        { 33.0f, Vector3(0.0f, FloorY, 108.0f), 0.0f, 0.0f, L"11_2面_奥の扉", false },
        { 36.0f, Vector3(0.0f, FloorY, 112.0f), 0.0f, 0.0f, nullptr, false },
    };

    // 角度の補間。差を-π〜πに直してから補間し、遠回りの方向へ回らないようにしている
    float LerpAngle(float from, float to, float t)
    {
        float delta = std::fmod(to - from, Pi * 2.0f);
        if (delta > Pi) delta -= Pi * 2.0f;
        if (delta < -Pi) delta += Pi * 2.0f;
        return from + delta * t;
    }

    // ------------------------------------------------------------------------
    // 動画の書き出し（Media Foundation の Sink Writer で H.264 / MP4 に符号化している）
    // ------------------------------------------------------------------------
    class VideoWriter
    {
    public:
        // MP4ファイルを作り、出力（H.264、8Mbps）と入力（RGB32の非圧縮画像）の形式を設定している
        bool Open(const std::wstring& path, UINT width, UINT height, UINT fps)
        {
            if (FAILED(MFStartup(MF_VERSION)))
            {
                return false;
            }
            m_Started = true;
            m_Width = width;
            m_Height = height;
            // 1フレームの長さ（Media Foundationの時間の単位は100ナノ秒）
            m_FrameDuration = 10'000'000LL / fps;

            if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &m_Writer)))
            {
                return false;
            }

            // 出力の形式：H.264、プログレッシブ、指定した大きさとフレームレート
            ComPtr<IMFMediaType> outputType;
            MFCreateMediaType(&outputType);
            outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
            outputType->SetUINT32(MF_MT_AVG_BITRATE, 8'000'000);
            outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width, height);
            MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, fps, 1);
            MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            if (FAILED(m_Writer->AddStream(outputType.Get(), &m_Stream)))
            {
                return false;
            }

            // 入力の形式：毎フレーム渡す非圧縮のRGB32画像
            ComPtr<IMFMediaType> inputType;
            MFCreateMediaType(&inputType);
            inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width, height);
            MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, fps, 1);
            MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            if (FAILED(m_Writer->SetInputMediaType(m_Stream, inputType.Get(), nullptr)) ||
                FAILED(m_Writer->BeginWriting()))
            {
                return false;
            }
            m_Open = true;
            return true;
        }

        // rgbaは上の行から並んだRGBA。RGB32（BGRA、下の行から並ぶ）へ詰め替えてから渡している。
        void WriteFrame(const BYTE* rgba, UINT rowPitch)
        {
            if (!m_Open)
            {
                return;
            }
            const DWORD size = m_Width * m_Height * 4;
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(MFCreateMemoryBuffer(size, &buffer)))
            {
                return;
            }
            // 1行ずつ上下を逆にしながら、RとBを入れ替えてコピーしている（透明度は使わないので不透明）
            BYTE* destination = nullptr;
            buffer->Lock(&destination, nullptr, nullptr);
            for (UINT y = 0; y < m_Height; ++y)
            {
                const BYTE* sourceRow = rgba + static_cast<size_t>(y) * rowPitch;
                BYTE* destinationRow = destination +
                    static_cast<size_t>(m_Height - 1 - y) * m_Width * 4;
                for (UINT x = 0; x < m_Width; ++x)
                {
                    destinationRow[x * 4 + 0] = sourceRow[x * 4 + 2];
                    destinationRow[x * 4 + 1] = sourceRow[x * 4 + 1];
                    destinationRow[x * 4 + 2] = sourceRow[x * 4 + 0];
                    destinationRow[x * 4 + 3] = 255;
                }
            }
            buffer->Unlock();
            buffer->SetCurrentLength(size);

            // 画像を1枚のサンプルにして、表示する時刻と長さを付けて書き込んでいる
            ComPtr<IMFSample> sample;
            MFCreateSample(&sample);
            sample->AddBuffer(buffer.Get());
            sample->SetSampleTime(m_Time);
            sample->SetSampleDuration(m_FrameDuration);
            m_Writer->WriteSample(m_Stream, sample.Get());
            m_Time += m_FrameDuration;
        }

        // 書き込みを終えてファイルを閉じ、Media Foundationを終了している
        void Close()
        {
            if (m_Open)
            {
                m_Writer->Finalize();
                m_Open = false;
            }
            m_Writer.Reset();
            if (m_Started)
            {
                MFShutdown();
                m_Started = false;
            }
        }

    private:
        // 書き込み先、ストリーム番号、画像の大きさ、次のフレームの時刻、1フレームの長さ、開いているか、MFStartupを呼んだか
        ComPtr<IMFSinkWriter> m_Writer;
        DWORD m_Stream = 0;
        UINT m_Width = 0;
        UINT m_Height = 0;
        LONGLONG m_Time = 0;
        LONGLONG m_FrameDuration = 0;
        bool m_Open = false;
        bool m_Started = false;
    };

    // RGBA（上の行から並ぶ）の画像をPNGで保存している。
    bool SavePng(const std::wstring& path, const BYTE* rgba, UINT rowPitch, UINT width, UINT height)
    {
        // WICのファクトリーを作り、ファイルへ書き出すPNGのエンコーダーと1枚分のフレームを準備している
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))))
        {
            return false;
        }
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
            FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
            FAILED(frame->Initialize(nullptr)) ||
            FAILED(frame->SetSize(width, height)))
        {
            return false;
        }
        // PNGの書き出しはBGRAの並びを使っている（RGBAのまま渡すと赤と青が入れ替わるため、並べ替えてから渡している）。
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        frame->SetPixelFormat(&format);
        std::vector<BYTE> bgra(static_cast<size_t>(width) * height * 4);
        for (UINT y = 0; y < height; ++y)
        {
            const BYTE* source = rgba + static_cast<size_t>(y) * rowPitch;
            BYTE* destination = bgra.data() + static_cast<size_t>(y) * width * 4;
            for (UINT x = 0; x < width; ++x)
            {
                destination[x * 4 + 0] = source[x * 4 + 2];
                destination[x * 4 + 1] = source[x * 4 + 1];
                destination[x * 4 + 2] = source[x * 4 + 0];
                destination[x * 4 + 3] = 255;   // 透明度は使わないので不透明にしている
            }
        }
        if (FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data())) ||
            FAILED(frame->Commit()) ||
            FAILED(encoder->Commit()))
        {
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------------
    // 撮影の状態（このファイルの中だけで使っている）
    // ------------------------------------------------------------------------
    // 撮影モードが有効か、保存先のフォルダ、動画、動画を開こうとしたか、画面の読み戻し用テクスチャ
    bool g_Active = false;
    std::filesystem::path g_OutputDirectory;
    VideoWriter g_Video;
    bool g_VideoTried = false;
    ComPtr<ID3D11Texture2D> g_Staging;
    // 今見て回っているシーン、そのシーンの道順の経過秒、次に調べるキーフレーム、次に描いた画面で保存する名前、見て回り終えたか
    SceneName g_TourScene = SceneName::Title;
    float g_TourTime = 0.0f;
    size_t g_NextShot = 0;
    const wchar_t* g_PendingShot = nullptr;
    bool g_Finished = false;
    bool g_Recording = false;   // 1面・2面を見て回っている間だけ動画に書き出している
    // 動画に書き出したフレーム数
    int g_FramesWritten = 0;
    // 見て回り終えてから終了するまでの残りフレーム数（最後のスクリーンショットを確実に保存するため）。
    int g_QuitCountdown = -1;

    // 撮影の経過を capture_log.txt に残している（うまく撮れなかったときの確認用）。
    void Log(const std::string& message)
    {
        std::ofstream file(g_OutputDirectory / L"capture_log.txt", std::ios::app);
        file << "[frame " << g_FramesWritten << "] " << message << "\n";
    }

    // ログに書くシーンの名前
    const char* SceneLabel(SceneName scene)
    {
        switch (scene)
        {
        case SceneName::Title: return "Title";
        case SceneName::Stage: return "Stage1";
        case SceneName::Stage2: return "Stage2";
        case SceneName::Result: return "Result";
        }
        return "?";
    }

    // シーンに対応する道順を返している（タイトルとリザルトには道順がない）
    const std::vector<Keyframe>* GetTour(SceneName scene)
    {
        if (scene == SceneName::Stage) return &Stage1Tour;
        if (scene == SceneName::Stage2) return &Stage2Tour;
        return nullptr;
    }

    // 道順の中で、指定した時刻の位置と向きを、前後のキーフレームから補間して求めている
    Keyframe Sample(const std::vector<Keyframe>& tour, float time)
    {
        // 最初のキーフレームより前は最初の位置、最後より後は最後の位置のままにしている
        if (time <= tour.front().Time)
        {
            return tour.front();
        }
        for (size_t index = 1; index < tour.size(); ++index)
        {
            const Keyframe& next = tour[index];
            if (time > next.Time)
            {
                continue;
            }
            const Keyframe& previous = tour[index - 1];
            const float span = (std::max)(next.Time - previous.Time, 0.001f);
            const float t = (time - previous.Time) / span;
            Keyframe result = previous;
            result.Position = Vector3::Lerp(previous.Position, next.Position, t);
            result.Yaw = LerpAngle(previous.Yaw, next.Yaw, t);
            result.Pitch = previous.Pitch + (next.Pitch - previous.Pitch) * t;
            // 隠れる区間かどうかは補間せず、前のキーフレームの値を使っている
            result.Hide = previous.Hide;
            return result;
        }
        return tour.back();
    }

    // ------------------------------------------------------------------------
    // 処理の重さの計測（--benchmark）
    // 1面の決まった地点ごとに「ライトOFF → ライトON」の順で同じ景色を測り、差を比べている。
    // ------------------------------------------------------------------------
    // 計測する地点（名前・立つ位置・向き）
    struct BenchmarkSpot
    {
        const char* Name;
        Vector3 Position;
        float Yaw;
        float Pitch;
    };

    const std::vector<BenchmarkSpot> BenchmarkSpots =
    {
        { "開始地点（壁の文字の前）", Vector3(0.0f, FloorY, -128.0f), Pi, -0.12f },
        { "開始地点から廊下の奥", Vector3(0.0f, FloorY, -110.0f), 0.0f, 0.0f },
        { "中央ホール", Vector3(-10.0f, FloorY, 0.0f), YawToward(-10.0f, 0.0f, -180.0f, 35.0f), -0.05f },
        { "左の倉庫", Vector3(-150.0f, FloorY, -110.0f), 0.0f, -0.26f },
        { "暗証番号の扉", Vector3(12.0f, FloorY, 112.0f), Pi * 0.5f, -0.05f },
        { "ループ廊下", Vector3(0.0f, FloorY, 185.0f), 0.0f, 0.0f },
        // 床一面が水に浸かった西棟。水面の反射（平面反射）が画面の広い範囲に出る場所。
        { "西棟（浸水した通路）", Vector3(-237.0f, FloorY, -82.0f), YawToward(-237.0f, -82.0f, -370.0f, -20.0f), -0.28f },
    };

    // 各段階のフレーム数。切り替えた直後はGPU時間の結果が数フレーム遅れて届くため、最初の45フレームは捨てている。
    constexpr int BenchmarkWarmupFrames = 45;
    constexpr int BenchmarkMeasureFrames = 180;

    // 1つの条件（地点×ライトの状態）で集めた値。
    struct BenchmarkSamples
    {
        std::vector<double> FrameMs;    // 前のフレームからの実際の経過時間
        std::vector<double> CpuMs;      // FrameMsからPresentの待ち時間を引いたもの（CPU側で使った時間の目安）
        // 描画の段階ごとのGPU時間の合計と、取れた回数（平均を出すのに使っている）
        std::array<double, static_cast<size_t>(GpuPass::Count)> GpuSum{};
        std::array<int, static_cast<size_t>(GpuPass::Count)> GpuCount{};
    };

    // 1つの地点の、ライトOFFとライトONの結果
    struct BenchmarkResult
    {
        const BenchmarkSpot* Spot;
        BenchmarkSamples Off;
        BenchmarkSamples On;
    };

    // 計測モードか、今の地点の番号、地点の中での経過フレーム、結果
    bool g_Benchmark = false;
    size_t g_BenchmarkSpot = 0;
    int g_BenchmarkFrame = 0;           // 地点の中での経過フレーム
    std::vector<BenchmarkResult> g_BenchmarkResults;
    // 前のフレームの時刻、直前のPresentにかかった時間、GPUの名前
    std::chrono::steady_clock::time_point g_LastFrameTime;
    bool g_HasLastFrameTime = false;
    double g_LastPresentMs = 0.0;
    std::string g_AdapterName;

    // 平均値を求めている
    double Average(const std::vector<double>& values)
    {
        if (values.empty())
        {
            return 0.0;
        }
        double sum = 0.0;
        for (double value : values)
        {
            sum += value;
        }
        return sum / static_cast<double>(values.size());
    }

    // 遅い方から(1 - ratio)の位置の値を求めている（0.99なら遅い方から1%）。かくつきの目安に使っている。
    double Percentile(std::vector<double> values, double ratio)
    {
        if (values.empty())
        {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const size_t index = (std::min)(values.size() - 1,
            static_cast<size_t>(ratio * static_cast<double>(values.size() - 1) + 0.5));
        return values[index];
    }

    // ある描画段階のGPU時間の平均を求めている（一度も取れていなければ0）
    double GpuAverage(const BenchmarkSamples& samples, GpuPass pass)
    {
        const size_t index = static_cast<size_t>(pass);
        return samples.GpuCount[index] == 0
            ? 0.0
            : samples.GpuSum[index] / samples.GpuCount[index];
    }

    // GPUの名前を記録している（結果を見る人が、どの環境の数字か分かるように）。
    void RecordAdapterName(ID3D11DeviceContext* context)
    {
        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC description{};
        if (FAILED(device.As(&dxgiDevice)) ||
            FAILED(dxgiDevice->GetAdapter(&adapter)) ||
            FAILED(adapter->GetDesc(&description)))
        {
            g_AdapterName = "不明";
            return;
        }
        // GPUの名前はワイド文字なので、ファイルに書くためUTF-8へ変換している
        const int size = WideCharToMultiByte(CP_UTF8, 0, description.Description, -1,
            nullptr, 0, nullptr, nullptr);
        std::string name(static_cast<size_t>((std::max)(size - 1, 0)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name.data(), size,
            nullptr, nullptr);
        g_AdapterName = name;
    }

    // 全地点の結果を表にして benchmark_result.txt に書き出している
    void WriteBenchmarkResult()
    {
        Core::Game* game = Core::Game::GetInstance();
        std::ostringstream text;
        text << std::fixed << std::setprecision(2);
        // 先頭に、画面の大きさ・描画解像度・エフェクト設定・GPUの名前を書いている
        text << "処理の重さの計測結果（1面・ライトOFFとONの比較）\n";
        text << "画面: " << Application::GetWindowWidth() << "x" << Application::GetWindowHeight()
             << "  描画解像度: " << Application::GetWidth() << "x" << Application::GetHeight()
             << "  垂直同期: なし  エフェクト設定: " << game->GetEffectLevel()
             << "  GPU: " << g_AdapterName << "\n";
        text << "各条件 " << BenchmarkMeasureFrames << " フレームの平均（ms）。"
                "1%遅は遅い方から1%のフレーム時間で、かくつきの目安です。\n\n";

        // GPU時間を表に出す描画段階と、その見出し
        const std::pair<const char*, GpuPass> passes[] =
        {
            { "GPU合計", GpuPass::Total },
            { "影", GpuPass::Shadow },
            { "水面反射", GpuPass::Reflection },
            { "本描画", GpuPass::MainScene },
            { "ブルーム", GpuPass::Bloom },
            { "画面効果", GpuPass::PostProcess },
        };

        for (const BenchmarkResult& result : g_BenchmarkResults)
        {
            text << "■ " << result.Spot->Name << "\n";
            text << "  項目        ライトOFF   ライトON       差\n";
            // 1行分（項目名・ライトOFF・ライトON・差）を書く関数。差には+-の符号を付けている
            const auto row = [&text](const char* label, double off, double on)
            {
                text << "  " << label << "\t" << std::setw(9) << off << "   " << std::setw(9) << on
                     << "   " << std::showpos << std::setw(8) << (on - off) << std::noshowpos << "\n";
            };
            const double offFrame = Average(result.Off.FrameMs);
            const double onFrame = Average(result.On.FrameMs);
            row("フレーム", offFrame, onFrame);
            row("1%遅", Percentile(result.Off.FrameMs, 0.99), Percentile(result.On.FrameMs, 0.99));
            row("CPU", Average(result.Off.CpuMs), Average(result.On.CpuMs));
            for (const auto& [label, pass] : passes)
            {
                row(label, GpuAverage(result.Off, pass), GpuAverage(result.On, pass));
            }
            row("FPS換算", offFrame > 0.0 ? 1000.0 / offFrame : 0.0,
                onFrame > 0.0 ? 1000.0 / onFrame : 0.0);
            text << "\n";
        }

        std::ofstream file(g_OutputDirectory / L"benchmark_result.txt", std::ios::binary);
        const std::string body = text.str();
        file.write("\xEF\xBB\xBF", 3);   // メモ帳で文字化けしないよう、BOM付きUTF-8にしている
        file.write(body.data(), static_cast<std::streamsize>(body.size()));
    }

    // 計測モードの1フレーム分。地点に立たせ、ライトを切り替えながら値を集めている。
    void UpdateBenchmark(Core::Game* game)
    {
        // 前のフレームからの経過時間を測っている（最初の1回は0）
        const auto now = std::chrono::steady_clock::now();
        const double frameMs = g_HasLastFrameTime
            ? std::chrono::duration<double, std::milli>(now - g_LastFrameTime).count()
            : 0.0;
        g_LastFrameTime = now;
        g_HasLastFrameTime = true;

        // タイトルからの切り替えが終わり、1面が始まるまでは何もしていない
        if (game->GetCurrentSceneName() != SceneName::Stage)
        {
            return;
        }
        Player* player = game->GetObj<Player>("Player");
        Camera* camera = game->GetCamera();
        if (player == nullptr || camera == nullptr)
        {
            return;
        }

        // 全地点を測り終えたら結果を書き出し、数フレーム後に終了している
        if (g_BenchmarkSpot >= BenchmarkSpots.size())
        {
            WriteBenchmarkResult();
            Log("benchmark finished");
            g_Finished = true;
            g_QuitCountdown = 3;
            return;
        }
        // 新しい地点に来たら、結果を入れる場所を用意している
        if (g_BenchmarkResults.size() <= g_BenchmarkSpot)
        {
            g_BenchmarkResults.push_back({ &BenchmarkSpots[g_BenchmarkSpot], {}, {} });
            Log("benchmark spot " + std::to_string(g_BenchmarkSpot));
        }

        // 段階: [OFFの捨てる区間][OFFの計測][ONの捨てる区間][ONの計測]
        const int phaseLength = BenchmarkWarmupFrames + BenchmarkMeasureFrames;
        const bool lightOn = g_BenchmarkFrame >= phaseLength;
        const int frameInPhase = g_BenchmarkFrame % phaseLength;
        // ここで測れるのは直前のフレームの値。条件を切り替えた直後のフレームは捨てる区間に入るようにしている。
        if (frameInPhase >= BenchmarkWarmupFrames && frameMs > 0.0)
        {
            BenchmarkResult& result = g_BenchmarkResults[g_BenchmarkSpot];
            BenchmarkSamples& samples = lightOn ? result.On : result.Off;
            samples.FrameMs.push_back(frameMs);
            samples.CpuMs.push_back((std::max)(frameMs - g_LastPresentMs, 0.0));
            // 結果が届いている描画段階だけ、GPU時間を足している
            if (GpuTimer* timer = game->GetGpuTimer())
            {
                for (size_t pass = 0; pass < static_cast<size_t>(GpuPass::Count); ++pass)
                {
                    const GpuTiming timing = timer->GetTiming(static_cast<GpuPass>(pass));
                    if (timing.Status == GpuTimingStatus::Available)
                    {
                        samples.GpuSum[pass] += timing.Milliseconds;
                        ++samples.GpuCount[pass];
                    }
                }
            }
        }

        // 毎フレーム、地点の位置と向きに立たせ直し、ライトの状態を決めている
        const BenchmarkSpot& spot = BenchmarkSpots[g_BenchmarkSpot];
        player->SetPosition(spot.Position);
        player->SetFlashlightOn(lightOn);
        player->AddBattery(100.0f);    // 電池切れの点滅で結果が揺れないよう、電池を満タンに保っている
        camera->SetCameraDirection(spot.Yaw);
        camera->SetCameraPitch(spot.Pitch);

        // OFFとONの両方を測り終えたら、次の地点へ進んでいる
        if (++g_BenchmarkFrame >= phaseLength * 2)
        {
            g_BenchmarkFrame = 0;
            ++g_BenchmarkSpot;
        }
    }
}

namespace Tools::CaptureMode
{
    // 起動オプションを読み、--capture か --benchmark の次の引数を保存先のフォルダにしている
    void ConfigureFromCommandLine()
    {
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
        {
            return;
        }
        for (int index = 1; index + 1 < argumentCount; ++index)
        {
            const std::wstring option = arguments[index];
            if (option == L"--capture" || option == L"--benchmark")
            {
                g_OutputDirectory = arguments[index + 1];
                std::error_code error;
                // 保存先のフォルダを作り、作れたときだけ有効にしている
                std::filesystem::create_directories(g_OutputDirectory, error);
                g_Active = !error;
                g_Benchmark = g_Active && option == L"--benchmark";
                break;
            }
        }
        LocalFree(arguments);
    }

    // 自動撮影モード（または計測モード）が有効かを返している
    bool IsActive()
    {
        return g_Active;
    }

    // 計測モードかどうかを返している
    bool IsBenchmark()
    {
        return g_Benchmark;
    }

    // Presentにかかった時間を記録している（CPU時間を求めるのに使っている）
    void OnPresentTimed(double milliseconds)
    {
        g_LastPresentMs = milliseconds;
    }

    // 1フレーム分、道順に沿ってプレイヤーと視点を動かしている（計測モードでは地点に立たせている）
    void UpdateBeforeObjects()
    {
        if (!g_Active)
        {
            return;
        }
        if (g_Finished)
        {
            // 最後のスクリーンショットを書き出す画面を描いてから終了している。
            if (g_QuitCountdown > 0 && --g_QuitCountdown == 0)
            {
                Log("quit");
                PostQuitMessage(0);
            }
            return;
        }

        Core::Game* game = Core::Game::GetInstance();
        const SceneName scene = game->GetCurrentSceneName();
        // タイトルはすぐに抜け、1面から始めている。視点はマウスで動かさないようにしている。
        if (scene == SceneName::Title)
        {
            game->GetCamera()->SetMouseLookEnabled(false);
            game->RequestSceneChange(SceneName::Stage);
            return;
        }
        if (g_Benchmark)
        {
            UpdateBenchmark(game);
            return;
        }

        const std::vector<Keyframe>* tour = GetTour(scene);
        if (tour == nullptr)
        {
            return;
        }
        // シーンが変わったら、そのシーンの道順を最初から始めている
        if (scene != g_TourScene)
        {
            g_TourScene = scene;
            g_TourTime = 0.0f;
            g_NextShot = 0;
            Log(std::string("start tour: ") + SceneLabel(scene));
        }
        g_Recording = true;

        // 道順の時間を1フレーム分進め、最後まで来たら1面は2面へ切り替え、2面なら終了している
        g_TourTime += FrameSeconds;
        if (g_TourTime > tour->back().Time)
        {
            if (scene == SceneName::Stage)
            {
                game->RequestSceneChange(SceneName::Stage2);
            }
            else
            {
                g_Finished = true;
                g_QuitCountdown = 3;
                Log("finished tour");
            }
            return;
        }

        // 名前の付いたキーフレームの時刻を過ぎたら、次に描いた画面を保存するよう予約している。
        while (g_NextShot < tour->size() && (*tour)[g_NextShot].Time <= g_TourTime)
        {
            if ((*tour)[g_NextShot].Shot != nullptr)
            {
                g_PendingShot = (*tour)[g_NextShot].Shot;
            }
            ++g_NextShot;
        }

        Player* player = game->GetObj<Player>("Player");
        Camera* camera = game->GetCamera();
        if (player == nullptr || camera == nullptr)
        {
            return;
        }

        const Keyframe pose = Sample(*tour, g_TourTime);
        // 2面のロッカー: 隠れる区間に入ったら右のロッカーに入り、区間を出たら外へ出ている。
        if (pose.Hide && !player->IsHiding())
        {
            if (Locker* locker = game->GetObj<Locker>("Stage2LockerRight"))
            {
                locker->Interact(*player);
            }
        }
        else if (!pose.Hide && player->IsHiding())
        {
            player->ForceExitHiding();
        }

        // 隠れている間は位置をロッカーの中に固定し、向きだけを道順に合わせている。懐中電灯は常に点けている
        if (!player->IsHiding())
        {
            player->SetPosition(pose.Position);
        }
        player->SetFlashlightOn(true);
        camera->SetCameraDirection(pose.Yaw);
        camera->SetCameraPitch(pose.Pitch);
    }

    // 描き終えた画面をGPUからCPUへ読み戻し、動画のフレームとスクリーンショットとして保存している
    void OnFrameRendered(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer)
    {
        // 計測モードは画面を保存していない（読み戻しの時間が計測に混ざらないように）。
        if (g_Benchmark)
        {
            if (g_AdapterName.empty() && context != nullptr)
            {
                RecordAdapterName(context);
            }
            // 最初の地点で1枚だけ画面を保存し、描画解像度とHUDの見え方を確認できるようにしている（捨てる区間の中なので計測には影響しない）。
            if (g_BenchmarkSpot == 0 && g_BenchmarkFrame == BenchmarkWarmupFrames / 2 &&
                context != nullptr && backBuffer != nullptr)
            {
                // CPUから読めるステージングテクスチャを作り、バックバッファをコピーして読み出している
                D3D11_TEXTURE2D_DESC description{};
                backBuffer->GetDesc(&description);
                D3D11_TEXTURE2D_DESC stagingDescription = description;
                stagingDescription.Usage = D3D11_USAGE_STAGING;
                stagingDescription.BindFlags = 0;
                stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                stagingDescription.MiscFlags = 0;
                ComPtr<ID3D11Device> device;
                context->GetDevice(&device);
                ComPtr<ID3D11Texture2D> staging;
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (SUCCEEDED(device->CreateTexture2D(&stagingDescription, nullptr, &staging)))
                {
                    context->CopyResource(staging.Get(), backBuffer);
                    if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
                    {
                        SavePng((g_OutputDirectory / L"benchmark_screen.png").wstring(),
                            static_cast<const BYTE*>(mapped.pData), mapped.RowPitch,
                            description.Width, description.Height);
                        context->Unmap(staging.Get(), 0);
                    }
                }
            }
            return;
        }
        if (!g_Active || !g_Recording || context == nullptr || backBuffer == nullptr)
        {
            return;
        }

        D3D11_TEXTURE2D_DESC description{};
        backBuffer->GetDesc(&description);
        // 読み戻し用のステージングテクスチャは、最初の1回だけ作って使い回している
        if (g_Staging == nullptr)
        {
            D3D11_TEXTURE2D_DESC stagingDescription = description;
            stagingDescription.Usage = D3D11_USAGE_STAGING;
            stagingDescription.BindFlags = 0;
            stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDescription.MiscFlags = 0;
            ComPtr<ID3D11Device> device;
            context->GetDevice(&device);
            if (FAILED(device->CreateTexture2D(&stagingDescription, nullptr, &g_Staging)))
            {
                g_Active = false;
                return;
            }
        }
        // 動画ファイルは、最初に画面を受け取ったときに開いている（画面の大きさが分かってから）
        if (!g_VideoTried)
        {
            g_VideoTried = true;
            g_Video.Open((g_OutputDirectory / L"プレイ動画.mp4").wstring(),
                description.Width, description.Height, 30);
        }

        // バックバッファをステージングテクスチャへコピーし、CPUから読める状態にしてから書き出している
        context->CopyResource(g_Staging.Get(), backBuffer);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(g_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return;
        }
        const BYTE* pixels = static_cast<const BYTE*>(mapped.pData);
        g_Video.WriteFrame(pixels, mapped.RowPitch);
        ++g_FramesWritten;
        // 予約されたスクリーンショットがあれば、同じ画面をPNGでも保存している
        if (g_PendingShot != nullptr)
        {
            Log("shot at " + std::to_string(g_TourTime) + "s");
            SavePng((g_OutputDirectory / (std::wstring(g_PendingShot) + L".png")).wstring(),
                pixels, mapped.RowPitch, description.Width, description.Height);
            g_PendingShot = nullptr;
        }
        context->Unmap(g_Staging.Get(), 0);
    }

    // ログに書き出したフレーム数を残し、動画を閉じて読み戻し用テクスチャを解放している
    void Shutdown()
    {
        if (g_Active)
        {
            Log("shutdown, video frames = " + std::to_string(g_FramesWritten));
        }
        g_Video.Close();
        g_Staging.Reset();
    }
}
