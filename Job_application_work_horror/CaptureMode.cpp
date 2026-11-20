// ============================================================================
// ファイルの役割: 作業報告用の「自動撮影モード」と「処理の重さの計測モード」を実装している（説明はCaptureMode.hを参照）。
// 主な技術: Media Foundation（Sink WriterでH.264へ符号化）、WIC（PNG保存）、
//           バックバッファの読み戻し（ステージングテクスチャ）、キーフレームの補間
// ============================================================================

#include "CaptureMode.h"
#include "CaptureBenchmark.h"
#include "CaptureTours.h"
#include "CapturePixelFormat.h"
#include "MathUtility.h"
#include "GIUpdateSettings.h"
#include "PostProcess.h"

#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "GameSettings.h"
#include "TechDemo.h"
#include "Ground.h"
#include "FuseBox.h"
#include "Door.h"
#include "Locker.h"
#include "Player.h"
#include "Scene.h"
#include "VideoWriter.h"

#include <SimpleMath.h>
#include <cstdlib>
#include <string_view>
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX::SimpleMath;

namespace
{
    using namespace Tools::CaptureTours;

    // 画面写真の BGRA の1画素のバイト数と、不透明な透明度
    using CapturePixelFormat::BytesPerPixel;
    // BGRAでは、末尾のバイトが透明度。

    // 角度の補間。差を-π〜πに直してから補間し、遠回りの方向へ回らないようにしている
    float LerpAngle(float from, float to, float t)
    {
        float delta = std::fmod(to - from, Pi * 2.0f);
        if (delta > Pi) delta -= Pi * 2.0f;
        if (delta < -Pi) delta += Pi * 2.0f;
        return from + delta * t;
    }

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
        if (FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
        {
            return false;
        }
        std::vector<BYTE> bgra(static_cast<size_t>(width) * height * BytesPerPixel);
        for (UINT y = 0; y < height; ++y)
        {
            const BYTE* source = rgba + static_cast<size_t>(y) * rowPitch;
            BYTE* destination = bgra.data() + static_cast<size_t>(y) * width * BytesPerPixel;
            CapturePixelFormat::CopyRgbaToOpaqueBgra(source, destination, width);
        }
        if (FAILED(frame->WritePixels(height, width * BytesPerPixel, static_cast<UINT>(bgra.size()), bgra.data())) ||
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
    bool g_Benchmark = false;
    bool g_BenchmarkVsync = false;
    bool g_BenchmarkFigures = false;
    bool g_CaptureFailed = false;
    std::filesystem::path g_OutputDirectory;
    VideoWriter g_Video;
    bool g_VideoTried = false;
    ComPtr<ID3D11Texture2D> g_Staging;
    // 今見て回っているシーン、そのシーンの道順の経過秒、次に調べるキーフレーム、次に描いた画面で保存する名前、見て回り終えたか
    SceneName g_TourScene = SceneName::Title;
    float g_TourTime = 0.0f;
    bool g_FeatureTour = false;
    bool g_FeatureActionRequested = false;
    size_t g_NextShot = 0;
    const wchar_t* g_PendingShot = nullptr;
    // タイトル画面を表示してからのフレーム数（タイトルを1枚撮ってから1面を始めるため）
    int g_TitleFrames = 0;
    bool g_Finished = false;
    // 最後に足元へ水たまりの波を立てた位置（位置を直接動かすと足音が出ないため、歩いた距離で代わりに波を立てている）
    Vector3 g_LastStepPosition;
    // 2面の道順で、鏡の異変を出したか
    bool g_MirrorAnomalyRequested = false;
    bool g_EndDoorOpenRequested = false;
    // 2面の道順で、2周目へ進めたか
    bool g_SecondLoopRequested = false;
    // 2面の道順で、肖像画の異変を出したか
    bool g_PortraitAnomalyRequested = false;
    // 2面の道順で、偽の扉の異変を出したか
    bool g_FalseDoorRequested = false;
    bool g_PuddleAnomalyRequested = false;
    bool g_CeilingReflectionRequested = false;
    bool g_BreathingBuildingRequested = false;
    bool g_UnseenAnomalyRequested = false;
    // 2面の道順で、最後の停電を始めたか
    bool g_FinalSequenceRequested = false;
    bool g_Recording = false;   // 1面・2面を見て回っている間だけ動画に書き出している
    // 動画に書き出したフレーム数
    int g_FramesWritten = 0;
    // 見て回り終えてから終了するまでの残りフレーム数（最後のスクリーンショットを確実に保存するため）。
    int g_QuitCountdown = -1;
    // 計測を終えてから終了するまでのフレーム数
    constexpr int BenchmarkQuitDelayFrames = 3;
    // 撮影の経過を capture_log.txt に残している（うまく撮れなかったときの確認用）。
    void Log(const std::string& message)
    {
        std::ofstream file(g_OutputDirectory / L"capture_log.txt", std::ios::app);
        file << "[frame " << g_FramesWritten << "] " << message << "\n";
    }

    void RecordCaptureFailure(const char* reason)
    {
        g_CaptureFailed = true;
        const std::string message = std::string("エラー: ") + reason;
        if (!g_OutputDirectory.empty())
        {
            Log(message);
        }
        OutputDebugStringA((message + "\n").c_str());
    }

    // ログに書くシーンの名前
    const char* SceneLabel(SceneName scene)
    {
        switch (scene)
        {
        case SceneName::Title: return "Title";
        case SceneName::Stage: return "Stage1";
        case SceneName::Stage2: return "Stage2";
        case SceneName::Stage3: return "Stage3";
        case SceneName::Result: return "Result";
        case SceneName::Forest: return "Forest";
        case SceneName::Intro: return "Intro";
        }
        return "?";
    }

    // シーンに対応する道順を返している（タイトルとリザルトには道順がない）
    const std::vector<Keyframe>* GetTour(SceneName scene)
    {
        if (scene == SceneName::Forest) return &ForestTour;
        if (scene == SceneName::Stage3) return g_FeatureTour ? &Stage3ObservationTour : &Stage3Tour;
        if (g_FeatureTour && scene == SceneName::Stage) return &FeatureStage1;
        if (g_FeatureTour && scene == SceneName::Stage2) return &FeatureStage2;
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
            // 同時刻のキーフレームがあっても、補間率の分母をゼロにしない。
            constexpr float MinimumKeyframeIntervalSeconds = 0.001f;
            const float span = (std::max)(next.Time - previous.Time, MinimumKeyframeIntervalSeconds);
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
}

namespace Tools::CaptureMode
{
    // 起動オプションを読み、--capture か --benchmark の次の引数を保存先のフォルダにしている
    int g_EffectOverride = -1;
    int g_ViewOverride = -1;
    int GetEffectLevelOverride() { return g_EffectOverride; }
    int g_ReflectionOverride = -1;
    int GetReflectionMethodOverride() { return g_ReflectionOverride; }
    std::optional<Effect::ToneMapMode> g_ToneMapOverride;
    std::optional<Effect::ToneMapMode> GetToneMapOverride() { return g_ToneMapOverride; }
    // DDGI の間接光を使うか（--ddgi off で止める）
    bool g_UsesGlobalIllumination = true;
    bool UsesGlobalIllumination() { return g_UsesGlobalIllumination; }
    int g_GIUpdateFramesOverride = 0;
    int GetGIUpdateFramesOverride() { return g_GIUpdateFramesOverride; }

    void ConfigureFromCommandLine()
    {
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
        {
            return;
        }
        for (int index = 1; index < argumentCount; ++index)
        {
            const std::wstring option = arguments[index];
            if (option == L"--capture" || option == L"--benchmark" || option == L"--capture-features")
            {
                if (index + 1 >= argumentCount || std::wstring_view(arguments[index + 1]).starts_with(L"--"))
                {
                    RecordCaptureFailure("撮影・計測の保存先を指定してください。");
                    break;
                }
                g_OutputDirectory = arguments[index + 1];
                std::error_code error;
                // 保存先のフォルダを作り、作れたときだけ有効にしている
                std::filesystem::create_directories(g_OutputDirectory, error);
                g_Active = !error;
                if (!g_Active)
                {
                    RecordCaptureFailure("撮影・計測の保存先を作れません。");
                }
                g_Benchmark = g_Active && option == L"--benchmark";
                g_FeatureTour = g_Active && option == L"--capture-features";
                break;
            }
        }
        if (g_Active)
        {
            for (int i = 1; i + 1 < argumentCount; ++i)
            {
                const std::wstring option = arguments[i];
                const std::wstring value = arguments[i + 1];
                if (option == L"--effect-level" && value.size() == 1)
                {
                    // 起動オプションは1始まり、ゲーム内部はLowEffectLevelから数える。
                    const int level = static_cast<int>(value[0] - L'1') + Core::GameSettings::LowEffectLevel;
                    if (level >= Core::GameSettings::LowEffectLevel && level <= Core::GameSettings::HighEffectLevel)
                    {
                        g_EffectOverride = level;
                    }
                }
                if (option == L"--debug-view")
                {
                    for (const int mode : { ShaderDebugView::AmbientOcclusion,
                        ShaderDebugView::FullbrightReflection, ShaderDebugView::GrassLod })
                    {
                        if (value == std::to_wstring(mode))
                        {
                            g_ViewOverride = mode;
                            break;
                        }
                    }
                }
                if (option == L"--reflection" && value.size() == 1)
                {
                    // 起動オプションでは、ゲーム本体の方式番号をそのまま使う。
                    const int method = static_cast<int>(value[0] - L'0');
                    if (method >= static_cast<int>(TechDemo::ReflectionMethod::Off) &&
                        method <= static_cast<int>(TechDemo::ReflectionMethod::RoomRay))
                    {
                        g_ReflectionOverride = method;
                    }
                }
                if (option == L"--tonemap" && value == L"aces")
                    g_ToneMapOverride = Effect::ToneMapMode::Aces;
                if (option == L"--tonemap" && value == L"legacy")
                    g_ToneMapOverride = Effect::ToneMapMode::Legacy;
                if (option == L"--tonemap" && value == L"reinhard")
                    g_ToneMapOverride = Effect::ToneMapMode::Reinhard;
                if (option == L"--ddgi" && value == L"off")
                    g_UsesGlobalIllumination = false;
                if (option == L"--gi-update-frames" && value.size() == 1)
                {
                    const int frames = static_cast<int>(value[0] - L'0');
                    if (frames >= Effect::GIUpdateSettings::MinFrames &&
                        frames <= Effect::GIUpdateSettings::MaxFrames)
                    {
                        g_GIUpdateFramesOverride = frames;
                    }
                }
            }
            for (int i = 1; i < argumentCount; ++i)
            {
                if (std::wstring(arguments[i]) == L"--vsync") g_BenchmarkVsync = g_Benchmark;
                if (std::wstring(arguments[i]) == L"--figures") g_BenchmarkFigures = g_Benchmark;
            }
        }
        LocalFree(arguments);
    }

    // 自動撮影モード（または計測モード）が有効かを返している
    void Abort(const char* reason)
    {
        RecordCaptureFailure(reason);
        if (g_Active)
        {
            PostQuitMessage(EXIT_FAILURE);
        }
    }

    int GetExitCode()
    {
        return g_CaptureFailed ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    bool IsActive()
    {
        return g_Active;
    }

    // 自動撮影・計測の間は決まった種、普段は毎回ちがう種を返している
    unsigned int MakeRandomSeed(unsigned int captureSeed)
    {
        return g_Active ? captureSeed : std::random_device{}();
    }

    // 計測モードかどうかを返している
    bool IsBenchmark()
    {
        return g_Benchmark;
    }

    bool UsesBenchmarkVsync()
    {
        return g_BenchmarkVsync;
    }


    namespace
    {
        // --- 道順の撮影の進め方（UpdateBeforeObjects とその下の関数）
        // タイトル：この数のフレームで1枚撮り、この数のフレームで1面へ進む
        constexpr int TitleShotFrame = 20;
        constexpr int TitleExitFrame = 30;
        // 最後の1枚を書き出す画面を描くために、終了を待つフレームの数
        constexpr int QuitDelayFrames = 3;
        // 機能の撮影（--capture-features）で、最初に場面を用意する時刻（秒）
        constexpr float FeatureSetupTime = 0.2f;
        // 位置を直接動かすと足音が出ないため、この距離を歩くごとに足元へ水たまりの波だけを立てる。
        // 場面を切り替える瞬間移動（1回でこの距離より長い）は歩いた扱いにしない
        constexpr float TourStepDistance = 9.0f;
        constexpr float TourTeleportDistance = 30.0f;
        constexpr float TourStepRippleStrength = 1.4f;
        constexpr float TourStepRippleSpeed = -1.2f;
        // 2面の最後の場面だけ電池を25%にして、弱った懐中電灯の光を写す（満タンからこの量を引く）
        constexpr float TourLowBatteryDrain = 75.0f;
        // 機能の撮影：3面で懐中電灯を消している時間、1面で点けている時間、1面のドアを形が分かる全体を明るい表示で見せる時間
        constexpr float FeatureStage3FlashlightOffStart = 17.3f;
        constexpr float FeatureStage3FlashlightOffEnd = 29.0f;
        constexpr float FeatureStage1FlashlightOnStart = 10.0f;
        constexpr float FeatureStage1FlashlightOnEnd = 13.0f;
        constexpr float FeatureStage1FullbrightEnd = 21.0f;
        constexpr float FeatureCorridorLightOffStart = 27.0f;
        constexpr float FeatureCorridorLightOffEnd = 30.1f;
        constexpr int FullbrightDebugView = ShaderDebugView::Fullbright;   // 照明・影・霧なしで全体を明るく
        constexpr int NormalDebugView = ShaderDebugView::Final;
        // 機能の撮影では、3面の観測の場面を足す分だけ、後半の操作の時刻を遅らせる
        constexpr float FeatureStage3RouteDelay = 4.0f;

        // 道順の決まった時刻に、デバッグ画面と同じ操作を一度だけ頼む（異変を出す・扉を開ける・周回を進めるなど）
        struct TourAction
        {
            SceneName Scene;
            float Time;
            SceneDebugAction Action;
            bool* Requested;
            const char* LogText;
        };

        // 3面の撮影で、決まった時刻に調べる操作の対象（名前と時刻）
        struct Stage3TourStep
        {
            const char* Name;
            float Time;
        };

        // 道順の状態を、新しい場面の最初へ戻している
        void ResetTourState(SceneName scene)
        {
            g_TourScene = scene;
            g_TourTime = 0.0f;
            g_NextShot = 0;
            g_FeatureActionRequested = false;
            g_MirrorAnomalyRequested = false;
            g_EndDoorOpenRequested = false;
            g_SecondLoopRequested = false;
            g_PortraitAnomalyRequested = false;
            g_FalseDoorRequested = false;
            g_PuddleAnomalyRequested = false;
            g_CeilingReflectionRequested = false;
            g_BreathingBuildingRequested = false;
            g_UnseenAnomalyRequested = false;
            g_FinalSequenceRequested = false;
            Log(std::string("start tour: ") + SceneLabel(scene));
        }

        // タイトルは、始める面の選択が写るよう1枚撮ってから抜け、1面から始めている（計測モードはすぐに抜ける）。
        // 視点はマウスで動かさないようにしている。
        void UpdateTitleTour(Core::Game* game)
        {
            game->GetCamera()->SetMouseLookEnabled(false);
            ++g_TitleFrames;
            if (!g_Benchmark && g_TitleFrames == TitleShotFrame)
            {
                g_Recording = true;
                g_PendingShot = L"00_タイトル";
            }
            if (g_Benchmark || g_TitleFrames >= TitleExitFrame)
            {
                game->RequestSceneChange(SceneName::Stage);
            }
        }

        // 道順の時間を1フレーム分進めている。最後まで来たら、1面は2面へ切り替え、2面なら終了して false を返している
        bool AdvanceTourTime(Core::Game* game, SceneName scene, const std::vector<Keyframe>& tour)
        {
            g_TourTime += FrameSeconds;
            if (g_TourTime <= tour.back().Time)
            {
                return true;
            }
            if (scene == SceneName::Stage)
            {
                game->RequestSceneChange(SceneName::Stage2);
            }
            else
            {
                g_Finished = true;
                g_QuitCountdown = QuitDelayFrames;
                Log("finished tour");
            }
            return false;
        }

        // 名前の付いたキーフレームの時刻を過ぎたら、次に描いた画面を保存するよう予約している。
        void QueueTourShots(const std::vector<Keyframe>& tour)
        {
            while (g_NextShot < tour.size() && tour[g_NextShot].Time <= g_TourTime)
            {
                if (tour[g_NextShot].Shot != nullptr)
                {
                    g_PendingShot = tour[g_NextShot].Shot;
                }
                ++g_NextShot;
            }
        }

        // 道順の決まった時刻に、場面の操作（異変を出す・扉を開ける・周回を進める・最後の停電）を一度だけ頼んでいる
        void RequestTourActions(Core::Game* game, SceneName scene)
        {
            if (g_FeatureTour && (scene == SceneName::Stage || scene == SceneName::Stage2) &&
                !g_FeatureActionRequested && g_TourTime >= FeatureSetupTime)
            {
                g_FeatureActionRequested = true;
                if (Scene* current = game->GetScene())
                    current->RequestDebugAction(scene == SceneName::Stage
                        ? SceneDebugAction::ShowHauntedStartDoor : SceneDebugAction::ShowUnseenAnomaly);
                Log("feature setup");
            }

            const TourAction actions[] =
            {
                // 1面: 西棟の水の中で、天井の人影を出している（水面をのぞいたときだけ映る）
                { SceneName::Stage, CeilingReflectionRequestTime, SceneDebugAction::ShowCeilingReflection, &g_CeilingReflectionRequested, "show ceiling reflection" },
                // 1面: 最後に、突き当たりの最後のドアを開けている（開けた瞬間に、開始地点の奥のドアの裏側へ移る）
                { SceneName::Stage, Stage1EndDoorOpenTime, SceneDebugAction::OpenStage1EndDoor, &g_EndDoorOpenRequested, "open stage 1 end door" },
                // 1面: 最後に、3周目・ヒューズ3本の状態にしている（中央ホールと外周の壁も息をする）
                { SceneName::Stage, BreathingBuildingRequestTime, SceneDebugAction::ShowBreathingBuilding, &g_BreathingBuildingRequested, "show breathing building" },
                // 2面: 鏡の前に立った後で、鏡の異変を出している（鏡の中にだけ、すぐ後ろの人影が映る）
                { SceneName::Stage2, MirrorAnomalyRequestTime, SceneDebugAction::ShowMirrorAnomaly, &g_MirrorAnomalyRequested, "show mirror anomaly" },
                // 2面: 濡れた足跡の場面の前に、2周目へ進めている
                { SceneName::Stage2, SecondLoopRequestTime, SceneDebugAction::AdvanceProgression, &g_SecondLoopRequested, "advance to second loop" },
                // 2面: 肖像画・偽の扉・見えない影・水たまりの場面の前に、それぞれの異変を出している
                { SceneName::Stage2, PortraitAnomalyRequestTime, SceneDebugAction::ShowPortraitAnomaly, &g_PortraitAnomalyRequested, "show portrait anomaly" },
                { SceneName::Stage2, FalseDoorRequestTime, SceneDebugAction::ShowFalseDoorAnomaly, &g_FalseDoorRequested, "show false door anomaly" },
                { SceneName::Stage2, UnseenAnomalyRequestTime, SceneDebugAction::ShowUnseenAnomaly, &g_UnseenAnomalyRequested, "show unseen anomaly" },
                { SceneName::Stage2, PuddleAnomalyRequestTime, SceneDebugAction::ShowPuddleAnomaly, &g_PuddleAnomalyRequested, "show puddle anomaly" },
                // 2面: 最後の停電と追跡を始めている
                { SceneName::Stage2, FinalSequenceRequestTime, SceneDebugAction::PlayFinalSequence, &g_FinalSequenceRequested, "play final sequence" },
            };
            for (const TourAction& action : actions)
            {
                if (scene != action.Scene || *action.Requested || g_TourTime < action.Time)
                {
                    continue;
                }
                *action.Requested = true;
                if (Scene* currentScene = game->GetScene())
                {
                    currentScene->RequestDebugAction(action.Action);
                    Log(action.LogText);
                }
            }
        }

        // 3面の撮影では、普段の操作対象を順に調べて進める（ちょうどその時刻を過ぎたフレームで一度だけ）
        void OperateStage3Tour(Core::Game* game, Player& player)
        {
            const auto justPassed = [](float start)
            {
                return g_TourTime >= start && g_TourTime - FrameSeconds < start;
            };
            const auto operate = [&](const char* name, float start)
            {
                if (justPassed(start))
                {
                    if (FuseBox* control = game->GetObj<FuseBox>(name);
                        control != nullptr && control->IsInteractionEnabled())
                    {
                        control->Interact(player);
                    }
                }
            };
            const auto openDoor = [&](const char* name, float start)
            {
                if (justPassed(start))
                {
                    if (Door* door = game->GetObj<Door>(name);
                        door != nullptr && door->IsInteractionEnabled())
                    {
                        door->Interact(player);
                    }
                }
            };

            // 前半：中継器のあと、普段は記録・送信・監視・受信、機能の撮影では観測を見せる
            static constexpr Stage3TourStep RegularFirstHalf[] =
            {
                { "Stage3Relay", 7.5f },
                { "Stage3Log", 14.8f },
                { "Stage3Transmit", 24.3f },
                { "Stage3MonitorChannel", 26.8f },
                { "Stage3Receive", 28.7f },
                { "Stage3Transmit", 29.3f },
            };
            static constexpr Stage3TourStep FeatureFirstHalf[] =
            {
                { "Stage3Relay", 7.5f },
                { "Stage3ObservationStart", 12.25f },
                { "Stage3ObservationEnd", 28.3f },
            };
            // 後半：試験・遮断器・解放・北と南の柱（機能の撮影では FeatureStage3RouteDelay だけ遅らせる）
            static constexpr Stage3TourStep SecondHalf[] =
            {
                { "Stage3Test", 39.3f },
                { "Stage3Breaker", 42.6f },
                { "Stage3Release", 50.2f },
                { "Stage3PostNorth", 60.2f },
                { "Stage3PostSouth", 68.1f },
            };
            constexpr float TapeChoiceTime = 81.5f;
            constexpr float ObservationDoorTime = 31.6f;
            constexpr float ExitDoorTime = 51.2f;
            constexpr float RoadGateTime = 95.0f;

            if (g_FeatureTour)
            {
                for (const Stage3TourStep& step : FeatureFirstHalf) operate(step.Name, step.Time);
            }
            else
            {
                for (const Stage3TourStep& step : RegularFirstHalf) operate(step.Name, step.Time);
            }
            const float routeDelay = g_FeatureTour ? FeatureStage3RouteDelay : 0.0f;
            for (const Stage3TourStep& step : SecondHalf) operate(step.Name, step.Time + routeDelay);
            operate(g_FeatureTour ? "Stage3EraseTape" : "Stage3KeepTape", TapeChoiceTime + routeDelay);
            if (g_FeatureTour) openDoor("Stage3ObservationDoor", ObservationDoorTime);
            openDoor("Stage3Exit", ExitDoorTime + routeDelay);
            openDoor("Stage3RoadGate", RoadGateTime + routeDelay);
        }

        // 道順の位置にプレイヤーを置き、ロッカー・電池・懐中電灯・視点を道順に合わせている
        void ApplyTourPose(Core::Game* game, SceneName scene, Player& player, Camera& camera, const Keyframe& pose)
        {
            // 2面のロッカー: 隠れる区間に入ったら右のロッカーに入り、区間を出たら外へ出ている。
            if (pose.Hide && !player.IsHiding())
            {
                if (Locker* locker = game->GetObj<Locker>("Stage2LockerRight"))
                {
                    locker->Interact(player);
                }
            }
            else if (!pose.Hide && player.IsHiding())
            {
                player.ForceExitHiding();
            }

            // 隠れている間は位置をロッカーの中に固定し、向きだけを道順に合わせている
            if (!player.IsHiding())
            {
                const float stepDistance = Vector3::Distance(g_LastStepPosition, pose.Position);
                if (stepDistance > TourTeleportDistance)
                {
                    g_LastStepPosition = pose.Position;
                }
                else if (stepDistance > TourStepDistance)
                {
                    g_LastStepPosition = pose.Position;
                    for (Ground* ground : game->GetObjects<Ground>())
                    {
                        ground->AddRippleImpulse(pose.Position, TourStepRippleStrength, TourStepRippleSpeed);
                        ground->RecordFootstep(pose.Position);
                    }
                }
                player.SetPosition(pose.Position);
            }
            // 道順の途中で電池が切れて懐中電灯が消えないよう、電池を満タンに保っている（計測モードと同じ）
            player.RefillBattery();
            if (scene == SceneName::Stage2 && g_TourTime >= LowBatteryTime && g_TourTime < LowBatteryEndTime)
            {
                player.AddBattery(-TourLowBatteryDrain);
            }
            // 懐中電灯は、道順で消すと決めた区間のほかは常に点けている
            player.SetFlashlightOn(!(scene == SceneName::Stage &&
                g_TourTime >= Stage1FlashlightOffStart && g_TourTime < Stage1FlashlightOffEnd) &&
                !(scene == SceneName::Stage2 && !g_Benchmark &&
                g_TourTime >= Stage2FlashlightOffStart && g_TourTime < Stage2FlashlightOffEnd));
            if (g_FeatureTour && scene == SceneName::Stage3 &&
                g_TourTime >= FeatureStage3FlashlightOffStart && g_TourTime < FeatureStage3FlashlightOffEnd)
                player.SetFlashlightOn(false);
            if (g_FeatureTour && scene == SceneName::Stage)
                player.SetFlashlightOn((g_TourTime >= FeatureStage1FlashlightOnStart && g_TourTime < FeatureStage1FlashlightOnEnd) ||
                    (g_TourTime >= FeatureStage1FullbrightEnd &&
                        !(g_TourTime >= FeatureCorridorLightOffStart && g_TourTime < FeatureCorridorLightOffEnd)));
            // ドア検証の間は Fullbright で形を見せる。通常表示の資料画像には使わない。
            if (g_FeatureTour && g_ViewOverride < 0)
                Renderer::SetDebugViewMode(scene == SceneName::Stage && g_TourTime < FeatureStage1FullbrightEnd
                    ? FullbrightDebugView : NormalDebugView);
            // 視点はこの道順だけで決める（タイトル画面が終わるときにマウスの視点操作を戻すので、ここで毎回止めている。矢印キーの視点操作は、前からの道順と同じく止めない）
            camera.SetMouseLookEnabled(false);
            camera.SetCameraDirection(pose.Yaw);
            camera.SetCameraPitch(pose.Pitch);
        }
    }

    // 1フレーム分、道順に沿ってプレイヤーと視点を動かしている（計測モードでは地点に立たせている）。段階ごとの中身は上の関数
    void UpdateBeforeObjects()
    {
        if (!g_Active)
        {
            return;
        }
        if (g_ViewOverride >= 0) Renderer::SetDebugViewMode(g_ViewOverride);
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
        if (scene == SceneName::Title)
        {
            UpdateTitleTour(game);
            return;
        }
        if (g_Benchmark)
        {
            if (CaptureBenchmark::Update(game, g_OutputDirectory, g_BenchmarkVsync, g_BenchmarkFigures, Log))
            {
                g_Finished = true;
                g_QuitCountdown = BenchmarkQuitDelayFrames;
            }
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
            ResetTourState(scene);
        }
        g_Recording = true;

        if (!AdvanceTourTime(game, scene, *tour))
        {
            return;
        }
        QueueTourShots(*tour);
        RequestTourActions(game, scene);

        Player* player = game->GetObj<Player>("Player");
        Camera* camera = game->GetCamera();
        if (player == nullptr || camera == nullptr)
        {
            return;
        }
        if (scene == SceneName::Stage3)
        {
            OperateStage3Tour(game, *player);
        }
        ApplyTourPose(game, scene, *player, *camera, Sample(*tour, g_TourTime));
    }

    // 描き終えた画面をGPUからCPUへ読み戻し、動画のフレームとスクリーンショットとして保存している
    void OnFrameRendered(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer)
    {
        // 計測モードは、計測する区間では画面を保存していない（読み戻しの時間が計測に混ざらないように）。
        if (g_Benchmark)
        {
            const std::wstring screenName = CaptureBenchmark::GetScreenshotName(context);
            if (!screenName.empty())
            {
                // 計測結果の数値は残すが、付属画像を保存できなければ終了コードを失敗にする。
                if (context == nullptr || backBuffer == nullptr)
                {
                    RecordCaptureFailure(("計測用画像の画面を取得できません: " + std::filesystem::path(screenName).string()).c_str());
                    return;
                }
                bool saved = false;
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
                        saved = SavePng((g_OutputDirectory / screenName).wstring(),
                            static_cast<const BYTE*>(mapped.pData), mapped.RowPitch,
                            description.Width, description.Height);
                        context->Unmap(staging.Get(), 0);
                    }
                }
                if (!saved)
                {
                    RecordCaptureFailure(("計測用画像を保存できません: " + std::filesystem::path(screenName).string()).c_str());
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
                Abort("撮影用の読み戻しテクスチャを作れません。");
                return;
            }
        }
        // 動画ファイルは、最初に画面を受け取ったときに開いている（画面の大きさが分かってから）
        if (!g_VideoTried)
        {
            g_VideoTried = true;
            if (!g_Video.Open((g_OutputDirectory / L"プレイ動画.mp4").wstring(),
                description.Width, description.Height, PlayVideoFps))
            {
                Abort("プレイ動画を開けません。");
                return;
            }
        }

        // バックバッファをステージングテクスチャへコピーし、CPUから読める状態にしてから書き出している
        context->CopyResource(g_Staging.Get(), backBuffer);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(g_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            Abort("撮影する画面を読み戻せません。");
            return;
        }
        const BYTE* pixels = static_cast<const BYTE*>(mapped.pData);
        if (!g_Video.WriteFrame(pixels, mapped.RowPitch))
        {
            context->Unmap(g_Staging.Get(), 0);
            Abort("プレイ動画にフレームを書き込めません。");
            return;
        }
        ++g_FramesWritten;
        // 予約されたスクリーンショットがあれば、同じ画面をPNGでも保存している
        if (g_PendingShot != nullptr)
        {
            if (g_FeatureTour)
            {
                if (Door* door = Core::Game::GetInstance()->GetObj<Door>("Stage1StartDoor"))
                    Log("door angle = " + std::to_string(door->GetOpenAngle()));
            }
            const bool saved = SavePng((g_OutputDirectory / (std::wstring(g_PendingShot) + L".png")).wstring(),
                pixels, mapped.RowPitch, description.Width, description.Height);
            if (!saved)
            {
                context->Unmap(g_Staging.Get(), 0);
                Abort("スクリーンショットを保存できません。");
                return;
            }
            Log("shot at " + std::to_string(g_TourTime) + "s");
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
        if (!g_Video.Close())
        {
            RecordCaptureFailure("プレイ動画を正常に閉じられません。");
        }
        if (g_Active && !g_CaptureFailed && (!g_Finished || (!g_Benchmark && g_FramesWritten == 0)))
        {
            RecordCaptureFailure("撮影・計測が完了する前に終了しました。");
        }
        g_Staging.Reset();
    }
}
