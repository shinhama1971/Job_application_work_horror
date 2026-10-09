// ============================================================================
// ファイルの役割: 2面の画面の左上に出す目的・知らせ・ヒントの文章を、今の状態から1つ選んでいる。
// 主な技術: 描画・入力に依存しない関数（同じ入力なら必ず同じ結果）、優先順位を付けた表示の選択
// ============================================================================

#include "Stage2Objective.h"

#include <cstddef>

namespace
{
    // この周回で探す異変が、まだ見つかっていないかを返している。
    bool IsSearchingAnomaly(const Stage2ObjectiveInput& in)
    {
        return in.requiredAnomaly != Stage2Anomaly::None && !in.requiredAnomalyFound;
    }

    // 15秒止まったときのヒント。どの異変を探す周回かは毎回変わるため、ここで初めて場所と方法を伝えている。
    std::string_view GetAnomalyHint(const Stage2ObjectiveInput& in)
    {
        switch (in.requiredAnomaly)
        {
        case Stage2Anomaly::FalseDoor:
            return in.falseDoorObserved
                ? "ヒント 偽物のドアから視線を外す"
                : "ヒント ライトを点け前方左側の壁を探す";
        case Stage2Anomaly::Clock:
            return "ヒント ライトを消して左の時計を見る";
        case Stage2Anomaly::Portrait:
            return "ヒント 右の肖像画をライトで照らす";
        case Stage2Anomaly::Knocking:
            return "ヒント 立ち止まって壁を叩く音の方向を探す";
        case Stage2Anomaly::None:
            break;
        }
        return "ヒント 廊下の変化を探す";
    }

    // 30秒止まったときのヒント。解き方をそのまま伝えている。
    std::string_view GetAnomalyStrongHint(const Stage2ObjectiveInput& in)
    {
        switch (in.requiredAnomaly)
        {
        case Stage2Anomaly::FalseDoor:
            return "ヒント ライトで偽物のドアを照らして視線を外す";
        case Stage2Anomaly::Clock:
            return "ヒント ライトを消して左の時計を正面から見る";
        case Stage2Anomaly::Portrait:
            return "ヒント ライトで右の肖像画を照らしたまま見つめ続ける";
        case Stage2Anomaly::Knocking:
            return "ヒント 音のする壁の前で止まり 壁の方を向いて耳を澄ます";
        case Stage2Anomaly::None:
            break;
        }
        return "ヒント 廊下の変化を探す";
    }
}

std::string_view SelectStage2Objective(const Stage2ObjectiveInput& in)
{
    // 周回ごとの普段の目的（扉が閉まっているとき・開いたとき）
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
    if (IsSearchingAnomaly(in))
    {
        // どの異変が出るかは毎回変わるため、普段の目的では答えを言わず、探させている。
        // 解き方の途中まで進んでいるときだけ、次の一手を伝えている。
        if (in.requiredAnomaly == Stage2Anomaly::FalseDoor && in.falseDoorObserved)
        {
            objective = "偽物のドアから視線を外す";
        }
        else if (in.requiredAnomaly == Stage2Anomaly::Portrait && in.portraitStaring)
        {
            objective = "肖像画から目を離さない";
        }
        else if (in.requiredAnomaly == Stage2Anomaly::Knocking && in.knockListening)
        {
            objective = "動かずに耳を澄ます";
        }
        else
        {
            objective = "廊下のどこかが変わっている 異常を探す";
        }
    }
    else if (in.confirmationPending)
    {
        objective = "奥の異常確認スイッチを押す";
    }
    // 3周目以降：信号盤を青・黄・赤の順に操作する。2回以上間違えたら、助けが入っていることを伝えている
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
    // ここから下は、上に書いたものほど優先して、普段の目的を上書きしている
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
    else if (in.hiding)
    {
        objective = in.noiseShadowActive
            ? "息を潜める 影が去るまで動かない"
            : "ロッカーに隠れている";
    }
    else if (in.hidingNotice)
    {
        objective = "影は見失って去った 外へ出て進む";
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
            ? "水音を聞いた影が来る ライトを当てるかロッカーに隠れる"
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
    // パズルの間違いの知らせ（種類によって、どう直せばよいかを伝えている）
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
    // 最後のイベント（影を見た罰・走って逃げる・出口が開いた）
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
    // 異変を見つけたときの知らせ
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
        objective = in.requiredAnomaly == Stage2Anomaly::Clock
            ? "逆回転を確認した 奥のスイッチへ進む"
            : "時計の時刻が変わった";
    }
    else if (in.portraitNotice)
    {
        objective = "目が開いた 奥のスイッチへ進む";
    }
    else if (in.knockNotice)
    {
        objective = "音の出どころを見つけた 奥のスイッチへ進む";
    }
    // 周回が変わったときの知らせ
    else if (in.loopNotice)
    {
        if (in.loopCount == 0) objective = "1回目 奥のドアを開ける";
        else if (in.loopCount == 1) objective = in.requiredAnomalyFound
            ? (in.confirmationHandledThisLoop
                ? "鍵が開いた 奥のドアへ進む"
                : "奥の異常確認スイッチを押す")
            : "2回目 廊下の変化を探す";
        else if (in.loopCount == 2) objective = in.requiredAnomalyFound
            ? (in.confirmationHandledThisLoop
                ? "鍵が開いた 後ろを見ずに進む"
                : "奥の異常確認スイッチを押す")
            : "3回目 前と違うところを探す";
        else objective = in.signalPuzzleComplete
            ? "信号が復旧した 廊下の中央へ進む"
            : "奥の青い信号盤から復旧する";
    }
    // 30秒止まっていたら、場所と方法がはっきり分かるヒントを出している
    else if (in.progressHintSeconds >= 30.0f)
    {
        if (IsSearchingAnomaly(in))
        {
            objective = GetAnomalyStrongHint(in);
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
    // 15秒止まっていたら、方向を示す軽いヒントを出している
    else if (in.progressHintSeconds >= 15.0f)
    {
        if (IsSearchingAnomaly(in))
        {
            objective = GetAnomalyHint(in);
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
