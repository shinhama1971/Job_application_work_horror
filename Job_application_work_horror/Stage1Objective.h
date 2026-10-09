// ============================================================================
// ファイルの役割: 1面の画面の左上に出す目的・知らせ・ヒントの文章を、今の状態から1つ選んでいる。
// 主な技術: 描画・入力に依存しない関数（同じ入力なら必ず同じ結果）、優先順位を付けた表示の選択
// Sceneは状態をStage1ObjectiveInputに詰めて渡すだけで、どの文章を出すかはここで決めている。
// 監視カメラの現地確認の間は残り秒数などを含む文章を組み立てるため、std::stringで返している。
// ============================================================================

#pragma once

#include <string>
#include <string_view>

// 目的表示を選ぶために必要な状態。bool の知らせは「その知らせの残り時間が0より大きい」ことを表している。
struct Stage1ObjectiveInput
{
    // 拾ったヒューズの数、ループ廊下を通った回数
    int fuseCount = 0;
    int corridorLoopCount = 0;
    float progressHintSeconds = 0.0f;   // 進行が止まっている時間。18秒・35秒でヒントを出している

    // 電力と出口
    bool powerRestored = false;
    bool powerRestoreActive = false;
    float powerRestoreSeconds = 0.0f;   // 電力が戻る演出が始まってからの秒数
    // 出口の送電盤を操作したか、送電が終わったか、出口の扉が開いたか、脱出の演出中か
    bool exitPowerActivated = false;
    bool exitPowerReady = false;
    bool exitDoorOpen = false;
    bool escaping = false;

    // 監視カメラの現地確認（向かう先のカメラの名前・異常の名前・残り秒数・光で消した割合・ライトが点いているか・知らせ）
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
    // 左の倉庫の演出の段階、出口の前兆の残り秒数、照明の演出の知らせの残り秒数
    int storageScarePhase = 0;
    float exitOmenSeconds = 0.0f;
    float scareLightNoticeSeconds = 0.0f;

    // 知らせ（残り時間が0より大きいか）
    bool chargerNotice = false;
    bool evidenceNotice = false;
    bool storageScareNotice = false;
    bool fuseNotice = false;
    bool fuseWatcherNotice = false;
    bool loopNotice = false;

    // 隠し部屋の閉じ込めイベントの文章（空なら出さない）
    std::string_view hiddenRoomText;

    // 書類保管室の「照らすと止まる影」の知らせ（空なら出さない）
    std::string_view archiveStalkerText;

    // 西棟の進み具合（3本目のヒューズを探す段階で使っている）。
    // 1 = 右の倉庫で鍵を探す、2 = 西側の扉を鍵で開ける、3 = 西棟の奥でヒューズを探す。それ以外は使わない。
    int westWingStep = 0;
};

// 状態に応じた目的表示の文章を返している。上に書いた条件ほど優先している
// （移動中 → 隠し部屋 → 書類保管室の影の知らせ → 監視カメラの現地確認 → 一時的な知らせ → 人影・電力の演出 → ヒント → 普段の目的）。
std::string SelectStage1Objective(const Stage1ObjectiveInput& in);
