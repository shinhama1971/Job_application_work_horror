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

#include "Stage2SceneConstants.h"

#include <array>
#include <cstddef>
#include <optional>

class Player;
class ExitTrigger;
class Door;
class ShadowMan;
class FuseBox;
class Wall;
class BatteryItem;
class CeilingLight;

// 2面の廊下照明。演出テーブルは名前文字列ではなくこの値で照明を指定します。
enum class Stage2Light
{
    Light1,         // 入口側（"Stage2Light1"）
    Light2,         // 中央（"Stage2Light2"）
    Light3,         // 奥（"Stage2Light3"）
    DoorLight,      // 出口扉の上（"CeilingLight4"）
    Count
};

// Init直後に名前で一度だけ取得し、以後は毎フレームの名前検索をせずに使うObject群です。
// 実体はObjectManagerが所有し、どれもSceneの終了まで破棄されないため非所有ポインタで保持します。
struct Stage2Objects
{
    Player* player = nullptr;
    ExitTrigger* exit = nullptr;
    Door* door = nullptr;
    ShadowMan* shadow = nullptr;
    ShadowMan* noiseShadow = nullptr;
    FuseBox* confirmationPanel = nullptr;
    FuseBox* emergencyCharger = nullptr;
    BatteryItem* battery = nullptr;
    Wall* doorIndicator = nullptr;
    Wall* portrait = nullptr;
    Wall* loopMark = nullptr;
    Wall* clockFace = nullptr;
    Wall* clockHourHand = nullptr;
    Wall* clockMinuteHand = nullptr;
    Wall* falseDoorPanel = nullptr;
    Wall* falseDoorFrameNear = nullptr;
    Wall* falseDoorFrameFar = nullptr;
    Wall* falseDoorFrameTop = nullptr;
    Wall* falseDoorHandle = nullptr;
    std::array<CeilingLight*, static_cast<std::size_t>(Stage2Light::Count)> lights{};
    std::array<Wall*, 3> puddles{};
    std::array<FuseBox*, 2> evidenceTerminals{};
    std::array<Wall*, 2> evidenceMarkers{};
    std::array<Wall*, 2> portraitEyes{};
    std::array<Wall*, 3> cycleMarks{};
    std::array<FuseBox*, 3> signalTerminals{};
    std::array<Wall*, 3> signalMarkers{};
    std::array<Wall*, Stage2ScratchCount> scratches{};

    CeilingLight* Light(Stage2Light light) const
    {
        return lights[static_cast<std::size_t>(light)];
    }
};

class Stage2Scene : public Scene
{
private:
    void CacheObjects();

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
