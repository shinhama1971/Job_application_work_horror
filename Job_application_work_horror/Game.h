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
        // 聞き手と音源の間にある壁・閉じた扉の量（0〜1）を返します。
        float ComputeSoundOcclusion(
            const DirectX::SimpleMath::Vector3& listener,
            const DirectX::SimpleMath::Vector3& emitter);

    public:
        Game();
        ~Game();

        // 描画デバイスを初期化できなかった場合はfalseを返します。
        static bool Init();
        // 1フレーム分の入力・Scene・Objectの更新と、遅延していた追加・削除・シーン切り替えを行います。
        static void Update();
        // 影・反射などの事前パス、本描画、ポストプロセス、HUDの順に描画します（GameRendering.cpp）。
        static void Draw();
        static void Uninit();

        // Sceneが所有する補助カメラへ、現在の3D Objectだけを描画します。
        void DrawWorldForAuxiliaryCamera(Camera& camera);

        static Game* GetInstance();

        Scene* GetScene() const
        {
            return m_Scene.get();
        }

        // シーン切り替えを予約します。実際の切り替えはUpdateの最後に行うため、
        // Objectの更新中に呼んでも安全です。同じフレームの2回目以降の予約は無視します。
        void RequestSceneChange(SceneName sName);
        SceneName GetCurrentSceneName() const { return m_CurrentScene; }

        // 音声初期化に失敗したPCでもゲームを続行できる安全な再生窓口です。
        void PlayAudioCue(SOUND_LABEL label, float pitch = 1.0f)
        {
            if (m_SoundReady)
            {
                m_Sound.Play(label, pitch);
            }
        }

        // ワールド上の位置から鳴らします。カメラとの位置関係で左右・距離・壁越しの聞こえ方が変わります。
        // minimumOcclusion を指定すると、壁がなくても天井越しのようにこもって聞こえます。
        void PlayAudioCueAt(
            SOUND_LABEL label,
            const DirectX::SimpleMath::Vector3& position,
            float pitch = 1.0f,
            float volume = 1.0f,
            float minimumOcclusion = 0.0f)
        {
            if (m_SoundReady)
            {
                m_Sound.PlayAt(label, position, pitch, volume, minimumOcclusion);
            }
        }

        // デバッグ表示用。いま鳴っている位置付きの音の数です。
        size_t GetActiveSpatialVoiceCount() const
        {
            return m_SoundReady ? m_Sound.GetActiveSpatialVoiceCount() : 0;
        }

        // 環境音のループなど、PlayAudioCueで鳴らした音を止めます。
        void StopAudioCue(SOUND_LABEL label)
        {
            if (m_SoundReady)
            {
                m_Sound.Stop(label);
            }
        }

        // --- Objectの生成・破棄・検索（実体はObjectManagerが所有します） ---
        // 破棄の予約です。実体はUpdateの破棄処理（RemoveDestroyed）で解放されます。
        void DeleteObject(Object* pt);
        // 名前で指定して破棄を予約します。
        void DestroyObj(const std::string& name);
        // 全Objectをその場で解放します。シーン切り替え時とGameの終了時だけ使います。
        void DeleteAllObject();

        // その場で生成します。更新ループの外（SceneのInitなど）で使います。
        template<typename T>
        T* AddObject()
        {
            return m_ObjectManager.AddObject<T>();
        }

        // 更新ループの中から生成したいときの予約です。setupは生成直後に呼ばれます。
        template<typename T, typename Setup>
        void RequestAddObject(Setup&& setup)
        {
            m_ObjectManager.RequestAddObject<T>(
                std::forward<Setup>(setup));
        }

        // 名前付きで生成します。SceneのInitで配置し、RequireObjで取得し直して使います。
        template<typename T>
        T* CreateObj(const std::string& name)
        {
            return m_ObjectManager.CreateNamedObject<T>(name);
        }

        // 見つからなくてもよい場合の名前検索です（無い場合はnullptr）。
        // 必ず存在するはずのObjectにはRequireObjを使い、打ち間違いを起動時に検出します。
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

        // 指定型のObjectをすべて返します。全Objectを走査するため、頻繁に使う場合は結果を保持します。
        template<typename T>
        std::vector<T*> GetObjects()
        {
            return m_ObjectManager.FindObjects<T>();
        }

        // --- プレイの進行と成績（GameStateへの委譲） ---
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

        // --- 設定値（GameSettingsへの委譲）。値はポーズメニューの段階番号です ---
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

        int GetResolutionLevel() const
        {
            return m_Settings.GetResolutionLevel();
        }

        // falseのとき、Sceneは目的表示と目的地ガイドを出しません。
        bool IsGuideEnabled() const
        {
            return m_Settings.IsGuideEnabled();
        }

        // ポーズメニューで選択中の項目番号です（HUDの強調表示に使います）。
        int GetPauseSettingIndex() const
        {
            return m_PauseMenu.GetSelectedIndex();
        }

        // --- 結果画面・ベスト記録用の成績 ---
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
        void SetWallWritingsRead(int count) { m_State.SetWallWritingsRead(count); }
        int GetWallWritingsRead() const { return m_State.GetWallWritingsRead(); }
        void RegisterHiddenRoomEscaped() { m_State.RegisterHiddenRoomEscaped(); }
        bool IsHiddenRoomEscaped() const { return m_State.IsHiddenRoomEscaped(); }
        void SetStage2Anomalies(int first, int second) { m_State.SetStage2Anomalies(first, second); }
        int GetStage2FirstAnomaly() const { return m_State.GetStage2FirstAnomaly(); }
        int GetStage2SecondAnomaly() const { return m_State.GetStage2SecondAnomaly(); }

        // --- 描画システムへのアクセス。所有権はGameにあり、返すポインタは非所有です ---
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
