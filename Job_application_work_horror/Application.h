// ============================================================================
// ファイルの役割: Windowsのウィンドウを作り、メインループを回し、終了時の後片付けをしている。
// 主な技術: Win32 API、DeltaTime、メッセージループ、フレーム時間の上限処理
// このヘッダーでは、ウィンドウの大きさ・描画解像度・経過時間をどこからでも読めるよう、静的な関数として公開している。
// ============================================================================

#pragma once

#include    <Windows.h>
#include    <cstdint>

//-----------------------------------------------------------------------------
// Applicationクラス：ウィンドウとメインループを持ち、ゲームの外側（Windowsとのやり取り）を担当している
//-----------------------------------------------------------------------------
class Application
{
public:
    // ウィンドウの大きさは起動時のデスクトップ解像度（全画面）で決めている。
    // コンストラクタ／デストラクタでタイマー精度を上げ下げし、Runで起動から終了までを実行している。
    Application();
    ~Application();
    void Run();

    // 描画解像度の幅を返している（バックバッファ・画面効果・HUDはこの大きさで描いている）
    static uint32_t GetWidth() {
        return m_Width;
    }

    // 描画解像度の高さを返している
    static uint32_t GetHeight() {
        return m_Height;
    }

    // ウィンドウ（画面）の大きさ。描画解像度が小さいときは、表示の際にここまで引き伸ばしている。
    static uint32_t GetWindowWidth() {
        return m_WindowWidth;
    }

    static uint32_t GetWindowHeight() {
        return m_WindowHeight;
    }

    // 画面の拡大率（100%なら1.0、125%なら1.25）を返している。
    static float GetDpiScale() {
        return m_DpiScale;
    }

    // 起動時に使った描画解像度の設定段階を返している（ポーズ画面で「次回起動から」を表示するために使っている）。
    static int GetLaunchResolutionLevel() {
        return m_LaunchResolutionLevel;
    }

    // ゲーム画面のウィンドウハンドルを返している
    static HWND GetWindow() {
        return m_hWnd;
    }

    // MainLoopで測った前フレームからの経過秒を返している。止まっていた後の急な変化を防ぐため、上限を0.25秒にしている。
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
    static int         m_LaunchResolutionLevel; // 起動時の描画解像度の設定段階
    static float       m_DpiScale;     // 画面の拡大率
    static float       m_DeltaTime;    // 前回の更新からの経過秒

    // DPIを認識するプロセスとして宣言し、拡大率を記録している
    static void EnableDpiAwareness();
    // 設定とGPUの種類から描画解像度を決めている
    static void DecideRenderSize();
    static bool InitApp(); // ウィンドウクラスの登録とウィンドウの生成
    static void UninitApp(); // ウィンドウクラスの登録解除
    static void MainLoop(); // メッセージ処理と、ゲームの更新・描画を繰り返すループ

    // Windowsから届くメッセージを処理する関数
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
};
