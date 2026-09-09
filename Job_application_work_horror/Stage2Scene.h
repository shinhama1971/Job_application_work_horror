// ============================================================================
// ファイルの役割: 2面のループ廊下、謎解き、段階的な異変とクリア条件を管理します。
// ============================================================================

#pragma once

#include "Scene.h"
#include "InteractionSystem.h"
#include "Hud.h"
#include "QuietRecovery.h"

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
    void StartCaughtSequence(Player& player);
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

    // timerが負数なら未実行、0以上なら対応する演出シーケンスが進行中です。
    int m_LoopCount = 0;
    float m_LoopCooldown = 0.0f;
    float m_NoticeTimer = 0.0f;
    float m_VisualTimer = 0.0f;
    float m_ObservedScareTimer = -1.0f;
    float m_GazeNoticeTimer = 0.0f;
    float m_FinalSequenceTimer = -1.0f;
    float m_ScratchNoticeTimer = 0.0f;
    float m_PortraitNoticeTimer = 0.0f;
    float m_FalseDoorNoticeTimer = 0.0f;
    float m_ClockHourAngle = 0.0f;
    float m_ClockMinuteAngle = 0.0f;
    float m_ClockNoticeTimer = 0.0f;
    float m_PuzzleFeedbackTimer = 0.0f;
    float m_NoiseThreat = 0.0f;
    float m_NoiseEventCooldown = 0.0f;
    float m_NoiseWarningTimer = 0.0f;
    float m_WetStepNoticeTimer = 0.0f;
    float m_NoiseStalkerCooldown = 0.0f;
    float m_NoiseStalkerNoticeTimer = 0.0f;
    float m_ChargerNoticeTimer = 0.0f;
    float m_EvidenceNoticeTimer = 0.0f;
    float m_SignalNoticeTimer = 0.0f;
    float m_LoopBlinkTimer = 0.0f;
    float m_LoopTransitionTimer = -1.0f;
    float m_FinalPursuitTimer = 0.0f;
    float m_PursuitPulseTimer = 0.0f;
    float m_PursuitGazePenaltyTimer = 0.0f;
    float m_CaughtTimer = -1.0f;
    float m_ProgressHintTimer = 0.0f;
    float m_GuidancePulseCooldown = 0.0f;
    float m_ScratchUpdateAccumulator = 0.0f;
    int m_ObservedScarePhase = 0;
    int m_FinalSequencePhase = 0;
    // ビットごとに通過済み照明区画を記録し、同じ演出の多重発生を防ぎます。
    unsigned int m_LightZoneMask = 0;
    bool m_ScratchScareTriggered = false;
    bool m_PortraitObserved = false;
    bool m_PortraitChangedThisLoop = false;
    bool m_FalseDoorObserved = false;
    bool m_FalseDoorMoved = false;
    bool m_ClockObservedThisLoop = false;
    bool m_ConfirmationHandledThisLoop = false;
    bool m_ChargerHandled = false;
    bool m_EvidenceHandled[2] = { false, false };
    bool m_SignalAccepted[3] = { false, false, false };
    // 3つの信号を正しい順序で確定すると最終シーケンスを解放します。
    int m_SignalStep = 0;
    bool m_SignalPuzzleComplete = false;
    int m_PuzzleFeedbackType = 0;
    int m_PuzzleMistakeCount = 0;
    bool m_FinalSequenceArmed = false;
    bool m_FinalDoorReady = false;
    bool m_NoiseCatch = false;
    int m_DebugCommand = 0;

public:
    Stage2Scene();
    ~Stage2Scene();

    void Update() override;
    void Draw(Camera* camera) override;

    int GetLoopCount() const { return m_LoopCount; }
    int GetSignalStep() const { return m_SignalStep; }
    int GetPuzzleMistakeCount() const { return m_PuzzleMistakeCount; }
    float GetNoiseThreat() const { return m_NoiseThreat; }
    bool IsSignalPuzzleComplete() const { return m_SignalPuzzleComplete; }
    bool IsFinalSequenceArmed() const { return m_FinalSequenceArmed; }
    bool IsFinalDoorReady() const { return m_FinalDoorReady; }
    void DebugRequestAdvanceLoop() { m_DebugCommand = 1; }
    void DebugRequestFinalSequence() { m_DebugCommand = 2; }
    void DebugRequestLightChase() { m_DebugCommand = 3; }
};
