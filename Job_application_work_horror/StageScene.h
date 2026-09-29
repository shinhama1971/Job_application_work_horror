// ============================================================================
// ファイルの役割: 1面のステージ配置、ヒューズ探索、電力復旧、出口までの進行を管理します
// 主な技術: シーン構成、オブジェクト配置、進行状態、環境ストーリーテリング
// ============================================================================

#pragma once

#include "Scene.h"
#include "InteractionSystem.h"
#include "Hud.h"
#include "ScareLightSequence.h"
#include "StagePowerSequence.h"
#include "ExitOmenSequence.h"
#include "StageSurveillanceController.h"
#include "Stage1Layout.h"
#include "Stage1Objective.h"
#include "Stage1AmbientSounds.h"

#include <array>
#include <cstddef>
#include <random>

class Player;
class ShadowMan;
class FuseBox;
class Wall;
class Door;
class ExitTrigger;
class Item;
class CeilingLight;

class StageScene : public Scene
{
private:
    // 初期化と終了。配置はStage1Layoutに任せ、Uninitでは配置時に記録した名前の一覧で破棄します。
    void Init();
    void Uninit();
    // 看板・表示灯などの小さな光源を設定します（タイルベースライティングで数を増やせるため）。
    void SetupPracticalLights();
    // 目的表示の文章を選ぶために、今の状態を集めます（StageSceneDraw.cpp）。
    Stage1ObjectiveInput MakeObjectiveInput() const;
    // 姿の見えない物音を鳴らしてよいか（台本の演出や監視映像と重ならないか）を判断します。
    bool IsAmbientSoundAllowed() const;
    void UpdateAmbientSounds();

    StageObjects m_Objects;

    // 1面の進行は「入口演出 → ヒューズ探索 → 電力復旧 → 出口演出」の順です。
    // 各演出はphaseとtimerで管理し、Updateを止めずに段階的に進めます。
    void UpdateCorridorLoop(class Player& player);
    void UpdateEntranceThresholdEvent(class Player& player);
    void UpdateStorageScare(class Player& player);
    void AdvanceCorridorLoop(class Player& player);

    void StartScareLightSequence();
    void UpdateScareLightSequence();
    void UpdatePowerRestoreSequence();
    void UpdateExitPowerSequence();
    void StartFuseWatcher(int fuseCount);
    void UpdateExitOmen(class Player& player);

    // InteractionSystemは視線先、Hudは現在目的と操作ヒントを担当します。
    InteractionSystem m_InteractionSystem;
    Hud m_Hud;
    // 監視カメラ巡回（映像・報告・現地確認・捕獲）はこのクラスに任せ、Sceneは呼び出すだけです。
    StageSurveillanceController m_Surveillance;
    // 天井裏の足音・配管を叩く音・遠くの扉。いつどこで鳴らすかはこのクラスが決めます。
    Stage1AmbientSounds m_AmbientSounds;
    std::vector<AmbientSoundCue> m_AmbientCues;
    ScareLightSequence m_ScareLightSequence;
    StagePowerSequence m_PowerSequence;
    ExitOmenSequence m_ExitOmenSequence;

    // 0以上のtimerは演出実行中、-1は未実行または終了を表します。
    int m_CorridorLoopCount = 0;
    int m_LastFuseCount = 0;
    float m_FuseNoticeTimer = 0.0f;
    float m_FuseWatcherNoticeTimer = 0.0f;
    int m_FuseWatcherState = 0;
    float m_ChargerNoticeTimer = 0.0f;
    bool m_ChargerHandled = false;
    float m_EvidenceNoticeTimer = 0.0f;
    float m_LoopCooldown = 0.0f;
    float m_LoopNoticeTimer = 0.0f;
    bool m_EntranceEventTriggered = false;
    float m_EntranceEventTimer = -1.0f;
    int m_EntranceEventPhase = -1;
    int m_StorageScarePhase = 0;
    float m_StorageScareTimer = 0.0f;
    float m_StorageScareNoticeTimer = 0.0f;
    float m_StageVisualTimer = 0.0f;
    float m_ProgressHintTimer = 0.0f;
public:
    StageScene();
    ~StageScene();

    void Update() override;
    void RenderOffscreen() override;
    void Draw(Camera* camera) override;
};
