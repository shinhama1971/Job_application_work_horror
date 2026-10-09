// ============================================================================
// ファイルの役割: ImGuiによるデバッグ画面と、ライティング・演出の値をゲーム中に調整する機能を提供している。
// 主な技術: Dear ImGui、ゲーム中のパラメータ編集、デバッグ用の表示切り替え
// ============================================================================

#pragma once

#include <Windows.h>

namespace Effect
{
    class PostProcess;
}

namespace Debug
{
    // F1で開くデバッグ画面（Debug構成だけ。Releaseでは何もしない関数になっている）
    class UI
    {
    public:
        // ImGuiを初期化している
        static bool Init(HWND window);
        // ImGuiを終了している
        static void Uninit();
        // フレームの始めに呼び、F1で表示を切り替えている
        static void BeginFrame();
        // デバッグ画面で調整した値を、画面効果とシェーダーへ反映している
        static void ApplyTuning(Effect::PostProcess& postProcess);
        // デバッグ画面を描いている
        static void Draw(Effect::PostProcess& postProcess);
        // WindowsのメッセージをImGuiへ渡している（画面を開いていて使われたらtrue）
        static bool HandleWindowMessage(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam);
        // デバッグ画面を開いているか
        static bool IsVisible();
        // 調整中にゲームを止めるか
        static bool ShouldPauseGameplay();
        // 水面の反射を何フレームに1回描き直すか
        static unsigned int GetReflectionUpdateInterval();
        // 描いた物と省いた物の数を受け取っている（デバッグ画面に表示するため）
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
