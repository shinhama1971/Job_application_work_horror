// ============================================================================
// ファイルの役割: ImGuiによるデバッグ画面と、ライティング・演出の値をゲーム中に調整する機能を提供している。
// 主な技術: Dear ImGui、ゲーム中のパラメータ編集、デバッグ用の表示切り替え
// ============================================================================

#include "DebugUI.h"
#include "GIUpdateSettings.h"

#include "Game.h"
#include "TiledLighting.h"
#include "GpuTimer.h"
#include "Ground.h"
#include "ForestGrass.h"
#include "ModelCache.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "Scene.h"

// ENABLE_IMGUIはDebug構成でだけ定義している。Releaseでは下の関数はほとんど何もしない
#if defined(ENABLE_IMGUI)
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);
#endif

namespace
{
    // カリングの結果（本描画・影・反射それぞれで描いた数と省いた数）。Releaseでも受け取るだけ受け取っている
    unsigned int g_MainDrawn = 0;
    unsigned int g_MainCulled = 0;
    unsigned int g_ShadowDrawn = 0;
    unsigned int g_ShadowCulled = 0;
    unsigned int g_ReflectionDrawn = 0;
#if defined(ENABLE_IMGUI)
    // 初期化済みか、画面を開いているか、調整中にゲームを止めるか
    bool g_Initialized = false;
    bool g_Visible = false;
    bool g_PauseGameplay = false;
    // 画面効果の値をデバッグ画面の値で上書きするか、と上書きする値
    bool g_OverridePostProcess = false;
    bool g_EnableBloom = true;
    bool g_EnableNoise = true;
    bool g_EnableVolumetricLight = false;
    float g_VolumetricIntensity = 0.58f;
    float g_BloomIntensity = 0.48f;
    float g_NoiseAmount = 0.18f;
    float g_VignetteStrength = 0.55f;
    float g_Exposure = 1.0f;
    float g_LensDistortion = 0.32f;
    float g_CorridorTension = 0.0f;
    float g_FilmGradeStrength = 0.55f;
    float g_LensDirtStrength = 0.16f;
    float g_SignalInterference = 0.0f;
    // 壁の湿り気の強さ（シェーダーへ渡している）
    float g_WallDampStrength = 1.0f;
    // タイルベースライティングで、タイルの一番奥の深度より奥の光源を外すか（外す前との比較用）
    bool g_TileDepthBounds = true;
    // DDGI の間接光を使うか（使わない場合との比較用）
    bool g_GlobalIllumination = true;
    int g_GIUpdateFrames = Effect::GIUpdateSettings::DefaultFrames;
    // 画面の明るさから目の慣れの倍率を求める自動露出を使うか（使わない場合との比較用）
    bool g_AutoExposure = true;
    // トーンマッピングの曲線（0 = ACES、1 = HDRにする前に近い曲線。Effect::ToneMapMode の番号）
    int g_ToneMapMode = static_cast<int>(Effect::ToneMapMode::Aces);
    // 直近のフレーム時間（グラフ表示用。この数だけ覚えている）と、次に書く位置
    constexpr int FrameHistoryCount = 120;
    float g_FrameTimes[FrameHistoryCount]{};
    int g_FrameTimeOffset = 0;
    // シェーダーのデバッグ表示の番号（0は通常の画面）
    int g_DebugViewMode = 0;
#endif
}

// ImGuiを作り、Win32とDirect3D 11の描画の仕組みにつないでいる
bool Debug::UI::Init(HWND window)
{
#if defined(ENABLE_IMGUI)
    if (g_Initialized)
    {
        return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // キーボードで項目を選べるようにし、窓の配置は imgui_debug_layout.ini に保存している
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "imgui_debug_layout.ini";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0f;
    style.FrameRounding = 2.0f;

    if (!ImGui_ImplWin32_Init(window))
    {
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplDX11_Init(
        Renderer::GetDevice(),
        Renderer::GetDeviceContext()))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    g_Initialized = true;
#else
    (void)window;
#endif
    return true;
}

// ImGuiを終了している
void Debug::UI::Uninit()
{
#if defined(ENABLE_IMGUI)
    if (!g_Initialized)
    {
        return;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_Initialized = false;
    g_Visible = false;
#endif
}

// F1が押されていたら画面の表示を切り替え、ImGuiの新しいフレームを始めている
void Debug::UI::BeginFrame()
{
#if defined(ENABLE_IMGUI)
    if (!g_Initialized)
    {
        return;
    }

    if ((GetAsyncKeyState(VK_F1) & 1) != 0)
    {
        g_Visible = !g_Visible;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    // デバッグ画面を開いている間だけ、ImGuiがマウスカーソルを描いている
    ImGui::GetIO().MouseDrawCursor = g_Visible;
#endif
}

// デバッグ画面で決めた値を反映している。シェーダーのデバッグ表示はいつも反映し、
// 画面効果の値は「Override post process」をオンにしたときだけ上書きしている
void Debug::UI::ApplyTuning(Effect::PostProcess& postProcess)
{
#if defined(ENABLE_IMGUI)
    Renderer::SetDebugViewMode(
        g_DebugViewMode, g_WallDampStrength);
    postProcess.SetAutoExposureEnabled(g_AutoExposure);
    postProcess.SetToneMapMode(static_cast<Effect::ToneMapMode>(g_ToneMapMode));

    if (!g_OverridePostProcess)
    {
        return;
    }

    postProcess.SetNoise(g_EnableNoise);
    postProcess.SetVolumetricLight(g_EnableVolumetricLight);
    postProcess.SetVolumetricIntensity(g_VolumetricIntensity);
    postProcess.SetBloomEnabled(g_EnableBloom);
    postProcess.SetBloomIntensity(g_BloomIntensity);
    postProcess.SetAtmosphere(g_NoiseAmount, g_VignetteStrength);
    postProcess.SetExposure(g_Exposure);
    postProcess.SetLensDistortionStrength(g_LensDistortion);
    postProcess.SetCorridorTension(g_CorridorTension);
    postProcess.SetFilmGradeStrength(g_FilmGradeStrength);
    postProcess.SetLensDirtStrength(g_LensDirtStrength);
    postProcess.SetSignalInterference(g_SignalInterference);
#else
    (void)postProcess;
#endif
}

#if defined(ENABLE_IMGUI)
// デバッグ画面の区切りごとの中身（Debug::UI::Draw から順に呼んでいる）
namespace
{
    // 画面の最初の幅、プリセットのボタンの幅
    constexpr float PanelWidth = 430.0f;
    constexpr float PresetButtonWidth = 196.0f;
    // FPS の色分け：これ以上なら緑、次の値以上なら黄、それ未満は赤
    constexpr float GoodFps = 57.0f;
    constexpr float AcceptableFps = 45.0f;
    const ImVec4 GoodFpsColor(0.38f, 0.90f, 0.48f, 1.0f);
    const ImVec4 AcceptableFpsColor(0.95f, 0.76f, 0.25f, 1.0f);
    const ImVec4 SlowFpsColor(0.95f, 0.30f, 0.28f, 1.0f);
    // フレーム時間のグラフ：縦軸の一番上（30fps の1フレーム）と、グラフの高さ
    constexpr float MillisecondsPerSecond = 1000.0f;
    constexpr float FrameGraphMaxMilliseconds = 33.33f;
    constexpr float FrameGraphHeight = 72.0f;
    // 森の草の LOD を色で見せるデバッグ表示の番号（common.hlsl の番号と同じ）
    constexpr int GrassLodDebugView = 12;
    // 画面効果の演出を試しに再生する強さと長さ
    constexpr float TestBloomPeak = 1.65f;
    constexpr float TestBloomSeconds = 0.65f;
    constexpr float TestHorrorStrength = 0.85f;
    constexpr float TestHorrorSeconds = 0.75f;
    constexpr float TestLensMoistureStrength = 0.75f;
    constexpr float TestLensMoistureSeconds = 2.5f;

    // FPSとフレーム時間（60fps近くは緑、45fps以上は黄、それ未満は赤）、フレーム時間のグラフ、描画の段階ごとのGPU時間
    void DrawPerformanceSection(Core::Game* game)
    {
        const float currentFps = ImGui::GetIO().Framerate;
        const float currentFrameMs = MillisecondsPerSecond * ImGui::GetIO().DeltaTime;
        g_FrameTimes[g_FrameTimeOffset] = currentFrameMs;
        g_FrameTimeOffset = (g_FrameTimeOffset + 1) % FrameHistoryCount;
        const ImVec4 performanceColor = currentFps >= GoodFps
            ? GoodFpsColor
            : (currentFps >= AcceptableFps ? AcceptableFpsColor : SlowFpsColor);
        ImGui::TextColored(performanceColor, "FPS %.1f  Frame %.2f ms",
            currentFps,
            MillisecondsPerSecond / (ImGui::GetIO().Framerate > 0.0f
                ? ImGui::GetIO().Framerate
                : 1.0f));
        ImGui::PlotLines("##FrameTimes", g_FrameTimes, FrameHistoryCount,
            g_FrameTimeOffset, "Frame time (16.67 ms = 60 FPS)",
            0.0f, FrameGraphMaxMilliseconds, ImVec2(-1.0f, FrameGraphHeight));
        // 描画の段階ごとのGPU時間（タイムスタンプのクエリで測っている）
        if (!ImGui::CollapsingHeader("GPU Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }
        ImGui::Text("CPU frame       %.2f ms", currentFrameMs);
        // 測れた時間・省いた・測れない・待っている、のどれかを表示する関数
        const auto drawGpuTiming = [](const char* label, GpuTiming timing)
        {
            switch (timing.Status)
            {
            case GpuTimingStatus::Available:
                ImGui::Text("%-15s %.3f ms", label, timing.Milliseconds);
                break;
            case GpuTimingStatus::Skipped:
                ImGui::Text("%-15s Skipped", label);
                break;
            case GpuTimingStatus::Invalid:
                ImGui::Text("%-15s N/A", label);
                break;
            default:
                ImGui::Text("%-15s Waiting", label);
                break;
            }
        };

        GpuTimer* gpuTimer = game == nullptr ? nullptr : game->GetGpuTimer();
        if (gpuTimer == nullptr)
        {
            return;
        }
        drawGpuTiming("GPU total", gpuTimer->GetTiming(GpuPass::Total));
        drawGpuTiming("Shadow", gpuTimer->GetTiming(GpuPass::Shadow));
        drawGpuTiming("Reflection", gpuTimer->GetTiming(GpuPass::Reflection));
        drawGpuTiming("Main scene", gpuTimer->GetTiming(GpuPass::MainScene));
        drawGpuTiming("Bloom", gpuTimer->GetTiming(GpuPass::Bloom));
        drawGpuTiming("PostProcess", gpuTimer->GetTiming(GpuPass::PostProcess));
        drawGpuTiming("Water ripples", gpuTimer->GetTiming(GpuPass::WaterRipples));
        drawGpuTiming("Environment probe", gpuTimer->GetTiming(GpuPass::EnvironmentProbe));
        drawGpuTiming("Bulb shadow", gpuTimer->GetTiming(GpuPass::BulbShadow));
        drawGpuTiming("Ambient occlusion", gpuTimer->GetTiming(GpuPass::AmbientOcclusion));
        drawGpuTiming("DDGI indirect light", gpuTimer->GetTiming(GpuPass::GlobalIllumination));
        ImGui::TextDisabled("4-frame query ring / no GPU wait");
    }

    // カリングの結果・光源とタイルの数・DDGIの切り替え・音の数・モデルの使い回し・森の草の本数と、外の森を開くボタン
    void DrawSceneStats(Core::Game* game)
    {
        ImGui::Text("Main objects: %u drawn / %u culled",
            g_MainDrawn, g_MainCulled);
        ImGui::Text("Shadow casters: %u drawn / %u culled",
            g_ShadowDrawn, g_ShadowCulled);
        if (game != nullptr)
        {
            // タイルベースライティング: 光源の数と、Compute Shaderで判定したタイルの数を表示している。
            const Effect::TiledLighting* tiledLighting = game->GetTiledLighting();
            ImGui::Text("Point lights: %u / %u   Light tiles: %u (16x16)",
                tiledLighting->GetLightCount(),
                Effect::TiledLighting::MaxLights,
                tiledLighting->GetTileCount());
            // 深度プリパスの深度で、タイルの一番奥より奥の光源を外すか（Light tilesの表示で効果を比べられる）
            ImGui::Checkbox("Tile depth bounds (cull lights behind walls)", &g_TileDepthBounds);
            ImGui::Checkbox("DDGI indirect light (probes)", &g_GlobalIllumination);
            if (g_GlobalIllumination)
            {
                ImGui::SliderInt("GI update over N frames", &g_GIUpdateFrames,
                    Effect::GIUpdateSettings::MinFrames, Effect::GIUpdateSettings::MaxFrames);
            }
            ImGui::Text("Spatial voices: %zu / %zu (X3DAudio)",
                game->GetActiveSpatialVoiceCount(),
                Sound::MaxSpatialVoices);
        }
        // モデルの読み込みを使い回せた回数（同じモデルを2回読まないようにしている効果の確認用）
        const ModelCacheStats modelCacheStats = ModelCache::GetStats();
        ImGui::Text("Model cache: %zu loaded / %llu hit / %llu miss",
            modelCacheStats.LoadedModels,
            static_cast<unsigned long long>(modelCacheStats.CacheHits),
            static_cast<unsigned long long>(modelCacheStats.CacheMisses));
        ImGui::Text("Assimp loads through cache: %llu",
            static_cast<unsigned long long>(modelCacheStats.AssimpLoads));
        // 水面の反射のキューブマップ（技術デモで選んだときだけ）に描いた物の数
        ImGui::Text("Reflection probe objects: %u drawn", g_ReflectionDrawn);
        ImGui::Separator();

        if (game == nullptr)
        {
            return;
        }
        // 外の森を開く（タイトルで選ぶか、--forest で起動しても開ける）
        if (ImGui::Button("外の森を開く"))
        {
            game->RequestSceneChange(SceneName::Forest);
        }
        // LODと区画のカリングで、本数がどれだけ減ったかを確認する。
        if (const ForestGrass* grass = game->GetObj<ForestGrass>("ForestGrass"))
        {
            ImGui::Text("森の草: %u 本 / 描画 %u 本 / %u 区画",
                grass->GetBladeCount(), grass->GetDrawnBladeCount(), grass->GetDrawnChunkCount());
        }
    }

    // 2面の進行の確認と、イベントをすぐ再生するボタン（2面の間だけ表示している）。
    // ボタンの操作はすぐ実行せず、次のゲームの更新の始めに実行している（描画中に状態を変えないため）
    void DrawStage2EventSection(Core::Game* game)
    {
        Scene* scene = game == nullptr ? nullptr : game->GetScene();
        SceneDebugInfo sceneDebugInfo{};
        if (scene == nullptr || !scene->TryGetDebugInfo(sceneDebugInfo) ||
            !ImGui::CollapsingHeader("2面 イベント確認", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }
        ImGui::Text("ループ %d / 3", sceneDebugInfo.progressionStep);
        ImGui::Text("探す異変  1周目: %s  2周目: %s",
            sceneDebugInfo.firstAnomaly, sceneDebugInfo.secondAnomaly);
        ImGui::Text("信号 %d / 3  足音危険度 %.0f%%",
            sceneDebugInfo.puzzleStep, sceneDebugInfo.threatLevel * 100.0f);
        ImGui::Text("失敗回数 %d  再試行補助 %s",
            sceneDebugInfo.puzzleMistakeCount,
            sceneDebugInfo.puzzleMistakeCount >= 2 ? "強" :
                (sceneDebugInfo.puzzleMistakeCount == 1 ? "弱" : "なし"));
        ImGui::Text("最終イベント: %s  出口: %s",
            sceneDebugInfo.finalSequenceArmed ? "準備済み" : "待機中",
            sceneDebugInfo.exitReady ? "解錠" : "施錠");

        // ボタンの文字と、押したときに頼む操作。sameLine が true のボタンは、前のボタンと同じ行に並べる
        struct ActionButton
        {
            const char* Label;
            SceneDebugAction Action;
            bool SameLine;
        };
        const ActionButton buttons[] =
        {
            { "ループを1回進める", SceneDebugAction::AdvanceProgression, false },
            { "最終停電を再生", SceneDebugAction::PlayFinalSequence, true },
            { "視線ライト演出を再生", SceneDebugAction::PlayLightingEvent, false },
            { "鏡の異変を出す", SceneDebugAction::ShowMirrorAnomaly, true },
            { "肖像画の異変を出す", SceneDebugAction::ShowPortraitAnomaly, true },
            { "偽の扉の異変を出す", SceneDebugAction::ShowFalseDoorAnomaly, true },
            { "見えない影の異変を出す", SceneDebugAction::ShowUnseenAnomaly, true },
            { "水たまりの異変を出す", SceneDebugAction::ShowPuddleAnomaly, true },
            { "西棟の天井の人影を出す（1面）", SceneDebugAction::ShowCeilingReflection, false },
        };
        for (const ActionButton& button : buttons)
        {
            if (button.SameLine)
            {
                ImGui::SameLine();
            }
            if (ImGui::Button(button.Label))
            {
                scene->RequestDebugAction(button.Action);
            }
        }
        ImGui::TextDisabled("操作は次のゲーム更新開始時に安全に実行されます。");
        ImGui::Separator();
    }

    // シェーダーのデバッグ表示の選択と、水たまりのメッシュのうちこのフレームに描いた数（細かさごと）
    void DrawShaderDebugView(Core::Game* game)
    {
        // 表示の順番は common.hlsl のデバッグ表示番号に合わせている。
        const char* debugViews[] = { "Final", "World normals", "Flashlight shadow", "Lighting only", "Puddle mask + mesh LOD (green=near yellow=middle red=far)", "Floor reflection", "Wall damp mask", "Light tiles (lights per 16x16 tile)", "Fullbright (照明・影・霧なしで全体を明るく)", "PBR material (R=metallic G=roughness B=wet)", "Ambient occlusion (SSAO)", "室内を明るく＋水たまりの反射", "森の草LODを色で表示" };
        ImGui::Combo("Shader debug view", &g_DebugViewMode,
            debugViews, IM_ARRAYSIZE(debugViews));
        if (g_DebugViewMode == GrassLodDebugView)
        {
            ImGui::TextUnformatted("草のLOD  緑: 近距離 / 黄: 中距離 / 赤: 遠距離");
        }
        if (game == nullptr)
        {
            return;
        }
        // 画面の外の物は描いていない。細かさの番号は 3 = 近い、2 = 中くらい、1 = 遠い
        constexpr int NearLod = 3;
        constexpr int MiddleLod = 2;
        constexpr int FarLod = 1;
        for (Ground* ground : game->GetObjects<Ground>())
        {
            ImGui::Text("Puddle meshes: near %d / middle %d / far %d (total %d)",
                ground->GetDrawnWaterSurfaceCount(NearLod), ground->GetDrawnWaterSurfaceCount(MiddleLod),
                ground->GetDrawnWaterSurfaceCount(FarLod), ground->GetWaterSurfaceTotal());
        }
    }

    // 画面効果の値をまとめて切り替えるプリセット（1つのボタンで、上書きの値を全部入れ替える）
    struct VisualPreset
    {
        const char* Label;
        bool Bloom;
        bool VolumetricLight;
        float VolumetricIntensity;
        float BloomIntensity;
        float NoiseAmount;
        float VignetteStrength;
        float Exposure;
        float LensDistortion;
        float CorridorTension;
        float FilmGradeStrength;
        float LensDirtStrength;
        float SignalInterference;
        float WallDampStrength;
    };

    void ApplyVisualPreset(const VisualPreset& preset)
    {
        g_OverridePostProcess = true;
        g_EnableBloom = preset.Bloom;
        g_EnableNoise = true;
        g_EnableVolumetricLight = preset.VolumetricLight;
        g_VolumetricIntensity = preset.VolumetricIntensity;
        g_BloomIntensity = preset.BloomIntensity;
        g_NoiseAmount = preset.NoiseAmount;
        g_VignetteStrength = preset.VignetteStrength;
        g_Exposure = preset.Exposure;
        g_LensDistortion = preset.LensDistortion;
        g_CorridorTension = preset.CorridorTension;
        g_FilmGradeStrength = preset.FilmGradeStrength;
        g_LensDirtStrength = preset.LensDirtStrength;
        g_SignalInterference = preset.SignalInterference;
        g_WallDampStrength = preset.WallDampStrength;
    }

    // 見やすさ重視・雰囲気重視・怖さ重視・軽さ重視の4つ（2つずつ同じ行に並べる）
    void DrawVisualPresets()
    {
        static constexpr VisualPreset Presets[] =
        {
            // 名前                 ブルーム 光の筋 光の筋の強さ ブルーム ノイズ 周辺減光 露出 歪み 緊張 色調 汚れ 乱れ 壁の湿り
            { "Readable gameplay", true, false, 0.42f, 0.55f, 0.10f, 0.42f, 1.05f, 0.18f, 0.15f, 0.38f, 0.12f, 0.0f, 0.65f },
            { "Moody corridor", true, true, 0.58f, 0.78f, 0.18f, 0.62f, 0.98f, 0.32f, 0.45f, 0.62f, 0.22f, 0.18f, 1.0f },
            { "Scare showcase", true, true, 0.78f, 1.05f, 0.30f, 0.78f, 0.96f, 0.48f, 0.75f, 0.78f, 0.32f, 0.56f, 1.35f },
            { "Performance", false, false, 0.0f, 0.45f, 0.06f, 0.35f, 1.06f, 0.10f, 0.10f, 0.30f, 0.0f, 0.0f, 0.45f },
        };
        constexpr int PresetsPerRow = 2;
        ImGui::TextUnformatted("Visual presets");
        for (int index = 0; index < IM_ARRAYSIZE(Presets); ++index)
        {
            if (index % PresetsPerRow != 0)
            {
                ImGui::SameLine();
            }
            if (ImGui::Button(Presets[index].Label, ImVec2(PresetButtonWidth, 0.0f)))
            {
                ApplyVisualPreset(Presets[index]);
            }
        }
        ImGui::Separator();
    }

    // 自動露出・トーンマッピングの切り替えと、画面効果の値を1つずつ調整するスライダー（上書きをオンにしたときだけ操作できる）、
    // 画面効果の演出を試しに再生するボタン
    void DrawPostProcessControls(Effect::PostProcess& postProcess)
    {
        // 自動露出（画面の明るさを測って目の慣れを決める）は、上書きとは別にいつでも切り替えられる
        ImGui::Checkbox("Auto exposure (measure scene luminance)", &g_AutoExposure);
        ImGui::RadioButton("Tone map: ACES (HDR)", &g_ToneMapMode, static_cast<int>(Effect::ToneMapMode::Aces));
        ImGui::SameLine();
        ImGui::RadioButton("Legacy (before HDR)", &g_ToneMapMode, static_cast<int>(Effect::ToneMapMode::Legacy));
        ImGui::SameLine();
        ImGui::RadioButton("Reinhard", &g_ToneMapMode, static_cast<int>(Effect::ToneMapMode::Reinhard));
        ImGui::Checkbox("Override post process", &g_OverridePostProcess);
        ImGui::BeginDisabled(!g_OverridePostProcess);
        ImGui::Checkbox("Bloom compute", &g_EnableBloom);
        ImGui::Checkbox("Film noise", &g_EnableNoise);
        ImGui::Checkbox("Volumetric light", &g_EnableVolumetricLight);
        // スライダーの範囲（下限〜上限）は、見た目が壊れない調整の幅
        ImGui::SliderFloat(
            "Volume intensity", &g_VolumetricIntensity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Bloom", &g_BloomIntensity, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Noise", &g_NoiseAmount, 0.0f, 0.50f, "%.3f");
        ImGui::SliderFloat("Vignette", &g_VignetteStrength, 0.0f, 1.20f, "%.2f");
        ImGui::SliderFloat("Exposure", &g_Exposure,
            Effect::PostProcess::MinimumSceneExposure, Effect::PostProcess::MaximumSceneExposure, "%.3f");
        ImGui::SliderFloat("Lens distortion", &g_LensDistortion, 0.0f, 0.80f, "%.2f");
        ImGui::SliderFloat("Corridor tension", &g_CorridorTension, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Filmic split tone", &g_FilmGradeStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Lens dirt bloom", &g_LensDirtStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Signal interference", &g_SignalInterference, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Wall dampness", &g_WallDampStrength, 0.0f, 2.0f, "%.2f");

        if (ImGui::Button("Bloom pulse"))
        {
            postProcess.TriggerBloomPulse(TestBloomPeak, TestBloomSeconds);
        }
        ImGui::SameLine();
        if (ImGui::Button("Horror pulse"))
        {
            postProcess.TriggerHorrorPulse(TestHorrorStrength, TestHorrorSeconds);
        }
        ImGui::SameLine();
        if (ImGui::Button("Lens moisture"))
        {
            postProcess.TriggerLensMoisture(TestLensMoistureStrength, TestLensMoistureSeconds);
        }
        ImGui::EndDisabled();
    }
}
#endif

// デバッグ画面の中身を作って描いている（性能・カリング・2面の進行・表示の切り替え・画面効果の調整）。区切りごとの中身は上の関数
void Debug::UI::Draw(Effect::PostProcess& postProcess)
{
#if defined(ENABLE_IMGUI)
    if (!g_Initialized)
    {
        return;
    }

    if (g_Visible)
    {
        ImGui::SetNextWindowSize(ImVec2(PanelWidth, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("SIGNAL LOST - Runtime Tuning", &g_Visible);
        ImGui::Text("F1: close tuning panel");
        ImGui::Checkbox(
            "Pause gameplay while tuning", &g_PauseGameplay);

        Core::Game* game = Core::Game::GetInstance();
        DrawPerformanceSection(game);
        DrawSceneStats(game);
        DrawStage2EventSection(game);
        DrawShaderDebugView(game);
        DrawVisualPresets();
        DrawPostProcessControls(postProcess);
        ImGui::End();
    }

    // ImGuiの描画データを作り、バックバッファへ直接描いている（画面効果の後なので、ぼかしなどが掛からない）
    ImGui::Render();
    Renderer::SetBackBufferRenderTarget();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#else
    (void)postProcess;
#endif
}

// WindowsのメッセージをImGuiへ渡している。デバッグ画面を開いていて、ImGuiがそのメッセージを使ったときだけtrueを返している
bool Debug::UI::HandleWindowMessage(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
#if defined(ENABLE_IMGUI)
    if (g_Initialized &&
        ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
    {
        return g_Visible;
    }
#else
    (void)window;
    (void)message;
    (void)wParam;
    (void)lParam;
#endif
    return false;
}

// デバッグ画面を開いているかを返している
bool Debug::UI::IsVisible()
{
#if defined(ENABLE_IMGUI)
    return g_Visible;
#else
    return false;
#endif
}

// デバッグ画面を開いていて、ゲームを止める設定のときだけtrueを返している
bool Debug::UI::ShouldPauseGameplay()
{
#if defined(ENABLE_IMGUI)
    return g_Visible && g_PauseGameplay;
#else
    return false;
#endif
}

// タイルの深度の範囲で光源を絞るかを返している（Releaseは常に絞る）
bool Debug::UI::IsGlobalIlluminationEnabled()
{
#if defined(ENABLE_IMGUI)
    return g_GlobalIllumination;
#else
    return true;
#endif
}

int Debug::UI::GetGIUpdateFrames()
{
#if defined(ENABLE_IMGUI)
    return g_GIUpdateFrames;
#else
    return Effect::GIUpdateSettings::DefaultFrames;
#endif
}

bool Debug::UI::IsTileDepthBoundsEnabled()
{
#if defined(ENABLE_IMGUI)
    return g_TileDepthBounds;
#else
    return true;
#endif
}

// カリングの結果を受け取り、デバッグ画面で表示できるように覚えている
void Debug::UI::SetCullingStats(
    unsigned int mainDrawn,
    unsigned int mainCulled,
    unsigned int shadowDrawn,
    unsigned int shadowCulled,
    unsigned int reflectionDrawn)
{
    g_MainDrawn = mainDrawn;
    g_MainCulled = mainCulled;
    g_ShadowDrawn = shadowDrawn;
    g_ShadowCulled = shadowCulled;
    g_ReflectionDrawn = reflectionDrawn;
}
