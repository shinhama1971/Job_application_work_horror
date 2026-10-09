// ============================================================================
// ファイルの役割: ImGuiによるデバッグ画面と、ライティング・演出の値をゲーム中に調整する機能を提供している。
// 主な技術: Dear ImGui、ゲーム中のパラメータ編集、デバッグ用の表示切り替え
// ============================================================================

#include "DebugUI.h"

#include "Game.h"
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
    unsigned int g_ReflectionCulled = 0;
    bool g_ReflectionSkipped = false;
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
    // 直近120フレームのフレーム時間（グラフ表示用）と、次に書く位置
    float g_FrameTimes[120]{};
    int g_FrameTimeOffset = 0;
    // シェーダーのデバッグ表示の番号（0は通常の画面）
    int g_DebugViewMode = 0;
    // 反射の更新頻度を自動で調整するか、選んだ頻度、今使っている間隔、FPSが低い・回復した状態の継続秒数
    bool g_AdaptiveReflectionQuality = true;
    int g_ReflectionPresetIndex = 0;
    int g_ActiveReflectionInterval = 1;
    float g_LowFpsTimer = 0.0f;
    float g_RecoveryFpsTimer = 0.0f;

    // FPSに合わせて水面の反射を描き直す間隔を自動で変えている。
    // 52fps未満が1.5秒続いたら間隔を1つ広げ（最大4フレームに1回）、58fps以上が4秒続いたら1つ戻している。
    void UpdateAdaptiveReflectionQuality()
    {
        const int requestedInterval = g_ReflectionPresetIndex + 1;
        if (!g_AdaptiveReflectionQuality)
        {
            g_ActiveReflectionInterval = requestedInterval;
            g_LowFpsTimer = 0.0f;
            g_RecoveryFpsTimer = 0.0f;
            return;
        }

        const float deltaTime = ImGui::GetIO().DeltaTime;
        const float fps = ImGui::GetIO().Framerate;
        g_ActiveReflectionInterval = g_ActiveReflectionInterval < requestedInterval
            ? requestedInterval
            : g_ActiveReflectionInterval;

        g_LowFpsTimer = fps > 1.0f && fps < 52.0f
            ? g_LowFpsTimer + deltaTime
            : 0.0f;
        g_RecoveryFpsTimer = fps >= 58.0f
            ? g_RecoveryFpsTimer + deltaTime
            : 0.0f;

        if (g_LowFpsTimer >= 1.5f && g_ActiveReflectionInterval < 4)
        {
            ++g_ActiveReflectionInterval;
            g_LowFpsTimer = 0.0f;
        }
        else if (g_RecoveryFpsTimer >= 4.0f &&
            g_ActiveReflectionInterval > requestedInterval)
        {
            --g_ActiveReflectionInterval;
            g_RecoveryFpsTimer = 0.0f;
        }
    }
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
    UpdateAdaptiveReflectionQuality();
    Renderer::SetDebugViewMode(
        g_DebugViewMode, g_WallDampStrength);

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

// デバッグ画面の中身を作って描いている（性能・カリング・2面の進行・表示の切り替え・画面効果の調整）
void Debug::UI::Draw(Effect::PostProcess& postProcess)
{
#if defined(ENABLE_IMGUI)
    if (!g_Initialized)
    {
        return;
    }

    if (g_Visible)
    {
        ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("SIGNAL LOST - Runtime Tuning", &g_Visible);
        ImGui::Text("F1: close tuning panel");
        ImGui::Checkbox(
            "Pause gameplay while tuning", &g_PauseGameplay);

        // FPSとフレーム時間。60fps近くは緑、45fps以上は黄、それ未満は赤で表示している
        const float currentFps = ImGui::GetIO().Framerate;
        const float currentFrameMs = 1000.0f * ImGui::GetIO().DeltaTime;
        g_FrameTimes[g_FrameTimeOffset] = currentFrameMs;
        g_FrameTimeOffset = (g_FrameTimeOffset + 1) % 120;
        const ImVec4 performanceColor = currentFps >= 57.0f
            ? ImVec4(0.38f, 0.90f, 0.48f, 1.0f)
            : (currentFps >= 45.0f
                ? ImVec4(0.95f, 0.76f, 0.25f, 1.0f)
                : ImVec4(0.95f, 0.30f, 0.28f, 1.0f));
        ImGui::TextColored(performanceColor, "FPS %.1f  Frame %.2f ms",
            currentFps,
            1000.0f / (ImGui::GetIO().Framerate > 0.0f
                ? ImGui::GetIO().Framerate
                : 1.0f));
        ImGui::PlotLines("##FrameTimes", g_FrameTimes, 120,
            g_FrameTimeOffset, "Frame time (16.67 ms = 60 FPS)",
            0.0f, 33.33f, ImVec2(-1.0f, 72.0f));
        Core::Game* game = Core::Game::GetInstance();
        // 描画の段階ごとのGPU時間（タイムスタンプのクエリで測っている）
        if (ImGui::CollapsingHeader(
            "GPU Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
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
            if (gpuTimer != nullptr)
            {
                drawGpuTiming("GPU total", gpuTimer->GetTiming(GpuPass::Total));
                drawGpuTiming("Shadow", gpuTimer->GetTiming(GpuPass::Shadow));
                drawGpuTiming(
                    "Reflection", gpuTimer->GetTiming(GpuPass::Reflection));
                drawGpuTiming(
                    "Main scene", gpuTimer->GetTiming(GpuPass::MainScene));
                drawGpuTiming("Bloom", gpuTimer->GetTiming(GpuPass::Bloom));
                drawGpuTiming(
                    "PostProcess", gpuTimer->GetTiming(GpuPass::PostProcess));
                ImGui::TextDisabled("4-frame query ring / no GPU wait");
            }
        }
        // カリングの結果（描いた数と省いた数）
        ImGui::Text("Main objects: %u drawn / %u culled",
            g_MainDrawn, g_MainCulled);
        ImGui::Text("Shadow casters: %u drawn / %u culled",
            g_ShadowDrawn, g_ShadowCulled);
        if (Core::Game* currentGame = Core::Game::GetInstance())
        {
            // タイルベースライティング: 光源の数と、Compute Shaderで判定したタイルの数を表示している。
            const Effect::TiledLighting* tiledLighting = currentGame->GetTiledLighting();
            ImGui::Text("Point lights: %u / %u   Light tiles: %u (16x16)",
                tiledLighting->GetLightCount(),
                Effect::TiledLighting::MaxLights,
                tiledLighting->GetTileCount());
            // 深度プリパスの深度で、タイルの一番奥より奥の光源を外すか（Light tilesの表示で効果を比べられる）
            ImGui::Checkbox("Tile depth bounds (cull lights behind walls)", &g_TileDepthBounds);
            ImGui::Text("Spatial voices: %zu / %zu (X3DAudio)",
                currentGame->GetActiveSpatialVoiceCount(),
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
        // 水面の反射：画面に水たまりが無ければ描くのを省いている
        if (g_ReflectionSkipped)
        {
            ImGui::TextColored(
                ImVec4(0.38f, 0.90f, 0.48f, 1.0f),
                "Reflection: skipped (no puddle in view)");
        }
        else
        {
            ImGui::Text("Reflection objects: %u drawn / %u culled",
                g_ReflectionDrawn, g_ReflectionCulled);
        }
        ImGui::Separator();

        // 2面の進行の確認と、イベントをすぐ再生するボタン（2面の間だけ表示している）
        Scene* scene = game == nullptr ? nullptr : game->GetScene();
        SceneDebugInfo sceneDebugInfo{};
        if (scene != nullptr && scene->TryGetDebugInfo(sceneDebugInfo) &&
            ImGui::CollapsingHeader(
            "2面 イベント確認", ImGuiTreeNodeFlags_DefaultOpen))
        {
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
            // ボタンの操作はすぐ実行せず、次のゲームの更新の始めに実行している（描画中に状態を変えないため）
            if (ImGui::Button("ループを1回進める"))
            {
                scene->RequestDebugAction(
                    SceneDebugAction::AdvanceProgression);
            }
            ImGui::SameLine();
            if (ImGui::Button("最終停電を再生"))
            {
                scene->RequestDebugAction(
                    SceneDebugAction::PlayFinalSequence);
            }
            if (ImGui::Button("視線ライト演出を再生"))
            {
                scene->RequestDebugAction(
                    SceneDebugAction::PlayLightingEvent);
            }
            ImGui::TextDisabled(
                "操作は次のゲーム更新開始時に安全に実行されます。");
            ImGui::Separator();
        }

        // 番号はシェーダーのDebugViewModeと対応している（8のフルブライトはcommon.hlslのDEBUG_VIEW_FULLBRIGHT）。
        const char* debugViews[] = { "Final", "World normals", "Flashlight shadow", "Lighting only", "Puddle mask", "Planar reflection", "Wall damp mask", "Light tiles (lights per 16x16 tile)", "Fullbright (照明・影・霧なしで全体を明るく)" };
        ImGui::Combo("Shader debug view", &g_DebugViewMode,
            debugViews, IM_ARRAYSIZE(debugViews));
        // 水面の反射を描き直す頻度（何フレームに1回か）
        const char* reflectionRates[] =
        {
            "60 Hz - every frame",
            "30 Hz - balanced",
            "20 Hz - performance",
            "15 Hz - low"
        };
        ImGui::Combo("Reflection update", &g_ReflectionPresetIndex,
            reflectionRates, IM_ARRAYSIZE(reflectionRates));
        ImGui::Checkbox(
            "Adaptive reflection quality", &g_AdaptiveReflectionQuality);
        ImGui::Text("Active reflection: %d Hz (every %d frame%s)",
            60 / g_ActiveReflectionInterval, g_ActiveReflectionInterval,
            g_ActiveReflectionInterval == 1 ? "" : "s");

        // 画面効果の値をまとめて切り替えるプリセット：見やすさ重視・雰囲気重視・怖さ重視・軽さ重視
        ImGui::TextUnformatted("Visual presets");
        if (ImGui::Button("Readable gameplay", ImVec2(196.0f, 0.0f)))
        {
            g_OverridePostProcess = true;
            g_ReflectionPresetIndex = 1;
            g_EnableBloom = true;
            g_EnableNoise = true;
            g_EnableVolumetricLight = false;
            g_VolumetricIntensity = 0.42f;
            g_BloomIntensity = 0.55f;
            g_NoiseAmount = 0.10f;
            g_VignetteStrength = 0.42f;
            g_Exposure = 1.05f;
            g_LensDistortion = 0.18f;
            g_CorridorTension = 0.15f;
            g_FilmGradeStrength = 0.38f;
            g_LensDirtStrength = 0.12f;
            g_SignalInterference = 0.0f;
            g_WallDampStrength = 0.65f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Moody corridor", ImVec2(196.0f, 0.0f)))
        {
            g_OverridePostProcess = true;
            g_ReflectionPresetIndex = 1;
            g_EnableBloom = true;
            g_EnableNoise = true;
            g_EnableVolumetricLight = true;
            g_VolumetricIntensity = 0.58f;
            g_BloomIntensity = 0.78f;
            g_NoiseAmount = 0.18f;
            g_VignetteStrength = 0.62f;
            g_Exposure = 0.98f;
            g_LensDistortion = 0.32f;
            g_CorridorTension = 0.45f;
            g_FilmGradeStrength = 0.62f;
            g_LensDirtStrength = 0.22f;
            g_SignalInterference = 0.18f;
            g_WallDampStrength = 1.0f;
        }
        if (ImGui::Button("Scare showcase", ImVec2(196.0f, 0.0f)))
        {
            g_OverridePostProcess = true;
            g_ReflectionPresetIndex = 0;
            g_EnableBloom = true;
            g_EnableNoise = true;
            g_EnableVolumetricLight = true;
            g_VolumetricIntensity = 0.78f;
            g_BloomIntensity = 1.05f;
            g_NoiseAmount = 0.30f;
            g_VignetteStrength = 0.78f;
            g_Exposure = 0.96f;
            g_LensDistortion = 0.48f;
            g_CorridorTension = 0.75f;
            g_FilmGradeStrength = 0.78f;
            g_LensDirtStrength = 0.32f;
            g_SignalInterference = 0.56f;
            g_WallDampStrength = 1.35f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Performance", ImVec2(196.0f, 0.0f)))
        {
            g_OverridePostProcess = true;
            g_ReflectionPresetIndex = 3;
            g_EnableBloom = false;
            g_EnableNoise = true;
            g_EnableVolumetricLight = false;
            g_VolumetricIntensity = 0.0f;
            g_BloomIntensity = 0.45f;
            g_NoiseAmount = 0.06f;
            g_VignetteStrength = 0.35f;
            g_Exposure = 1.06f;
            g_LensDistortion = 0.10f;
            g_CorridorTension = 0.10f;
            g_FilmGradeStrength = 0.30f;
            g_LensDirtStrength = 0.0f;
            g_SignalInterference = 0.0f;
            g_WallDampStrength = 0.45f;
        }
        ImGui::Separator();

        // 画面効果の値を1つずつ調整するスライダー（上書きをオンにしたときだけ操作できる）
        ImGui::Checkbox("Override post process", &g_OverridePostProcess);
        ImGui::BeginDisabled(!g_OverridePostProcess);
        ImGui::Checkbox("Bloom compute", &g_EnableBloom);
        ImGui::Checkbox("Film noise", &g_EnableNoise);
        ImGui::Checkbox("Volumetric light", &g_EnableVolumetricLight);
        ImGui::SliderFloat(
            "Volume intensity", &g_VolumetricIntensity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Bloom", &g_BloomIntensity, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Noise", &g_NoiseAmount, 0.0f, 0.50f, "%.3f");
        ImGui::SliderFloat("Vignette", &g_VignetteStrength, 0.0f, 1.20f, "%.2f");
        ImGui::SliderFloat("Exposure", &g_Exposure, 0.85f, 1.20f, "%.3f");
        ImGui::SliderFloat("Lens distortion", &g_LensDistortion, 0.0f, 0.80f, "%.2f");
        ImGui::SliderFloat("Corridor tension", &g_CorridorTension, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Filmic split tone", &g_FilmGradeStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Lens dirt bloom", &g_LensDirtStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Signal interference", &g_SignalInterference, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Wall dampness", &g_WallDampStrength, 0.0f, 2.0f, "%.2f");

        // 画面効果の演出を試しに再生するボタン
        if (ImGui::Button("Bloom pulse"))
        {
            postProcess.TriggerBloomPulse(1.65f, 0.65f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Horror pulse"))
        {
            postProcess.TriggerHorrorPulse(0.85f, 0.75f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Lens moisture"))
        {
            postProcess.TriggerLensMoisture(0.75f, 2.5f);
        }
        ImGui::EndDisabled();
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

// 水面の反射を何フレームに1回描き直すかを返している
unsigned int Debug::UI::GetReflectionUpdateInterval()
{
#if defined(ENABLE_IMGUI)
    return static_cast<unsigned int>(g_ActiveReflectionInterval);
#else
    // Releaseは毎フレーム描き直す。反射は縦横半分の解像度で描き、反射に映る物だけに絞っているので、移動中は滑らかさを優先している。
    // 止まっているときに描き直す回数を減らす処理はGameRendering側で行っている。
    return 1u;
#endif
}

// タイルの深度の範囲で光源を絞るかを返している（Releaseは常に絞る）
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
    unsigned int reflectionDrawn,
    unsigned int reflectionCulled,
    bool reflectionSkipped)
{
    g_MainDrawn = mainDrawn;
    g_MainCulled = mainCulled;
    g_ShadowDrawn = shadowDrawn;
    g_ShadowCulled = shadowCulled;
    g_ReflectionDrawn = reflectionDrawn;
    g_ReflectionCulled = reflectionCulled;
    g_ReflectionSkipped = reflectionSkipped;
}
