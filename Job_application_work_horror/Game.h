// ============================================================================
// ファイルの役割: ゲーム全体のObjectの所有、更新と描画の順番、シーンの切り替えをまとめて管理している。
// 主な技術: RAII、unique_ptr、Objectの追加・削除の後回し、シングルトン、決まった更新順
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
// ゲームのシーン（タイトル・1面・2面・リザルト）
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
    // ゲームループの一番上にあるクラス。
    // SceneとObjectの所有権はGameがunique_ptrで持ち、外へ返す生ポインタは
    // 「参照するだけ」のものとして扱っている。シーンの切り替えとObjectの追加・削除は更新ループの後にまとめて行い、
    // vectorを順に処理している途中で、持ち主の入れ物が変わらないようにしている。
    class Game
    {
    private:
        // Game自身と今のSceneは、それぞれ1か所だけが所有している。明示的なdeleteはしていない。
        static std::unique_ptr<Game> m_Instance;

        // 今のシーン、プレイヤーの視点のカメラ、画面効果、影、水面の反射、タイルベースライティング
        std::unique_ptr<Scene> m_Scene;
        Camera m_Camera;
        Effect::PostProcess m_PostProcess;
        Effect::ShadowMap m_ShadowMap;
        Effect::PlanarReflection m_PlanarReflection;
        Effect::TiledLighting m_TiledLighting;
        // 毎フレームObjectから集める点光源。確保し直しを避けるため、同じvectorを使い回している。
        std::vector<ENVIRONMENT_POINT_LIGHT> m_FramePointLights;
        // 描画の段階ごとのGPU時間の計測、音、音の初期化に成功したか
        GpuTimer m_GpuTimer;
        Sound m_Sound;
        bool m_SoundReady = false;

        // 全Objectの持ち主と、予約されたシーンの切り替え先
        ObjectManager m_ObjectManager;
        std::optional<SceneName> m_PendingScene;

        // プレイの進行と成績、設定、今のシーン
        GameState m_State;
        GameSettings m_Settings;
        SceneName m_CurrentScene = SceneName::Title;
        // 水面の反射と影を何フレーム目に描き直すかの数え方
        unsigned int m_ReflectionFrameIndex = 0;
        unsigned int m_ShadowFrameIndex = 0;
		// 前フレームで水面が見えていたか、反射を最後に描いたときのカメラの位置と向き（止まっている間は描き直しを減らすため）
		bool m_WasReflectionVisible = false;
        DirectX::SimpleMath::Vector3 m_LastReflectionCameraPosition{};
        DirectX::SimpleMath::Vector3 m_LastReflectionCameraForward{ 0.0f, 0.0f, 1.0f };
        bool m_HasReflectionCameraPose = false;
        // ポーズメニュー
        PauseMenu m_PauseMenu;

        // シーンを実際に切り替えている（前のシーンのObjectを全部解放してから、新しいシーンを作っている）
        void ChangeScene(SceneName sName);
        // ベスト記録を読み込む・保存する
        void LoadBestRecord();
        void SaveBestRecord() const;
        // 音量の設定を反映している（ポーズ中は小さくしている）
        void ApplyAudioVolume(bool paused);
        // 明るさ・演出の強さ・視点の感度を、PostProcessとCameraへ反映している。
        void ApplyVisualSettings();
        // ポーズメニューの開閉と操作を処理している
        void UpdatePauseMenu();
        // 聞き手と音源の間にある壁・閉じた扉の量（0〜1）を返している。
        float ComputeSoundOcclusion(
            const DirectX::SimpleMath::Vector3& listener,
            const DirectX::SimpleMath::Vector3& emitter);

    public:
        Game();
        ~Game();

        // 描画デバイスを初期化できなかった場合はfalseを返している。
        static bool Init();
        // 1フレーム分の入力・Scene・Objectの更新と、後回しにしていた追加・削除・シーンの切り替えを行っている。
        static void Update();
        // 影・反射などの事前の描画、本描画、ポストプロセス、HUDの順に描いている（GameRendering.cpp）。
        static void Draw();
        // 全Objectとシーン、描画・音の仕組みを解放している
        static void Uninit();

        // Sceneが持っている補助カメラ（監視カメラなど）の視点で、今の3D Objectだけを描いている。
        void DrawWorldForAuxiliaryCamera(Camera& camera);

        // Gameのただ1つのインスタンスを返している
        static Game* GetInstance();

        // 今のシーンを返している（所有権は渡さない）
        Scene* GetScene() const
        {
            return m_Scene.get();
        }

        // シーンの切り替えを予約している。実際の切り替えはUpdateの最後に行うため、
        // Objectの更新中に呼んでも安全。同じフレームの2回目以降の予約は無視している。
        void RequestSceneChange(SceneName sName);
        SceneName GetCurrentSceneName() const { return m_CurrentScene; }

        // 音の初期化に失敗したPCでも、ゲームを止めずに続けられる安全な再生の窓口。
        // pitchは再生速度（1で元の高さ）、volumeは音量の倍率
        void PlayAudioCue(SOUND_LABEL label, float pitch = 1.0f, float volume = 1.0f)
        {
            if (m_SoundReady)
            {
                m_Sound.Play(label, pitch, volume);
            }
        }

        // ワールド上の位置から鳴らしている。カメラとの位置関係で、左右・距離・壁越しの聞こえ方が変わる。
        // minimumOcclusion を指定すると、壁がなくても天井越しのようにこもって聞こえる。
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

        // デバッグ表示用。今鳴っている位置付きの音の数を返している。
        size_t GetActiveSpatialVoiceCount() const
        {
            return m_SoundReady ? m_Sound.GetActiveSpatialVoiceCount() : 0;
        }

        // 環境音のループなど、PlayAudioCueで鳴らした音を止めている。
        void StopAudioCue(SOUND_LABEL label)
        {
            if (m_SoundReady)
            {
                m_Sound.Stop(label);
            }
        }

        // --- Objectの生成・破棄・検索（実体はObjectManagerが所有している） ---
        // 破棄の予約。実体はUpdateの破棄処理（RemoveDestroyed）で解放している。
        void DeleteObject(Object* pt);
        // 名前で指定して破棄を予約している。
        void DestroyObj(const std::string& name);
        // 全Objectをその場で解放している。シーンの切り替え時とGameの終了時だけ使っている。
        void DeleteAllObject();

        // その場で生成している。更新ループの外（SceneのInitなど）で使っている。
        template<typename T>
        T* AddObject()
        {
            return m_ObjectManager.AddObject<T>();
        }

        // 更新ループの中から生成したいときの予約。setupは生成した直後に呼ばれる。
        template<typename T, typename Setup>
        void RequestAddObject(Setup&& setup)
        {
            m_ObjectManager.RequestAddObject<T>(
                std::forward<Setup>(setup));
        }

        // 名前付きで生成している。SceneのInitで配置し、RequireObjで取得し直して使っている。
        template<typename T>
        T* CreateObj(const std::string& name)
        {
            return m_ObjectManager.CreateNamedObject<T>(name);
        }

        // 見つからなくてもよい場合の名前の検索（無い場合はnullptrを返している）。
        // 必ずあるはずのObjectにはRequireObjを使い、名前の打ち間違いを起動時に見つけている。
        template<typename T>
        T* GetObj(const std::string& name)
        {
            return m_ObjectManager.FindNamedObject<T>(name);
        }

        // Sceneの初期化時に、その後使い続けるObjectを取得している。
        // 名前の打ち間違いや生成し忘れがあれば、その場でObjectの名前を表示して終了している。
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

        // 指定した型のObjectをすべて返している。全Objectを調べるため、何度も使う場合は結果を覚えておいて使っている。
        template<typename T>
        std::vector<T*> GetObjects()
        {
            return m_ObjectManager.FindObjects<T>();
        }

        // --- プレイの進行と成績（GameStateに任せている） ---
        // 拾ったヒューズの数を増やしている・返している
        void AddItemCount()
        {
            m_State.AddItem();
        }

        int GetItemCount() const
        {
            return m_State.GetItemCount();
        }

        // 電力が戻ったかを記録している・返している
        void SetPowerRestored(bool restored)
        {
            m_State.SetPowerRestored(restored);
        }

        bool IsPowerRestored() const
        {
            return m_State.IsPowerRestored();
        }

        // ポーズメニューを開いているか
        bool IsPaused() const
        {
            return m_PauseMenu.IsOpen();
        }

        // --- 設定値（GameSettingsに任せている）。値はポーズメニューの段階の番号 ---
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

        // falseのとき、Sceneは目的の文章と目的地の矢印を出さない。
        bool IsGuideEnabled() const
        {
            return m_Settings.IsGuideEnabled();
        }

        // ポーズメニューで選んでいる項目の番号を返している（HUDで強調して表示するのに使っている）。
        int GetPauseSettingIndex() const
        {
            return m_PauseMenu.GetSelectedIndex();
        }

        // --- リザルト画面・ベスト記録用の成績 ---
        // 前回クリアしたときのタイムと、今のプレイの経過時間
        float GetLastClearTimeSeconds() const
        {
            return m_State.GetLastClearTimeSeconds();
        }

        float GetRunTimeSeconds() const
        {
            return m_State.GetRunTimeSeconds();
        }

        // クリアの記録があるか、ベストタイム、ベストの捕まった回数、今のプレイで捕まった回数
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

        // 前回のプレイがベストタイム・ベストの捕まった回数を更新したか
        bool IsLastRunBestTime() const
        {
            return m_State.IsLastRunBestTime();
        }

        bool IsLastRunBestCaught() const
        {
            return m_State.IsLastRunBestCaught();
        }

        // 捕まった回数を増やしている
        void RegisterCaught()
        {
            m_State.RegisterCaught();
        }

        // リザルト画面に出す成績と「今回の発見」の記録（異変を解いた数・パズルの失敗・充電器を使った回数・
        // 残された記録・読んだ壁の文字・隠し部屋から脱出したか・2面で出た異変）
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

        // --- 描画の仕組みへのアクセス。所有権はGameにあり、返すポインタは所有しない ---
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
