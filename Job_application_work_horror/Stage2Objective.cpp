// ============================================================================
// ファイルの役割: 2面の画面の左上に出す目的・知らせ・ヒントの文章を、今の状態から1つ選んでいる。
// 主な技術: 描画・入力に依存しない関数（同じ入力なら必ず同じ結果）、優先順位を付けた表示の選択
// ============================================================================

#include "Stage2Objective.h"
#include "PuzzleFeedback.h"
#include "Stage2SceneConstants.h"

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
        case Stage2Anomaly::Handprint:
            return "ヒント ライトで壁を照らし 浮かんだ手形を探す";
        case Stage2Anomaly::Mirror:
            return "ヒント 左の壁の鏡をのぞき込む";
        case Stage2Anomaly::Unseen:
            return "ヒント ラジオの雑音が強くなる方から 何かが来る";
        case Stage2Anomaly::Puddle:
            return "ヒント 中央の水たまりをのぞき込む";
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
        case Stage2Anomaly::Handprint:
            return "ヒント 手形をライトで照らすと 奥の反対側の壁に次の手形が現れる";
        case Stage2Anomaly::Mirror:
            return "ヒント 鏡の前で鏡をのぞき 映った物を確かめに振り返る";
        case Stage2Anomaly::Unseen:
            return "ヒント 雑音が強くなったら 壁際で止まるかロッカーに隠れてやり過ごす";
        case Stage2Anomaly::Puddle:
            return "ヒント 水たまりの手前で足元をのぞき 映った物を確かめに顔を上げる";
        case Stage2Anomaly::None:
            break;
        }
        return "ヒント 廊下の変化を探す";
    }

    // 止まっている時間がこれだけ続いたら、軽いヒント（方向）・強いヒント（場所と方法）を出す
    constexpr float LightHintSeconds = 15.0f;
    constexpr float StrongHintSeconds = 30.0f;
    // 間違えた回数がこれ以上なら「再試行補助中」と伝える（信号盤の間に来る影が遠く遅くなる）
    constexpr int RetryAssistMistakes = 2;
    // 信号盤の段階（青・黄・赤の順に操作する）
    constexpr int SignalBlueStep = 0;
    constexpr int SignalYellowStep = 1;
    // 最後のイベントで走ることを伝える文章（コントローラー・キーボード）
    constexpr std::string_view SprintToExitWithController = "左スティック押し込みで出口まで走る";
    constexpr std::string_view SprintToExitWithKeyboard = "SHIFTを押して出口まで走る";

    // 周回ごとの普段の目的と、異変を探している途中の次の一手、信号盤の目的（何も知らせがないときに出す）
    std::string_view SelectBaseObjective(const Stage2ObjectiveInput& in)
    {
        // 周回ごとの普段の目的（扉が閉まっているとき・開いたとき）
        constexpr std::string_view closedLoopObjectives[Stage2FinalLoopCount] =
        {
            "奥のドアを開ける 1回目",
            "奥のドアを開ける 2回目",
            "奥のドアを開ける 3回目"
        };
        constexpr std::string_view openLoopObjectives[Stage2FinalLoopCount] =
        {
            "開いたドアを通り抜ける 1回目",
            "開いたドアを通り抜ける 2回目",
            "開いたドアを通り抜ける 3回目"
        };

        if (IsSearchingAnomaly(in))
        {
            // どの異変が出るかは毎回変わるため、普段の目的では答えを言わず、探させている。
            // 解き方の途中まで進んでいるときだけ、次の一手を伝えている。
            if (in.requiredAnomaly == Stage2Anomaly::FalseDoor && in.falseDoorObserved) return "偽物のドアから視線を外す";
            if (in.requiredAnomaly == Stage2Anomaly::Portrait && in.portraitStaring) return "肖像画から目を離さない";
            if (in.requiredAnomaly == Stage2Anomaly::Knocking && in.knockListening) return "動かずに耳を澄ます";
            if (in.requiredAnomaly == Stage2Anomaly::Handprint && in.handprintFollowing) return "手形をライトでたどる";
            if (in.requiredAnomaly == Stage2Anomaly::Mirror && in.mirrorWaitingForTurn) return "後ろを振り返る";
            if (in.requiredAnomaly == Stage2Anomaly::Puddle && in.puddleWaitingForLookUp) return "顔を上げる";
            if (in.requiredAnomaly == Stage2Anomaly::Unseen && in.unseenNear) return "動かずに やり過ごす";
            return "廊下のどこかが変わっている 異常を探す";
        }
        if (in.confirmationPending)
        {
            return "奥の異常確認スイッチを押す";
        }
        // 最後の周回：信号盤を青・黄・赤の順に操作する。何回も間違えたら、助けが入っていることを伝えている
        if (in.loopCount >= Stage2FinalLoopCount && !in.signalPuzzleComplete)
        {
            if (in.signalPuzzleStep == SignalBlueStep)
            {
                return in.puzzleMistakeCount >= RetryAssistMistakes
                    ? "再試行補助中 奥の青い信号盤からやり直す"
                    : "信号復旧 まず奥の青い信号盤を操作する";
            }
            return in.signalPuzzleStep == SignalYellowStep
                ? "信号復旧 黄色へ戻る 背後の影はライトで追い払う"
                : "信号復旧 赤へ戻る 背後の影はライトで追い払う";
        }
        if (in.loopCount < Stage2FinalLoopCount)
        {
            const std::size_t loopIndex = static_cast<std::size_t>(in.loopCount);
            return in.finalDoorOpen
                ? openLoopObjectives[loopIndex]
                : closedLoopObjectives[loopIndex];
        }
        return "廊下の奥にある出口へ向かう";
    }

    // プレイヤーの状態の知らせ（脱出・捕まった・隠れている・気配を振り切った・充電器・記録）。無ければ空を返している
    std::string_view SelectPlayerStateNotice(const Stage2ObjectiveInput& in)
    {
        if (in.escaping) return "脱出中";
        if (in.observedScareNotice) return "止まらず奥のドアへ進む";
        if (in.caught) return "捕まった チェックポイントへ戻る";
        if (in.hiding)
        {
            return in.noiseShadowActive
                ? "息を潜める 影が去るまで動かない"
                : "ロッカーに隠れている";
        }
        if (in.hidingNotice) return "影は見失って去った 外へ出て進む";
        if (in.presenceTooClose) return "影が近すぎる 距離を取りライトを消して止まる";
        if (in.quietRecoverySucceeded) return "気配が遠のいた 静かに探索を続ける";
        if (in.quietRecoveryInProgress) return "息を潜めている 消灯したまま動かない";
        if (in.chargerNotice) return "充電器の音で廊下が反応した";
        if (in.evidenceNotice) return "残された記録を回収した";
        return {};
    }

    // 信号盤パズルの知らせ（完了・やり直し・追い払った・次の色へ）
    std::string_view SelectSignalNotice(const Stage2ObjectiveInput& in)
    {
        if (in.signalPuzzleComplete) return "信号復旧完了 廊下の中央へ進む";
        switch (in.puzzleFeedbackType)
        {
        case PuzzleFeedback::SignalWrongOrder: return "順番が違う 青からやり直す";
        case PuzzleFeedback::SignalNoiseReset: return "走る音で同期が切れた 歩いて青からやり直す";
        case PuzzleFeedback::SignalStalkerCaught: return "影に追いつかれた 青からやり直す";
        case PuzzleFeedback::SignalStalkerRepelled: return "影を追い払った 次の信号盤へ進む";
        default: break;
        }
        return in.signalPuzzleStep == SignalYellowStep
            ? "青を確認 黄色へ戻る 背後に注意"
            : "黄色を確認 赤へ戻る 背後に注意";
    }

    // 足音の影と足音の知らせ。無ければ空を返している
    std::string_view SelectNoiseNotice(const Stage2ObjectiveInput& in)
    {
        if (in.stalkerNotice)
        {
            return in.noiseShadowActive
                ? "水音を聞いた影が来る ライトを当てるかロッカーに隠れる"
                : "影を追い払った 静かに進む";
        }
        if (in.wetStepNotice) return "水音が廊下に響いた 水たまりは歩いて渡る";
        if (in.noiseWarning) return "足音が響いている 歩いて静める";
        return {};
    }

    // パズルの間違いの知らせ（種類によって、どう直せばよいかを伝えている）
    std::string_view SelectPuzzleFeedback(const Stage2ObjectiveInput& in)
    {
        if (in.puzzleFeedbackType == PuzzleFeedback::SignalStalkerCaught) return "影に追いつかれた 青からやり直す";
        if (in.puzzleFeedbackType == PuzzleFeedback::SignalStalkerRepelled) return "影を追い払った 次の信号盤へ進む";
        if (in.puzzleMistakeCount >= PuzzleFeedback::MaxMistakeCount) return "照明が消えた 正しい方法を試す";
        switch (in.puzzleFeedbackType)
        {
        case PuzzleFeedback::FalseDoorNeedsLight: return "光が必要だ 懐中電灯でドアを照らす";
        case PuzzleFeedback::ClockNeedsDark: return "光が邪魔だ 懐中電灯を消して時計を見る";
        case PuzzleFeedback::SignalWrongOrder: return "信号の順番が違う 青からやり直す";
        default: break;
        }
        return "走る音で同期が切れた 歩いて青からやり直す";
    }

    // 最後のイベント（影を見た罰・走って逃げる・出口が開いた）。無ければ空を返している
    std::string_view SelectFinalEventObjective(const Stage2ObjectiveInput& in)
    {
        if (in.finalGazePenalty) return "それを見てはいけない 足元の水面なら確かめられる";
        if ((in.finalSequenceActive && !in.finalDoorReady) || in.finalPursuitActive)
        {
            return in.controllerConnected ? SprintToExitWithController : SprintToExitWithKeyboard;
        }
        if (in.finalDoorReady)
        {
            return !in.finalDoorOpen
                ? "奥のドアを開ける"
                : "開いた出口を通り抜ける";
        }
        return {};
    }

    // 異変を見つけたときの知らせ。無ければ空を返している
    std::string_view SelectAnomalyFoundNotice(const Stage2ObjectiveInput& in)
    {
        if (in.scratchNotice) return "止まらず奥のドアへ進む";
        if (in.falseDoorNotice) return "異常を確認した 奥のスイッチへ進む";
        if (in.clockNotice)
        {
            return in.requiredAnomaly == Stage2Anomaly::Clock
                ? "逆回転を確認した 奥のスイッチへ進む"
                : "時計の時刻が変わった";
        }
        if (in.portraitNotice) return "目が開いた 奥のスイッチへ進む";
        if (in.knockNotice) return "音の出どころを見つけた 奥のスイッチへ進む";
        if (in.handprintNotice) return "手形が出口へ続いている 奥のスイッチへ進む";
        if (in.mirrorNotice) return "誰もいない 奥のスイッチへ進む";
        if (in.puddleNotice) return "誰もいない 奥のスイッチへ進む";
        if (in.unseenNotice) return "何かが通り過ぎた 奥のスイッチへ進む";
        return {};
    }

    // 周回が変わったときの知らせ
    std::string_view SelectLoopNotice(const Stage2ObjectiveInput& in)
    {
        constexpr int FirstLoop = 0;
        constexpr int SecondLoop = 1;
        constexpr int ThirdLoop = 2;
        if (in.loopCount == FirstLoop) return "1回目 奥のドアを開ける";
        if (in.loopCount == SecondLoop)
        {
            return in.requiredAnomalyFound
                ? (in.confirmationHandledThisLoop
                    ? "鍵が開いた 奥のドアへ進む"
                    : "奥の異常確認スイッチを押す")
                : "2回目 廊下の変化を探す";
        }
        if (in.loopCount == ThirdLoop)
        {
            return in.requiredAnomalyFound
                ? (in.confirmationHandledThisLoop
                    ? "鍵が開いた 後ろを見ずに進む"
                    : "奥の異常確認スイッチを押す")
                : "3回目 前と違うところを探す";
        }
        return in.signalPuzzleComplete
            ? "信号が復旧した 廊下の中央へ進む"
            : "奥の青い信号盤から復旧する";
    }

    // StrongHintSeconds 止まっていたら、場所と方法がはっきり分かるヒントを出している
    std::string_view SelectStrongHint(const Stage2ObjectiveInput& in)
    {
        if (IsSearchingAnomaly(in)) return GetAnomalyStrongHint(in);
        if (in.confirmationPending) return "ヒント 奥の壁にある赤い確認スイッチを押す";
        if (in.loopCount >= Stage2FinalLoopCount && !in.signalPuzzleComplete)
        {
            return in.signalPuzzleStep == SignalBlueStep
                ? "ヒント 青は廊下の奥の右壁"
                : in.signalPuzzleStep == SignalYellowStep
                    ? "ヒント 黄色は中央左 影は振り返ってライトを当てる"
                    : "ヒント 赤は入口右 影は振り返ってライトを当てる";
        }
        if (in.finalDoorReady)
        {
            return in.finalDoorOpen
                ? "ヒント 開いた出口を通り抜ける"
                : "ヒント 今すぐ奥のドアを開ける";
        }
        if (in.finalSequenceArmed) return "ヒント 廊下の中央より先へ進む";
        if (in.finalDoorOpen) return "ヒント 開いた奥のドアを通り抜ける";
        return "ヒント まっすぐ進み奥のドアを開ける";
    }

    // LightHintSeconds 止まっていたら、方向を示す軽いヒントを出している
    std::string_view SelectLightHint(const Stage2ObjectiveInput& in)
    {
        if (IsSearchingAnomaly(in)) return GetAnomalyHint(in);
        if (in.confirmationPending) return "ヒント ドア手前の確認スイッチへ進む";
        if (in.loopCount >= Stage2FinalLoopCount && !in.signalPuzzleComplete)
        {
            return "ヒント 発光している信号盤を 青 黄 赤 の順で操作する";
        }
        if (in.finalDoorReady) return "ヒント 廊下の奥にある出口が開いている";
        if (in.finalSequenceArmed) return "ヒント 廊下をそのまま歩き続ける";
        if (in.finalDoorOpen) return "ヒント 奥のドアを開けると次へ進む";
        return "ヒント 奥のドアを通り抜ける";
    }
}

// 画面の左上に出す文章を1つ選んでいる。上に書いたものほど優先し、どれにも当たらなければ普段の目的を出している
std::string_view SelectStage2Objective(const Stage2ObjectiveInput& in)
{
    if (!in.layoutNotice.empty())
    {
        return in.layoutNotice;
    }
    if (const std::string_view notice = SelectPlayerStateNotice(in); !notice.empty())
    {
        return notice;
    }
    if (in.signalNotice)
    {
        return SelectSignalNotice(in);
    }
    if (const std::string_view notice = SelectNoiseNotice(in); !notice.empty())
    {
        return notice;
    }
    if (in.puzzleFeedbackVisible)
    {
        return SelectPuzzleFeedback(in);
    }
    if (const std::string_view objective = SelectFinalEventObjective(in); !objective.empty())
    {
        return objective;
    }
    if (const std::string_view notice = SelectAnomalyFoundNotice(in); !notice.empty())
    {
        return notice;
    }
    if (in.loopNotice)
    {
        return SelectLoopNotice(in);
    }
    if (in.progressHintSeconds >= StrongHintSeconds)
    {
        return SelectStrongHint(in);
    }
    if (in.progressHintSeconds >= LightHintSeconds)
    {
        return SelectLightHint(in);
    }
    if (in.finalSequenceArmed)
    {
        return "廊下の奥にある出口へ向かう";
    }
    return SelectBaseObjective(in);
}
