// ============================================================================
// ファイルの役割: Windowsアプリケーションの生成、メインループ、終了処理を管理します。
// この実装ファイルでは宣言された機能の具体的な処理を定義します。
// ============================================================================

#include <chrono>
#include <thread>
#include "Application.h"
#include "Game.h"
#include "DebugUI.h"

namespace
{
    constexpr auto ClassName = TEXT("SignalLostWindowClass");
    constexpr auto WindowName = TEXT("SIGNAL LOST");
    constexpr auto FixedTimeStep = std::chrono::duration<double>(1.0 / 60.0);
    constexpr auto MaximumFrameTime = std::chrono::duration<double>(0.25);
}

HINSTANCE  Application::m_hInst;   // インスタンスハンドル
HWND       Application::m_hWnd;    // ウィンドウハンドル
uint32_t   Application::m_Width;   // ウィンドウの横幅
uint32_t   Application::m_Height;  // ウィンドウの縦幅

//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
Application::Application(uint32_t width, uint32_t height)
{ 
    m_Height = height;
    m_Width = width;

    timeBeginPeriod(1); //タイマー精度を1ミリ秒に設定
}

//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
Application::~Application()
{ 
    timeEndPeriod(1); // タイマー精度を元に戻す
}

//-----------------------------------------------------------------------------
// 実行
//-----------------------------------------------------------------------------
void Application::Run()
{
    //初期化
    bool okfg = InitApp();
    if (okfg) { MainLoop(); }

    UninitApp(); // 終了処理
}

//-----------------------------------------------------------------------------
// 初期化処理
//-----------------------------------------------------------------------------
bool Application::InitApp()
{
    // インスタンスハンドルを取得
    auto hInst = GetModuleHandle(nullptr);
    if (hInst == nullptr)
    {
        return false;
    }

    // ウィンドウの設定
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hIcon = LoadIcon(hInst, IDI_APPLICATION);
    wc.hCursor = LoadCursor(hInst, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BACKGROUND);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = ClassName;
    wc.hIconSm = LoadIcon(hInst, IDI_APPLICATION);

    // ウィンドウの登録
    if (!RegisterClassEx(&wc))
    {
        return false;
    }

    // インスタンスハンドル設定
    m_hInst = hInst;

    // Use the desktop resolution before Direct3D and post-process textures are
    // created. This keeps every render target the same size in fullscreen.
    m_Width = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
    m_Height = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));

    // ウィンドウのサイズを設定
    RECT rc = {};
    rc.right = static_cast<LONG>(m_Width);
    rc.bottom = static_cast<LONG>(m_Height);

    // ウィンドウサイズを調整
    auto style = WS_POPUP | WS_MINIMIZEBOX;

    // ウィンドウを生成
    m_hWnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        ClassName,
        WindowName,
        style,
        0,
        0,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        m_hInst,
        nullptr);

    if (m_hWnd == nullptr)
    {
        return false;
    }

    // ウィンドウを表示
    ShowWindow(m_hWnd, SW_SHOW);

    // ウィンドウを更新
    UpdateWindow(m_hWnd);

    // ウィンドウにフォーカスを設定
    SetFocus(m_hWnd);

    // 正常終了
    return true;

}

//-----------------------------------------------------------------------------
// 終了処理
//-----------------------------------------------------------------------------
void Application::UninitApp()
{
    // ウィンドウの登録を解除
    if (m_hInst != nullptr)
    {
        UnregisterClass(ClassName, m_hInst);
    }

    m_hInst = nullptr;
    m_hWnd = nullptr;
}

//-----------------------------------------------------------------------------
// メインループ
//-----------------------------------------------------------------------------
void Application::MainLoop()
{
    MSG msg = {};

    Core::Game::Init();

    using Clock = std::chrono::steady_clock;
    auto previousTime = Clock::now();
    std::chrono::duration<double> accumulator = std::chrono::duration<double>::zero();
    bool running = true;

    while (running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                running = false;
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!running)
        {
            break;
        }

        const auto currentTime = Clock::now();
        auto frameTime = std::chrono::duration<double>(currentTime - previousTime);
        previousTime = currentTime;

        if (frameTime > MaximumFrameTime)
        {
            frameTime = MaximumFrameTime;
        }

        accumulator += frameTime;

        bool updated = false;
        while (accumulator >= FixedTimeStep)
        {
            Core::Game::Update();
            accumulator -= FixedTimeStep;
            updated = true;
        }

        if (updated)
        {
            Core::Game::Draw();
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    Core::Game::Uninit();
}

//-----------------------------------------------------------------------------
// ウィンドウプロシージャ
//-----------------------------------------------------------------------------
LRESULT CALLBACK Application::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static bool isFullscreen = true;
    static bool isMessageBoxShowed = false;
    if (Debug::UI::HandleWindowMessage(hWnd, uMsg, wParam, lParam))
    {
        return 1;
    }

    switch (uMsg)
    {
    case WM_DESTROY:// ウィンドウ破棄のメッセージ
        PostQuitMessage(0);// 「WM_QUIT」メッセージを送る　→　アプリ終了
        break;

    case WM_CLOSE:  // 「x」ボタンが押されたら
    {
        // マウスカーソルを一時的に表示
        ShowCursor(TRUE);

        int res = MessageBoxA(NULL, "終了しますか？", "確認", MB_OKCANCEL);
        if (res == IDOK) {
            DestroyWindow(hWnd);  // 「WM_DESTROY」メッセージを送る
        }
        else {
            // キャンセルされたらカーソルを隠す
            ShowCursor(FALSE);
        }
    }
    break;

    case WM_ACTIVATE:
        if (wParam == WA_INACTIVE) {
            // フルスクリーン表示かつメッセージボックス非表示なら
            if (isFullscreen && !isMessageBoxShowed)
            {
                // ウインドウを最小化する（タスク切替時に背後に残る問題対策）
                ShowWindow(hWnd, SW_MINIMIZE);
            }
        }
        // 標準挙動を実行
        return DefWindowProc(hWnd, uMsg, wParam, lParam);

    case WM_SIZE: //ウィンドウサイズに変更があったメッセージ

        if (wParam != SIZE_MINIMIZED)
        {
            int width = LOWORD(lParam); //横幅
            int height = HIWORD(lParam); //縦幅
            Renderer::ResizeWindow(width, height);
        }
        break;

    default:
        // 受け取ったメッセージに対してデフォルトの処理を実行
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
        break;
    }

    return 0;
}

