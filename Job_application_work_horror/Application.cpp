// ============================================================================
// ファイルの役割: Windowsアプリケーションの生成、メインループ、終了処理を管理します。
// 主な技術: Win32 API、DeltaTime、メッセージループ、フレーム時間の上限処理
// この実装ファイルでは宣言された機能の具体的な処理を定義します。
// ============================================================================

#include <chrono>
#include <algorithm>
#include <thread>
#include "Application.h"
#include "Game.h"
#include "DebugUI.h"
#include "CaptureMode.h"
#include "GameSettings.h"
#include "Renderer.h"
#include <objbase.h>

namespace
{
    constexpr auto ClassName = TEXT("SignalLostWindowClass");
    constexpr auto WindowName = TEXT("SIGNAL LOST");
    constexpr auto MaximumFrameTime = std::chrono::duration<double>(0.25);
}

HINSTANCE  Application::m_hInst;   // インスタンスハンドル
HWND       Application::m_hWnd;    // ウィンドウハンドル
uint32_t   Application::m_Width;   // 描画解像度の横幅
uint32_t   Application::m_Height;  // 描画解像度の縦幅
uint32_t   Application::m_WindowWidth;
uint32_t   Application::m_WindowHeight;
int        Application::m_LaunchResolutionLevel = 0;
float      Application::m_DpiScale = 1.0f;
float      Application::m_DeltaTime = 1.0f / 60.0f;

//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
Application::Application()
{
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
    // 画面の拡大率（125%など）で引き伸ばされず、モニター本来の解像度で描けるようにします。
    EnableDpiAwareness();

    // 起動オプション --capture <出力フォルダ> があれば、自動撮影モードにします。
    Tools::CaptureMode::ConfigureFromCommandLine();
    if (Tools::CaptureMode::IsActive())
    {
        // スクリーンショットの保存（WIC）にCOMを使います。
        (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }

    //初期化
    bool okfg = InitApp();
    if (okfg) { MainLoop(); }

    UninitApp(); // 終了処理
}

//-----------------------------------------------------------------------------
// 拡大率への対応
//-----------------------------------------------------------------------------
void Application::EnableDpiAwareness()
{
    // 対応していないと、拡大率125%の1920x1080画面では1536x864として扱われ、描いた画面が引き伸ばされてぼやけます。
    // SetProcessDpiAwarenessContextはWindows 10（1703）以降にしか無いため、無ければ古い関数を使います。
    using SetDpiAwarenessContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto setContext = user32 == nullptr
        ? nullptr
        : reinterpret_cast<SetDpiAwarenessContext>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext == nullptr ||
        !setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    {
        SetProcessDPIAware();
    }

    // 拡大率を記録します（マウスの移動量が実際の画素単位になるため、視点の速さをそろえるのに使います）。
    if (const HDC screen = GetDC(nullptr))
    {
        m_DpiScale = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX)) / 96.0f;
        ReleaseDC(nullptr, screen);
    }
}

//-----------------------------------------------------------------------------
// 描画解像度を決める
//-----------------------------------------------------------------------------
void Application::DecideRenderSize()
{
    m_Width = m_WindowWidth;
    m_Height = m_WindowHeight;
    // 自動撮影モードは、ウィンドウも描画も動画の大きさ（720p）です。
    if (Tools::CaptureMode::IsActive() && !Tools::CaptureMode::IsBenchmark())
    {
        return;
    }

    // 描画用のテクスチャはGameの初期化で作るため、設定はここで先に読みます。
    Core::GameSettings settings;
    settings.Load();
    m_LaunchResolutionLevel = settings.GetResolutionLevel();
    float scale = settings.GetRenderScale();
    if (scale <= 0.0f)
    {
        // 自動: 内蔵GPUは画素数を減らして軽くし、単体GPUはそのままの解像度で描きます。
        scale = Renderer::IsHighPerformanceAdapterIntegrated() ? 0.67f : 1.0f;
    }

    // HUDは画素単位で配置しているため、縦720より小さくはしません（ポーズ画面などが収まる大きさ）。
    constexpr float MinimumRenderHeight = 720.0f;
    const float windowHeight = static_cast<float>(m_WindowHeight);
    if (windowHeight * scale < MinimumRenderHeight)
    {
        scale = (std::min)(1.0f, MinimumRenderHeight / windowHeight);
    }
    // 幅と高さは偶数にそろえます（縮小用のテクスチャで端の1画素がずれないように）。
    m_Width = (static_cast<uint32_t>(static_cast<float>(m_WindowWidth) * scale) + 1u) & ~1u;
    m_Height = (static_cast<uint32_t>(windowHeight * scale) + 1u) & ~1u;
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

    // Direct3Dと画面効果用テクスチャを作る前にデスクトップ解像度を取得します。
    // 全画面時も全レンダーターゲットを同じ大きさに保つためです。
    m_WindowWidth = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
    m_WindowHeight = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));
    // 自動撮影モードは、動画の大きさ（720p）のウィンドウにします。計測モードは実際に遊ぶときと同じ大きさのままです。
    const bool capturing = Tools::CaptureMode::IsActive();
    if (capturing && !Tools::CaptureMode::IsBenchmark())
    {
        m_WindowWidth = Tools::CaptureMode::Width;
        m_WindowHeight = Tools::CaptureMode::Height;
    }
    // 描画解像度は、設定（自動ならGPUの種類）に応じてウィンドウより小さくすることがあります。
    DecideRenderSize();

    // ウィンドウのサイズを設定
    RECT rc = {};
    rc.right = static_cast<LONG>(m_WindowWidth);
    rc.bottom = static_cast<LONG>(m_WindowHeight);

    // ウィンドウサイズを調整
    auto style = WS_POPUP | WS_MINIMIZEBOX;

    // 自動撮影モードは、作業の邪魔にならないよう画面の外（全モニターの左端よりさらに左）に置きます。
    const int windowX = capturing
        ? GetSystemMetrics(SM_XVIRTUALSCREEN) - static_cast<int>(m_WindowWidth) - 64
        : 0;

    // ウィンドウを生成
    m_hWnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        ClassName,
        WindowName,
        style,
        windowX,
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

    // ウィンドウを表示（自動撮影モードは前面に出さず、フォーカスも奪いません）
    ShowWindow(m_hWnd, capturing ? SW_SHOWNOACTIVATE : SW_SHOW);

    // ウィンドウを更新
    UpdateWindow(m_hWnd);

    // ウィンドウにフォーカスを設定
    if (!capturing)
    {
        SetFocus(m_hWnd);
    }

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

    if (!Core::Game::Init())
    {
        MessageBoxA(m_hWnd,
            "DirectX 11の初期化に失敗しました。\n"
            "グラフィックドライバーとDirectX 11対応環境を確認してください。",
            "起動エラー", MB_OK | MB_ICONERROR);
        return;
    }

    using Clock = std::chrono::steady_clock;
    auto previousTime = Clock::now();
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

        m_DeltaTime = static_cast<float>(frameTime.count());
        // 自動撮影モードは1フレーム=1/30秒で進め、動画が実際の速さで再生されるようにします。
        if (Tools::CaptureMode::IsActive())
        {
            m_DeltaTime = Tools::CaptureMode::FrameSeconds;
        }
        Core::Game::Update();
        Core::Game::Draw();
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

        // 確認ダイアログへフォーカスが移るとWM_ACTIVATE(WA_INACTIVE)が届くため、
        // 表示中はフラグを立てて最小化処理を抑止します。
        isMessageBoxShowed = true;
        int res = MessageBoxA(hWnd, "終了しますか？", "確認", MB_OKCANCEL);
        isMessageBoxShowed = false;
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

