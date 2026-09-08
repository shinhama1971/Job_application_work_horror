// ============================================================================
// ファイルの役割: ImGuiによるデバッグ表示と、ライティング・演出値の実行時調整を提供します。
// ============================================================================

#pragma once

#include <Windows.h>

namespace Effect
{
    class PostProcess;
}

namespace Debug
{
    class UI
    {
    public:
        static bool Init(HWND window);
        static void Uninit();
        static void BeginFrame();
        static void ApplyTuning(Effect::PostProcess& postProcess);
        static void Draw(Effect::PostProcess& postProcess);
        static bool HandleWindowMessage(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam);
        static bool IsVisible();
        static bool ShouldPauseGameplay();
        static unsigned int GetReflectionUpdateInterval();
        static void SetCullingStats(
            unsigned int mainDrawn,
            unsigned int mainCulled,
            unsigned int shadowDrawn,
            unsigned int shadowCulled,
            unsigned int reflectionDrawn,
            unsigned int reflectionCulled,
            bool reflectionSkipped);
    };
}
