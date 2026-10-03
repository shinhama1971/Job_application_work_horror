// ============================================================================
// ファイルの役割: 作業報告用の「自動撮影モード」です（説明はCaptureMode.hを参照）。
// 主な技術: Media Foundation（Sink WriterでH.264へ符号化）、WIC（PNG保存）、
//           バックバッファの読み戻し（ステージングテクスチャ）、キーフレームの補間
// ============================================================================

#include "CaptureMode.h"

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
    // 道順（キーフレーム）。時刻の間は位置と向きを直線で補間します。
    // shotに名前があるキーフレームの時刻で、スクリーンショットを1枚保存します。
    // ------------------------------------------------------------------------
    struct Keyframe
    {
        float Time;
        Vector3 Position;   // 足元の位置（yは床の高さ）
        float Yaw;          // 水平の向き（+Zが0、+Xがπ/2）
        float Pitch;        // 上下の向き（負で下を向く）
        const wchar_t* Shot;
        bool Hide;          // 2面のロッカーに隠れている区間
    };

    constexpr float Pi = 3.14159265f;
    constexpr float FloorY = -99.0f;

    float YawToward(float fromX, float fromZ, float toX, float toZ)
    {
        return std::atan2(toX - fromX, toZ - fromZ);
    }

    // 1面: 開始地点 → 中央ホール → 左の倉庫 → 暗証番号の扉 → ループ廊下
    const std::vector<Keyframe> Stage1Tour =
    {
        { 0.0f, Vector3(0.0f, FloorY, -120.0f), Pi, -0.12f, nullptr, false },
        { 4.5f, Vector3(0.0f, FloorY, -128.0f), Pi, -0.12f, L"01_1面_開始地点_照らすと浮かぶ壁の文字", false },
        { 8.0f, Vector3(0.0f, FloorY, -90.0f), 0.0f, 0.0f, nullptr, false },
        { 13.0f, Vector3(0.0f, FloorY, -10.0f), YawToward(0.0f, -10.0f, -180.0f, 35.0f), 0.0f, nullptr, false },
        { 15.5f, Vector3(-10.0f, FloorY, 0.0f), YawToward(-10.0f, 0.0f, -180.0f, 35.0f), -0.05f, L"02_1面_中央ホールと配電盤", false },
        { 19.5f, Vector3(-60.0f, FloorY, -88.0f), YawToward(-60.0f, -88.0f, -150.0f, -140.0f), 0.0f, nullptr, false },
        { 24.0f, Vector3(-150.0f, FloorY, -110.0f), 0.0f, -0.26f, L"03_1面_左の倉庫の壁の文字", false },
        { 28.0f, Vector3(-60.0f, FloorY, -30.0f), YawToward(-60.0f, -30.0f, 0.0f, 40.0f), 0.0f, nullptr, false },
        { 32.0f, Vector3(0.0f, FloorY, 70.0f), 0.0f, 0.0f, nullptr, false },
        { 36.5f, Vector3(12.0f, FloorY, 112.0f), Pi * 0.5f, -0.05f, L"04_1面_暗証番号の扉", false },
        { 41.0f, Vector3(0.0f, FloorY, 185.0f), 0.0f, 0.0f, nullptr, false },
        { 46.5f, Vector3(0.0f, FloorY, 240.0f), 0.0f, -0.10f, L"05_1面_ループ廊下の突き当たり", false },
        { 50.0f, Vector3(0.0f, FloorY, 245.0f), 0.0f, -0.10f, nullptr, false },
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

    float LerpAngle(float from, float to, float t)
    {
        float delta = std::fmod(to - from, Pi * 2.0f);
        if (delta > Pi) delta -= Pi * 2.0f;
        if (delta < -Pi) delta += Pi * 2.0f;
        return from + delta * t;
    }

    // ------------------------------------------------------------------------
    // 動画の書き出し（Media Foundation Sink Writer、H.264 / MP4）
    // ------------------------------------------------------------------------
    class VideoWriter
    {
    public:
        bool Open(const std::wstring& path, UINT width, UINT height, UINT fps)
        {
            if (FAILED(MFStartup(MF_VERSION)))
            {
                return false;
            }
            m_Started = true;
            m_Width = width;
            m_Height = height;
            m_FrameDuration = 10'000'000LL / fps;

            if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &m_Writer)))
            {
                return false;
            }

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

        // rgbaは上の行から並んだRGBA。RGB32（BGRA、下の行から並ぶ）へ詰め替えて渡します。
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

            ComPtr<IMFSample> sample;
            MFCreateSample(&sample);
            sample->AddBuffer(buffer.Get());
            sample->SetSampleTime(m_Time);
            sample->SetSampleDuration(m_FrameDuration);
            m_Writer->WriteSample(m_Stream, sample.Get());
            m_Time += m_FrameDuration;
        }

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
        ComPtr<IMFSinkWriter> m_Writer;
        DWORD m_Stream = 0;
        UINT m_Width = 0;
        UINT m_Height = 0;
        LONGLONG m_Time = 0;
        LONGLONG m_FrameDuration = 0;
        bool m_Open = false;
        bool m_Started = false;
    };

    // RGBA（上の行から）をPNGで保存します。
    bool SavePng(const std::wstring& path, const BYTE* rgba, UINT rowPitch, UINT width, UINT height)
    {
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
        // PNGの書き出しはBGRAの並びを使います（RGBAを渡すと赤と青が入れ替わるため、並べ替えてから渡します）。
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
                destination[x * 4 + 3] = 255;   // 透明度は使わないので不透明にします
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
    // 撮影の状態
    // ------------------------------------------------------------------------
    bool g_Active = false;
    std::filesystem::path g_OutputDirectory;
    VideoWriter g_Video;
    bool g_VideoTried = false;
    ComPtr<ID3D11Texture2D> g_Staging;
    SceneName g_TourScene = SceneName::Title;
    float g_TourTime = 0.0f;
    size_t g_NextShot = 0;
    const wchar_t* g_PendingShot = nullptr;
    bool g_Finished = false;
    bool g_Recording = false;   // 1面・2面を見て回っている間だけ動画に書き出します
    int g_FramesWritten = 0;
    // 見て回り終えてから終了するまでの残りフレーム数（最後のスクリーンショットを確実に保存するため）。
    int g_QuitCountdown = -1;

    // 撮影の経過を capture_log.txt に残します（うまく撮れなかったときの確認用）。
    void Log(const std::string& message)
    {
        std::ofstream file(g_OutputDirectory / L"capture_log.txt", std::ios::app);
        file << "[frame " << g_FramesWritten << "] " << message << "\n";
    }

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

    const std::vector<Keyframe>* GetTour(SceneName scene)
    {
        if (scene == SceneName::Stage) return &Stage1Tour;
        if (scene == SceneName::Stage2) return &Stage2Tour;
        return nullptr;
    }

    Keyframe Sample(const std::vector<Keyframe>& tour, float time)
    {
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
            result.Hide = previous.Hide;
            return result;
        }
        return tour.back();
    }

    // ------------------------------------------------------------------------
    // 処理の重さの計測（--benchmark）
    // 1面の決まった地点ごとに「ライトOFF → ライトON」の順で同じ景色を測り、差を比べます。
    // ------------------------------------------------------------------------
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
    };

    // 各段階のフレーム数。切り替え直後はGPU時間の結果が数フレーム遅れて届くため、捨てる区間を置きます。
    constexpr int BenchmarkWarmupFrames = 45;
    constexpr int BenchmarkMeasureFrames = 180;

    // 1つの条件（地点×ライトの状態）で集めた値です。
    struct BenchmarkSamples
    {
        std::vector<double> FrameMs;    // 前のフレームからの実時間
        std::vector<double> CpuMs;      // FrameMsからPresentの待ち時間を引いたもの
        std::array<double, static_cast<size_t>(GpuPass::Count)> GpuSum{};
        std::array<int, static_cast<size_t>(GpuPass::Count)> GpuCount{};
    };

    struct BenchmarkResult
    {
        const BenchmarkSpot* Spot;
        BenchmarkSamples Off;
        BenchmarkSamples On;
    };

    bool g_Benchmark = false;
    size_t g_BenchmarkSpot = 0;
    int g_BenchmarkFrame = 0;           // 地点の中での経過フレーム
    std::vector<BenchmarkResult> g_BenchmarkResults;
    std::chrono::steady_clock::time_point g_LastFrameTime;
    bool g_HasLastFrameTime = false;
    double g_LastPresentMs = 0.0;
    std::string g_AdapterName;

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

    // 遅い方から(1 - ratio)の位置の値です（0.99なら遅い方から1%）。かくつきの目安に使います。
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

    double GpuAverage(const BenchmarkSamples& samples, GpuPass pass)
    {
        const size_t index = static_cast<size_t>(pass);
        return samples.GpuCount[index] == 0
            ? 0.0
            : samples.GpuSum[index] / samples.GpuCount[index];
    }

    // GPUの名前を記録します（結果を見る人がどの環境の数字か分かるように）。
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
        const int size = WideCharToMultiByte(CP_UTF8, 0, description.Description, -1,
            nullptr, 0, nullptr, nullptr);
        std::string name(static_cast<size_t>((std::max)(size - 1, 0)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name.data(), size,
            nullptr, nullptr);
        g_AdapterName = name;
    }

    void WriteBenchmarkResult()
    {
        Core::Game* game = Core::Game::GetInstance();
        std::ostringstream text;
        text << std::fixed << std::setprecision(2);
        text << "処理の重さの計測結果（1面・ライトOFFとONの比較）\n";
        text << "解像度: " << GetSystemMetrics(SM_CXSCREEN) << "x" << GetSystemMetrics(SM_CYSCREEN)
             << "  垂直同期: なし  エフェクト設定: " << game->GetEffectLevel()
             << "  GPU: " << g_AdapterName << "\n";
        text << "各条件 " << BenchmarkMeasureFrames << " フレームの平均（ms）。"
                "1%遅は遅い方から1%のフレーム時間で、かくつきの目安です。\n\n";

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
        file.write("\xEF\xBB\xBF", 3);   // メモ帳で文字化けしないようBOM付きUTF-8にします
        file.write(body.data(), static_cast<std::streamsize>(body.size()));
    }

    // 計測モードの1フレーム分です。地点に立たせ、ライトを切り替えながら値を集めます。
    void UpdateBenchmark(Core::Game* game)
    {
        const auto now = std::chrono::steady_clock::now();
        const double frameMs = g_HasLastFrameTime
            ? std::chrono::duration<double, std::milli>(now - g_LastFrameTime).count()
            : 0.0;
        g_LastFrameTime = now;
        g_HasLastFrameTime = true;

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

        if (g_BenchmarkSpot >= BenchmarkSpots.size())
        {
            WriteBenchmarkResult();
            Log("benchmark finished");
            g_Finished = true;
            g_QuitCountdown = 3;
            return;
        }
        if (g_BenchmarkResults.size() <= g_BenchmarkSpot)
        {
            g_BenchmarkResults.push_back({ &BenchmarkSpots[g_BenchmarkSpot], {}, {} });
            Log("benchmark spot " + std::to_string(g_BenchmarkSpot));
        }

        // 段階: [OFFの捨て][OFFの計測][ONの捨て][ONの計測]
        const int phaseLength = BenchmarkWarmupFrames + BenchmarkMeasureFrames;
        const bool lightOn = g_BenchmarkFrame >= phaseLength;
        const int frameInPhase = g_BenchmarkFrame % phaseLength;
        // ここで測れるのは直前のフレームの値です。条件を切り替えた直後のフレームは捨てる区間に入ります。
        if (frameInPhase >= BenchmarkWarmupFrames && frameMs > 0.0)
        {
            BenchmarkResult& result = g_BenchmarkResults[g_BenchmarkSpot];
            BenchmarkSamples& samples = lightOn ? result.On : result.Off;
            samples.FrameMs.push_back(frameMs);
            samples.CpuMs.push_back((std::max)(frameMs - g_LastPresentMs, 0.0));
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

        const BenchmarkSpot& spot = BenchmarkSpots[g_BenchmarkSpot];
        player->SetPosition(spot.Position);
        player->SetFlashlightOn(lightOn);
        player->AddBattery(100.0f);    // 電池切れの点滅で結果が揺れないよう満タンに保ちます
        camera->SetCameraDirection(spot.Yaw);
        camera->SetCameraPitch(spot.Pitch);

        if (++g_BenchmarkFrame >= phaseLength * 2)
        {
            g_BenchmarkFrame = 0;
            ++g_BenchmarkSpot;
        }
    }
}

namespace Tools::CaptureMode
{
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
                std::filesystem::create_directories(g_OutputDirectory, error);
                g_Active = !error;
                g_Benchmark = g_Active && option == L"--benchmark";
                break;
            }
        }
        LocalFree(arguments);
    }

    bool IsActive()
    {
        return g_Active;
    }

    bool IsBenchmark()
    {
        return g_Benchmark;
    }

    void OnPresentTimed(double milliseconds)
    {
        g_LastPresentMs = milliseconds;
    }

    void UpdateBeforeObjects()
    {
        if (!g_Active)
        {
            return;
        }
        if (g_Finished)
        {
            // 最後のスクリーンショットを書き出す画面を描いてから終了します。
            if (g_QuitCountdown > 0 && --g_QuitCountdown == 0)
            {
                Log("quit");
                PostQuitMessage(0);
            }
            return;
        }

        Core::Game* game = Core::Game::GetInstance();
        const SceneName scene = game->GetCurrentSceneName();
        // タイトルはすぐに抜け、1面から始めます。視点はマウスで動かさないようにします。
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
        if (scene != g_TourScene)
        {
            g_TourScene = scene;
            g_TourTime = 0.0f;
            g_NextShot = 0;
            Log(std::string("start tour: ") + SceneLabel(scene));
        }
        g_Recording = true;

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

        // 名前の付いたキーフレームの時刻を過ぎたら、次に描いた画面を保存します。
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
        // 2面のロッカー: 隠れる区間に入ったら右のロッカーに入り、区間を出たら外へ出ます。
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

        if (!player->IsHiding())
        {
            player->SetPosition(pose.Position);
        }
        player->SetFlashlightOn(true);
        camera->SetCameraDirection(pose.Yaw);
        camera->SetCameraPitch(pose.Pitch);
    }

    void OnFrameRendered(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer)
    {
        // 計測モードは画面を保存しません（読み戻しの時間が計測に混ざらないように）。
        if (g_Benchmark)
        {
            if (g_AdapterName.empty() && context != nullptr)
            {
                RecordAdapterName(context);
            }
            // 最初の地点で1枚だけ画面を保存し、描画解像度とHUDの見え方を確認できるようにします（捨てる区間の中なので計測には影響しません）。
            if (g_BenchmarkSpot == 0 && g_BenchmarkFrame == BenchmarkWarmupFrames / 2 &&
                context != nullptr && backBuffer != nullptr)
            {
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
        if (!g_VideoTried)
        {
            g_VideoTried = true;
            g_Video.Open((g_OutputDirectory / L"プレイ動画.mp4").wstring(),
                description.Width, description.Height, 30);
        }

        context->CopyResource(g_Staging.Get(), backBuffer);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(g_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return;
        }
        const BYTE* pixels = static_cast<const BYTE*>(mapped.pData);
        g_Video.WriteFrame(pixels, mapped.RowPitch);
        ++g_FramesWritten;
        if (g_PendingShot != nullptr)
        {
            Log("shot at " + std::to_string(g_TourTime) + "s");
            SavePng((g_OutputDirectory / (std::wstring(g_PendingShot) + L".png")).wstring(),
                pixels, mapped.RowPitch, description.Width, description.Height);
            g_PendingShot = nullptr;
        }
        context->Unmap(g_Staging.Get(), 0);
    }

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
