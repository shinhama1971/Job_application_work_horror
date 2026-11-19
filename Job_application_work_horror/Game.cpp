// ============================================================================
// ファイルの役割: ゲーム全体のObjectの所有、更新と描画の順番、シーンの切り替えをまとめて管理している。
// 主な技術: RAII、unique_ptr、Objectの追加・削除の後回し、シングルトン、決まった更新順
// ============================================================================

#include "Game.h"
#include "AmbientOcclusion.h"
#include "DDGIVolume.h"
#include "FigureVolumes.h"
#include "GpuTimer.h"
#include "LightShafts.h"
#include "PointShadowMap.h"
#include "PostProcess.h"
#include "ReflectionProbe.h"
#include "ScreenSpaceReflection.h"
#include "ShadowMap.h"
#include "TiledLighting.h"
#include "Renderer.h"
#include "Input.h"
#include "Scene.h"
#include "TitleScene.h"
#include "StageScene.h"
#include "Stage2Scene.h"
#include "Stage3Scene.h"
#include "ForestScene.h"
#include "IntroScene.h"
#include "ResultScene.h"
#include "Shader.h"
#include "ModelCache.h"
#include "DebugUI.h"
#include "Application.h"
#include "CaptureMode.h"
#include "Wall.h"
#include "Door.h"

namespace
{
    // 環境マップ（キューブマップ）を設定するテクスチャの番号（シェーダーの t14）
    constexpr UINT EnvironmentProbeTextureSlot = 14;
    // 環境マップで何も描かれない所の色。闇に溶けるよう、ほとんど黒にわずかな青みを残している
    const DirectX::SimpleMath::Color EnvironmentProbeClearColor(0.012f, 0.013f, 0.014f, 1.0f);
    // ポーズを開け閉めしたときの、コントローラーの短く弱い振動（60fpsで数えたフレーム数と強さ）
    constexpr int PauseToggleVibrationFrames = 2;
    constexpr float PauseToggleVibrationStrength = 0.06f;
    // 聞き手と音源の間の壁と閉じた扉の数による、音のこもり具合。1枚でほぼこもり、この枚数からは完全に向こう側の音にしている
    constexpr int FullyMuffledBlockerCount = 2;
    constexpr float SingleBlockerMuffle = 0.8f;
    constexpr float FullMuffle = 1.0f;
    // 導入の受信記録の間に、後ろで小さく鳴らす無線の雑音の音量
    constexpr float IntroRadioStaticVolume = 0.35f;
}

namespace Core
{
    std::unique_ptr<Game> Game::m_Instance;

    // 描画の仕組みの入れ物を作り（中身の初期化はデバイスができた後の Init）、ベスト記録と設定ファイルを読み込んでいる
    Game::Game()
        : m_PostProcess(std::make_unique<Effect::PostProcess>()),
          m_ShadowMap(std::make_unique<Effect::ShadowMap>()),
          m_ReflectionProbe(std::make_unique<Effect::ReflectionProbe>()),
          m_EnvironmentProbe(std::make_unique<Effect::ReflectionProbe>()),
          m_PointShadowMap(std::make_unique<Effect::PointShadowMap>()),
          m_FigureVolumes(std::make_unique<Effect::FigureVolumes>()),
          m_AmbientOcclusion(std::make_unique<Effect::AmbientOcclusion>()),
          m_ScreenSpaceReflection(std::make_unique<Effect::ScreenSpaceReflection>()),
          m_TiledLighting(std::make_unique<Effect::TiledLighting>()),
          m_GpuTimer(std::make_unique<GpuTimer>())
    {
        LoadBestRecord();
        m_Settings.Load();
    }

    // 残っているObjectをすべて解放している
    Game::~Game()
    {
        m_ObjectManager.DeleteAll();
    }

    // 描画・入力・音などの仕組みを、依存する順に初期化し、最初のタイトルシーンを作っている。
    bool Game::Init()
    {
        if (m_Instance)
        {
            return true;
        }

        m_Instance = std::make_unique<Game>();

        // D3Dデバイスが無いとこの後の全部の仕組みが動かないため、ここで中断している。
        if (FAILED(Renderer::Init()))
        {
            Renderer::Uninit();
            m_Instance.reset();
            return false;
        }
        // GPU時間の計測とデバッグ画面は、デバイスができた後に作っている
        m_Instance->m_GpuTimer->Init(Renderer::GetDevice());
        Debug::UI::Init(Application::GetWindow());


        // キーボード・マウス・コントローラーの入力を使えるようにしている
        Input::Create();

        // 音が使えない環境ではfalseのまま進み、描画とゲームの進行は続けている。
        m_Instance->m_SoundReady = SUCCEEDED(m_Instance->m_Sound.Init());
        if (m_Instance->m_SoundReady)
        {
            m_Instance->ApplyAudioVolume(false);
            // 位置付きの音が壁や閉じた扉の向こうにあるかを、ステージのObjectで判定させている。
            m_Instance->m_Sound.SetOcclusionQuery(
                [](const DirectX::SimpleMath::Vector3& listener,
                    const DirectX::SimpleMath::Vector3& emitter)
                {
                    return m_Instance->ComputeSoundOcclusion(listener, emitter);
                });
        }

        // カメラ・水面の反射・影・画面効果・タイルベースライティングを作り、設定を反映してからタイトルを始めている
        m_Instance->m_Camera.Init();
        m_Instance->m_ReflectionProbe->Init();
        m_Instance->m_EnvironmentProbe->Init(EnvironmentProbeTextureSlot, true);
        m_Instance->m_EnvironmentProbe->SetClearColor(EnvironmentProbeClearColor);
        m_Instance->m_FigureVolumes->Init();
        m_Instance->m_LightShafts = std::make_unique<Effect::LightShafts>();
        m_Instance->m_LightShafts->Init(static_cast<int>(Application::GetWidth()), static_cast<int>(Application::GetHeight()));
        m_Instance->m_AmbientOcclusion->Init(static_cast<int>(Application::GetWidth()), static_cast<int>(Application::GetHeight()));
        m_Instance->m_GlobalIllumination = std::make_unique<Effect::DDGIVolume>();
        m_Instance->m_GlobalIllumination->Init(static_cast<int>(Application::GetWidth()), static_cast<int>(Application::GetHeight()));
        m_Instance->m_ScreenSpaceReflection->Init(Application::GetWidth(), Application::GetHeight());
        m_Instance->m_TiledLighting->Init();
        if (!m_Instance->m_ShadowMap->Init() || !m_Instance->m_PointShadowMap->Init() ||
            !m_Instance->m_PostProcess->Init())
        {
            Uninit();
            return false;
        }
        if (Tools::CaptureMode::GetEffectLevelOverride() >= 0)
            m_Instance->m_Settings.SetEffectLevel(Tools::CaptureMode::GetEffectLevelOverride());
        m_Instance->ApplyVisualSettings();
        // --forest を付けて起動したときは、タイトルの代わりに外の森から始めている
        const SceneName initialScene = Stage3Scene::IsRequestedByCommandLine()
            ? SceneName::Stage3
            : (ForestScene::IsRequestedByCommandLine() ? SceneName::Forest : SceneName::Title);
        m_Instance->ChangeScene(initialScene);
        return true;
    }

    // 1フレームの更新の順番:
    // 入力 → シーン → カメラ/画面効果 → Object → 破棄 → シーンの切り替え → 予約した追加。
    // シーンを切り替えるフレームでは、予約した追加は捨てている。
    // 後回しの処理を最後に置くことで、Objectの配列を順に処理している途中で要素が増えたり減ったりしない。
    // Alt+Tab などで別のウィンドウへ移ったとき、遊んでいる途中ならポーズメニューを開いている。
    // ウィンドウは最小化されるが、そのままだと裏で敵や時間が進み、戻ったときに捕まっていることがあるため。
    // 自動撮影・計測の間は、決めた道順を止めないよう何もしない。
    void Game::OnFocusLost()
    {
        if (!m_Instance || Tools::CaptureMode::IsActive())
        {
            return;
        }

        Input::OnFocusLost();
        m_Instance->m_Camera.ResetMouseLook();
        if (m_Instance->m_PauseMenu.IsOpen())
        {
            return;
        }

        const SceneName scene = m_Instance->m_CurrentScene;
        const bool gameplayScene =
            scene == SceneName::Stage ||
            scene == SceneName::Stage2 ||
            scene == SceneName::Stage3 ||
            scene == SceneName::Forest ||
            scene == SceneName::Intro;
        if (gameplayScene)
        {
            m_Instance->m_PauseMenu.Open();
            m_Instance->ApplyAudioVolume(true);
        }
    }

    void Game::Update()
    {
        Input::Update();

        // 1面・2面・外の森・導入の間は、Esc・P・STARTでポーズメニューを開け閉めしている（デバッグ画面を開いている間は除く）。
        // メニューは各シーンの Draw が描いている（タイトルには終了の項目があり、リザルトからはタイトルへ戻れる）
        const bool gameplayScene =
            m_Instance->m_CurrentScene == SceneName::Stage ||
            m_Instance->m_CurrentScene == SceneName::Stage2 ||
            m_Instance->m_CurrentScene == SceneName::Stage3 ||
            m_Instance->m_CurrentScene == SceneName::Forest ||
            m_Instance->m_CurrentScene == SceneName::Intro;
        const bool pausePressed =
            Input::GetKeyTrigger(VK_ESCAPE) ||
            Input::GetKeyTrigger(VK_P) ||
            Input::GetButtonTrigger(XINPUT_START);
        if (gameplayScene && !Debug::UI::IsVisible() && pausePressed)
        {
            PauseMenu& pauseMenu = m_Instance->m_PauseMenu;
            if (pauseMenu.IsOpen())
            {
                pauseMenu.Close();
            }
            else
            {
                pauseMenu.Open();
            }
            m_Instance->ApplyAudioVolume(pauseMenu.IsOpen());
            Input::SetVibration(PauseToggleVibrationFrames, PauseToggleVibrationStrength);
            return;
        }

        // ポーズ中はメニューの操作だけを処理し、ゲームは止めている
        if (m_Instance->m_PauseMenu.IsOpen())
        {
            m_Instance->UpdatePauseMenu();
            return;
        }

        // デバッグ画面で「調整中はゲームを止める」にしているときは、画面効果だけを更新している
        if (Debug::UI::ShouldPauseGameplay())
        {
            Debug::UI::ApplyTuning(*m_Instance->m_PostProcess);
            m_Instance->m_PostProcess->Update();
            return;
        }

        // 1面・2面で遊んでいる時間を数えている（リザルト画面のタイムになる）
        if (gameplayScene)
        {
            m_Instance->m_State.AddRunTime(Application::GetDeltaTime());
        }


        // シーンの更新（進行の判定やイベント）を、Objectの更新より先に行っている
        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->Update();
        }

        // デバッグ画面の調整値を反映し、カメラと画面効果を更新している
        Debug::UI::ApplyTuning(*m_Instance->m_PostProcess);

        m_Instance->m_Camera.Update();
        m_Instance->m_PostProcess->Update();

        // 自動撮影モード（--capture）のときだけ、プレイヤーと視点を決めた道順で動かしている。
        Tools::CaptureMode::UpdateBeforeObjects();

        // 全Objectを更新し、破棄を予約されたObjectを解放している
        m_Instance->m_ObjectManager.UpdateAll();
        m_Instance->m_ObjectManager.RemoveDestroyed();

        // Playerがカメラを動かした後の位置と向きで、鳴っている音の聞こえ方を更新している。
        if (m_Instance->m_SoundReady)
        {
            SoundListener listener;
            listener.Position = m_Instance->m_Camera.GetPosition();
            listener.Forward = m_Instance->m_Camera.GetForward();
            m_Instance->m_Sound.UpdateListener(listener, Application::GetDeltaTime());
        }

        // シーンの切り替えと予約の後始末は ChangeScene に任せている
        if (m_Instance->m_PendingScene.has_value())
        {
            m_Instance->ChangeScene(*m_Instance->m_PendingScene);
            return;
        }

        // 切り替えがなければ、予約された追加をここで実行している
        m_Instance->m_ObjectManager.FlushPendingCommands();
    }

    // 作った順と逆の順で解放している。unique_ptrやComPtrで持っている物は、resetで確実に破棄している。
    void Game::Uninit()
    {
        if (m_Instance == nullptr) return;

        Tools::CaptureMode::Shutdown();

        m_Instance->m_Camera.Uninit();

        m_Instance->m_Scene.reset();

        m_Instance->m_ObjectManager.DeleteAll();
        m_Instance->m_TiledLighting->Uninit();
        m_Instance->m_PostProcess->Uninit();
        m_Instance->m_ShadowMap->Uninit();
        m_Instance->m_ReflectionProbe->Uninit();
        m_Instance->m_EnvironmentProbe->Uninit();
        m_Instance->m_PointShadowMap->Uninit();
        m_Instance->m_AmbientOcclusion->Uninit();
        m_Instance->m_GlobalIllumination->Uninit();
        m_Instance->m_ScreenSpaceReflection->Uninit();

        m_Instance->m_Sound.Uninit();
        m_Instance->m_SoundReady = false;

        Input::Release();
        Debug::UI::Uninit();
        m_Instance->m_GpuTimer->Uninit();

        // D3Dデバイスが有効な間に、キャッシュとGameが持つGPUリソースを解放している。
        // こうしておくと、Renderer::Uninitのデバッグ出力が、Gameに残った参照ではなく
        // 本当のリークだけを報告できる。
        Shader::ClearCache();
        ModelCache::Clear();
        m_Instance.reset();

        Renderer::Uninit();
    }

    // Gameのただ1つのインスタンスを返している
    Game* Core::Game::GetInstance()
    {
        return m_Instance.get();
    }

    // ポーズ中の入力はPauseMenuが解釈し、ここでは設定の反映とシーンの操作だけを行っている。
    void Game::UpdatePauseMenu()
    {
        const PauseMenu::Result result = m_PauseMenu.Update(m_Settings);
        // 設定が変わったら、見た目と音量に反映し、ファイルへ保存している
        if (result.settingsChanged)
        {
            ApplyVisualSettings();
            ApplyAudioVolume(true);
            if (!m_Settings.Save())
            {
                MessageBoxW(Application::GetWindow(),
                    L"設定を保存できませんでした。変更は今回のプレイ中だけ反映されます。",
                    L"保存エラー", MB_OK | MB_ICONWARNING);
            }
            if (result.changedItem == PauseMenu::Item::Volume)
            {
                // 変更した後の音量を確かめられるよう、短い効果音を鳴らしている。
                PlayAudioCue(SOUND_CUE_PICKUP);
            }
        }

        switch (result.command)
        {
        case PauseMenu::Command::RestartStage:
            // 1面はBeginRunで全体を初期化するため、2面だけ開始時点の状態へ戻している。
            if (m_CurrentScene == SceneName::Stage2)
            {
                m_State.RestoreStage2Start();
            }
            ChangeScene(m_CurrentScene);
            break;
        case PauseMenu::Command::ReturnToTitle:
            ChangeScene(SceneName::Title);
            break;
        case PauseMenu::Command::Quit:
            // ゲームを終える：ウィンドウを閉じる操作と同じ扱いにして、終了の確認を出している
            PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
            break;
        case PauseMenu::Command::None:
            break;
        }
    }

    // 明るさ・演出の強さ・視点の感度の設定を、画面効果とカメラへ反映している
    void Game::ApplyVisualSettings()
    {
        m_PostProcess->SetUserBrightnessOffset(m_Settings.GetBrightnessOffset());
        m_PostProcess->SetUserEffectScale(m_Settings.GetEffectScale());
        m_Camera.SetLookSensitivityScale(m_Settings.GetLookSensitivityScale());
    }

    // シーンの切り替えは予約だけしている。実際の破棄・生成はUpdateの最後の安全な位置で行っている。
    void Game::RequestSceneChange(SceneName sName)
    {
        if (m_PendingScene.has_value())
        {
            return;
        }

        m_PendingScene = sName;
    }

    // 聞き手から音源までの間にある壁と閉じた扉を数え、音のこもり具合（0〜1）を返している
    float Game::ComputeSoundOcclusion(
        const DirectX::SimpleMath::Vector3& listener,
        const DirectX::SimpleMath::Vector3& emitter)
    {
        // 音源の少し手前までを調べ、扉の音が扉自身にさえぎられないようにしている。
        DirectX::SimpleMath::Vector3 toEmitter = emitter - listener;
        const float distance = toEmitter.Length();
        constexpr float EmitterClearance = 6.0f;
        if (distance <= EmitterClearance)
        {
            return 0.0f;
        }
        toEmitter /= distance;
        const DirectX::SimpleMath::Vector3 end =
            listener + toEmitter * (distance - EmitterClearance);

        // 壁1枚でほぼこもり、FullyMuffledBlockerCount 枚以上で完全に向こう側の音にしている。
        int blockers = 0;
        for (const Wall* wall : GetObjects<Wall>())
        {
            float hitDistance = 0.0f;
            if (wall->IntersectsInteractionSegment(listener, end, hitDistance) &&
                ++blockers >= FullyMuffledBlockerCount)
            {
                return FullMuffle;
            }
        }
        for (const Door* door : GetObjects<Door>())
        {
            if (door->BlocksSoundSegment(listener, end) && ++blockers >= FullyMuffledBlockerCount)
            {
                return FullMuffle;
            }
        }
        return blockers == 0 ? 0.0f : SingleBlockerMuffle;
    }

    // 今のSceneとObjectを終了してから、指定された次のSceneを1つだけ作っている。
    void Game::ChangeScene(SceneName sName)
    {
        const SceneName previousScene = m_CurrentScene;
        const bool introFromForest = sName == SceneName::Intro && previousScene == SceneName::Forest;
        // リザルト画面へ移るときは、プレイを終えた記録を付けてベスト記録を保存している
        if (sName == SceneName::Result && m_State.CompleteRun(!Tools::CaptureMode::IsActive()) && !SaveBestRecord())
        {
            MessageBoxW(Application::GetWindow(), L"ベスト記録を保存できませんでした。",
                L"保存エラー", MB_OK | MB_ICONWARNING);
        }

        // ポーズメニューを閉じ、音量を戻し、前のシーンを解放している。反射と影の描き直しの数え方も最初からにしている
        m_CurrentScene = sName;
        m_PauseMenu.Close();
        m_ListenerMuffle = 0.0f;
        ApplyAudioVolume(false);
        m_Scene.reset();
        m_ShadowFrameIndex = 0;
        // 前の画面の明るさに慣れたままにならないよう、自動露出は新しいシーンの最初の画面にすぐ合わせている。
        m_PostProcess->ResetAutoExposure();
        // 前の場所の効果音と環境音を止めてから、新しいシーンの音を始める。
        if (m_SoundReady)
        {
            m_Sound.StopAll();
        }
        m_PostProcess->SetPresenceStatic(0.0f);
        // 前の場所の景色を金属に映さないよう、環境マップを次の描画で6面とも撮り直させている
        m_EnvironmentProbe->Invalidate();
        m_ScreenSpaceReflection->Invalidate();
        // 光の筋の揺らぎの時間も最初に戻している（前のシーン（タイトルの後ろの森など）の長さで、次のシーンの見え方が変わらないように）
        m_LightShafts->ResetTime();

        m_ObjectManager.DeleteAll();
        m_PendingScene.reset();

        // 新しいシーンを作っている。1面の開始でプレイ全体の記録を始め、2面の開始で2面の開始時点を覚えている。
        // タイトルで2面を選んで始めたときは、そこで記録を始めている（ベスト記録には残さないプレイにしている）
        switch (sName)
        {
        case SceneName::Title:
            m_Scene = std::make_unique<TitleScene>();
            break;

        case SceneName::Stage:
            m_State.BeginRun();
            m_Scene = std::make_unique<StageScene>();
            break;

        case SceneName::Stage2:
            if (previousScene == SceneName::Title)
            {
                m_State.BeginRun(false);
            }
            m_State.EnterStage2();
            m_Scene = std::make_unique<Stage2Scene>();
            break;

        case SceneName::Result:
            m_Scene = std::make_unique<ResultScene>();
            break;

        case SceneName::Stage3:
            // タイトルから3面を選んだときは、2面から始めたときと同じく、ベスト記録に残さないプレイとして始めている。
            // 2面の階段を上ってきたときは、それまでの時間と捕まった回数を引き継いでいる
            if (previousScene == SceneName::Title)
            {
                m_State.BeginRun(false);
                m_State.EnterStage2();
            }
            m_Scene = std::make_unique<Stage3Scene>();
            break;

        case SceneName::Forest:
            m_Scene = std::make_unique<ForestScene>();
            break;

        case SceneName::Intro:
            m_Scene = std::make_unique<IntroScene>(introFromForest);
            break;
        }

        // 階ごとに違う環境音へ切り替え、タイトルとリザルトでは止めている。
        if (m_SoundReady)
        {
            // 外の森が映る画面（タイトルの後ろ・導入・外の森）では、雨音をループさせている（ForestRain と同じ雨）
            const bool showTransmission = sName == SceneName::Intro && !introFromForest;
            const bool forestOnScreen = sName == SceneName::Title || sName == SceneName::Forest || showTransmission;
            if constexpr (ForestScene::RainEnabled)
            {
                if (forestOnScreen)
                {
                    // 雨音は雨の強さに合わせて小さくしている（強さ0で素材の18%、1で50%。強さ0.35で約3割）。
                    // 素材のままだと大きく、足音や物音が埋もれるので、全体を素材の半分以下に抑えている
                    constexpr float RainVolumeAtNoRain = 0.18f;
                    constexpr float RainVolumeAtDownpour = 0.50f;
                    const float rainVolume = RainVolumeAtNoRain + (RainVolumeAtDownpour - RainVolumeAtNoRain) * ForestScene::RainStrength;
                    m_Sound.Play(SOUND_CUE_RAIN, 1.0f, rainVolume);
                }
            }
            // 導入の受信記録の間は、無線の雑音を小さく鳴らしている（「通信途絶」で IntroScene が止める）。
            // 新しいシーンの環境音と同じタイミングで鳴らし始めている。
            if (showTransmission)
            {
                m_Sound.Play(SOUND_CUE_RADIO_STATIC, 1.0f, IntroRadioStaticVolume);
            }
            if (sName == SceneName::Stage)
            {
                m_Sound.Play(SOUND_CUE_AMBIENCE_STAGE1);
            }
            else if (sName == SceneName::Stage2)
            {
                m_Sound.Play(SOUND_CUE_AMBIENCE_STAGE2);
            }
            // 携帯ラジオの雑音は、1面と2面の間ずっと音量0で鳴らしておき、影が近いときだけ大きくしている（RadioStatic）
            if (sName == SceneName::Stage || sName == SceneName::Stage2)
            {
                m_Sound.Play(SOUND_CUE_RADIO_STATIC, 1.0f, 0.0f);
            }
        }
    }


    // 名前で指定してObjectの破棄を予約している
    void Game::DestroyObj(const std::string& name)
    {
        m_ObjectManager.DestroyNamedObject(name);
    }

}
