// ============================================================================
// ファイルの役割: 1面の画面上部に出す目的・通知・ヒントの文章を、現在の状態から1つ選びます。
// 主な技術: 描画・入力に依存しない純粋関数、優先順位付きの表示選択
// ============================================================================

#include "Stage1Objective.h"

std::string SelectStage1Objective(const Stage1ObjectiveInput& in)
{
    std::string objectiveText;
    if (in.fuseCount <= 0)
    {
        objectiveText = "開始地点の近くでヒューズを探す";
    }
    else if (in.fuseCount == 1)
    {
        objectiveText = in.corridorLoopCount < 1
            ? "中央のドアを開けて廊下の奥へ進む"
            : "左側の部屋でヒューズを探す";
    }
    else if (in.fuseCount == 2)
    {
        objectiveText = in.corridorLoopCount < 2
            ? "もう一度廊下の奥まで進む"
            : "右側の部屋でヒューズを探す";
    }
    else
    {
        objectiveText = "左の部屋にある配電盤を調べる";
    }
    if (in.escaping)
    {
        objectiveText = "ドアの先へ移動中";
    }
    else if (!in.hiddenRoomText.empty())
    {
        // 隠し部屋に閉じ込められている間は、その場の状況だけを伝えます。
        objectiveText = std::string(in.hiddenRoomText);
    }
    else if (in.patrolDispatched)
    {
        // 現地確認中の残り時間と除去の進み具合は毎フレーム変わるため、ここで文章を組み立てます。
        const std::string cameraLabel(in.patrolCameraLabel);
        const std::string anomalyLabel(in.patrolAnomalyLabel);
        if (!in.flashlightOn)
        {
            objectiveText = cameraLabel +
                " へ向かい " + anomalyLabel + "をライトで照らす  残り " +
                std::to_string(in.patrolRemainingSeconds) + "秒";
        }
        else if (in.patrolConfirmPercent > 0)
        {
            objectiveText = "異常を光で除去中  " +
                std::to_string(in.patrolConfirmPercent) + "%";
        }
        else
        {
            objectiveText = cameraLabel +
                " の" + anomalyLabel + "をライトで探す  残り " +
                std::to_string(in.patrolRemainingSeconds) + "秒";
        }
    }
    else if (in.patrolNotice)
    {
        objectiveText = in.patrolNoticeText;
    }
    else if (in.chargerNotice)
    {
        objectiveText = in.fuseWatcherState == 1
            ? "充電音で影が現れた ライトを向ける"
            : "バッテリーを充電した";
    }
    else if (in.evidenceNotice)
    {
        objectiveText = "監視カメラの巡回を終えた";
    }
    else if (in.storageScareNotice)
    {
        objectiveText = in.storageScarePhase == 1
            ? "背後で金属音がした"
            : (in.storageScarePhase == 2
                ? "背後に気配がある"
                : "影は光の中へ消えた");
    }
    else if (in.fuseNotice)
    {
        if (in.fuseWatcherState == 1)
            objectiveText = "影に懐中電灯を向ける";
        else if (in.fuseCount == 1) objectiveText = "ヒューズを1本入手";
        else if (in.fuseCount == 2) objectiveText = "ヒューズを2本入手";
        else objectiveText = "ヒューズを3本入手";
    }
    else if (in.fuseWatcherNotice)
    {
        objectiveText = in.fuseWatcherState == 1
            ? "影を正面から懐中電灯で照らす"
            : "影が光の中へ消えた";
    }
    else if (in.exitOmenSeconds > 0.0f)
    {
        objectiveText = in.exitOmenSeconds > 1.75f
            ? "何かが待っている"
            : "立ち止まらず進む";
    }
    else if (in.powerRestored &&
        in.powerRestoreActive &&
        in.powerRestoreSeconds < 4.5f)
    {
        objectiveText = in.powerRestoreSeconds < 1.55f
            ? "電力が復旧した"
            : "出口側の非常送電盤へ向かう";
    }
    else if (in.exitPowerActivated && !in.exitPowerReady)
    {
        objectiveText = "非常電源を送電中";
    }
    else if (in.scareLightNoticeSeconds > 0.0f)
    {
        objectiveText = in.scareLightNoticeSeconds > 2.65f
            ? "何かに見られている"
            : "点灯した照明をたどる";
    }
    else if (in.loopNotice)
    {
        if (in.corridorLoopCount == 1)
        {
            objectiveText = "廊下の様子が変わった";
        }
        else if (in.corridorLoopCount == 2)
        {
            objectiveText = "そのまま歩き続ける";
        }
        else
        {
            objectiveText = "後ろを振り返らない";
        }
    }
    else if (in.progressHintSeconds >= 35.0f)
    {
        if (in.powerRestored && !in.exitPowerActivated)
        {
            objectiveText = "ヒント 右奥の赤い送電盤を調べる";
        }
        else if (in.powerRestored)
        {
            objectiveText = "ヒント 右奥の緑色の出口へ向かう";
        }
        else if (in.fuseCount <= 0)
        {
            objectiveText = "ヒント 最初の壁付近を探す";
        }
        else if (in.fuseCount == 1 && in.corridorLoopCount < 1)
        {
            objectiveText = "ヒント 中央のドアを開け廊下の奥へ進む";
        }
        else if (in.fuseCount == 1)
        {
            objectiveText = "ヒント 左奥の部屋を探す";
        }
        else if (in.fuseCount == 2 && in.corridorLoopCount < 2)
        {
            objectiveText = "ヒント もう一度廊下の奥まで進む";
        }
        else if (in.fuseCount == 2)
        {
            objectiveText = "ヒント 右奥の部屋を探す";
        }
        else
        {
            objectiveText = "ヒント 左の部屋の配電盤を調べる";
        }
    }
    else if (in.progressHintSeconds >= 18.0f)
    {
        if (in.powerRestored && !in.exitPowerActivated)
        {
            objectiveText = "ヒント 出口手前の送電盤へ向かう";
        }
        else if (in.powerRestored)
        {
            objectiveText = "ヒント 緑色の出口灯をたどる";
        }
        else if (in.fuseCount <= 0)
        {
            objectiveText = "ヒント 開始地点の周囲を探す";
        }
        else if (in.fuseCount == 1 && in.corridorLoopCount < 1)
        {
            objectiveText = "ヒント 中央のドアが進行ルート";
        }
        else if (in.fuseCount == 1)
        {
            objectiveText = "ヒント 左側の部屋を確認する";
        }
        else if (in.fuseCount == 2 && in.corridorLoopCount < 2)
        {
            objectiveText = "ヒント 長い廊下をもう一度進む";
        }
        else if (in.fuseCount == 2)
        {
            objectiveText = "ヒント 右側の部屋を確認する";
        }
        else
        {
            objectiveText = "ヒント 配電盤へ戻る";
        }
    }
    else if (in.powerRestored && !in.exitPowerActivated)
    {
        objectiveText = "出口手前の非常送電盤を操作する";
    }
    else if (in.powerRestored)
    {
        objectiveText = in.exitDoorOpen
            ? "開いた出口ドアを通り抜ける"
            : "右奥の出口ドアを開ける";
    }
    return objectiveText;
}
