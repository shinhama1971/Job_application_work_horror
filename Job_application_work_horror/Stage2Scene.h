// ============================================================================
// ファイルの役割: 2面のループ廊下、謎解き、段階的な異変とクリア条件を管理します。
// 主な技術: シーン分割、有限状態機械、観察型パズル、追跡演出
// ============================================================================

#pragma once

#include "Scene.h"
#include "InteractionSystem.h"
#include "Hud.h"
#include "QuietRecovery.h"
#include "SignalPuzzle.h"
#include "NoiseThreatSystem.h"
#include "ClockAnomaly.h"
#include "FalseDoorAnomaly.h"
#include "PortraitAnomaly.h"
#include "ScratchAnomaly.h"
#include "ObservedScareSequence.h"
#include "CaughtSequence.h"
#include "FinalSequence.h"
#include "LightZoneProgress.h"
#include "PuzzleFeedback.h"
#include "BehindPresence.h"
#include "Stage2AnomalyPlan.h"

#include "Stage2SceneConstants.h"
#include "Stage2Layout.h"
#include "Stage2Objective.h"

#include <array>
#include <cstddef>
#include <optional>
#include <random>

class Player;
class ExitTrigger;
class Door;
class ShadowMan;
class FuseBox;
class Wall;
class BatteryItem;
class CeilingLight;

class Stage2Scene : public Scene
{
private:
    // 看板・表示灯などの小さな光源を設定します（タイルベースライティングで数を増やせるため）。
    void SetupPracticalLights();
    // 目的表示の文章を選ぶために、今の状態を集めます（Stage2SceneDraw.cpp）。
    Stage2ObjectiveInput MakeObjectiveInput(bool confirmationPending) const;

    // 2面は同じ廊下を周回するたびに異変が追加される。
    // AdvanceLoopが周回段階を進め、個別Update関数が異変と謎解きを更新します。
    void Init();
    void Uninit();
    void AdvanceLoop(Player& player);
    void StartObservedScare();
    void UpdateObservedScare(float deltaTime);
    void StartFinalSequence();
    void UpdateFinalSequence(float deltaTime);
    void UpdateFinalPursuit(float deltaTime);
    void StartCaughtSequence(Player& player, CaughtSequence::Reason reason);
    void UpdateCaughtSequence(Player& player, float deltaTime);
    void RevealScratchPieces(int first, int last, float emission);
    void UpdateLightZones(const Player& player);
    void UpdateScratchMessage(const Player& player, float deltaTime);
    void UpdatePortraitAnomaly(const Player& player, float deltaTime);
    // この周回で見つけるべき異変（Stage2AnomalyPlanが決めたもの）を見つけ終えたか。
    bool IsRequiredAnomalyFound() const;
    void UpdateFalseDoorAnomaly(const Player& player);
    void SetFalseDoorState(bool visible, bool rightSide);
    void ConfigureClockForLoop();
    void UpdateClock(float deltaTime);
    void UpdateClockObservation();
    void RegisterPuzzleMistake(int type);
    void UpdateNoiseThreat(Player& player, float deltaTime);
    void UpdateSignalPuzzle();
    void UpdateSignalStalker();
    void ApplySignalLightingState();
    void ResetSignalPuzzle();
    void UpdateBehindPresence(Player& player, float deltaTime);
    bool IsBehindPresenceAllowed() const;

    Stage2Objects m_Objects;

    // プレイヤーが見ている操作対象と、画面へ出す案内を分離して管理します。
    InteractionSystem m_InteractionSystem;
    Hud m_Hud;
    QuietRecovery m_QuietRecovery;
    SignalPuzzle m_SignalPuzzle;
    NoiseThreatSystem m_NoiseThreatSystem;
    ClockAnomaly m_ClockAnomaly;
    FalseDoorAnomaly m_FalseDoorAnomaly;
    PortraitAnomaly m_PortraitAnomaly;
    ScratchAnomaly m_ScratchAnomaly;
    ObservedScareSequence m_ObservedScareSequence;
    CaughtSequence m_CaughtSequence;
    FinalSequence m_FinalSequence;
    LightZoneProgress m_LightZoneProgress;
    PuzzleFeedback m_PuzzleFeedback;
    BehindPresence m_BehindPresence;
    // 1周目・2周目にどの異変を探させるか。プレイごとにランダムに決めます。
    Stage2AnomalyPlan m_AnomalyPlan;
    std::mt19937 m_PresenceRandom{ std::random_device{}() };

    // timerが負数なら未実行、0以上なら対応する演出シーケンスが進行中です。
    int m_LoopCount = 0;
    float m_LoopCooldown = 0.0f;
    // 周回・充電器・記録・信号盤の一時的な通知。表示する文章はSelectStage2Objectiveが選びます。
    Stage2Notices m_Notices;
    float m_VisualTimer = 0.0f;
    float m_LoopBlinkTimer = 0.0f;
    float m_LoopTransitionTimer = -1.0f;
    float m_ProgressHintTimer = 0.0f;
    float m_GuidancePulseCooldown = 0.0f;
    bool m_ConfirmationHandledThisLoop = false;
    bool m_ChargerHandled = false;
    bool m_EvidenceHandled[2] = { false, false };
    bool m_FinalSequenceArmed = false;
    bool m_FinalDoorReady = false;
    // デバッグUIから予約された操作。次の操作可能フレームで実行します。
    std::optional<SceneDebugAction> m_PendingDebugAction;

public:
    Stage2Scene();
    ~Stage2Scene();

    void Update() override;
    void Draw(Camera* camera) override;

    bool TryGetDebugInfo(SceneDebugInfo& info) const override;
    void RequestDebugAction(SceneDebugAction action) override;
};
