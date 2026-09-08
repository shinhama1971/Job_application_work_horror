// ============================================================================
// ファイルの役割: ゲーム全体のオブジェクト所有、更新・描画順、シーン遷移をまとめる
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

#include "DebugUI.h"
#include "Application.h"

namespace Core
{
    std::unique_ptr<Game> Game::m_Instance;

    Game::Game()
    {
        LoadBestRecord();
        LoadSettings();
    }

    Game::~Game()
    {
        DeleteAllObject();
    }

    // サブシステムを依存順に初期化し、最初のタイトルシーンを生成します。
    void Game::Init()
    {
        if (m_Instance)
        {
            return;
        }

        m_Instance = std::make_unique<Game>();

        Renderer::Init();
        Debug::UI::Init(Application::GetWindow());


        Input::Create();

        // 音声が使えない環境ではfalseのまま進み、描画とゲーム進行は継続します。
        m_Instance->m_SoundReady = SUCCEEDED(m_Instance->m_Sound.Init());
        if (m_Instance->m_SoundReady)
        {
            m_Instance->ApplyAudioVolume(false);
        }

        m_Instance->m_Camera.Init();
        m_Instance->m_Camera.SetLookSensitivityScale(
            0.60f +
            static_cast<float>(
                m_Instance->m_LookSensitivityLevel) * 0.20f);

        m_Instance->m_PlanarReflection.Init();
        m_Instance->m_ShadowMap.Init();
        m_Instance->m_PostProcess.Init();
        m_Instance->m_PostProcess.SetUserBrightnessOffset(
            static_cast<float>(
                m_Instance->m_BrightnessLevel - 2) * 0.055f);
        constexpr float effectScales[] = { 0.70f, 1.0f, 1.25f };
        m_Instance->m_PostProcess.SetUserEffectScale(
            effectScales[m_Instance->m_EffectLevel]);
        m_Instance->ChangeScene(SceneName::Title);
    }

    // 1フレームの更新順:
    // 入力 → シーン → カメラ/画面効果 → Object → 破棄 → 遅延追加 → シーン変更。
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
            m_Instance->m_IsPaused = !m_Instance->m_IsPaused;
            if (m_Instance->m_IsPaused)
            {
                m_Instance->m_PauseSettingIndex = 0;
            }
            m_Instance->ApplyAudioVolume(m_Instance->m_IsPaused);
            Input::SetVibration(2, 0.06f);
            return;
        }

        if (m_Instance->m_IsPaused)
        {
            int selectionDelta = 0;
            if (Input::GetKeyTrigger(VK_UP) ||
                Input::GetButtonTrigger(XINPUT_UP))
            {
                selectionDelta = -1;
            }
            else if (Input::GetKeyTrigger(VK_DOWN) ||
                Input::GetButtonTrigger(XINPUT_DOWN))
            {
                selectionDelta = 1;
            }
            if (selectionDelta != 0)
            {
                m_Instance->m_PauseSettingIndex = (std::clamp)(
                m_Instance->m_PauseSettingIndex + selectionDelta,
                    0, 3);
                Input::SetVibration(1, 0.03f);
            }

            int settingDelta = 0;
            if (Input::GetKeyTrigger(VK_LEFT) ||
                Input::GetButtonTrigger(XINPUT_LEFT))
            {
                settingDelta = -1;
            }
            else if (Input::GetKeyTrigger(VK_RIGHT) ||
                Input::GetButtonTrigger(XINPUT_RIGHT))
            {
                settingDelta = 1;
            }
            bool settingChanged = false;
            if (settingDelta != 0 &&
                m_Instance->m_PauseSettingIndex == 0)
            {
                const int newBrightnessLevel = (std::clamp)(
                    m_Instance->m_BrightnessLevel + settingDelta,
                    0, 4);
                if (newBrightnessLevel !=
                    m_Instance->m_BrightnessLevel)
                {
                    m_Instance->m_BrightnessLevel =
                        newBrightnessLevel;
                    const float brightnessOffset =
                        static_cast<float>(
                            m_Instance->m_BrightnessLevel - 2) *
                        0.055f;
                    m_Instance->m_PostProcess.SetUserBrightnessOffset(
                        brightnessOffset);
                    settingChanged = true;
                }
            }
            else if (settingDelta != 0 &&
                m_Instance->m_PauseSettingIndex == 1)
            {
                const int newEffectLevel = (std::clamp)(
                    m_Instance->m_EffectLevel + settingDelta, 0, 2);
                if (newEffectLevel != m_Instance->m_EffectLevel)
                {
                    m_Instance->m_EffectLevel = newEffectLevel;
                    constexpr float effectScales[] =
                    {
                        0.70f, 1.0f, 1.25f
                    };
                    m_Instance->m_PostProcess.SetUserEffectScale(
                        effectScales[m_Instance->m_EffectLevel]);
                    settingChanged = true;
                }
            }
            else if (settingDelta != 0 &&
                m_Instance->m_PauseSettingIndex == 2)
            {
                const int newSensitivityLevel = (std::clamp)(
                    m_Instance->m_LookSensitivityLevel +
                    settingDelta, 0, 4);
                if (newSensitivityLevel !=
                    m_Instance->m_LookSensitivityLevel)
                {
                    m_Instance->m_LookSensitivityLevel =
                        newSensitivityLevel;
                    m_Instance->m_Camera.SetLookSensitivityScale(
                        0.60f +
                        static_cast<float>(
                            m_Instance->m_LookSensitivityLevel) *
                        0.20f);
                    settingChanged = true;
                }
            }
            else if (settingDelta != 0)
            {
                const int newVolumeLevel = (std::clamp)(
                    m_Instance->m_VolumeLevel + settingDelta, 0, 4);
                if (newVolumeLevel != m_Instance->m_VolumeLevel)
                {
                    m_Instance->m_VolumeLevel = newVolumeLevel;
                    m_Instance->ApplyAudioVolume(true);
                    settingChanged = true;
                }
            }
            if (settingChanged)
            {
                m_Instance->SaveSettings();
                Input::SetVibration(2, 0.045f);
                if (m_Instance->m_PauseSettingIndex == 3)
                {
                    m_Instance->PlayAudioCue(SOUND_CUE_PICKUP);
                }
            }

            const bool restartPressed =
                Input::GetKeyTrigger(VK_R) ||
                Input::GetButtonTrigger(XINPUT_Y);
            const bool titlePressed =
                Input::GetKeyTrigger(VK_T) ||
                Input::GetButtonTrigger(XINPUT_B);
            const bool quitPressed =
                Input::GetKeyTrigger(VK_Q) ||
                Input::GetButtonTrigger(XINPUT_BACK);
            if (restartPressed)
            {
                const SceneName currentScene = m_Instance->m_CurrentScene;
                m_Instance->m_IsPaused = false;
                m_Instance->ChangeScene(currentScene);
                return;
            }
            if (titlePressed)
            {
                m_Instance->m_IsPaused = false;
                m_Instance->ChangeScene(SceneName::Title);
                return;
            }
            if (quitPressed)
            {
                PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
                return;
            }

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
            m_Instance->m_RunTimeSeconds += 1.0f / 60.0f;
        }


        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->Update();
        }

        Debug::UI::ApplyTuning(m_Instance->m_PostProcess);

        m_Instance->m_Camera.Update();
        m_Instance->m_PostProcess.Update();


        for (auto& o : m_Instance->m_Objects)
        {
            if (!o->IsDestroy())
            {
                o->Update();
            }
        }

        // Remove lookup entries before their owning unique_ptr objects are
        // erased. This also keeps GetObj safe when an object destroys itself.
        for (auto it = m_Instance->m_NamedObjects.begin();
            it != m_Instance->m_NamedObjects.end();)
        {
            Object* object = it->second;
            if (object == nullptr || object->IsDestroy())
            {
                it = m_Instance->m_NamedObjects.erase(it);
            }
            else
            {
                ++it;
            }
        }

        std::erase_if(
            m_Instance->m_Objects,
            [](const std::unique_ptr<Object>& o)
            {
                if (o->IsDestroy())
                {
                    o->Uninit();
                    return true;
                }

                return false;
            }
        );

        if (m_Instance->m_PendingScene.has_value())
        {
            m_Instance->m_PendingObjectCommands.clear();

            const SceneName nextScene =
                m_Instance->m_PendingScene.value();

            m_Instance->m_PendingScene.reset();
            m_Instance->ChangeScene(nextScene);
            return;
        }

        auto pendingCommands =
            std::move(m_Instance->m_PendingObjectCommands);

        m_Instance->m_PendingObjectCommands.clear();

        for (auto& command : pendingCommands)
        {
            command();
        }
    }

    // 生成と逆順に解放します。unique_ptr/ComPtr所有物はresetで確実に破棄されます。
    void Game::Uninit()
    {
        if (m_Instance == nullptr) return;

        m_Instance->m_Camera.Uninit();

        m_Instance->m_Scene.reset();

        for (auto& o : m_Instance->m_Objects)
        {
            o->Uninit();
        }

        m_Instance->m_Objects.clear();
        m_Instance->m_NamedObjects.clear();
        m_Instance->m_PostProcess.Uninit();
        m_Instance->m_ShadowMap.Uninit();
        m_Instance->m_PlanarReflection.Uninit();

        m_Instance->m_Sound.Uninit();
        m_Instance->m_SoundReady = false;

        Input::Release();
        Debug::UI::Uninit();

        // Release cached and member-owned GPU objects while the D3D device is
        // still valid. Renderer::Uninit can then report genuine leaks instead
        // of resources retained by the still-alive Game singleton.
        Shader::ClearCache();
        m_Instance.reset();

        Renderer::Uninit();
    }

    Game* Core::Game::GetInstance()
    {
        return m_Instance.get();
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

    // 現在のSceneとObjectを終了してから、指定された次Sceneを一つだけ生成します。
    void Game::ChangeScene(SceneName sName)
    {
        const SceneName previousScene = m_CurrentScene;
        if (sName == SceneName::Result)
        {
            m_LastClearTimeSeconds = m_RunTimeSeconds;
            m_LastRunBestTime = !m_HasClearRecord ||
                m_LastClearTimeSeconds < m_BestClearTimeSeconds;
            m_LastRunBestCaught = !m_HasClearRecord ||
                m_CaughtCount < m_BestCaughtCount;
            if (m_LastRunBestTime)
            {
                m_BestClearTimeSeconds = m_LastClearTimeSeconds;
            }
            if (m_LastRunBestCaught)
            {
                m_BestCaughtCount = m_CaughtCount;
            }
            m_HasClearRecord = true;
            SaveBestRecord();
        }

        m_CurrentScene = sName;
        m_IsPaused = false;
        ApplyAudioVolume(false);
        m_Scene.reset();
        m_ReflectionFrameIndex = 0;
        m_ShadowFrameIndex = 0;
		m_WasReflectionVisible = false;

        DeleteAllObject();

        switch (sName)
        {
        case SceneName::Title:
            m_Scene = std::make_unique<TitleScene>();
            break;

        case SceneName::Stage:
            m_ItemCount = 0;
            m_PowerRestored = false;
            m_RunTimeSeconds = 0.0f;
            m_CaughtCount = 0;
            m_AnomaliesHandled = 0;
            m_PuzzleMistakes = 0;
            m_ChargersUsed = 0;
            m_EvidenceCollected = 0;
            m_LastRunBestTime = false;
            m_LastRunBestCaught = false;
            m_Scene = std::make_unique<StageScene>();
            break;

        case SceneName::Stage2:
            m_ItemCount = 3;
            m_PowerRestored = true;
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
        if (pt == nullptr) return;

        pt->Destroy();

        for (auto it = m_NamedObjects.begin(); it != m_NamedObjects.end();)
        {
            if (it->second == pt)
            {
                it = m_NamedObjects.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void Game::DestroyObj(const std::string& name)
    {
        auto it = m_NamedObjects.find(name);

        if (it == m_NamedObjects.end())
        {
            return;
        }

        if (it->second != nullptr)
        {
            it->second->Destroy();
        }

        m_NamedObjects.erase(it);
    }

    void Game::DeleteAllObject()
    {
        for (auto& o : m_Objects)
        {
            o->Uninit();
        }

        m_Objects.clear();

        m_NamedObjects.clear();
    }
}
