// ============================================================================
// ファイルの役割: 2面のループ廊下、謎解き、段階的な異変とクリア条件を管理します。
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

class Player;

class Stage2Scene : public Scene
{
private:
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
    void UpdatePortraitAnomaly(const Player& player);
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

    // timerが負数なら未実行、0以上なら対応する演出シーケンスが進行中です。
    int m_LoopCount = 0;
    float m_LoopCooldown = 0.0f;
    float m_NoticeTimer = 0.0f;
    float m_VisualTimer = 0.0f;
    float m_ChargerNoticeTimer = 0.0f;
    float m_EvidenceNoticeTimer = 0.0f;
    float m_SignalNoticeTimer = 0.0f;
    float m_LoopBlinkTimer = 0.0f;
    float m_LoopTransitionTimer = -1.0f;
    float m_ProgressHintTimer = 0.0f;
    float m_GuidancePulseCooldown = 0.0f;
    bool m_ConfirmationHandledThisLoop = false;
    bool m_ChargerHandled = false;
    bool m_EvidenceHandled[2] = { false, false };
    bool m_FinalSequenceArmed = false;
    bool m_FinalDoorReady = false;
    int m_DebugCommand = 0;

public:
    Stage2Scene();
    ~Stage2Scene();

    void Update() override;
    void Draw(Camera* camera) override;

    bool TryGetDebugInfo(SceneDebugInfo& info) const override;
    void RequestDebugAction(SceneDebugAction action) override;
};
