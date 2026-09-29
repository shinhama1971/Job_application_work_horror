// ============================================================================
// ファイルの役割: 2面の画面上部に出す目的・通知・ヒントの文章を、現在の状態から1つ選びます。
// 主な技術: 描画・入力に依存しない純粋関数、優先順位付きの表示選択
// ============================================================================

#include "Stage2Objective.h"

#include <cstddef>

std::string_view SelectStage2Objective(const Stage2ObjectiveInput& in)
{
    constexpr std::string_view closedLoopObjectives[] =
    {
        "奥のドアを開ける 1回目",
        "奥のドアを開ける 2回目",
        "奥のドアを開ける 3回目"
    };
    constexpr std::string_view openLoopObjectives[] =
    {
        "開いたドアを通り抜ける 1回目",
        "開いたドアを通り抜ける 2回目",
        "開いたドアを通り抜ける 3回目"
    };

    std::string_view objective = "廊下の奥にある出口へ向かう";
    if (in.loopCount < 3)
    {
        const std::size_t loopIndex = static_cast<std::size_t>(in.loopCount);
        objective = in.finalDoorOpen
            ? openLoopObjectives[loopIndex]
            : closedLoopObjectives[loopIndex];
    }
    if (in.loopCount == 1 && !in.falseDoorMoved)
    {
        objective = in.falseDoorObserved
            ? "偽物のドアから視線を外す"
            : "懐中電灯で左の偽物のドアを照らす";
    }
    else if (in.loopCount == 2 && !in.clockObservedThisLoop)
    {
        objective = "ライトを消して左の時計を見る";
    }
    else if (in.confirmationPending)
    {
        objective = "奥の異常確認スイッチを押す";
    }
    else if (in.loopCount >= 3 && !in.signalPuzzleComplete)
    {
        if (in.signalPuzzleStep == 0)
        {
            objective = in.puzzleMistakeCount >= 2
                ? "再試行補助中 奥の青い信号盤からやり直す"
                : "信号復旧 まず奥の青い信号盤を操作する";
        }
        else if (in.signalPuzzleStep == 1)
        {
            objective = "信号復旧 黄色へ戻る 背後の影はライトで追い払う";
        }
        else
        {
            objective = "信号復旧 赤へ戻る 背後の影はライトで追い払う";
        }
    }
    if (in.escaping)
    {
        objective = "脱出中";
    }
    else if (in.observedScareNotice)
    {
        objective = "止まらず奥のドアへ進む";
    }
    else if (in.caught)
    {
        objective = "捕まった チェックポイントへ戻る";
    }
    else if (in.presenceTooClose)
    {
        objective = "影が近すぎる 距離を取りライトを消して止まる";
    }
    else if (in.quietRecoverySucceeded)
    {
        objective = "気配が遠のいた 静かに探索を続ける";
    }
    else if (in.quietRecoveryInProgress)
    {
        objective = "息を潜めている 消灯したまま動かない";
    }
    else if (in.chargerNotice)
    {
        objective = "充電器の音で廊下が反応した";
    }
    else if (in.evidenceNotice)
    {
        objective = "残された記録を回収した";
    }
    else if (in.signalNotice)
    {
        if (in.signalPuzzleComplete)
        {
            objective = "信号復旧完了 廊下の中央へ進む";
        }
        else if (in.puzzleFeedbackType == 3)
        {
            objective = "順番が違う 青からやり直す";
        }
        else if (in.puzzleFeedbackType == 4)
        {
            objective = "走る音で同期が切れた 歩いて青からやり直す";
        }
        else if (in.puzzleFeedbackType == 5)
        {
            objective = "影に追いつかれた 青からやり直す";
        }
        else if (in.puzzleFeedbackType == 6)
        {
            objective = "影を追い払った 次の信号盤へ進む";
        }
        else if (in.signalPuzzleStep == 1)
        {
            objective = "青を確認 黄色へ戻る 背後に注意";
        }
        else
        {
            objective = "黄色を確認 赤へ戻る 背後に注意";
        }
    }
    else if (in.stalkerNotice)
    {
        objective = in.noiseShadowActive
            ? "水音を聞いた影が来る 振り返ってライトを当てる"
            : "影を追い払った 静かに進む";
    }
    else if (in.wetStepNotice)
    {
        objective = "水音が廊下に響いた 水たまりは歩いて渡る";
    }
    else if (in.noiseWarning)
    {
        objective = "足音が響いている 歩いて静める";
    }
    else if (in.puzzleFeedbackVisible)
    {
        if (in.puzzleFeedbackType == 5)
        {
            objective = "影に追いつかれた 青からやり直す";
        }
        else if (in.puzzleFeedbackType == 6)
        {
            objective = "影を追い払った 次の信号盤へ進む";
        }
        else if (in.puzzleMistakeCount >= 3)
        {
            objective = "照明が消えた 正しい方法を試す";
        }
        else
        {
            if (in.puzzleFeedbackType == 1)
            {
                objective = "光が必要だ 懐中電灯でドアを照らす";
            }
            else if (in.puzzleFeedbackType == 2)
            {
                objective = "光が邪魔だ 懐中電灯を消して時計を見る";
            }
            else if (in.puzzleFeedbackType == 3)
            {
                objective = "信号の順番が違う 青からやり直す";
            }
            else
            {
                objective = "走る音で同期が切れた 歩いて青からやり直す";
            }
        }
    }
    else if (in.finalGazePenalty)
    {
        objective = "それを見てはいけない";
    }
    else if (in.finalSequenceActive && !in.finalDoorReady)
    {
        objective = in.controllerConnected
            ? "左スティック押し込みで出口まで走る"
            : "SHIFTを押して出口まで走る";
    }
    else if (in.finalPursuitActive)
    {
        objective = in.controllerConnected
            ? "左スティック押し込みで出口まで走る"
            : "SHIFTを押して出口まで走る";
    }
    else if (in.finalDoorReady)
    {
        objective = !in.finalDoorOpen
            ? "奥のドアを開ける"
            : "開いた出口を通り抜ける";
    }
    else if (in.scratchNotice)
    {
        objective = "止まらず奥のドアへ進む";
    }
    else if (in.falseDoorNotice)
    {
        objective = "異常を確認した 奥のスイッチへ進む";
    }
    else if (in.clockNotice)
    {
        objective = in.loopCount == 2
            ? "逆回転を確認した 奥のスイッチへ進む"
            : "時計の時刻が変わった";
    }
    else if (in.portraitNotice)
    {
        objective = "止まらず奥のドアへ進む";
    }
    else if (in.loopNotice)
    {
        if (in.loopCount == 0) objective = "1回目 奥のドアを開ける";
        else if (in.loopCount == 1) objective = in.falseDoorMoved
            ? (in.confirmationHandledThisLoop
                ? "鍵が開いた 奥のドアへ進む"
                : "奥の異常確認スイッチを押す")
            : "2回目 廊下の変化を探す";
        else if (in.loopCount == 2) objective = in.clockObservedThisLoop
            ? (in.confirmationHandledThisLoop
                ? "鍵が開いた 後ろを見ずに進む"
                : "奥の異常確認スイッチを押す")
            : "3回目 左の時計を調べる";
        else objective = in.signalPuzzleComplete
            ? "信号が復旧した 廊下の中央へ進む"
            : "奥の青い信号盤から復旧する";
    }
    else if (in.progressHintSeconds >= 30.0f)
    {
        if (in.loopCount == 1 && !in.falseDoorMoved)
        {
            objective = "ヒント ライトで偽物のドアを照らして視線を外す";
        }
        else if (in.loopCount == 2 && !in.clockObservedThisLoop)
        {
            objective = "ヒント ライトを消して左の時計を正面から見る";
        }
        else if (in.confirmationPending)
        {
            objective = "ヒント 奥の壁にある赤い確認スイッチを押す";
        }
        else if (in.loopCount >= 3 && !in.signalPuzzleComplete)
        {
            objective = in.signalPuzzleStep == 0
                ? "ヒント 青は廊下の奥の右壁"
                : in.signalPuzzleStep == 1
                    ? "ヒント 黄色は中央左 影は振り返ってライトを当てる"
                    : "ヒント 赤は入口右 影は振り返ってライトを当てる";
        }
        else if (in.finalDoorReady)
        {
            objective = in.finalDoorOpen
                ? "ヒント 開いた出口を通り抜ける"
                : "ヒント 今すぐ奥のドアを開ける";
        }
        else if (in.finalSequenceArmed)
        {
            objective = "ヒント 廊下の中央より先へ進む";
        }
        else if (in.finalDoorOpen)
        {
            objective = "ヒント 開いた奥のドアを通り抜ける";
        }
        else
        {
            objective = "ヒント まっすぐ進み奥のドアを開ける";
        }
    }
    else if (in.progressHintSeconds >= 15.0f)
    {
        if (in.loopCount == 1 && !in.falseDoorMoved)
        {
            objective = in.falseDoorObserved
                ? "ヒント 偽物のドアから視線を外す"
                : "ヒント ライトを点け前方左側の壁を探す";
        }
        else if (in.loopCount == 2 && !in.clockObservedThisLoop)
        {
            objective = "ヒント ライトを消して左の時計を見る";
        }
        else if (in.confirmationPending)
        {
            objective = "ヒント ドア手前の確認スイッチへ進む";
        }
        else if (in.loopCount >= 3 && !in.signalPuzzleComplete)
        {
            objective = "ヒント 発光している信号盤を 青 黄 赤 の順で操作する";
        }
        else if (in.finalDoorReady)
        {
            objective = "ヒント 廊下の奥にある出口が開いている";
        }
        else if (in.finalSequenceArmed)
        {
            objective = "ヒント 廊下をそのまま歩き続ける";
        }
        else if (in.finalDoorOpen)
        {
            objective = "ヒント 奥のドアを開けると次へ進む";
        }
        else
        {
            objective = "ヒント 奥のドアを通り抜ける";
        }
    }
    else if (in.finalSequenceArmed)
    {
        objective = "廊下の奥にある出口へ向かう";
    }
    return objective;
}
