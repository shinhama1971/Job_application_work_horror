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
    };
}
