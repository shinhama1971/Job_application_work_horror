// ============================================================================
// ファイルの役割: 1面の画面上部に出す目的・通知・ヒントの文章を、現在の状態から1つ選びます。
// 主な技術: 描画・入力に依存しない純粋関数、優先順位付きの表示選択
// Sceneは状態をStage1ObjectiveInputに詰めて渡すだけで、どの文章を出すかはここで決まります。
// 監視カメラの現地確認中は残り秒数などを含む文章を組み立てるため、std::stringで返します。
// ============================================================================

#pragma once

#include <string>
#include <string_view>

// 目的表示を選ぶために必要な状態です。bool の通知は「その通知の残り時間が0より大きい」ことを表します。
struct Stage1ObjectiveInput
{
    int fuseCount = 0;
    int corridorLoopCount = 0;
    float progressHintSeconds = 0.0f;   // 進行が止まっている時間。18秒・35秒でヒントを出します

    // 電力と出口
    bool powerRestored = false;
    bool powerRestoreActive = false;
    float powerRestoreSeconds = 0.0f;   // 電力復旧の演出が始まってからの秒数
    bool exitPowerActivated = false;
    bool exitPowerReady = false;
    bool exitDoorOpen = false;
    bool escaping = false;

    // 監視カメラの現地確認
    bool patrolDispatched = false;
    std::string_view patrolCameraLabel;
    std::string_view patrolAnomalyLabel;
    int patrolRemainingSeconds = 0;
    int patrolConfirmPercent = 0;
    bool flashlightOn = false;
    bool patrolNotice = false;
    std::string_view patrolNoticeText;

    // 人影の演出（fuseWatcherState: 1なら影が出ていてライトを向ける段階）
    int fuseWatcherState = 0;
    int storageScarePhase = 0;
    float exitOmenSeconds = 0.0f;
    float scareLightNoticeSeconds = 0.0f;

    // 通知（残り時間が0より大きいか）
    bool chargerNotice = false;
    bool evidenceNotice = false;
    bool storageScareNotice = false;
    bool fuseNotice = false;
    bool fuseWatcherNotice = false;
    bool loopNotice = false;
};

// 状態に応じた目的表示の文章を返します。上に書いた条件ほど優先されます
// （移動中 → 監視カメラの現地確認 → 一時的な通知 → 人影・電力の演出 → ヒント → 通常の目的）。
std::string SelectStage1Objective(const Stage1ObjectiveInput& in);
