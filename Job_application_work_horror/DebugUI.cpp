// ============================================================================
// ファイルの役割: ImGuiによるデバッグ表示と、ライティング・演出値の実行時調整を提供します。
// ============================================================================

#include "DebugUI.h"

#include "Game.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "Scene.h"

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
    unsigned int g_MainDrawn = 0;
    unsigned int g_MainCulled = 0;
    unsigned int g_ShadowDrawn = 0;
    unsigned int g_ShadowCulled = 0;
    unsigned int g_ReflectionDrawn = 0;
    unsigned int g_ReflectionCulled = 0;
    bool g_ReflectionSkipped = false;
#if defined(ENABLE_IMGUI)
    bool g_Initialized = false;
    bool g_Visible = false;
    bool g_PauseGameplay = false;
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
    float g_WallDampStrength = 1.0f;
    float g_FrameTimes[120]{};
    int g_FrameTimeOffset = 0;
    int g_DebugViewMode = 0;
    bool g_AdaptiveReflectionQuality = true;
    int g_ReflectionPresetIndex = 0;
    int g_ActiveReflectionInterval = 1;
    float g_LowFpsTimer = 0.0f;
    float g_RecoveryFpsTimer = 0.0f;

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
    ImGui::GetIO().MouseDrawCursor = g_Visible;
#endif
}

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
        ImGui::Text("Main objects: %u drawn / %u culled",
            g_MainDrawn, g_MainCulled);
        ImGui::Text("Shadow casters: %u drawn / %u culled",
            g_ShadowDrawn, g_ShadowCulled);
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

        Core::Game* game = Core::Game::GetInstance();
        Scene* scene = game == nullptr ? nullptr : game->GetScene();
        SceneDebugInfo sceneDebugInfo{};
        if (scene != nullptr && scene->TryGetDebugInfo(sceneDebugInfo) &&
            ImGui::CollapsingHeader(
            "2面 イベント確認", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("ループ %d / 3", sceneDebugInfo.progressionStep);
            ImGui::Text("信号 %d / 3  足音危険度 %.0f%%",
                sceneDebugInfo.puzzleStep, sceneDebugInfo.threatLevel * 100.0f);
            ImGui::Text("失敗回数 %d  再試行補助 %s",
                sceneDebugInfo.puzzleMistakeCount,
                sceneDebugInfo.puzzleMistakeCount >= 2 ? "強" :
                    (sceneDebugInfo.puzzleMistakeCount == 1 ? "弱" : "なし"));
            ImGui::Text("最終イベント: %s  出口: %s",
                sceneDebugInfo.finalSequenceArmed ? "準備済み" : "待機中",
                sceneDebugInfo.exitReady ? "解錠" : "施錠");
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

        const char* debugViews[] = { "Final", "World normals", "Flashlight shadow", "Lighting only", "Puddle mask", "Planar reflection", "Wall damp mask" };
        ImGui::Combo("Shader debug view", &g_DebugViewMode,
            debugViews, IM_ARRAYSIZE(debugViews));
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

    ImGui::Render();
    Renderer::SetBackBufferRenderTarget();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#else
    (void)postProcess;
#endif
}

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

bool Debug::UI::IsVisible()
{
#if defined(ENABLE_IMGUI)
    return g_Visible;
#else
    return false;
#endif
}

bool Debug::UI::ShouldPauseGameplay()
{
#if defined(ENABLE_IMGUI)
    return g_Visible && g_PauseGameplay;
#else
    return false;
#endif
}

unsigned int Debug::UI::GetReflectionUpdateInterval()
{
#if defined(ENABLE_IMGUI)
    return static_cast<unsigned int>(g_ActiveReflectionInterval);
#else
    // 1/6解像度と反射専用カリングにより、移動中は滑らかな60Hzを優先します。
    // 静止中の間引きはGameRendering側で行います。
    return 1u;
#endif
}

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
