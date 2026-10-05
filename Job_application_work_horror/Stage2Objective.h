// ============================================================================
// ファイルの役割: 2面の画面上部に出す目的・通知・ヒントの文章を、現在の状態から1つ選びます。
// 主な技術: 描画・入力に依存しない純粋関数、優先順位付きの表示選択
// Sceneは状態をStage2ObjectiveInputに詰めて渡すだけで、どの文章を出すかはここで決まります。
// ============================================================================

#pragma once

#include "Stage2AnomalyPlan.h"

#include <string_view>

// 2面のSceneが持つ一時的な通知の残り時間です。まとめて進め、まとめてリセットします。
struct Stage2Notices
{
    float loop = 0.0f;       // 周回の開始や鍵の解除などの通知
    float charger = 0.0f;    // 非常用充電器を使った通知
    float evidence = 0.0f;   // 記録を回収した通知
    float signal = 0.0f;     // 信号盤パズルの進行・失敗の通知
    float hiding = 0.0f;     // ロッカーに隠れてやり過ごした通知

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

    void Reset()
    {
        *this = Stage2Notices{};
    }
};

// 目的表示を選ぶために必要な状態です。bool の通知は「その通知の残り時間が0より大きい」ことを表します。
struct Stage2ObjectiveInput
{
    int loopCount = 0;
    float progressHintSeconds = 0.0f;   // 進行が止まっている時間。15秒・30秒でヒントを出します
    bool controllerConnected = false;

    // 扉・出口
    bool finalDoorOpen = false;
    bool escaping = false;
    bool finalDoorReady = false;
    bool confirmationPending = false;
    bool confirmationHandledThisLoop = false;

    // 異変。この周回で探す異変（Stage2AnomalyPlanが決めたもの）と、見つけたかどうか
    Stage2Anomaly requiredAnomaly = Stage2Anomaly::None;
    bool requiredAnomalyFound = false;
    bool falseDoorObserved = false;     // 偽ドアを照らした（あとは目を離すだけ）
    bool portraitStaring = false;       // 肖像画を見つめている途中
    bool knockListening = false;        // ノックの出どころの壁の前で耳を澄ませている途中

    // 信号盤パズル
    bool signalPuzzleComplete = false;
    int signalPuzzleStep = 0;
    bool puzzleFeedbackVisible = false;
    int puzzleFeedbackType = 0;
    int puzzleMistakeCount = 0;

    // 危険と最終イベント
    bool caught = false;
    bool hiding = false;                // ロッカーに隠れている
    bool hidingNotice = false;          // 隠れて影をやり過ごした直後
    bool presenceTooClose = false;
    bool quietRecoverySucceeded = false;
    bool quietRecoveryInProgress = false;
    bool noiseShadowActive = false;
    bool finalGazePenalty = false;
    bool finalSequenceActive = false;
    bool finalPursuitActive = false;
    bool finalSequenceArmed = false;

    // 通知（残り時間が0より大きいか）
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

// 状態に応じた目的表示の文章を返します。上に書いた条件ほど優先されます
// （脱出中・捕獲などの緊急表示 → 一時的な通知 → 最終イベント → 異変の確認 → ヒント → 通常の目的）。
std::string_view SelectStage2Objective(const Stage2ObjectiveInput& in);
