// ============================================================================
// ファイルの役割: 2面の画面の左上に出す目的・知らせ・ヒントの文章を、今の状態から1つ選んでいる。
// 主な技術: 描画・入力に依存しない関数（同じ入力なら必ず同じ結果）、優先順位を付けた表示の選択
// Sceneは状態をStage2ObjectiveInputに詰めて渡すだけで、どの文章を出すかはここで決めている。
// ============================================================================

#pragma once

#include "Stage2AnomalyPlan.h"

#include <string_view>

// 2面のSceneが持つ、一時的な知らせの残り時間。まとめて減らし、まとめて0に戻している。
struct Stage2Notices
{
    float loop = 0.0f;       // 周回の始まりや鍵が開いたことなどの知らせ
    float charger = 0.0f;    // 非常用充電器を使った知らせ
    float evidence = 0.0f;   // 記録を回収した知らせ
    float signal = 0.0f;     // 信号盤パズルの進み具合・失敗の知らせ
    float hiding = 0.0f;     // ロッカーに隠れてやり過ごした知らせ

    // 全部の知らせの残り時間を減らしている（0より下にはしない）
    void Tick(float deltaTime)
    {
        const auto tick = [deltaTime](float& timer)
        {
            timer = timer - deltaTime > 0.0f ? timer - deltaTime : 0.0f;
        };
        tick(loop);
        tick(charger);
        tick(evidence);
        tick(signal);
        tick(hiding);
    }

    // 全部の知らせを消している
    void Reset()
    {
        *this = Stage2Notices{};
    }
};

// 目的表示を選ぶために必要な状態。bool の知らせは「その知らせの残り時間が0より大きい」ことを表している。
struct Stage2ObjectiveInput
{
    // 周回の数
    int loopCount = 0;
    float progressHintSeconds = 0.0f;   // 進行が止まっている時間。15秒・30秒でヒントを出している
    // コントローラーがつながっているか（操作の説明を切り替えるため）
    bool controllerConnected = false;

    // 扉・出口（奥の扉が開いたか、脱出中か、最後の出口が開けられるか、異常確認のスイッチを押す段階か、この周回で押したか）
    bool finalDoorOpen = false;
    bool escaping = false;
    bool finalDoorReady = false;
    bool confirmationPending = false;
    bool confirmationHandledThisLoop = false;

    // 異変。この周回で探す異変（Stage2AnomalyPlanが決めたもの）と、見つけたかどうか
    Stage2Anomaly requiredAnomaly = Stage2Anomaly::None;
    bool requiredAnomalyFound = false;
    bool falseDoorObserved = false;     // 偽の扉を照らした（あとは目を離すだけ）
    bool portraitStaring = false;       // 肖像画を見つめている途中
    bool knockListening = false;        // ノックの出どころの壁の前で、耳を澄ませている途中

    // 信号盤パズル（解けたか、段階、知らせを出しているか、知らせの種類、間違えた回数）
    bool signalPuzzleComplete = false;
    int signalPuzzleStep = 0;
    bool puzzleFeedbackVisible = false;
    int puzzleFeedbackType = 0;
    int puzzleMistakeCount = 0;

    // 危険と最後のイベント
    // 捕まった演出の最中か
    bool caught = false;
    bool hiding = false;                // ロッカーに隠れている
    bool hidingNotice = false;          // 隠れて影をやり過ごした直後
    // 背後の気配が近すぎるか、息を潜めるのに成功した直後か、息を潜めている途中か、足音の影が出ているか
    bool presenceTooClose = false;
    bool quietRecoverySucceeded = false;
    bool quietRecoveryInProgress = false;
    bool noiseShadowActive = false;
    // 最後の追跡で影を見てしまった罰の最中か、最後の演出中か、追跡中か、最後のイベントの準備ができたか
    bool finalGazePenalty = false;
    bool finalSequenceActive = false;
    bool finalPursuitActive = false;
    bool finalSequenceArmed = false;

    // 知らせ（残り時間が0より大きいか）
    bool observedScareNotice = false;
    bool chargerNotice = false;
    bool evidenceNotice = false;
    bool signalNotice = false;
    bool stalkerNotice = false;
    bool wetStepNotice = false;
    bool noiseWarning = false;
    bool scratchNotice = false;
    bool falseDoorNotice = false;
    bool clockNotice = false;
    bool portraitNotice = false;
    bool knockNotice = false;
    bool loopNotice = false;
};

// 状態に応じた目的表示の文章を返している。上に書いた条件ほど優先している
// （脱出中・捕まったなどの緊急の表示 → 一時的な知らせ → 最後のイベント → 異変の確認 → ヒント → 普段の目的）。
std::string_view SelectStage2Objective(const Stage2ObjectiveInput& in);
