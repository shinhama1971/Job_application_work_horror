// ============================================================================
// ファイルの役割: 2面のループ廊下、謎解き、周回ごとに増える異変、クリアの条件を管理している。
// 主な技術: Sceneの処理を複数のファイルに分ける構成、有限状態機械、よく見て気づく謎解き、追われる演出
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
#include "KnockingAnomaly.h"
#include "ScratchAnomaly.h"
#include "ObservedScareSequence.h"
#include "CaughtSequence.h"
#include "FinalSequence.h"
#include "LightZoneProgress.h"
#include "PuzzleFeedback.h"
#include "BehindPresence.h"
#include "Stage2AnomalyPlan.h"
#include "TensionPulse.h"

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

// 2面のシーン。同じ廊下を3周しながら異変を見つけ、信号盤を直し、最後の停電から走って逃げる。
// 処理はStage2Scene.cpp（初期化・更新）、Stage2ScenePuzzles.cpp（周回と謎解き）、Stage2SceneHorror.cpp（怖い演出）、
// Stage2SceneDraw.cpp（描画と目的表示）に分けている。
class Stage2Scene : public Scene
{
private:
    // 看板・ランプなどの小さな光源を設定している（タイルベースライティングで数を増やせるため）。
    void SetupPracticalLights();
    // 目的表示の文章を選ぶために、今の状態を集めている（Stage2SceneDraw.cpp）。
    Stage2ObjectiveInput MakeObjectiveInput(bool confirmationPending) const;

    // 2面は同じ廊下を周回するたびに異変が追加される。
    // AdvanceLoopが周回の段階を進め、それぞれのUpdate関数が異変と謎解きを更新している。
    void Init();
    void Uninit();
    void AdvanceLoop(Player& player);
    // 影を見てしまったときの照明の演出を始める／進める
    void StartObservedScare();
    void UpdateObservedScare(float deltaTime);
    // 最後の停電の演出と、その後の追跡を始める／進める
    void StartFinalSequence();
    void UpdateFinalSequence(float deltaTime);
    void UpdateFinalPursuit(float deltaTime);
    // 捕まった演出を始める／進めて復帰させる
    void StartCaughtSequence(Player& player, CaughtSequence::Reason reason);
    void UpdateCaughtSequence(Player& player, float deltaTime);
    // 壁の引っかき傷を指定の範囲だけ見せる／照明の区画を通ったときの演出／傷の異変の更新
    void RevealScratchPieces(int first, int last, float emission);
    void UpdateLightZones(const Player& player);
    void UpdateScratchMessage(const Player& player, float deltaTime);
    // 肖像画の異変を更新している
    void UpdatePortraitAnomaly(const Player& player, float deltaTime);
    // この周回で見つけるべき異変（Stage2AnomalyPlanが決めたもの）を見つけ終えたかを返している。
    bool IsRequiredAnomalyFound() const;
    // ノックの異変を更新している／耳を澄ませる位置を返している
    void UpdateKnockingAnomaly(const Player& player, float deltaTime);
    DirectX::SimpleMath::Vector3 GetKnockListenPoint() const;
    // 偽の扉の異変を更新している／偽の扉の表示と位置を切り替えている
    void UpdateFalseDoorAnomaly(const Player& player);
    void SetFalseDoorState(bool visible, bool rightSide);
    // 周回に合わせて時計の針を決める／針を動かす／ライトを消して見たかを調べている
    void ConfigureClockForLoop();
    void UpdateClock(float deltaTime);
    void UpdateClockObservation();
    // パズルの間違いを記録している（種類によって知らせる文章が変わる）
    void RegisterPuzzleMistake(int type);
    // 足音の危険度と、足音を追う影を更新している
    void UpdateNoiseThreat(Player& player, float deltaTime);
    // 信号盤パズル・信号盤の段階で背後から来る影・信号盤の照明の状態・パズルのやり直し
    void UpdateSignalPuzzle();
    void UpdateSignalStalker();
    void ApplySignalLightingState();
    void ResetSignalPuzzle();
    // 背後の気配を更新している／今出してよい状況か
    void UpdateBehindPresence(Player& player, float deltaTime);
    bool IsBehindPresenceAllowed() const;
    // ロッカーに隠れている間の、足音の影の振る舞い。trueを返したらUpdateNoiseThreatを終えている。
    bool UpdateHiddenFromStalker(Player& player, float deltaTime);
    // 今の危険度（0〜1）を返している。足音の危険度・最後の追跡の影・足音の影までの距離から求めている。
    // includeMistakes が true のときは、1・2周目の観察の間違いの回数も含めている（画面の危険ゲージ用）。
    float ComputeThreatRate(const Player& player, bool includeMistakes) const;
    // 危険度に合わせて、自分の心拍音と呼吸音を鳴らしている（Stage2SceneHorror.cpp）。
    void UpdateTensionPulse(const Player& player, float deltaTime);

    // 配置で作った、進行で使うObject
    Stage2Objects m_Objects;

    // プレイヤーが見ている調べる対象と、画面へ出す案内を分けて管理している。
    InteractionSystem m_InteractionSystem;
    // HUD、息を潜める操作、信号盤パズル、足音の危険度、各異変、各演出の状態クラス
    Hud m_Hud;
    QuietRecovery m_QuietRecovery;
    SignalPuzzle m_SignalPuzzle;
    NoiseThreatSystem m_NoiseThreatSystem;
    ClockAnomaly m_ClockAnomaly;
    FalseDoorAnomaly m_FalseDoorAnomaly;
    PortraitAnomaly m_PortraitAnomaly;
    KnockingAnomaly m_KnockingAnomaly;
    ScratchAnomaly m_ScratchAnomaly;
    ObservedScareSequence m_ObservedScareSequence;
    CaughtSequence m_CaughtSequence;
    FinalSequence m_FinalSequence;
    LightZoneProgress m_LightZoneProgress;
    PuzzleFeedback m_PuzzleFeedback;
    BehindPresence m_BehindPresence;
    // 自分の心拍と呼吸をいつ鳴らすか。
    TensionPulse m_TensionPulse;
    // 1周目・2周目にどの異変を探させるか。プレイごとにランダムに決めている。
    Stage2AnomalyPlan m_AnomalyPlan;
    // 背後の気配の間隔に使う乱数
    std::mt19937 m_PresenceRandom{ std::random_device{}() };

    // timerが負なら実行していない、0以上なら対応する演出が進んでいる途中。
    // 何周目か（0〜3）
    int m_LoopCount = 0;
    // ロッカーに隠れて影に見失わせてから、影が去るまでの残り秒数（負なら見失わせていない）。
    float m_HiddenStalkerTimer = -1.0f;
    // 周回をすぐに続けて進めないための待ち時間
    float m_LoopCooldown = 0.0f;
    // 周回・充電器・記録・信号盤の一時的な知らせ。表示する文章はSelectStage2Objectiveが選んでいる。
    Stage2Notices m_Notices;
    // 見た目の演出に使う経過時間、周回の印の点滅、周回の切り替えの演出、進行が止まっている時間、案内の脈動の待ち時間
    float m_VisualTimer = 0.0f;
    float m_LoopBlinkTimer = 0.0f;
    float m_LoopTransitionTimer = -1.0f;
    float m_ProgressHintTimer = 0.0f;
    float m_GuidancePulseCooldown = 0.0f;
    // この周回で異常確認のスイッチを押したか、充電器を使ったか、記録を回収したか
    bool m_ConfirmationHandledThisLoop = false;
    bool m_ChargerHandled = false;
    bool m_EvidenceHandled[2] = { false, false };
    // 最後のイベントの準備ができたか、最後の出口が開けられるか
    bool m_FinalSequenceArmed = false;
    bool m_FinalDoorReady = false;
    // デバッグ画面から予約された操作。次に操作できるフレームで実行している。
    std::optional<SceneDebugAction> m_PendingDebugAction;

public:
    // コンストラクタでInitを、デストラクタでUninitを呼んでいる
    Stage2Scene();
    ~Stage2Scene();

    // 1フレーム分の進行を更新している
    void Update() override;
    // HUD・目的表示・監視映像などを描いている
    void Draw(Camera* camera) override;

    // デバッグ画面に進行の状態を渡している／デバッグ画面のボタンの操作を受け取っている
    bool TryGetDebugInfo(SceneDebugInfo& info) const override;
    void RequestDebugAction(SceneDebugAction action) override;
};
