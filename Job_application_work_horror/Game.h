// ============================================================================
// ファイルの役割: ゲーム全体のオブジェクト所有、更新・描画順、シーン遷移を統括します。
// 主な技術: RAII、unique_ptr、遅延追加・削除、シングルトン、固定更新順
// ============================================================================

#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <optional>
#include <utility>

#include "Camera.h"
#include "Renderer.h"
#include "Object.h"
#include"PostProcess.h"
#include "ShadowMap.h"
#include "PlanarReflection.h"
#include "TiledLighting.h"
#include "GameState.h"
#include "GameSettings.h"
#include "PauseMenu.h"
#include "ObjectManager.h"
#include "GpuTimer.h"
#include "sound.h"
#include "utility.h"
enum class SceneName
{
    Title,
    Stage,
    Stage2,
    Result
};

class Scene;

namespace Core
{
    // ゲームループの最上位クラス。
    // SceneとObjectの所有権はGameがunique_ptrで保持し、外部へ返す生ポインタは
    // 「参照専用」として扱います。シーン変更と追加・削除は更新ループ後に遅延実行し、
    // vector走査中に所有コンテナが変化することを防いでいます。
    class Game
    {
    private:
        // Game自身と現在のSceneは単一所有。明示的なdeleteは行いません。
        static std::unique_ptr<Game> m_Instance;

        std::unique_ptr<Scene> m_Scene;
        Camera m_Camera;
        Effect::PostProcess m_PostProcess;
        Effect::ShadowMap m_ShadowMap;
        Effect::PlanarReflection m_PlanarReflection;
        Effect::TiledLighting m_TiledLighting;
        // 毎フレームObjectから集める点光源。確保し直しを避けるため使い回します。
        std::vector<ENVIRONMENT_POINT_LIGHT> m_FramePointLights;
        GpuTimer m_GpuTimer;
        Sound m_Sound;
        bool m_SoundReady = false;

        ObjectManager m_ObjectManager;
        std::optional<SceneName> m_PendingScene;

        GameState m_State;
        GameSettings m_Settings;
        SceneName m_CurrentScene = SceneName::Title;
        unsigned int m_ReflectionFrameIndex = 0;
        unsigned int m_ShadowFrameIndex = 0;
		bool m_WasReflectionVisible = false;
        DirectX::SimpleMath::Vector3 m_LastReflectionCameraPosition{};
        DirectX::SimpleMath::Vector3 m_LastReflectionCameraForward{ 0.0f, 0.0f, 1.0f };
        bool m_HasReflectionCameraPose = false;
        PauseMenu m_PauseMenu;

        void ChangeScene(SceneName sName);
        void LoadBestRecord();
        void SaveBestRecord() const;
        void ApplyAudioVolume(bool paused);
        // 明るさ・演出強度・視点感度をPostProcessとCameraへ反映します。
        void ApplyVisualSettings();
        void UpdatePauseMenu();

    public:
        Game();
        ~Game();

        // 描画デバイスを初期化できなかった場合はfalseを返します。
        static bool Init();
        static void Update();
        static void Draw();
        static void Uninit();

        // Sceneが所有する補助カメラへ、現在の3D Objectだけを描画します。
        void DrawWorldForAuxiliaryCamera(Camera& camera);

        static Game* GetInstance();

        Scene* GetScene() const
        {
            return m_Scene.get();
        }

        void RequestSceneChange(SceneName sName);

        // 音声初期化に失敗したPCでもゲームを続行できる安全な再生窓口です。
        void PlayAudioCue(SOUND_LABEL label, float pitch = 1.0f)
        {
            if (m_SoundReady)
            {
                m_Sound.Play(label, pitch);
            }
        }

        void StopAudioCue(SOUND_LABEL label)
        {
            if (m_SoundReady)
            {
                m_Sound.Stop(label);
            }
        }

        void DeleteObject(Object* pt);
        void DestroyObj(const std::string& name);
        void DeleteAllObject();

        template<typename T>
        T* AddObject()
        {
            return m_ObjectManager.AddObject<T>();
        }

        template<typename T, typename Setup>
        void RequestAddObject(Setup&& setup)
        {
            m_ObjectManager.RequestAddObject<T>(
                std::forward<Setup>(setup));
        }

        template<typename T>
        T* CreateObj(const std::string& name)
        {
            return m_ObjectManager.CreateNamedObject<T>(name);
        }

        template<typename T>
        T* GetObj(const std::string& name)
        {
            return m_ObjectManager.FindNamedObject<T>(name);
        }

        // Sceneの初期化時に、以後使い続けるObjectを取得します。
        // 名前の打ち間違いや生成漏れは、その場でObject名を示して終了します。
        template<typename T>
        T* RequireObj(const std::string& name)
        {
            T* object = m_ObjectManager.FindNamedObject<T>(name);
            if (object == nullptr)
            {
                utility::ReportFatalError(
                    "シーンに必要なObjectが見つかりません: " + name);
            }
            return object;
        }

        template<typename T>
        std::vector<T*> GetObjects()
        {
            return m_ObjectManager.FindObjects<T>();
        }


        void AddItemCount()
        {
            m_State.AddItem();
        }

        int GetItemCount() const
        {
            return m_State.GetItemCount();
        }

        void SetPowerRestored(bool restored)
        {
            m_State.SetPowerRestored(restored);
        }

        bool IsPowerRestored() const
        {
            return m_State.IsPowerRestored();
        }

        bool IsPaused() const
        {
            return m_PauseMenu.IsOpen();
        }

        int GetBrightnessLevel() const
        {
            return m_Settings.GetBrightnessLevel();
        }

        int GetEffectLevel() const
        {
            return m_Settings.GetEffectLevel();
        }

        int GetLookSensitivityLevel() const
        {
            return m_Settings.GetLookSensitivityLevel();
        }

        int GetVolumeLevel() const
        {
            return m_Settings.GetVolumeLevel();
        }

        // falseのとき、Sceneは目的表示と目的地ガイドを出しません。
        bool IsGuideEnabled() const
        {
            return m_Settings.IsGuideEnabled();
        }

        int GetPauseSettingIndex() const
        {
            return m_PauseMenu.GetSelectedIndex();
        }

        float GetLastClearTimeSeconds() const
        {
            return m_State.GetLastClearTimeSeconds();
        }

        float GetRunTimeSeconds() const
        {
            return m_State.GetRunTimeSeconds();
        }

        bool HasClearRecord() const
        {
            return m_State.HasClearRecord();
        }

        float GetBestClearTimeSeconds() const
        {
            return m_State.GetBestClearTimeSeconds();
        }

        int GetBestCaughtCount() const
        {
            return m_State.GetBestCaughtCount();
        }

        int GetCaughtCount() const
        {
            return m_State.GetCaughtCount();
        }

        bool IsLastRunBestTime() const
        {
            return m_State.IsLastRunBestTime();
        }

        bool IsLastRunBestCaught() const
        {
            return m_State.IsLastRunBestCaught();
        }

        void RegisterCaught()
        {
            m_State.RegisterCaught();
        }

        void RegisterAnomalyHandled() { m_State.RegisterAnomalyHandled(); }
        void RegisterPuzzleMistake() { m_State.RegisterPuzzleMistake(); }
        void RegisterChargerUsed() { m_State.RegisterChargerUsed(); }
        int GetAnomaliesHandled() const { return m_State.GetAnomaliesHandled(); }
        int GetPuzzleMistakes() const { return m_State.GetPuzzleMistakes(); }
        int GetChargersUsed() const { return m_State.GetChargersUsed(); }
        void RegisterEvidenceCollected() { m_State.RegisterEvidenceCollected(); }
        int GetEvidenceCollected() const { return m_State.GetEvidenceCollected(); }



        Camera* GetCamera()
        {
            return &m_Camera;
        }

        Effect::PostProcess* GetPostProcess()
        {
            return &m_PostProcess;
        }

        Effect::TiledLighting* GetTiledLighting()
        {
            return &m_TiledLighting;
        }

        Effect::ShadowMap* GetShadowMap()
        {
            return &m_ShadowMap;
        }

        GpuTimer* GetGpuTimer()
        {
            return &m_GpuTimer;
        }
    };
}
