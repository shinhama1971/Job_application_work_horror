// ============================================================================
// ファイルの役割: ゲーム全体のオブジェクト所有、更新・描画順、シーン遷移をまとめる
// 主な技術: RAII、unique_ptr、遅延追加・削除、シングルトン、固定更新順
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
#include "Wall.h"
#include "Door.h"

namespace Core
{
    std::unique_ptr<Game> Game::m_Instance;

    Game::Game()
    {
        LoadBestRecord();
        m_Settings.Load();
    }

    Game::~Game()
    {
        DeleteAllObject();
    }

    // サブシステムを依存順に初期化し、最初のタイトルシーンを生成します。
    bool Game::Init()
    {
        if (m_Instance)
        {
            return true;
        }

        m_Instance = std::make_unique<Game>();

        // D3Dデバイスが無いと以降の全システムが動作しないため、ここで中断します。
        if (FAILED(Renderer::Init()))
        {
            Renderer::Uninit();
            m_Instance.reset();
            return false;
        }
        m_Instance->m_GpuTimer.Init(Renderer::GetDevice());
        Debug::UI::Init(Application::GetWindow());


        Input::Create();

        // 音声が使えない環境ではfalseのまま進み、描画とゲーム進行は継続します。
        m_Instance->m_SoundReady = SUCCEEDED(m_Instance->m_Sound.Init());
        if (m_Instance->m_SoundReady)
        {
            m_Instance->ApplyAudioVolume(false);
            // 位置付きの音が壁や閉じた扉の向こうにあるかを、ステージのObjectで判定させます。
            m_Instance->m_Sound.SetOcclusionQuery(
                [](const DirectX::SimpleMath::Vector3& listener,
                    const DirectX::SimpleMath::Vector3& emitter)
                {
                    return m_Instance->ComputeSoundOcclusion(listener, emitter);
                });
        }

        m_Instance->m_Camera.Init();
        m_Instance->m_PlanarReflection.Init();
        m_Instance->m_ShadowMap.Init();
        m_Instance->m_PostProcess.Init();
        m_Instance->m_TiledLighting.Init();
        m_Instance->ApplyVisualSettings();
        m_Instance->ChangeScene(SceneName::Title);
        return true;
    }

    // 1フレームの更新順:
    // 入力 → シーン → カメラ/画面効果 → Object → 破棄 → シーン変更 → 遅延追加。
    // シーンを切り替えるフレームでは遅延追加を破棄します。
    // 遅延処理を最後に置くことで、Object配列の走査中に要素が増減しません。
    void Game::Update()
    {
        Input::Update();

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

        if (m_Instance->m_PauseMenu.IsOpen())
        {
            m_Instance->UpdatePauseMenu();
            return;
        }

        if (Debug::UI::ShouldPauseGameplay())
        {
            Debug::UI::ApplyTuning(m_Instance->m_PostProcess);
            m_Instance->m_PostProcess.Update();
            return;
        }

        if (gameplayScene)
        {
            m_Instance->m_State.AddRunTime(Application::GetDeltaTime());
        }


        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->Update();
        }

        Debug::UI::ApplyTuning(m_Instance->m_PostProcess);

        m_Instance->m_Camera.Update();
        m_Instance->m_PostProcess.Update();


        m_Instance->m_ObjectManager.UpdateAll();
        m_Instance->m_ObjectManager.RemoveDestroyed();

        // Playerがカメラを動かした後の位置と向きで、鳴っている音の聞こえ方を更新します。
        if (m_Instance->m_SoundReady)
        {
            SoundListener listener;
            listener.Position = m_Instance->m_Camera.GetPosition();
            listener.Forward = m_Instance->m_Camera.GetForward();
            m_Instance->m_Sound.UpdateListener(listener, Application::GetDeltaTime());
        }

        if (m_Instance->m_PendingScene.has_value())
        {
            m_Instance->m_ObjectManager.ClearPendingCommands();

            const SceneName nextScene =
                m_Instance->m_PendingScene.value();

            m_Instance->m_PendingScene.reset();
            m_Instance->ChangeScene(nextScene);
            return;
        }

        m_Instance->m_ObjectManager.FlushPendingCommands();
    }

    // 生成と逆順に解放します。unique_ptr/ComPtr所有物はresetで確実に破棄されます。
    void Game::Uninit()
    {
        if (m_Instance == nullptr) return;

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

        // D3Dデバイスが有効な間に、キャッシュとGame所有のGPUリソースを解放します。
        // これによりRenderer::Uninitのデバッグ出力が、Gameに残った参照ではなく
        // 本当のリークだけを報告できます。
        Shader::ClearCache();
        ModelCache::Clear();
        m_Instance.reset();

        Renderer::Uninit();
    }

    Game* Core::Game::GetInstance()
    {
        return m_Instance.get();
    }

    // ポーズ中の入力はPauseMenuが解釈し、設定の反映とシーン操作だけをここで行います。
    void Game::UpdatePauseMenu()
    {
        const PauseMenu::Result result = m_PauseMenu.Update(m_Settings);
        if (result.settingsChanged)
        {
            ApplyVisualSettings();
            ApplyAudioVolume(true);
            m_Settings.Save();
            if (result.changedItem == PauseMenu::Item::Volume)
            {
                // 変更後の音量を確認できるよう、短い効果音を鳴らします。
                PlayAudioCue(SOUND_CUE_PICKUP);
            }
        }

        switch (result.command)
        {
        case PauseMenu::Command::RestartStage:
            // 1面はBeginRunで全体を初期化するため、2面だけ開始時点へ戻します。
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
            PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
            break;
        case PauseMenu::Command::None:
            break;
        }
    }

    void Game::ApplyVisualSettings()
    {
        m_PostProcess.SetUserBrightnessOffset(m_Settings.GetBrightnessOffset());
        m_PostProcess.SetUserEffectScale(m_Settings.GetEffectScale());
        m_Camera.SetLookSensitivityScale(m_Settings.GetLookSensitivityScale());
    }

    // シーン遷移は予約のみ。実際の破棄・生成はUpdate末尾の安全な位置で行います。
    void Game::RequestSceneChange(SceneName sName)
    {
        if (m_PendingScene.has_value())
        {
            return;
        }

        m_PendingScene = sName;
    }

    float Game::ComputeSoundOcclusion(
        const DirectX::SimpleMath::Vector3& listener,
        const DirectX::SimpleMath::Vector3& emitter)
    {
        // 音源の少し手前までを調べ、扉の音が扉自身に遮られないようにします。
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

        // 壁1枚でほぼこもり、2枚以上で完全に向こう側の音になります。
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

    // 現在のSceneとObjectを終了してから、指定された次Sceneを一つだけ生成します。
    void Game::ChangeScene(SceneName sName)
    {
        const SceneName previousScene = m_CurrentScene;
        if (sName == SceneName::Result)
        {
            m_State.CompleteRun();
            SaveBestRecord();
        }

        m_CurrentScene = sName;
        m_PauseMenu.Close();
        ApplyAudioVolume(false);
        m_Scene.reset();
        m_ReflectionFrameIndex = 0;
        m_ShadowFrameIndex = 0;
		m_WasReflectionVisible = false;
        m_HasReflectionCameraPose = false;
        // 前の場所で鳴っていた位置付きの音（足音・扉など）を持ち越さないよう止めます。
        if (m_SoundReady)
        {
            m_Sound.StopAllSpatial();
        }

        DeleteAllObject();

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

        // 階ごとに異なる環境音へ切り替え、タイトルとリザルトでは停止します。
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


    void Game::DeleteObject(Object* pt)
    {
        m_ObjectManager.DeleteObject(pt);
    }

    void Game::DestroyObj(const std::string& name)
    {
        m_ObjectManager.DestroyNamedObject(name);
    }

    void Game::DeleteAllObject()
    {
        m_ObjectManager.DeleteAll();
    }
}
