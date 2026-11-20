// ============================================================================
// ファイルの役割: 1面の画面の左上に出す目的・知らせ・ヒントの文章を、今の状態から1つ選んでいる。
// 主な技術: 描画・入力に依存しない関数（同じ入力なら必ず同じ結果）、優先順位を付けた表示の選択
// ============================================================================

#include "Stage1Objective.h"
#include "Stage1FuseProgress.h"
#include "Stage1WestWing.h"

#include <optional>

namespace
{
    // 3本目のヒューズを探す段階（西棟）の文章。目的・ヒント（35秒・18秒）の3種類がある。
    // 水を抜く段階の目的には、回し終えたバルブの数を付けている。
    // 西棟の段階の数と、水を抜く段階（回し終えたバルブの数を付ける段階）、バルブの数（Stage1WestWing::ValveCount と同じ）
    constexpr int WestWingStepCount = static_cast<int>(Stage1WestWing::Step::FindFuse);
    constexpr int DrainStep = static_cast<int>(Stage1WestWing::Step::DrainWater);
    constexpr int WestWingValveCount = Stage1WestWing::ValveCount;
    // 文章の種類の数（目的・35秒のヒント・18秒のヒント）
    enum TextVariant { ObjectiveText, LongIdleHint, ShortIdleHint, WestWingTextVariants };

    std::string SelectWestWingText(int westWingStep, int valvesOpened, TextVariant variant)
    {
        static const char* const Texts[WestWingStepCount][WestWingTextVariants] =
        {
            { "右側の部屋で鍵を探す", "ヒント 右奥の部屋に鍵が落ちている", "ヒント 右側の部屋を確認する" },
            { "西側の壁の扉を鍵で開ける", "ヒント 配電盤の近く、左の壁の扉", "ヒント 西側の壁沿いを探す" },
            { "排水バルブを回して西棟の水を抜く", "ヒント 赤いランプの下のバルブを、Eを押し続けて回す", "ヒント 赤く光るランプを探す" },
            { "水の引いた西棟の奥でヒューズを探す", "ヒント 仕切りの奥のポンプ室", "ヒント 西棟の奥へ進む" },
        };
        // 段階は1〜4の範囲に収めている
        const int step = westWingStep < 1 ? 1 : (westWingStep > WestWingStepCount ? WestWingStepCount : westWingStep);
        std::string text = Texts[step - 1][variant];
        if (step == DrainStep && variant == ObjectiveText)
        {
            text += "  " + std::to_string(valvesOpened) + "/" + std::to_string(WestWingValveCount);
        }
        return text;
    }

    // 3本目のヒューズを探す段階の文章。西棟の鍵を拾っていれば西棟の文章、暗証番号の扉が開いていればその部屋、
    // どちらもまだなら、2つの手に入れ方を両方示している（一本道にしないため。StageScene::UpdateThirdFuseRoutes）
    std::string SelectThirdFuseText(const Stage1ObjectiveInput& in, TextVariant variant)
    {
        if (in.westWingStep >= static_cast<int>(Stage1WestWing::Step::OpenDoor))
        {
            return SelectWestWingText(in.westWingStep, in.westWingValvesOpened, variant);
        }
        static const char* const KeypadTexts[WestWingTextVariants] =
        {
            "暗証番号の扉の先、部屋の一番奥でヒューズを探す", "ヒント 閉じ込められた部屋の奥、記録端末の近く", "ヒント 暗証番号の扉の部屋へ戻る"
        };
        static const char* const ChoiceTexts[WestWingTextVariants] =
        {
            "3本目 西棟の奥か、暗証番号の扉の先", "ヒント 右の倉庫の鍵で西棟へ、または壁の光る数字で扉を開ける", "ヒント 右側の部屋か、壁の数字を照らす"
        };
        // 先に部屋を開けていた人には、空だった箱にヒューズが戻ったことを伝えている（ループを2周した合図）
        if (in.keypadSolved && in.keypadFuseReturned && variant == ObjectiveText)
        {
            return "暗証番号の部屋の箱に、何かが戻っている";
        }
        return in.keypadSolved ? KeypadTexts[variant] : ChoiceTexts[variant];
    }

    // 演出の知らせを切り替える秒数（残り秒数がこれより多い間は前半の文章）：出口の前兆、電力が戻った直後、照らす驚き
    constexpr float ExitOmenFirstLineSeconds = 1.75f;
    constexpr float PowerRestoreNoticeSeconds = 4.5f;
    constexpr float PowerRestoreFirstLineSeconds = 1.55f;
    constexpr float ScareLightFirstLineSeconds = 2.65f;
    // 進めないまま、この秒数がたつと強いヒント、この秒数で軽いヒントを出す
    constexpr float StrongHintSeconds = 35.0f;
    constexpr float LightHintSeconds = 18.0f;
}

namespace
{
    // 普段の目的：ヒューズの数とループ廊下の回数から決めている
    std::string SelectBaseObjective(const Stage1ObjectiveInput& in)
    {
        if (in.fuseCount <= 0)
        {
            return "開始地点の近くでヒューズを探す";
        }
        if (in.fuseCount == 1)
        {
            return in.corridorLoopCount < 1
                ? "廊下の突き当たりのドアを開けて進む"
                : "左側の部屋でヒューズを探す";
        }
        if (in.fuseCount == Stage1FuseProgress::BeforeLastFuse)
        {
            return in.corridorLoopCount < Stage1FuseProgress::LoopsToUnlockLastFuse
                ? "もう一度、廊下の突き当たりのドアを開ける"
                : SelectThirdFuseText(in, ObjectiveText);
        }
        return "左の部屋にある配電盤を調べる";
    }

    // 監視カメラの現地確認の間の文章（残り時間と、光で消した割合は毎フレーム変わるため、ここで組み立てている）
    std::string SelectPatrolText(const Stage1ObjectiveInput& in)
    {
        const std::string cameraLabel(in.patrolCameraLabel);
        const std::string anomalyLabel(in.patrolAnomalyLabel);
        if (!in.flashlightOn)
        {
            return cameraLabel +
                " へ向かい " + anomalyLabel + "をライトで照らす  残り " +
                std::to_string(in.patrolRemainingSeconds) + "秒";
        }
        if (in.patrolConfirmPercent > 0)
        {
            return "異常を光で除去中  " +
                std::to_string(in.patrolConfirmPercent) + "%";
        }
        return cameraLabel +
            " の" + anomalyLabel + "をライトで探す  残り " +
            std::to_string(in.patrolRemainingSeconds) + "秒";
    }

    // 一番優先する、その場の状況の文章（配置の読み直し・脱出中・閉じ込め・照らすと止まる影・西棟・空の箱・安全地帯・監視カメラ）
    std::optional<std::string> SelectSituationText(const Stage1ObjectiveInput& in)
    {
        if (!in.layoutNotice.empty())
        {
            // Debug版で小物の配置のファイルを読み直したときの結果（読めなかった行も伝える）
            return std::string(in.layoutNotice);
        }
        if (in.escaping)
        {
            return std::string("ドアの先へ移動中");
        }
        if (!in.hiddenRoomText.empty())
        {
            // 隠し部屋に閉じ込められている間は、その場の状況だけを伝えている。
            return std::string(in.hiddenRoomText);
        }
        if (!in.archiveStalkerText.empty())
        {
            // 照らすと止まる影が現れた・触れられたときの知らせは、短い間だけ普段の目的より優先している。
            return std::string(in.archiveStalkerText);
        }
        if (!in.westWingText.empty())
        {
            // 西棟のバルブを回している間の進み具合と、水が引いたなどの知らせ。
            return std::string(in.westWingText);
        }
        if (in.emptyFuseCaseNotice)
        {
            // 3本目の段階より前に暗証番号の部屋を開けた人に、ここにはまだヒューズがないことを伝えている。
            return std::string("ヒューズの箱は空だ。誰かが持ち去ったらしい");
        }
        if (!in.safeLightText.empty())
        {
            // 光の安全地帯に初めて入ったときの知らせ。
            return std::string(in.safeLightText);
        }
        if (in.patrolDispatched)
        {
            return SelectPatrolText(in);
        }
        if (in.patrolNotice)
        {
            return std::string(in.patrolNoticeText);
        }
        return std::nullopt;
    }

    // 一時的な知らせ（充電・監視カメラ・倉庫の物音・ヒューズを拾った・影が消えた）
    std::optional<std::string> SelectNoticeText(const Stage1ObjectiveInput& in)
    {
        if (in.chargerNotice)
        {
            return std::string(in.fuseWatcherState == FuseWatcherState::Watching
                ? "充電音で影が現れた ライトを向ける"
                : "バッテリーを充電した");
        }
        if (in.evidenceNotice)
        {
            return std::string("監視カメラの巡回を終えた");
        }
        if (in.storageScareNotice)
        {
            return std::string(in.storageScarePhase == StorageScarePhase::NoiseHeard
                ? "背後で金属音がした"
                : (in.storageScarePhase == StorageScarePhase::ShadowShown
                    ? "背後に気配がある"
                    : "影は光の中へ消えた"));
        }
        if (in.fuseNotice)
        {
            if (in.fuseWatcherState == FuseWatcherState::Watching) return std::string("影に懐中電灯を向ける");
            if (in.fuseCount == 1) return std::string("ヒューズを1本入手");
            if (in.fuseCount == Stage1FuseProgress::BeforeLastFuse) return std::string("ヒューズを2本入手");
            return std::string("ヒューズを3本入手");
        }
        if (in.fuseWatcherNotice)
        {
            return std::string(in.fuseWatcherState == FuseWatcherState::Watching
                ? "影を正面から懐中電灯で照らす"
                : "影が光の中へ消えた");
        }
        return std::nullopt;
    }

    // 演出の最中の文章（出口の前兆・電力が戻った瞬間・送電中・照明の演出・ループ廊下）
    std::optional<std::string> SelectEventText(const Stage1ObjectiveInput& in)
    {
        if (in.exitOmenSeconds > 0.0f)
        {
            return std::string(in.exitOmenSeconds > ExitOmenFirstLineSeconds
                ? "何かが待っている"
                : "立ち止まらず進む");
        }
        if (in.powerRestored &&
            in.powerRestoreActive &&
            in.powerRestoreSeconds < PowerRestoreNoticeSeconds)
        {
            return std::string(in.powerRestoreSeconds < PowerRestoreFirstLineSeconds
                ? "電力が復旧した"
                : "出口側の非常送電盤へ向かう");
        }
        if (in.exitPowerActivated && !in.exitPowerReady)
        {
            return std::string("非常電源を送電中");
        }
        if (in.scareLightNoticeSeconds > 0.0f)
        {
            return std::string(in.scareLightNoticeSeconds > ScareLightFirstLineSeconds
                ? "何かに見られている"
                : "点灯した照明をたどる");
        }
        if (in.loopNotice)
        {
            if (in.corridorLoopCount == 1)
            {
                return std::string("廊下の様子が変わった");
            }
            if (in.corridorLoopCount == Stage1FuseProgress::LoopsToUnlockLastFuse)
            {
                return std::string("そのまま歩き続ける");
            }
            return std::string("後ろを振り返らない");
        }
        return std::nullopt;
    }

    // 進めないときのヒントの文章（進み具合ごと）。強いヒント（35秒）と軽いヒント（18秒）で、同じ並びの文章を使い分けている
    struct IdleHintTexts
    {
        const char* GoToExitPanel;
        const char* GoToExitDoor;
        const char* FindFirstFuse;
        const char* OpenCorridorDoor;
        const char* FindSecondFuse;
        const char* OpenCorridorDoorAgain;
        TextVariant ThirdFuseVariant;
        const char* ReturnToFuseBox;
    };

    constexpr IdleHintTexts StrongHints
    {
        "ヒント 右奥の赤い送電盤を調べる",
        "ヒント 廊下の突き当たりの緑色の出口へ",
        "ヒント 最初の壁付近を探す",
        "ヒント 中央のドアから廊下の奥のドアへ",
        "ヒント 左奥の部屋を探す",
        "ヒント もう一度廊下の奥のドアを開ける",
        LongIdleHint,
        "ヒント 左の部屋の配電盤を調べる",
    };

    constexpr IdleHintTexts LightHints
    {
        "ヒント 出口手前の送電盤へ向かう",
        "ヒント 緑色の出口灯をたどる",
        "ヒント 開始地点の周囲を探す",
        "ヒント 中央のドアが進行ルート",
        "ヒント 左側の部屋を確認する",
        "ヒント 長い廊下の奥のドアをもう一度",
        ShortIdleHint,
        "ヒント 配電盤へ戻る",
    };

    std::string SelectIdleHint(const Stage1ObjectiveInput& in, const IdleHintTexts& texts)
    {
        if (in.powerRestored && !in.exitPowerActivated) return texts.GoToExitPanel;
        if (in.powerRestored) return texts.GoToExitDoor;
        if (in.fuseCount <= 0) return texts.FindFirstFuse;
        if (in.fuseCount == 1 && in.corridorLoopCount < 1) return texts.OpenCorridorDoor;
        if (in.fuseCount == 1) return texts.FindSecondFuse;
        if (in.fuseCount == Stage1FuseProgress::BeforeLastFuse &&
            in.corridorLoopCount < Stage1FuseProgress::LoopsToUnlockLastFuse)
        {
            return texts.OpenCorridorDoorAgain;
        }
        if (in.fuseCount == Stage1FuseProgress::BeforeLastFuse) return SelectThirdFuseText(in, texts.ThirdFuseVariant);
        return texts.ReturnToFuseBox;
    }

    // 電力が戻った後の普段の目的
    std::optional<std::string> SelectPoweredObjective(const Stage1ObjectiveInput& in)
    {
        if (in.powerRestored && !in.exitPowerActivated)
        {
            return std::string("出口手前の非常送電盤を操作する");
        }
        if (in.powerRestored)
        {
            return std::string(in.exitDoorOpen
                ? "開いた出口ドアを通り抜ける"
                : "廊下の突き当たりのドアを開ける");
        }
        return std::nullopt;
    }
}

// 上に書いたものほど優先して、1つの文章を選んでいる：その場の状況 → 一時的な知らせ → 演出の最中 →
// 35秒・18秒止まっていたときのヒント → 電力が戻った後の目的 → 普段の目的
std::string SelectStage1Objective(const Stage1ObjectiveInput& in)
{
    if (std::optional<std::string> text = SelectSituationText(in))
    {
        return *text;
    }
    if (std::optional<std::string> text = SelectNoticeText(in))
    {
        return *text;
    }
    if (std::optional<std::string> text = SelectEventText(in))
    {
        return *text;
    }
    if (in.progressHintSeconds >= StrongHintSeconds)
    {
        return SelectIdleHint(in, StrongHints);
    }
    if (in.progressHintSeconds >= LightHintSeconds)
    {
        return SelectIdleHint(in, LightHints);
    }
    if (std::optional<std::string> text = SelectPoweredObjective(in))
    {
        return *text;
    }
    return SelectBaseObjective(in);
}
