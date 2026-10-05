// ============================================================================
// ファイルの役割: Windowsアプリケーションの生成、メインループ、終了処理を管理します。
// 主な技術: Win32 API、DeltaTime、メッセージループ、フレーム時間の上限処理
// このヘッダーでは外部へ公開する型・状態・操作を宣言します。
// ============================================================================

#pragma once

#include    <Windows.h>
#include    <cstdint>

//-----------------------------------------------------------------------------
// Applicationクラス
//-----------------------------------------------------------------------------
class Application
{
public:
    // ウィンドウサイズは起動時のデスクトップ解像度（全画面）で決まります。
    Application();
    ~Application();
    void Run();

    // 描画解像度の幅を取得（バックバッファ・画面効果・HUDはこの大きさで描きます）
    static uint32_t GetWidth() {
        return m_Width;
    }

    // 描画解像度の高さを取得
    static uint32_t GetHeight() {
        return m_Height;
    }

    // ウィンドウ（画面）の大きさです。描画解像度が小さいときは、表示の際に引き伸ばされます。
    static uint32_t GetWindowWidth() {
        return m_WindowWidth;
    }

    static uint32_t GetWindowHeight() {
        return m_WindowHeight;
    }

    // 画面の拡大率（100%なら1.0、125%なら1.25）です。
    static float GetDpiScale() {
        return m_DpiScale;
    }

    // 起動時に使った描画解像度の設定段階です（ポーズ画面で「次回起動から」を表示するために使います）。
    static int GetLaunchResolutionLevel() {
        return m_LaunchResolutionLevel;
    }

    // ウインドウハンドルを返す
    static HWND GetWindow() {
        return m_hWnd;
    }

    // MainLoopで計測し、停止復帰時の急変を抑えるため上限を設定した経過秒です。
    static float GetDeltaTime() {
        return m_DeltaTime;
    }

private:
    static HINSTANCE   m_hInst;        // インスタンスハンドル
    static HWND        m_hWnd;         // ウィンドウハンドル
    static uint32_t    m_Width;        // 描画解像度の横幅
    static uint32_t    m_Height;       // 描画解像度の縦幅
    static uint32_t    m_WindowWidth;  // ウィンドウの横幅
    static uint32_t    m_WindowHeight; // ウィンドウの縦幅
    static int         m_LaunchResolutionLevel;
    static float       m_DpiScale;     // 画面の拡大率
    static float       m_DeltaTime;    // 前回更新からの経過秒

    static void EnableDpiAwareness();
    static void DecideRenderSize();
    static bool InitApp(); //初期化
    static void UninitApp(); //終了処理
    static void MainLoop(); //メインループ

    //プロシージャ
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
};
