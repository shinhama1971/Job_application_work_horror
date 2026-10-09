// ============================================================================
// ファイルの役割: ゲーム全体のObjectの所有、更新と描画の順番、シーンの切り替えをまとめて管理している。
// 主な技術: RAII、unique_ptr、Objectの追加・削除の後回し、シングルトン、決まった更新順
// ============================================================================

#include "Game.h"
#include "Renderer.h"
#include "Input.h"
#include "Scene.h"
#include "TitleScene.h"
#include "StageScene.h"
#include "Stage2Scene.h"
#include "ResultScene.h"
#include "Shader.h"
#include "ModelCache.h"
#include "DebugUI.h"
#include "Application.h"
#include "CaptureMode.h"
#include "Wall.h"
#include "Door.h"

namespace Core
{
    std::unique_ptr<Game> Game::m_Instance;

    // 作った時点で、ベスト記録と設定ファイルを読み込んでいる
    Game::Game()
    {
        LoadBestRecord();
        m_Settings.Load();
    }

    // 残っているObjectをすべて解放している
    Game::~Game()
    {
        DeleteAllObject();
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
        m_Instance->m_GpuTimer.Init(Renderer::GetDevice());
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
        m_Instance->m_PlanarReflection.Init();
        m_Instance->m_ShadowMap.Init();
        m_Instance->m_PostProcess.Init();
        m_Instance->m_TiledLighting.Init();
        m_Instance->ApplyVisualSettings();
        m_Instance->ChangeScene(SceneName::Title);
        return true;
    }

    // 1フレームの更新の順番:
    // 入力 → シーン → カメラ/画面効果 → Object → 破棄 → シーンの切り替え → 予約した追加。
    // シーンを切り替えるフレームでは、予約した追加は捨てている。
    // 後回しの処理を最後に置くことで、Objectの配列を順に処理している途中で要素が増えたり減ったりしない。
    void Game::Update()
    {
        Input::Update();

        // 1面・2面の間だけ、Esc・P・STARTでポーズメニューを開け閉めしている（デバッグ画面を開いている間は除く）
        const bool gameplayScene =
            m_Instance->m_CurrentScene == SceneName::Stage ||
            m_Instance->m_CurrentScene == SceneName::Stage2;
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
            Input::SetVibration(2, 0.06f);
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
            Debug::UI::ApplyTuning(m_Instance->m_PostProcess);
            m_Instance->m_PostProcess.Update();
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
        Debug::UI::ApplyTuning(m_Instance->m_PostProcess);

        m_Instance->m_Camera.Update();
        m_Instance->m_PostProcess.Update();

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

        // シーンの切り替えが予約されていれば、予約された追加を捨ててから切り替えている
        if (m_Instance->m_PendingScene.has_value())
        {
            m_Instance->m_ObjectManager.ClearPendingCommands();

            const SceneName nextScene =
                m_Instance->m_PendingScene.value();

            m_Instance->m_PendingScene.reset();
            m_Instance->ChangeScene(nextScene);
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
        m_Instance->m_TiledLighting.Uninit();
        m_Instance->m_PostProcess.Uninit();
        m_Instance->m_ShadowMap.Uninit();
        m_Instance->m_PlanarReflection.Uninit();

        m_Instance->m_Sound.Uninit();
        m_Instance->m_SoundReady = false;

        Input::Release();
        Debug::UI::Uninit();
        m_Instance->m_GpuTimer.Uninit();

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
            m_Settings.Save();
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
        m_PostProcess.SetUserBrightnessOffset(m_Settings.GetBrightnessOffset());
        m_PostProcess.SetUserEffectScale(m_Settings.GetEffectScale());
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

        // 壁1枚でほぼこもり（0.8）、2枚以上で完全に向こう側の音（1.0）にしている。
        int blockers = 0;
        for (const Wall* wall : GetObjects<Wall>())
        {
            float hitDistance = 0.0f;
            if (wall->IntersectsInteractionSegment(listener, end, hitDistance) &&
                ++blockers >= 2)
            {
                return 1.0f;
            }
        }
        for (const Door* door : GetObjects<Door>())
        {
            if (door->BlocksSoundSegment(listener, end) && ++blockers >= 2)
            {
                return 1.0f;
            }
        }
        return blockers == 0 ? 0.0f : 0.8f;
    }

    // 今のSceneとObjectを終了してから、指定された次のSceneを1つだけ作っている。
    void Game::ChangeScene(SceneName sName)
    {
        const SceneName previousScene = m_CurrentScene;
        // リザルト画面へ移るときは、プレイを終えた記録を付けてベスト記録を保存している
        if (sName == SceneName::Result)
        {
            m_State.CompleteRun();
            SaveBestRecord();
        }

        // ポーズメニューを閉じ、音量を戻し、前のシーンを解放している。反射と影の描き直しの数え方も最初からにしている
        m_CurrentScene = sName;
        m_PauseMenu.Close();
        ApplyAudioVolume(false);
        m_Scene.reset();
        m_ReflectionFrameIndex = 0;
        m_ShadowFrameIndex = 0;
		m_WasReflectionVisible = false;
        m_HasReflectionCameraPose = false;
        // 前の場所で鳴っていた位置付きの音（足音・扉など）を持ち越さないよう止めている。
        if (m_SoundReady)
        {
            m_Sound.StopAllSpatial();
        }

        DeleteAllObject();

        // 新しいシーンを作っている。1面の開始でプレイ全体の記録を始め、2面の開始で2面の開始時点を覚えている
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
            m_State.EnterStage2();
            m_Scene = std::make_unique<Stage2Scene>();
            break;

        case SceneName::Result:
            m_Scene = std::make_unique<ResultScene>();
            break;
        }

        // 階ごとに違う環境音へ切り替え、タイトルとリザルトでは止めている。
        if (m_SoundReady)
        {
            (void)previousScene;
            m_Sound.Stop(SOUND_CUE_AMBIENCE_STAGE1);
            m_Sound.Stop(SOUND_CUE_AMBIENCE_STAGE2);
            if (sName == SceneName::Stage)
            {
                m_Sound.Play(SOUND_CUE_AMBIENCE_STAGE1);
            }
            else if (sName == SceneName::Stage2)
            {
                m_Sound.Play(SOUND_CUE_AMBIENCE_STAGE2);
            }
        }
    }


    // Objectの破棄を予約している（解放はUpdateの破棄処理で行っている）
    void Game::DeleteObject(Object* pt)
    {
        m_ObjectManager.DeleteObject(pt);
    }

    // 名前で指定してObjectの破棄を予約している
    void Game::DestroyObj(const std::string& name)
    {
        m_ObjectManager.DestroyNamedObject(name);
    }

    // 全Objectをその場で解放している
    void Game::DeleteAllObject()
    {
        m_ObjectManager.DeleteAll();
    }
}
