// ============================================================================
// ファイルの役割: Windowsのウィンドウを作り、メインループを回し、終了時の後片付けをしている。
// 主な技術: Win32 API、DeltaTime、メッセージループ、フレーム時間の上限処理
// Application.hで宣言した処理の中身（ウィンドウ生成・描画解像度の決定・メッセージ処理）をここで定義している。
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
    // 1フレームの経過時間の上限。ウィンドウのドラッグや停止から戻った直後に、大きな経過時間で
    // プレイヤーや演出が一気に進まないよう、0.25秒より長いフレームは0.25秒として扱っている。
    constexpr auto MaximumFrameTime = std::chrono::duration<double>(0.25);
}

HINSTANCE  Application::m_hInst;   // インスタンスハンドル（ウィンドウクラスの登録と解除に使っている）
HWND       Application::m_hWnd;    // ゲーム画面のウィンドウハンドル
uint32_t   Application::m_Width;   // 描画解像度の横幅（設定によってはウィンドウより小さい）
uint32_t   Application::m_Height;  // 描画解像度の縦幅
// ウィンドウ（モニター）の大きさ。描画解像度が小さいときは、表示の際にここまで引き伸ばしている。
uint32_t   Application::m_WindowWidth;
uint32_t   Application::m_WindowHeight;
// 起動時に読んだ「描画解像度」の設定段階（ポーズ画面で「次回起動から反映」を出すために覚えている）
int        Application::m_LaunchResolutionLevel = 0;
// 画面の拡大率（100%なら1.0、125%なら1.25）
float      Application::m_DpiScale = 1.0f;
// 前フレームからの経過秒。最初のフレームは60fps相当の値にしている。
float      Application::m_DeltaTime = 1.0f / 60.0f;

//-----------------------------------------------------------------------------
// コンストラクタ：Sleepやタイマーの精度を1ミリ秒に上げている
//-----------------------------------------------------------------------------
Application::Application()
{
    timeBeginPeriod(1); // タイマー精度を1ミリ秒にし、フレーム待ちのずれを小さくしている
}

//-----------------------------------------------------------------------------
// デストラクタ：上げたタイマー精度を元に戻している
//-----------------------------------------------------------------------------
Application::~Application()
{ 
    timeEndPeriod(1); // timeBeginPeriodと対にして、タイマー精度を元に戻している
}

//-----------------------------------------------------------------------------
// 実行：拡大率への対応・起動オプションの読み取り・初期化・メインループ・終了処理を順に呼んでいる
//-----------------------------------------------------------------------------
void Application::Run()
{
    // 画面の拡大率（125%など）で引き伸ばされず、モニター本来の解像度で描けるようにしている。
    EnableDpiAwareness();

    // 起動オプション --capture <出力フォルダ>（または --benchmark）があれば、自動撮影・計測モードにしている。
    Tools::CaptureMode::ConfigureFromCommandLine();
    if (Tools::CaptureMode::IsActive())
    {
        // スクリーンショットの保存（WIC）がCOMを使うため、撮影モードのときだけCOMを初期化している。
        (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }

    // ウィンドウを作り、成功したときだけメインループへ進んでいる
    bool okfg = InitApp();
    if (okfg) { MainLoop(); }

    UninitApp(); // 失敗した場合も含めて、登録したウィンドウクラスを解除している
}

//-----------------------------------------------------------------------------
// 拡大率への対応：DPIを認識するプロセスとして宣言し、拡大率を記録している
//-----------------------------------------------------------------------------
void Application::EnableDpiAwareness()
{
    // 対応していないと、拡大率125%の1920x1080画面では1536x864として扱われ、描いた画面が引き伸ばされてぼやける。
    // SetProcessDpiAwarenessContextはWindows 10（1703）以降にしか無いため、関数を名前で探し、無ければ古いSetProcessDPIAwareを使っている。
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

    // 拡大率を記録している（マウスの移動量が実際の画素単位になるため、視点の速さをそろえるのに使っている）。
    if (const HDC screen = GetDC(nullptr))
    {
        m_DpiScale = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX)) / 96.0f;
        ReleaseDC(nullptr, screen);
    }
}

//-----------------------------------------------------------------------------
// 描画解像度を決める：設定の倍率（自動ならGPUの種類で決めた倍率）をウィンドウの大きさに掛けている
//-----------------------------------------------------------------------------
void Application::DecideRenderSize()
{
    m_Width = m_WindowWidth;
    m_Height = m_WindowHeight;
    // 自動撮影モードは、ウィンドウも描画も動画の大きさ（720p）のままにしている。
    if (Tools::CaptureMode::IsActive() && !Tools::CaptureMode::IsBenchmark())
    {
        return;
    }

    // 描画用のテクスチャはGameの初期化で作るため、Gameより先にここで設定ファイルを読んでいる。
    Core::GameSettings settings;
    settings.Load();
    m_LaunchResolutionLevel = settings.GetResolutionLevel();
    float scale = settings.GetRenderScale();
    if (scale <= 0.0f)
    {
        // 自動: 内蔵GPUは画素数を減らして軽くし、単体GPUはそのままの解像度で描いている。
        scale = Renderer::IsHighPerformanceAdapterIntegrated() ? 0.67f : 1.0f;
    }

    // HUDの文字が潰れないよう、縦720より小さくはしていない（ポーズ画面などが収まる大きさ）。
    constexpr float MinimumRenderHeight = 720.0f;
    const float windowHeight = static_cast<float>(m_WindowHeight);
    if (windowHeight * scale < MinimumRenderHeight)
    {
        scale = (std::min)(1.0f, MinimumRenderHeight / windowHeight);
    }
    // 幅と高さは偶数にそろえている（半分の大きさの縮小用テクスチャで、端の1画素がずれないように）。
    m_Width = (static_cast<uint32_t>(static_cast<float>(m_WindowWidth) * scale) + 1u) & ~1u;
    m_Height = (static_cast<uint32_t>(windowHeight * scale) + 1u) & ~1u;
}

//-----------------------------------------------------------------------------
// 初期化処理：ウィンドウクラスを登録し、全画面のウィンドウを作って表示している
//-----------------------------------------------------------------------------
bool Application::InitApp()
{
    // 実行ファイル自身のインスタンスハンドルを取得している
    auto hInst = GetModuleHandle(nullptr);
    if (hInst == nullptr)
    {
        return false;
    }

    // ウィンドウクラスの設定（メッセージを受け取る関数・アイコン・カーソル・背景色）
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

    // ウィンドウクラスをWindowsに登録している
    if (!RegisterClassEx(&wc))
    {
        return false;
    }

    // 登録に成功したので、解除用にインスタンスハンドルを覚えている
    m_hInst = hInst;

    // Direct3Dと画面効果用テクスチャを作る前に、デスクトップ解像度を取得している。
    // 全画面のボーダーレスウィンドウにして、全レンダーターゲットを同じ大きさに保つためである。
    m_WindowWidth = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
    m_WindowHeight = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));
    // 自動撮影モードは、動画の大きさ（720p）のウィンドウにしている。計測モードは実際に遊ぶときと同じ大きさのまま。
    const bool capturing = Tools::CaptureMode::IsActive();
    if (capturing && !Tools::CaptureMode::IsBenchmark())
    {
        m_WindowWidth = Tools::CaptureMode::Width;
        m_WindowHeight = Tools::CaptureMode::Height;
    }
    // 描画解像度は、設定（自動ならGPUの種類）に応じてウィンドウより小さくすることがある。
    DecideRenderSize();

    // ウィンドウの大きさを、モニターの大きさの矩形として用意している
    RECT rc = {};
    rc.right = static_cast<LONG>(m_WindowWidth);
    rc.bottom = static_cast<LONG>(m_WindowHeight);

    // 枠もタイトルバーもないポップアップウィンドウにしている（最小化だけは受け付ける）
    auto style = WS_POPUP | WS_MINIMIZEBOX;

    // 自動撮影モードは、作業の邪魔にならないよう画面の外（全モニターの左端よりさらに左）に置いている。
    const int windowX = capturing
        ? GetSystemMetrics(SM_XVIRTUALSCREEN) - static_cast<int>(m_WindowWidth) - 64
        : 0;

    // ウィンドウを生成している
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

    // ウィンドウを表示している（自動撮影モードは前面に出さず、フォーカスも奪わない）
    ShowWindow(m_hWnd, capturing ? SW_SHOWNOACTIVATE : SW_SHOW);

    // WM_PAINTをすぐに処理させ、最初の表示を確定させている
    UpdateWindow(m_hWnd);

    // キーボード入力がゲームに届くよう、ウィンドウにフォーカスを移している
    if (!capturing)
    {
        SetFocus(m_hWnd);
    }

    // ここまで来れば初期化は成功している
    return true;

}

//-----------------------------------------------------------------------------
// 終了処理：ウィンドウクラスの登録を解除し、ハンドルを無効にしている
//-----------------------------------------------------------------------------
void Application::UninitApp()
{
    // InitAppで登録したウィンドウクラスを解除している
    if (m_hInst != nullptr)
    {
        UnregisterClass(ClassName, m_hInst);
    }

    m_hInst = nullptr;
    m_hWnd = nullptr;
}

//-----------------------------------------------------------------------------
// メインループ：Windowsのメッセージを処理しながら、ゲームの更新と描画を毎フレーム呼んでいる
//-----------------------------------------------------------------------------
void Application::MainLoop()
{
    MSG msg = {};

    // ゲーム全体（描画・音・入力・最初のScene）を初期化している。失敗したら原因の候補を表示して終わる。
    if (!Core::Game::Init())
    {
        MessageBoxA(m_hWnd,
            "DirectX 11の初期化に失敗しました。\n"
            "グラフィックドライバーとDirectX 11対応環境を確認してください。",
            "起動エラー", MB_OK | MB_ICONERROR);
        return;
    }

    // 経過時間は、システム時刻の変更に影響されないsteady_clockで測っている。
    using Clock = std::chrono::steady_clock;
    auto previousTime = Clock::now();
    bool running = true;

    while (running)
    {
        // 溜まっているメッセージをすべて処理してから、ゲームを1フレーム進めている（PeekMessageは待たずに戻る）。
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

        // 前フレームからの経過時間を測り、上限（0.25秒）を超えたら切り詰めている。
        const auto currentTime = Clock::now();
        auto frameTime = std::chrono::duration<double>(currentTime - previousTime);
        previousTime = currentTime;

        if (frameTime > MaximumFrameTime)
        {
            frameTime = MaximumFrameTime;
        }

        m_DeltaTime = static_cast<float>(frameTime.count());
        // 自動撮影モードは1フレーム=1/30秒で進め、処理が遅くても動画が実際の速さで再生されるようにしている。
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
// ウィンドウプロシージャ：終了確認・切り替え時の最小化・大きさの変更などのメッセージを処理している
//-----------------------------------------------------------------------------
LRESULT CALLBACK Application::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // ゲームは常に全画面なので、別のウィンドウへ切り替えたときは最小化している。
    // isMessageBoxShowedは、終了確認の表示中に最小化しないためのフラグ。
    static bool isFullscreen = true;
    static bool isMessageBoxShowed = false;
    // Debug構成ではImGuiが先にメッセージを受け取り、使った場合はここで処理を終えている。
    if (Debug::UI::HandleWindowMessage(hWnd, uMsg, wParam, lParam))
    {
        return 1;
    }

    switch (uMsg)
    {
    case WM_DESTROY:// ウィンドウが破棄された
        PostQuitMessage(0);// WM_QUITを送り、メインループを抜けさせている
        break;

    case WM_CLOSE:  // Alt+F4などでウィンドウを閉じようとした
    {
        // 確認ダイアログを操作できるよう、隠していたマウスカーソルを一時的に表示している
        ShowCursor(TRUE);

        // 確認ダイアログへフォーカスが移るとWM_ACTIVATE(WA_INACTIVE)が届くため、
        // 表示中はフラグを立てて最小化処理を止めている。
        isMessageBoxShowed = true;
        int res = MessageBoxA(hWnd, "終了しますか？", "確認", MB_OKCANCEL);
        isMessageBoxShowed = false;
        if (res == IDOK) {
            DestroyWindow(hWnd);  // WM_DESTROYが届き、アプリが終了する
        }
        else {
            // キャンセルされたら、ゲームに戻るのでカーソルをまた隠している
            ShowCursor(FALSE);
        }
    }
    break;

    case WM_ACTIVATE:
        if (wParam == WA_INACTIVE) {
            // 全画面表示で、終了確認も出していないときだけ
            if (isFullscreen && !isMessageBoxShowed)
            {
                // ウィンドウを最小化している（Alt+Tabで切り替えたとき、ゲーム画面が背後に残って邪魔にならないように）
                ShowWindow(hWnd, SW_MINIMIZE);
            }
        }
        // アクティブ化の標準処理はWindowsに任せている
        return DefWindowProc(hWnd, uMsg, wParam, lParam);

    case WM_SIZE: // ウィンドウの大きさが変わった

        if (wParam != SIZE_MINIMIZED)
        {
            int width = LOWORD(lParam); // 新しい横幅
            int height = HIWORD(lParam); // 新しい縦幅
            // スワップチェーンのバックバッファを新しい大きさで作り直している
            Renderer::ResizeWindow(width, height);
        }
        break;

    default:
        // それ以外のメッセージは、Windowsの標準処理に任せている
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
        break;
    }

    return 0;
}

