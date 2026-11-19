// ============================================================================
// ファイルの役割: タイトル・リザルト・監視映像・章のカード・目的地の方向・暗証番号・隠れている間の視界・ポーズなど、画面ごとのUIを描いている。
// 主な技術: 画面ごとの描画関数、画面の大きさに合わせた中央・端の配置、選んでいる項目の強調表示
// ============================================================================

#include "Hud.h"
#include "TitleStageChoices.h"
#include "StoryText.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <unordered_map>
#include <windows.h>

#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "GameSettings.h"
#include "Input.h"
#include "PauseMenu.h"
#include "Player.h"
#include "Renderer.h"
#include "Stage1WallWritings.h"

using namespace DirectX::SimpleMath;

namespace
{
    // UTF-8の文字列で、画面に出る文字の数を数えている（中央に置くための幅の計算に使っている）
    size_t CountDisplayedCharacters(std::string_view text)
    {
        size_t count = 0;
        for (unsigned char byte : text)
        {
            if (!StoryText::IsUtf8ContinuationByte(byte))
            {
                ++count;
            }
        }
        return count;
    }

    // 色の透明度に、表示の進み具合（0〜1）を掛けている
    Color Faded(const Color& color, float reveal)
    {
        return Color(color.x, color.y, color.z, color.w * reveal);
    }

    // 1分・1時間の秒数（時間を分と秒、時・分・秒に分けて見せるため）
    constexpr int SecondsPerMinute = 60;
    constexpr int SecondsPerHour = 3600;
}

namespace TitleLayout
{
    // --- 色：暗い緑（選んでいない面の印）、題名、本文、薄い文字、左の帯の地の色、走査線、左端の線
    const Color DimGreen(0.08f, 0.20f, 0.14f, 0.55f);
    const Color TitleGreen(0.48f, 0.90f, 0.58f, 0.96f);
    const Color TextColor(0.50f, 0.68f, 0.57f, 0.86f);
    const Color FaintColor(0.36f, 0.50f, 0.42f, 0.72f);
    const Color BandColor(0.0f, 0.004f, 0.003f, 0.80f);
    const Color ScanlineColor(0.0f, 0.0f, 0.0f, 0.10f);
    const Color SideLineColor(0.30f, 0.62f, 0.40f, 0.70f);
    // 「開始」の文字と選んでいる面の縁の明滅：速さと、明滅しない色（赤・緑・青・不透明度）と明滅の幅
    constexpr float PromptPulseRate = 2.1f;
    const Color PromptBaseColor(0.58f, 0.70f, 0.62f, 0.62f);
    const Color PromptPulseRange(0.16f, 0.18f, 0.14f, 0.34f);
    // sin を使う決まった式で 0〜1 の値を作る係数
    constexpr float HashFrequency = 12.9898f;
    constexpr float HashAmplitude = 43758.5453f;

    // --- 左側の暗い帯（幅・右へ透明にする幅・その段数）、走査線の間隔、左端の線（x・上下の余白・太さ）
    constexpr float BandWidth = 560.0f;
    constexpr float FadeWidth = 260.0f;
    constexpr int FadeSteps = 26;
    // 段の継ぎ目に描き残しが出ないよう、隣の段へ少し重ねる。
    constexpr float FadeStripOverlap = 0.5f;
    constexpr float ScanlineSpacing = 6.0f;
    constexpr float SideLineX = 56.0f;
    constexpr float SideLineMargin = 120.0f;
    constexpr float SideLineWidth = 3.0f;

    // --- 題名：左端、文字の大きさ、上の位置、ノイズのずれ（1秒あたりの刻み・横のずれの幅）
    constexpr float Left = 84.0f;
    constexpr float TitlePixelSize = 11.0f;
    constexpr float TitleY = 150.0f;
    constexpr float NoiseStepsPerSecond = 24.0f;
    constexpr float NoiseJitter = 28.0f;
    // 色のにじみ：起こすノイズの強さ、左右のずれ（最低＋ノイズの幅）、赤と青緑の色
    constexpr float NoiseVisibleLevel = 0.05f;
    // 青緑のにじみは横だけでなく縦もずらし、題名と完全には重ならないようにする。
    constexpr float SplitCyanVerticalOffset = 1.0f;
    constexpr float SplitBase = 4.0f;
    constexpr float SplitRange = 10.0f;
    const Color SplitRed(0.95f, 0.18f, 0.16f, 0.55f);
    const Color SplitCyan(0.18f, 0.85f, 0.90f, 0.45f);
    // 電波が途切れて一瞬暗くなる：起こすノイズの強さ、乱数のずらし、確率、そのときの濃さ
    constexpr float DropoutNoiseLevel = 0.3f;
    constexpr float DropoutSeedOffset = 5.1f;
    constexpr float DropoutChance = 0.35f;
    constexpr float DropoutAlpha = 0.35f;
    // 抜ける横の帯：本数（最低＋ノイズの幅）、乱数の種の倍率、帯の高さ（最低＋乱数の幅）、帯の左右のはみ出し、
    // 緑の線の切れ端（横のずれ・置く範囲の割合・長さ（最低＋幅））
    constexpr int MinBars = 2;
    constexpr float BarsPerNoise = 5.0f;
    constexpr float BarSeedStep = 3.1f;
    constexpr float BarSeedPerBar = 7.7f;
    constexpr float BarHeightBase = 2.0f;
    constexpr float BarHeightRange = 6.0f;
    constexpr float BarLeftMargin = 10.0f;
    constexpr float BarExtraWidth = 40.0f;
    const Color BarColor(0.0f, 0.004f, 0.003f, 0.92f);
    constexpr float SliverShift = 60.0f;
    constexpr float SliverSpan = 0.6f;
    constexpr float SliverLengthBase = 30.0f;
    constexpr float SliverLengthRange = 90.0f;
    const Color SliverColor(0.48f, 0.90f, 0.58f, 0.55f);
    // 乱数を引くときのずらし（帯の高さ・ずれ・置く位置・長さ）
    constexpr float BarHeightSeed = 1.3f;
    constexpr float SliverShiftSeed = 2.9f;
    constexpr float SliverPlaceSeed = 4.4f;
    constexpr float SliverLengthSeed = 6.1f;

    // --- 英語の題名とひとことの説明：題名からの字下げ、題名の下からの距離、文字の大きさ、色
    constexpr float Indent = 4.0f;
    constexpr float SubtitleGap = 18.0f;
    constexpr float SubtitlePixelSize = 3.0f;
    const Color SubtitleColor(0.40f, 0.70f, 0.50f, 0.80f);
    constexpr float TaglineGap = 54.0f;
    constexpr float TaglinePixelSize = 2.4f;

    // --- 始める面の選択：枠の幅と高さ（4つでも下の位置が3つのときと同じになるよう低め）、すき間、文字の大きさ、上の位置、
    // 選んでいる面・いない面の地の色、縁の太さ、左の太い印の幅、文字の字下げ
    constexpr int StageCount = TitleStageChoices::Count;
    constexpr float StageBoxWidth = 340.0f;
    constexpr float StageBoxHeight = 34.0f;
    constexpr float StageBoxGap = 6.0f;
    constexpr float StageTextPixelSize = 2.4f;
    constexpr float StageTop = 370.0f;
    const Color SelectedBoxColor(0.05f, 0.14f, 0.09f, 0.88f);
    const Color UnselectedBoxColor(0.01f, 0.02f, 0.015f, 0.55f);
    constexpr float BorderWidth = 2.0f;
    constexpr float SelectedMarkWidth = 6.0f;
    constexpr float StageTextIndent = 22.0f;
    // 選んだ面の説明・開始の案内・選び方の案内・ベスト記録の、面の枠の下からの距離と文字の大きさ、記録の色
    constexpr float NoteGap = 4.0f;
    constexpr float NotePixelSize = 1.7f;
    constexpr float PromptGap = 44.0f;
    constexpr float PromptPixelSize = 3.0f;
    constexpr float HelpGap = 84.0f;
    constexpr float HelpPixelSize = 1.8f;
    constexpr float RecordGap = 116.0f;
    const Color RecordColor(0.72f, 0.78f, 0.48f, 0.88f);
    // 操作の説明（左下）：下からの位置、行の間隔、行の数
    constexpr float ControlsBottom = 118.0f;
    constexpr float ControlsLineSpacing = 24.0f;

    // --- 右上の受信表示：右端からの距離、明滅（1秒のうち点いている割合）、印の位置と大きさ、色（受信中・信号なし）、
    // 文字の位置（印からの距離・上の位置・2行目・時刻の横の位置）と大きさ、信号なしにするノイズの強さ
    constexpr float StatusRightMargin = 250.0f;
    // 監視映像の状態ランプは、周期ごとに点灯時間と消灯時間を繰り返す。
    constexpr float StatusBlinkCycleSeconds = 1.0f;
    constexpr float StatusBlinkOnSeconds = 0.6f;
    constexpr float StatusMarkY = 46.0f;
    constexpr float StatusMarkSize = 10.0f;
    const Color LostMarkColor(0.92f, 0.22f, 0.18f, 0.90f);
    const Color LiveMarkColor(0.48f, 0.90f, 0.58f, 0.90f);
    const Color LostTextColor(0.92f, 0.40f, 0.32f, 0.90f);
    constexpr float StatusTextOffset = 20.0f;
    constexpr float StatusTextY = 43.0f;
    constexpr float StatusTextPixelSize = 2.0f;
    constexpr float ChannelY = 70.0f;
    constexpr float ClockOffset = 100.0f;
    constexpr float LostNoiseLevel = 0.45f;
    // 受信表示の時刻の始まり（2時14分37秒から進める）
    constexpr int ClockStartHour = 2;
    constexpr int ClockStartMinute = 14;
    constexpr int ClockStartSecond = 37;
}

// タイトル画面：後ろに映す3Dの森（TitleScene）の上に、左側の暗い帯を重ね、題名・面の選択・操作の説明・ベスト記録を縦に並べている。
// noise（0〜1）が大きいほど、題名の文字が横にずれ、赤と青緑に色がにじみ、横の帯が抜けて、電波が乱れたように見せている
void Hud::DrawTitle(
    float time,
    bool hasClearRecord,
    float bestClearTimeSeconds,
    int bestCaughtCount,
    int selectedStage,
    float noise)
{
    using namespace TitleLayout;
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    // 「開始」の文字と、選んでいる面の縁を、ゆっくり明滅させている
    const float pulse = std::sin(time * PromptPulseRate) * 0.5f + 0.5f;
    const Color promptColor(
        PromptBaseColor.R() + pulse * PromptPulseRange.R(),
        PromptBaseColor.G() + pulse * PromptPulseRange.G(),
        PromptBaseColor.B() + pulse * PromptPulseRange.B(),
        PromptBaseColor.A() + pulse * PromptPulseRange.A());
    noise = (std::clamp)(noise, 0.0f, 1.0f);
    // 0〜1の乱数（同じ値からは同じ結果）。ノイズのずれ方を、時間の刻みごとに変えるのに使っている
    const auto hash = [](float value)
    {
        const float s = std::sin(value * HashFrequency) * HashAmplitude;
        return s - std::floor(s);
    };
    const auto textWidth = [](std::string_view text, float pixelSize)
    {
        return static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * GlyphAdvance;
    };

    // 左側の暗い帯：左端はほぼ黒、右へ行くほど透明にして、後ろの森が右側に見えるようにしている
    AddRectangle(0.0f, 0.0f, BandWidth - FadeWidth, screenHeight, BandColor);
    for (int step = 0; step < FadeSteps; ++step)
    {
        const float t = (static_cast<float>(step) + 0.5f) / static_cast<float>(FadeSteps);
        AddRectangle(BandWidth - FadeWidth + FadeWidth * static_cast<float>(step) / FadeSteps, 0.0f,
            FadeWidth / FadeSteps + FadeStripOverlap, screenHeight, Faded(BandColor, (1.0f - t) * (1.0f - t)));
    }
    // 画面全体の薄い走査線（古いモニターや監視映像のような見た目）
    for (float y = 0.0f; y < screenHeight; y += ScanlineSpacing)
    {
        AddRectangle(0.0f, y, screenWidth, 1.0f, ScanlineColor);
    }
    // 左端の緑の線
    AddRectangle(SideLineX, SideLineMargin, SideLineWidth, screenHeight - SideLineMargin * 2.0f, SideLineColor);

    // --- 題名：通常は静かに表示し、ノイズのときに文字を横へずらし、色をにじませ、横の帯を抜いている
    constexpr std::string_view title = "通信途絶";
    const float titleHeight = TitlePixelSize * GlyphRows;
    const float noiseStep = std::floor(time * NoiseStepsPerSecond);
    const float jitter = noise > 0.0f ? (hash(noiseStep) - 0.5f) * NoiseJitter * noise : 0.0f;
    if (noise > NoiseVisibleLevel)
    {
        // 色のにじみ（赤と青緑を、左右に少しずらして重ねている）
        const float split = SplitBase + SplitRange * noise;
        AddText(Left + jitter - split, TitleY, title, TitlePixelSize, Faded(SplitRed, noise));
        AddText(Left + jitter + split, TitleY + SplitCyanVerticalOffset, title, TitlePixelSize, Faded(SplitCyan, noise));
    }
    // 本体の文字。乱れている間は、ときどき一瞬暗くしている（電波が途切れる）
    const float dropout = (noise > DropoutNoiseLevel && hash(noiseStep + DropoutSeedOffset) < DropoutChance) ? DropoutAlpha : 1.0f;
    AddText(Left + jitter, TitleY, title, TitlePixelSize, Faded(TitleGreen, dropout));
    if (noise > NoiseVisibleLevel)
    {
        // 横の帯が抜ける：題名の上に、背景と同じ暗い横線を数本重ね、その少し横に緑の線の切れ端を置いている
        const int bars = MinBars + static_cast<int>(noise * BarsPerNoise);
        const float titleWidth = textWidth(title, TitlePixelSize);
        for (int bar = 0; bar < bars; ++bar)
        {
            const float seed = noiseStep * BarSeedStep + static_cast<float>(bar) * BarSeedPerBar;
            const float barY = TitleY + hash(seed) * titleHeight;
            const float barHeight = BarHeightBase + hash(seed + BarHeightSeed) * BarHeightRange;
            AddRectangle(Left - BarLeftMargin, barY, titleWidth + BarExtraWidth, barHeight, BarColor);
            const float shift = (hash(seed + SliverShiftSeed) - 0.5f) * SliverShift * noise;
            AddRectangle(Left + shift + hash(seed + SliverPlaceSeed) * titleWidth * SliverSpan, barY,
                SliverLengthBase + hash(seed + SliverLengthSeed) * SliverLengthRange,
                (std::max)(1.0f, barHeight * 0.5f), Faded(SliverColor, noise));
        }
    }
    // 英語の題名と、ひとことの説明
    AddText(Left + Indent, TitleY + titleHeight + SubtitleGap, "SIGNAL LOST", SubtitlePixelSize, SubtitleColor);
    AddText(Left + Indent, TitleY + titleHeight + TaglineGap, "終わらない廊下から脱出する", TaglinePixelSize, TextColor);

    // --- 始める面の選択：縦に4つ並べ、選んでいる面の枠を明るくし、その下に説明を出している（試作中の3面は一番下）
    const int stage = (std::clamp)(selectedStage, 0, StageCount - 1);
    for (int index = 0; index < StageCount; ++index)
    {
        const bool selected = index == stage;
        const float boxY = StageTop + static_cast<float>(index) * (StageBoxHeight + StageBoxGap);
        AddRectangle(Left, boxY, StageBoxWidth, StageBoxHeight, selected ? SelectedBoxColor : UnselectedBoxColor);
        if (selected)
        {
            // 選んでいる面：明滅する縁と、左の太い印
            AddRectangle(Left, boxY, StageBoxWidth, BorderWidth, promptColor);
            AddRectangle(Left, boxY + StageBoxHeight - BorderWidth, StageBoxWidth, BorderWidth, promptColor);
            AddRectangle(Left + StageBoxWidth - BorderWidth, boxY, BorderWidth, StageBoxHeight, promptColor);
            AddRectangle(Left, boxY, SelectedMarkWidth, StageBoxHeight, TitleGreen);
        }
        else
        {
            AddRectangle(Left, boxY, BorderWidth, StageBoxHeight, DimGreen);
        }
        AddText(Left + StageTextIndent, boxY + (StageBoxHeight - StageTextPixelSize * GlyphRows) * 0.5f,
            TitleStageChoices::Entries[index].Name, StageTextPixelSize, selected ? TitleGreen : FaintColor);
    }
    const float afterStages = StageTop + StageCount * (StageBoxHeight + StageBoxGap);
    AddText(Left + Indent, afterStages + NoteGap, TitleStageChoices::Entries[stage].Description, NotePixelSize, TextColor);

    // 開始と終了の操作
    const bool controller = Input::IsControllerConnected();
    const std::string_view prompt = controller ? "Aでゲーム開始" : "ENTERでゲーム開始";
    AddText(Left + Indent, afterStages + PromptGap, prompt, PromptPixelSize, promptColor);
    AddText(Left + Indent, afterStages + HelpGap,
        controller ? "上下で面を選ぶ  Bでゲーム終了" : "上下で面を選ぶ  Qでゲーム終了", HelpPixelSize, FaintColor);

    // クリアの記録があれば、最短タイムと最少の捕まった回数を出している
    if (hasClearRecord)
    {
        const int bestSeconds = (std::max)(0, static_cast<int>(bestClearTimeSeconds + 0.5f));
        const std::string recordText =
            "最短記録 " + std::to_string(bestSeconds / SecondsPerMinute) + "分 " + std::to_string(bestSeconds % SecondsPerMinute) +
            "秒   最少捕獲 " + std::to_string((std::max)(bestCaughtCount, 0)) + "回";
        AddText(Left + Indent, afterStages + RecordGap, recordText, HelpPixelSize, RecordColor);
    }

    // 操作の説明（左下）。コントローラーがつながっていれば、コントローラーのボタン名で表示している
    const std::string_view controls[] =
    {
        controller ? "左スティック 移動  右スティック 視点  A 調べる" : "WASD 移動  マウス 視点  E 調べる",
        controller ? "左スティック押し込み 走る  Y ライト" : "SHIFT 走る  F ライト",
        controller ? "LB ヒント  START ポーズ" : "H ヒント  ESC または P ポーズ",
    };
    for (int line = 0; line < static_cast<int>(std::size(controls)); ++line)
    {
        AddText(Left + Indent, screenHeight - ControlsBottom + static_cast<float>(line) * ControlsLineSpacing,
            controls[line], NotePixelSize, FaintColor);
    }

    // 右上：監視映像のような受信表示（「受信中」と、明滅する印と、時間）。ノイズのときは「信号なし」にしている
    const bool lost = noise > LostNoiseLevel;
    const std::string_view status = lost ? "信号なし" : "受信中...";
    const int clock = static_cast<int>(time);
    // 時・分・秒の区切りと終端文字を含めた監視時計の書式用バッファ。
    constexpr std::size_t SurveillanceClockTextCapacity = 16;
    char timeText[SurveillanceClockTextCapacity];
    std::snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", ClockStartHour + clock / SecondsPerHour,
        (ClockStartMinute + clock / SecondsPerMinute) % SecondsPerMinute, (ClockStartSecond + clock) % SecondsPerMinute);
    const float statusX = screenWidth - StatusRightMargin;
    const bool blink = std::fmod(time, StatusBlinkCycleSeconds) < StatusBlinkOnSeconds;
    if (blink || lost)
    {
        AddRectangle(statusX, StatusMarkY, StatusMarkSize, StatusMarkSize, lost ? LostMarkColor : LiveMarkColor);
    }
    AddText(statusX + StatusTextOffset, StatusTextY, status, StatusTextPixelSize, lost ? LostTextColor : TextColor);
    AddText(statusX + StatusTextOffset, ChannelY, "CH-03", HelpPixelSize, FaintColor);
    AddText(statusX + ClockOffset, ChannelY, timeText, HelpPixelSize, FaintColor);

    Flush();
}

namespace
{
    // --- リザルト画面の評価の点数：満点から、時間・捕まった回数・観察の失敗を引き、異変への対処と残された記録を足す
    constexpr int ResultMaxScore = 100;
    constexpr int ResultParSeconds = 360;               // この時間（6分）を超えた分だけ減点する
    constexpr int ResultSecondsPerTimePenalty = 15;     // 15秒ごとに1点
    constexpr int ResultMaxTimePenalty = 40;
    constexpr int ResultCaughtPenalty = 15;             // 1回15点
    constexpr int ResultMaxCaughtPenalty = 45;
    constexpr int ResultMistakePenalty = 5;             // 1回5点
    constexpr int ResultMaxMistakePenalty = 25;
    constexpr int ResultAnomalyBonus = 2;               // 1回2点
    constexpr int ResultMaxAnomalyBonus = 8;
    constexpr int ResultEvidenceBonus = 3;              // 1つ3点
    // 評価の境目（この点数以上で S・A・B、それ未満は C）
    constexpr int ResultRankSScore = 90;
    constexpr int ResultRankAScore = 75;
    constexpr int ResultRankBScore = 55;

    // 評価の点数と評価の文字（S・A・B・C）を求めている
    char ComputeResultRank(int clearSeconds, int caughtCount, int puzzleMistakes, int anomaliesHandled, int evidenceCollected)
    {
        const int timePenalty = (std::min)(
            (std::max)(clearSeconds - ResultParSeconds, 0) / ResultSecondsPerTimePenalty, ResultMaxTimePenalty);
        const int caughtPenalty = (std::min)(caughtCount * ResultCaughtPenalty, ResultMaxCaughtPenalty);
        const int mistakePenalty = (std::min)(puzzleMistakes * ResultMistakePenalty, ResultMaxMistakePenalty);
        const int anomalyBonus = (std::min)(anomaliesHandled * ResultAnomalyBonus, ResultMaxAnomalyBonus);
        const int evidenceBonus = evidenceCollected * ResultEvidenceBonus;
        const int performanceScore = (std::clamp)(
            ResultMaxScore - timePenalty - caughtPenalty - mistakePenalty + anomalyBonus + evidenceBonus,
            0,
            ResultMaxScore);
        return performanceScore >= ResultRankSScore
            ? 'S'
            : (performanceScore >= ResultRankAScore
                ? 'A'
                : (performanceScore >= ResultRankBScore ? 'B' : 'C'));
    }

    // 達成の一言：全部見つけて一度も失敗しない「完全探索」を最優先にし、次に記録の更新、捕まらなかったことの順で選んでいる
    std::string_view SelectResultAchievement(bool everythingFound, int caughtCount, int puzzleMistakes,
        bool newBestTime, bool newBestCaught)
    {
        if (everythingFound && caughtCount == 0 && puzzleMistakes == 0) return "完全探索で脱出";
        if (newBestTime && newBestCaught) return "自己記録を更新";
        if (newBestTime) return "最速クリア記録を更新";
        if (newBestCaught) return "最少捕獲記録を更新";
        if (caughtCount == 0) return "一度も捕まらず脱出";
        return {};
    }

    // --- リザルト画面の見た目：パネルの大きさ（画面の幅に対する割合と上限・高さ）、左の線と上の線の太さ、背景の走査線の間隔
    constexpr float ResultPanelWidthRatio = 0.72f;
    constexpr float ResultPanelMaxWidth = 860.0f;
    constexpr float ResultPanelHeight = 714.0f;     // 下段に「今回の発見」（壁の文字・録音・隠し部屋・今回の異変）を並べるため、縦に広げている
    constexpr float ResultPanelAccentWidth = 6.0f;
    constexpr float ResultPanelTopLineHeight = 2.0f;
    constexpr float ResultScanlineSpacing = 8.0f;
    constexpr float ResultScanlineHeight = 1.0f;
    // 文字の大きさ（題名・階・評価・成績の行・小見出し・操作の案内）
    constexpr float ResultTitlePixelSize = 7.0f;
    constexpr float ResultFloorPixelSize = 3.0f;
    constexpr float ResultRankPixelSize = 5.0f;
    constexpr float ResultStatPixelSize = 2.5f;
    constexpr float ResultSmallPixelSize = 2.2f;
    constexpr float ResultPromptPixelSize = 3.0f;
    // パネルの上からの各行の位置。成績と今回の発見の行は、この間隔で並べる
    constexpr float ResultTitleY = 48.0f;
    constexpr float ResultFloorY = 116.0f;
    constexpr float ResultRankY = 158.0f;
    constexpr float ResultStatsY = 232.0f;
    constexpr float ResultRowSpacing = 34.0f;
    // 「今回の発見」の行の番号（壁の文字が0行目、録音・隠し部屋・2階の異変の順）
    constexpr float ResultRecordingRow = 1.0f;
    constexpr float ResultHiddenRoomRow = 2.0f;
    constexpr float ResultStage2AnomalyRow = 3.0f;
    constexpr float ResultDividerY = 446.0f;
    constexpr float ResultDividerInset = 60.0f;
    constexpr float ResultDiscoveryHeaderY = 458.0f;
    constexpr float ResultDiscoveryY = 490.0f;
    constexpr float ResultAchievementY = 632.0f;
    constexpr float ResultPromptY = 672.0f;
    // 色（背景・パネル・緑・淡い文字・金・控えめな文字・評価がCのときの赤茶・走査線）。透明度は表示の進み具合を掛けて使う
    const Color ResultBackground(0.004f, 0.007f, 0.006f, 1.0f);
    const Color ResultPanelColor(0.018f, 0.028f, 0.025f, 0.94f);
    const Color ResultGreen(0.48f, 0.90f, 0.56f, 0.96f);
    const Color ResultPale(0.74f, 0.82f, 0.76f, 0.78f);
    const Color ResultGold(0.92f, 0.78f, 0.34f, 0.96f);
    const Color ResultRankSGold(0.92f, 0.78f, 0.34f, 0.98f);
    const Color ResultDim(0.50f, 0.58f, 0.52f, 0.80f);
    const Color ResultPoorRank(0.74f, 0.42f, 0.34f, 0.92f);
    const Color ResultScanline(0.10f, 0.16f, 0.12f, 0.075f);
}

// 文字列を、画面の横の中央に置いている
void Hud::AddCenteredText(float y, std::string_view text, float pixelSize, const Color& color)
{
    const float width = static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * GlyphAdvance;
    AddText((GetCanvasWidth() - width) * 0.5f, y, text, pixelSize, color);
}

// リザルト画面：評価・成績・今回の発見・達成の一言・次の操作を並べている。revealAmountで全体をフェードインさせている
void Hud::DrawResult(
    float revealAmount,
    float clearTimeSeconds,
    int caughtCount,
    int anomaliesHandled,
    int puzzleMistakes,
    int chargersUsed,
    int evidenceCollected,
    int wallWritingsRead,
    int recordingsPlayed,
    bool hiddenRoomEscaped,
    std::string_view stage2Anomalies,
    bool newBestTime,
    bool newBestCaught)
{
    m_Vertices.clear();

    const float reveal = (std::clamp)(revealAmount, 0.0f, 1.0f);
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const Color green = Faded(ResultGreen, reveal);
    const Color pale = Faded(ResultPale, reveal);
    const Color gold = Faded(ResultGold, reveal);
    const Color dim = Faded(ResultDim, reveal);

    // 背景・走査線・パネル
    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, ResultBackground);
    for (float y = 0.0f; y < screenHeight; y += ResultScanlineSpacing)
    {
        AddRectangle(0.0f, y, screenWidth, ResultScanlineHeight, Faded(ResultScanline, reveal));
    }
    const float panelWidth = (std::min)(screenWidth * ResultPanelWidthRatio, ResultPanelMaxWidth);
    const float panelX = (screenWidth - panelWidth) * 0.5f;
    const float panelY = (screenHeight - ResultPanelHeight) * 0.5f;
    AddRectangle(panelX, panelY, panelWidth, ResultPanelHeight, Faded(ResultPanelColor, reveal));
    AddRectangle(panelX, panelY, ResultPanelAccentWidth, ResultPanelHeight, green);
    AddRectangle(panelX, panelY, panelWidth, ResultPanelTopLineHeight, green);

    AddCenteredText(panelY + ResultTitleY, "脱出成功", ResultTitlePixelSize, green);
    AddCenteredText(panelY + ResultFloorY, "2階をクリア", ResultFloorPixelSize, pale);

    // 数を、0未満や全部の数を超えないよう収めている
    const int clearSeconds = (std::max)(0, static_cast<int>(clearTimeSeconds + 0.5f));
    const int safeCaughtCount = (std::max)(caughtCount, 0);
    const int safeAnomaliesHandled = (std::max)(anomaliesHandled, 0);
    const int safePuzzleMistakes = (std::max)(puzzleMistakes, 0);
    const int safeChargersUsed = (std::max)(chargersUsed, 0);
    constexpr int totalEvidence = Core::GameState::TotalEvidenceCount;
    constexpr int totalWritings = Stage1WallWritings::Count;
    constexpr int totalRecordings = StoryText::RecordingCount;
    const int safeEvidenceCollected = (std::clamp)(evidenceCollected, 0, totalEvidence);
    const int safeWritingsRead = (std::clamp)(wallWritingsRead, 0, totalWritings);
    const int safeRecordingsPlayed = (std::clamp)(recordingsPlayed, 0, totalRecordings);

    // 評価
    const char rank = ComputeResultRank(
        clearSeconds, safeCaughtCount, safePuzzleMistakes, safeAnomaliesHandled, safeEvidenceCollected);
    const Color rankColor = rank == 'S'
        ? Faded(ResultRankSGold, reveal)
        : (rank == 'A' ? green : (rank == 'B' ? pale : Faded(ResultPoorRank, reveal)));
    AddCenteredText(panelY + ResultRankY, "評価 " + std::string(1, rank), ResultRankPixelSize, rankColor);

    // 成績の各行（タイム・捕まった回数・異変への対処・観察の失敗・充電器・残された記録）
    const std::string statLines[] =
    {
        "クリア時間 " + std::to_string(clearSeconds / SecondsPerMinute) + "分 " +
            std::to_string(clearSeconds % SecondsPerMinute) + "秒",
        "捕まった回数 " + std::to_string(safeCaughtCount) + "回",
        "怪異への対処 " + std::to_string(safeAnomaliesHandled) + "回",
        "観察の失敗 " + std::to_string(safePuzzleMistakes) + "回",
        "充電器の使用 " + std::to_string(safeChargersUsed) + "回",
        "残された記録 " + std::to_string(safeEvidenceCollected) + " / " + std::to_string(totalEvidence),
    };
    constexpr std::size_t evidenceLine = std::size(statLines) - 1;
    for (std::size_t line = 0; line < std::size(statLines); ++line)
    {
        const bool complete = line == evidenceLine && safeEvidenceCollected >= totalEvidence;
        AddCenteredText(panelY + ResultStatsY + static_cast<float>(line) * ResultRowSpacing,
            statLines[line], ResultStatPixelSize, complete ? gold : pale);
    }

    // 今回の発見。プレイごとに変わる要素と任意の探索を並べ、もう一度遊ぶ理由にしている。
    AddRectangle(panelX + ResultDividerInset, panelY + ResultDividerY,
        panelWidth - ResultDividerInset * 2.0f, ResultScanlineHeight, dim);
    AddCenteredText(panelY + ResultDiscoveryHeaderY, "今回の発見", ResultSmallPixelSize, dim);
    const float discoveryY = panelY + ResultDiscoveryY;
    AddCenteredText(discoveryY,
        "壁の文字 " + std::to_string(safeWritingsRead) + " / " + std::to_string(totalWritings),
        ResultStatPixelSize, safeWritingsRead >= totalWritings ? gold : pale);
    AddCenteredText(discoveryY + ResultRowSpacing * ResultRecordingRow,
        "録音 " + std::to_string(safeRecordingsPlayed) + " / " + std::to_string(totalRecordings),
        ResultStatPixelSize, safeRecordingsPlayed >= totalRecordings ? gold : pale);
    AddCenteredText(discoveryY + ResultRowSpacing * ResultHiddenRoomRow,
        hiddenRoomEscaped ? "隠し部屋 脱出した" : "隠し部屋 見つけていない",
        ResultStatPixelSize, hiddenRoomEscaped ? gold : pale);
    if (!stage2Anomalies.empty())
    {
        AddCenteredText(discoveryY + ResultRowSpacing * ResultStage2AnomalyRow,
            "2階の異変 " + std::string(stage2Anomalies), ResultStatPixelSize, pale);
    }

    // 達成の一言と、次の操作
    const bool everythingFound = safeEvidenceCollected >= totalEvidence && safeWritingsRead >= totalWritings &&
        safeRecordingsPlayed >= totalRecordings && hiddenRoomEscaped;
    const std::string_view achievement = SelectResultAchievement(
        everythingFound, safeCaughtCount, safePuzzleMistakes, newBestTime, newBestCaught);
    if (!achievement.empty())
    {
        AddCenteredText(panelY + ResultAchievementY, achievement, ResultSmallPixelSize, gold);
    }
    const std::string_view prompt = Input::IsControllerConnected()
        ? "A タイトルへ   X もう一度"
        : "ENTER タイトルへ   R もう一度";
    AddCenteredText(panelY + ResultPromptY, prompt, ResultPromptPixelSize, pale);

    Flush();
}

namespace FeedLayout
{
    // 映像の大きさ：上下の文字に空ける高さ（と、高さの下限）、画面の幅に対する割合と幅の上限、縦横比、中央から下へずらす量
    constexpr float ReservedHeight = 156.0f;
    constexpr float MinimumAvailableHeight = 300.0f;
    constexpr float WidthRatio = 0.90f;
    constexpr float MaximumWidth = 1280.0f;
    constexpr float AspectWidth = 16.0f;
    constexpr float AspectHeight = 9.0f;
    constexpr float VerticalOffset = 8.0f;
    // 色：背景を暗くする色、枠と文字の緑、薄い文字、文字の下の暗い帯、操作の説明の文字、間違いの赤、
    // 失敗の印（使った・まだの）、選んでいないカメラの印、流れる横線
    const Color BackgroundColor(0.0f, 0.004f, 0.003f, 0.94f);
    const Color Green(0.38f, 0.94f, 0.55f, 0.96f);
    const Color Pale(0.76f, 0.88f, 0.80f, 0.94f);
    const Color Dark(0.005f, 0.018f, 0.012f, 0.88f);
    const Color HelpColor(0.66f, 0.85f, 0.71f, 0.95f);
    const Color WrongColor(1.0f, 0.30f, 0.22f, 0.96f);
    const Color UsedMistakeColor(1.0f, 0.24f, 0.18f, 0.96f);
    const Color SpareMistakeColor(0.20f, 0.34f, 0.26f, 0.80f);
    const Color OtherCameraColor(0.20f, 0.34f, 0.26f, 0.70f);
    const Color ScanlineColor(0.38f, 0.85f, 0.52f, 0.10f);
    // 枠の太さ、上の文字の帯の高さ、文字の字下げ、1行目・2行目の位置と大きさ
    constexpr float FrameWidth = 5.0f;
    constexpr float HeaderHeight = 62.0f;
    constexpr float TextIndent = 18.0f;
    constexpr float LabelY = 10.0f;
    constexpr float LabelPixelSize = 2.5f;
    constexpr float HelpY = 37.0f;
    constexpr float HelpPixelSize = 1.9f;
    // 右上の巡回の進み具合：文字の大きさ、失敗の印の大きさとすき間、文字と印の間、印の上の位置
    constexpr float StatusPixelSize = 2.0f;
    constexpr float MistakeBoxSize = 12.0f;
    constexpr float MistakeBoxGap = 6.0f;
    constexpr float StatusGap = 16.0f;
    constexpr float MistakeBoxY = 13.0f;
    // 下端のカメラの並び：印の幅とすき間、下からの位置、高さ
    constexpr float IndicatorWidth = 28.0f;
    constexpr float IndicatorGap = 8.0f;
    constexpr float IndicatorBottom = 14.0f;
    constexpr float IndicatorHeight = 5.0f;
    // 流れる横線：本数、線ごとのずらし、流れる速さ、映像の上の帯の下から始める位置、流れる範囲から引く高さ
    constexpr int ScanlineCount = 8;
    constexpr int ScanlineSpacing = 73;
    constexpr float ScanlineSpeed = 46.0f;
    constexpr float ScanlineTop = 64.0f;
    constexpr float ScanlineRangeMargin = 69.0f;
    // 下の操作の説明：文字の大きさ、映像の下からの帯の位置と高さ、文字の位置
    constexpr float PromptPixelSize = 2.8f;
    constexpr float PromptBandGap = 18.0f;
    constexpr float PromptBandHeight = 54.0f;
    constexpr float PromptTextGap = 34.0f;
}

// 監視カメラの映像を見ている画面：暗くした背景に映像を貼り、枠・カメラの名前・巡回の進み具合・操作を重ねている
void Hud::DrawSurveillanceFeed(
    ID3D11ShaderResourceView* feed,
    Shader& textureShader,
    float elapsedSeconds,
    std::string_view cameraLabel,
    int cameraIndex,
    int cameraCount,
    int roundsCleared,
    int requiredRounds,
    int mistakes,
    int mistakesUntilCaught,
    bool zoomed,
    bool showingReference,
    bool reportReady,
    bool wrongReportVisible)
{
    using namespace FeedLayout;
    // 映像は16:9で、画面の90%・幅1280・上下の文字が入る高さのうち、最も小さい大きさにしている
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float availableHeight = (std::max)(screenHeight - ReservedHeight, MinimumAvailableHeight);
    const float feedWidth = (std::min)({
        screenWidth * WidthRatio,
        MaximumWidth,
        availableHeight * AspectWidth / AspectHeight });
    const float feedHeight = feedWidth * AspectHeight / AspectWidth;
    const float feedX = (screenWidth - feedWidth) * 0.5f;
    const float feedY = (screenHeight - feedHeight) * 0.5f + VerticalOffset;

    // 背景を暗くし、映像のテクスチャを貼っている（映像はHUDの頂点とは別に描いている）
    m_Vertices.clear();
    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, BackgroundColor);
    Flush();

    DrawTextureRectangle(
        feed, textureShader, feedX, feedY, feedWidth, feedHeight);

    // 映像の上に重ねる枠と、カメラの名前・操作の説明
    m_Vertices.clear();
    AddRectangle(feedX - FrameWidth, feedY - FrameWidth,
        feedWidth + FrameWidth * 2.0f, FrameWidth, Green);
    AddRectangle(feedX - FrameWidth, feedY + feedHeight,
        feedWidth + FrameWidth * 2.0f, FrameWidth, Green);
    AddRectangle(feedX - FrameWidth, feedY, FrameWidth, feedHeight, Green);
    AddRectangle(feedX + feedWidth, feedY, FrameWidth, feedHeight, Green);
    AddRectangle(feedX, feedY, feedWidth, HeaderHeight, Dark);
    AddText(feedX + TextIndent, feedY + LabelY,
        std::string(cameraLabel) +
            (showingReference ? "   基準映像" : "   LIVE"),
        LabelPixelSize, Green);
    AddText(feedX + TextIndent, feedY + HelpY,
        showingReference
            ? "異常発生前の映像   C / Y でライブへ戻る"
            : (zoomed
                ? "暗視補正 ON   拡大中   Z / X で標準画角へ"
                : "暗視補正 ON   C / Y 基準映像   Z / X 拡大"),
        HelpPixelSize, HelpColor);

    // 右上に巡回の進み具合と、捕まるまでに許される失敗の回数（使った分は赤）を表示している。
    const std::string status =
        "巡回 " + std::to_string(roundsCleared) + " / " +
        std::to_string(requiredRounds);
    const float statusWidth =
        static_cast<float>(CountDisplayedCharacters(status)) * StatusPixelSize * GlyphAdvance;
    const float warningWidth =
        static_cast<float>(mistakesUntilCaught) * (MistakeBoxSize + MistakeBoxGap);
    const float statusRight = feedX + feedWidth - TextIndent;
    AddText(statusRight - warningWidth - StatusGap - statusWidth,
        feedY + LabelY, status, StatusPixelSize, Pale);
    for (int warning = 0; warning < mistakesUntilCaught; ++warning)
    {
        const bool used = warning < mistakes;
        AddRectangle(
            statusRight - warningWidth +
                static_cast<float>(warning) * (MistakeBoxSize + MistakeBoxGap),
            feedY + MistakeBoxY, MistakeBoxSize, MistakeBoxSize,
            used ? UsedMistakeColor : SpareMistakeColor);
    }

    // 下端にカメラの並びを表示し、切り替えられることを伝えている。
    const float indicatorsWidth =
        static_cast<float>(cameraCount) * (IndicatorWidth + IndicatorGap) -
        IndicatorGap;
    for (int camera = 0; camera < cameraCount; ++camera)
    {
        AddRectangle(
            (screenWidth - indicatorsWidth) * 0.5f +
                static_cast<float>(camera) * (IndicatorWidth + IndicatorGap),
            feedY + feedHeight - IndicatorBottom, IndicatorWidth, IndicatorHeight,
            camera == cameraIndex ? Green : OtherCameraColor);
    }

    // 映像の上を流れる薄い横線（監視カメラのノイズらしさ）
    for (int line = 0; line < ScanlineCount; ++line)
    {
        const float offset = std::fmod(
            static_cast<float>(line * ScanlineSpacing) + elapsedSeconds * ScanlineSpeed,
            (std::max)(feedHeight - ScanlineRangeMargin, 1.0f));
        AddRectangle(feedX, feedY + ScanlineTop + offset,
            feedWidth, 1.0f, ScanlineColor);
    }

    // 下の操作の説明：基準映像・判定の間違い・判定できる状態・受信中で切り替えている
    const std::string_view prompt = showingReference
        ? (Input::IsControllerConnected()
            ? "記録映像   Y ライブ映像へ戻る"
            : "記録映像   C ライブ映像へ戻る")
        : wrongReportVisible
        ? "判定不一致   映像をもう一度確認"
        : (reportReady
            ? (Input::IsControllerConnected()
                ? "LB RB 切替   A このカメラに異常   B 異常なし"
                : "← → 切替   E このカメラに異常   Q 異常なし")
            : "監視映像を受信中...");
    const float promptWidth =
        static_cast<float>(CountDisplayedCharacters(prompt)) *
        PromptPixelSize * GlyphAdvance;
    AddRectangle(feedX, feedY + feedHeight + PromptBandGap,
        feedWidth, PromptBandHeight, Dark);
    AddText((screenWidth - promptWidth) * 0.5f,
        feedY + feedHeight + PromptTextGap, prompt, PromptPixelSize,
        wrongReportVisible ? WrongColor : (reportReady ? Pale : Green));
    Flush();
}

namespace ChapterCardLayout
{
    // 現れる秒数と、薄れ始める秒数（消えるのは Hud::ChapterCardSeconds）
    constexpr float FadeInSeconds = 0.45f;
    constexpr float FadeOutStart = 3.15f;
    // パネル：幅（画面の幅に対する割合・上限）、高さ、中央から上へずらす量
    constexpr float PanelWidthRatio = 0.72f;
    constexpr float PanelMaxWidth = 760.0f;
    constexpr float PanelHeight = 142.0f;
    constexpr float PanelRaise = 88.0f;
    // 色（不透明度に、見え方を掛ける）：画面全体の影、パネル、縁取りと章の名前、副題
    const Color ShadeColor(0.0f, 0.004f, 0.003f, 0.22f);
    const Color PanelColor(0.008f, 0.016f, 0.013f, 0.88f);
    const Color AccentColor(0.48f, 0.88f, 0.57f, 0.94f);
    const Color PaleColor(0.70f, 0.78f, 0.72f, 0.86f);
    // 左の縁の幅、上の線の高さ
    constexpr float AccentWidth = 5.0f;
    constexpr float TopLineHeight = 2.0f;
    // 章の名前と副題：文字の大きさと、パネルの上からの位置
    constexpr float ChapterPixelSize = 4.0f;
    constexpr float SubtitlePixelSize = 2.5f;
    constexpr float ChapterY = 28.0f;
    constexpr float SubtitleY = 91.0f;
}

// 章の始まりのカード：0.45秒で現れ、3.15秒から薄れ始め、4.2秒で消えている
void Hud::DrawChapterCard(
    std::string_view chapter,
    std::string_view subtitle,
    float elapsedSeconds)
{
    using namespace ChapterCardLayout;
    constexpr float totalDuration = ChapterCardSeconds;
    float visibility = 1.0f;
    if (elapsedSeconds < FadeInSeconds)
    {
        visibility = elapsedSeconds / FadeInSeconds;
    }
    else if (elapsedSeconds > FadeOutStart)
    {
        visibility =
            (totalDuration - elapsedSeconds) /
            (totalDuration - FadeOutStart);
    }
    visibility = (std::clamp)(visibility, 0.0f, 1.0f);
    if (visibility <= 0.0f)
    {
        return;
    }

    m_Vertices.clear();
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float panelWidth = (std::min)(screenWidth * PanelWidthRatio, PanelMaxWidth);
    const float panelX = (screenWidth - panelWidth) * 0.5f;
    const float panelY = screenHeight * 0.5f - PanelRaise;
    const Color shade = Faded(ShadeColor, visibility);
    const Color panel = Faded(PanelColor, visibility);
    const Color accent = Faded(AccentColor, visibility);
    const Color pale = Faded(PaleColor, visibility);

    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, shade);
    AddRectangle(panelX, panelY, panelWidth, PanelHeight, panel);
    AddRectangle(panelX, panelY, AccentWidth, PanelHeight, accent);
    AddRectangle(panelX, panelY, panelWidth, TopLineHeight, accent);

    // 章の名前は英字の想定なので、文字数（バイト数）から幅を出して中央に置いている
    const float chapterWidth =
        static_cast<float>(chapter.size()) * ChapterPixelSize * GlyphAdvance;
    const float subtitleWidth =
        static_cast<float>(subtitle.size()) * SubtitlePixelSize * GlyphAdvance;
    AddText((screenWidth - chapterWidth) * 0.5f,
        panelY + ChapterY, chapter, ChapterPixelSize, accent);
    AddText((screenWidth - subtitleWidth) * 0.5f,
        panelY + SubtitleY, subtitle, SubtitlePixelSize, pale);
    Flush();
}

namespace GuideLayout
{
    // 向きを計算しないほど近いとみなす長さ
    constexpr float MinimumLength = 0.001f;
    // この距離より近ければ「近く」、この内積より後ろなら「後方」、この内積より横なら「右・左」
    constexpr float NearDistance = 28.0f;
    constexpr float BehindFacing = -0.55f;
    constexpr float SideFacing = 0.65f;
    // パネル：文字の大きさ、文字の左右の余白、位置、高さ、左の縁の幅、文字の位置、色（地・縁・文字）
    constexpr float PixelSize = 2.0f;
    constexpr float HorizontalPadding = 28.0f;
    constexpr float PanelX = 34.0f;
    constexpr float PanelY = 76.0f;
    constexpr float PanelHeight = 30.0f;
    constexpr float AccentWidth = 3.0f;
    constexpr float TextIndent = 14.0f;
    constexpr float TextTop = 8.0f;
    const Color PanelColor(0.004f, 0.010f, 0.008f, 0.72f);
    const Color AccentColor(0.42f, 0.72f, 0.48f, 0.82f);
    const Color TextColor(0.58f, 0.76f, 0.62f, 0.88f);
}

// 目的地の方向を、左上の小さなパネルに「前方・右・左・後方・近く」の言葉で出している
void Hud::DrawObjectiveGuide(
    const Camera& camera,
    const Vector3& origin,
    const Vector3& target)
{
    using namespace GuideLayout;
    // 水平面だけで、プレイヤーの向きと目的地への向きを比べている
    Vector3 toTarget = target - origin;
    toTarget.y = 0.0f;
    const float distance = toTarget.Length();
    if (distance <= MinimumLength)
    {
        return;
    }
    toTarget /= distance;

    Vector3 forward = camera.GetForward();
    forward.y = 0.0f;
    const float forwardLength = forward.Length();
    if (forwardLength <= MinimumLength)
    {
        return;
    }
    forward /= forwardLength;

    // 内積で前か後ろか、外積のy成分で右か左かを決めている（28より近ければ「近く」）
    std::string_view direction = "前方";
    const float facing = forward.Dot(toTarget);
    const float side = forward.z * toTarget.x - forward.x * toTarget.z;
    if (distance < NearDistance)
    {
        direction = "近く";
    }
    else if (facing < BehindFacing)
    {
        direction = "後方";
    }
    else if (facing < SideFacing)
    {
        direction = side >= 0.0f ? "右" : "左";
    }

    const std::string text =
        "目的地 " + std::string(direction) +
        (Input::IsControllerConnected() ? "   LB ヒント" : "   H ヒント");
    const float textWidth =
        static_cast<float>(CountDisplayedCharacters(text)) * PixelSize * GlyphAdvance;
    const float panelWidth = textWidth + HorizontalPadding;

    m_Vertices.clear();
    AddRectangle(PanelX, PanelY, panelWidth, PanelHeight, PanelColor);
    AddRectangle(PanelX, PanelY, AccentWidth, PanelHeight, AccentColor);
    AddText(PanelX + TextIndent, PanelY + TextTop, text, PixelSize, TextColor);
    Flush();
}

namespace Stage2StatusLayout
{
    // パネル：幅と高さ（Hud::Stage2StatusReservedHeight の 58 と同じ）、右端・上からの位置
    constexpr float PanelWidth = 214.0f;
    constexpr float PanelHeight = 58.0f;
    constexpr float RightMargin = 34.0f;
    constexpr float PanelY = 28.0f;
    // 色：パネルの地、点いていない区画、進み具合の色（出口が開いた・まだ）、信号盤の色（青・黄・赤）
    const Color DarkColor(0.004f, 0.010f, 0.008f, 0.72f);
    const Color InactiveColor(0.12f, 0.15f, 0.14f, 0.88f);
    const Color ExitReadyColor(0.46f, 0.90f, 0.54f, 0.96f);
    const Color ProgressColor(0.80f, 0.66f, 0.38f, 0.94f);
    const Color SignalColors[] =
    {
        Color(0.12f, 0.52f, 0.96f, 0.96f),
        Color(0.96f, 0.68f, 0.14f, 0.96f),
        Color(0.94f, 0.18f, 0.10f, 0.96f)
    };
    // 次に押す信号盤の色の暗さ（色に掛ける割合）と、そのときの不透明度
    constexpr float NextSignalDim = 0.58f;
    constexpr float NextSignalAlpha = 0.96f;
    // 左の縁の幅、文字の位置（字下げ・上）と大きさ
    constexpr float AccentWidth = 3.0f;
    constexpr float TextIndent = 14.0f;
    constexpr float TextTop = 8.0f;
    constexpr float TextPixelSize = 1.8f;
    // 3つの区画：数、幅とすき間、上からの位置、高さ
    constexpr int SegmentCount = 3;
    constexpr float SegmentWidth = 52.0f;
    constexpr float SegmentGap = 7.0f;
    constexpr float SegmentY = 35.0f;
    constexpr float SegmentHeight = 10.0f;
    // 危険のゲージ：パネルとのすき間、高さ、文字の上の位置、ゲージの左からの位置・上の位置・長さ・太さ
    constexpr float DangerGap = 8.0f;
    constexpr float DangerPanelHeight = 40.0f;
    constexpr float DangerTextTop = 7.0f;
    constexpr float DangerBarX = 88.0f;
    constexpr float DangerBarY = 12.0f;
    constexpr float DangerBarWidth = 108.0f;
    constexpr float DangerBarHeight = 10.0f;
    // 危険の色（危険度0の色と、危険度1までに変わる量。不透明度は一定）
    const Color DangerBaseColor(0.78f, 0.24f, 0.14f, 0.96f);
    const Vector3 DangerColorShift(0.18f, -0.14f, -0.08f);
}

// 2面の右上の状態パネル：周回の進み具合（信号盤の段階に入ったら青・黄・赤）と、足音の危険度のゲージ
void Hud::DrawStage2Status(
    int completedLoops,
    float threatRate,
    bool exitReady,
    int signalStep,
    bool signalActive)
{
    using namespace Stage2StatusLayout;
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float panelX = screenWidth - PanelWidth - RightMargin;
    const Color active = exitReady ? ExitReadyColor : ProgressColor;

    AddRectangle(panelX, PanelY, PanelWidth, PanelHeight, DarkColor);
    AddRectangle(panelX, PanelY, AccentWidth, PanelHeight, active);
    AddText(panelX + TextIndent, PanelY + TextTop,
        exitReady ? "出口が開いた" :
            (signalActive ? "信号復旧 青 黄 赤" : "廊下の繰り返し"),
        TextPixelSize, active);

    // 3つの区画：周回の数、信号盤の段階なら終えた色まで点け、次に押す色は暗くして示している
    const int safeCompletedLoops = (std::clamp)(completedLoops, 0, SegmentCount);
    for (int index = 0; index < SegmentCount; ++index)
    {
        const bool segmentComplete = signalActive
            ? index < (std::clamp)(signalStep, 0, SegmentCount)
            : index < safeCompletedLoops;
        Color segmentColor = segmentComplete
            ? (signalActive ? SignalColors[index] : active)
            : InactiveColor;
        if (signalActive && index == (std::clamp)(signalStep, 0, SegmentCount - 1))
        {
            segmentColor = Color(
                SignalColors[index].x * NextSignalDim,
                SignalColors[index].y * NextSignalDim,
                SignalColors[index].z * NextSignalDim,
                NextSignalAlpha);
        }
        AddRectangle(
            panelX + TextIndent + static_cast<float>(index) *
                (SegmentWidth + SegmentGap),
            PanelY + SegmentY,
            SegmentWidth,
            SegmentHeight,
            segmentColor);
    }

    // 危険度がある間だけ、その下に危険のゲージを出している
    const float safeThreatRate = (std::clamp)(threatRate, 0.0f, 1.0f);
    if (safeThreatRate > 0.0f)
    {
        constexpr float dangerPanelY = PanelY + PanelHeight + DangerGap;
        const Color dangerColor(
            DangerBaseColor.R() + safeThreatRate * DangerColorShift.x,
            DangerBaseColor.G() + safeThreatRate * DangerColorShift.y,
            DangerBaseColor.B() + safeThreatRate * DangerColorShift.z,
            DangerBaseColor.A());
        AddRectangle(panelX, dangerPanelY, PanelWidth, DangerPanelHeight, DarkColor);
        AddRectangle(panelX, dangerPanelY, AccentWidth, DangerPanelHeight, dangerColor);
        AddText(panelX + TextIndent, dangerPanelY + DangerTextTop,
            "危険", TextPixelSize, dangerColor);
        AddRectangle(panelX + DangerBarX, dangerPanelY + DangerBarY,
            DangerBarWidth, DangerBarHeight, InactiveColor);
        AddRectangle(panelX + DangerBarX, dangerPanelY + DangerBarY,
            DangerBarWidth * safeThreatRate, DangerBarHeight, dangerColor);
    }

    Flush();
}

namespace KeypadLayout
{
    // パネルの大きさ
    constexpr float PanelWidth = 440.0f;
    constexpr float PanelHeight = 300.0f;
    // 色：選んでいる桁と緑の枠、文字、選んでいない枠と案内、間違いの赤、画面全体の影、パネルの地、桁の箱の地
    const Color Green(0.48f, 0.90f, 0.58f, 0.96f);
    const Color Pale(0.72f, 0.82f, 0.74f, 0.92f);
    const Color Dim(0.30f, 0.38f, 0.33f, 0.90f);
    const Color Red(0.96f, 0.22f, 0.14f, 0.96f);
    const Color ShadeColor(0.0f, 0.004f, 0.004f, 0.55f);
    const Color PanelColor(0.012f, 0.020f, 0.017f, 0.96f);
    const Color BoxColor(0.005f, 0.010f, 0.008f, 1.0f);
    // 不正解の直後に枠を赤く点滅させる速さ（間違いの残りの割合に掛けて、点滅の回数にする）
    constexpr float WrongFlashRate = 7.0f;
    // パネルの上下の線の太さ、見出しの位置と大きさ
    constexpr float FrameLineHeight = 3.0f;
    constexpr float TitleY = 20.0f;
    constexpr float TitlePixelSize = 3.4f;
    // 桁の箱：数、幅と高さ、すき間、数字の大きさ、パネルの上からの位置、枠の太さ（選んでいる・いない）
    constexpr int DigitCount = KeypadLock::DigitCount;
    constexpr float BoxWidth = 70.0f;
    constexpr float BoxHeight = 92.0f;
    constexpr float BoxGap = 20.0f;
    constexpr float DigitPixelSize = 9.0f;
    constexpr float BoxY = 88.0f;
    constexpr float SelectedBorder = 4.0f;
    constexpr float NormalBorder = 2.0f;
    // 数字1文字の幅（5画素の字）
    constexpr float DigitGlyphColumns = 5.0f;
    // 上下の三角：段の数、先の半分の幅と1段ごとに広げる量、1段の高さ、箱の上・下からの距離
    constexpr int ArrowSteps = 4;
    constexpr float ArrowTipHalfWidth = 2.0f;
    constexpr float ArrowStepWidening = 3.0f;
    constexpr float ArrowStepHeight = 3.0f;
    constexpr float ArrowTopGap = 22.0f;
    constexpr float ArrowBottomGap = 19.0f;
    // 扉の向こうの気配を知らせる間違いの回数
    // 下のメッセージと操作の説明：位置と大きさ
    constexpr float MessageY = 222.0f;
    constexpr float MessagePixelSize = 2.4f;
    constexpr float HelpY = 256.0f;
    constexpr float HelpPixelSize = 2.0f;
}

// 画面の中央に4桁の入力盤を出している。選んでいる桁は緑の枠で示し、上下の三角で増減できることを伝えている。
void Hud::DrawKeypad(
    const KeypadLock::Digits& entered,
    int cursor,
    float wrongRate,
    int mistakes)
{
    using namespace KeypadLayout;
    m_Vertices.clear();
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float panelX = (screenWidth - PanelWidth) * 0.5f;
    const float panelY = (screenHeight - PanelHeight) * 0.5f;

    // 不正解の直後は、枠を赤く点滅させている。
    const bool wrongFlash = wrongRate > 0.0f &&
        static_cast<int>(wrongRate * WrongFlashRate) % 2 == 0;
    const Color frame = wrongFlash ? Red : Green;

    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, ShadeColor);
    AddRectangle(panelX, panelY, PanelWidth, PanelHeight, PanelColor);
    AddRectangle(panelX, panelY, PanelWidth, FrameLineHeight, frame);
    AddRectangle(panelX, panelY + PanelHeight - FrameLineHeight, PanelWidth, FrameLineHeight, frame);

    AddCenteredText(panelY + TitleY, "暗証番号", TitlePixelSize, Pale);

    const float rowWidth = BoxWidth * DigitCount + BoxGap * (DigitCount - 1);
    const float rowX = (screenWidth - rowWidth) * 0.5f;
    const float boxY = panelY + BoxY;
    // 4つの桁の箱と数字を描いている（選んでいる桁は枠を太くしている）
    for (int index = 0; index < DigitCount; ++index)
    {
        const float boxX = rowX + static_cast<float>(index) * (BoxWidth + BoxGap);
        const bool selected = index == cursor;
        const Color border = selected ? frame : Dim;
        const float thickness = selected ? SelectedBorder : NormalBorder;
        AddRectangle(boxX, boxY, BoxWidth, BoxHeight, BoxColor);
        AddRectangle(boxX, boxY, BoxWidth, thickness, border);
        AddRectangle(boxX, boxY + BoxHeight - thickness, BoxWidth, thickness, border);
        AddRectangle(boxX, boxY, thickness, BoxHeight, border);
        AddRectangle(boxX + BoxWidth - thickness, boxY, thickness, BoxHeight, border);

        const char digitText[2] = {
            static_cast<char>('0' + entered[static_cast<std::size_t>(index)]), '\0' };
        AddText(boxX + (BoxWidth - DigitPixelSize * DigitGlyphColumns) * 0.5f,
            boxY + (BoxHeight - DigitPixelSize * GlyphRows) * 0.5f,
            digitText, DigitPixelSize, selected ? Green : Pale);

        if (selected)
        {
            // 上下の三角（増減できる合図）を、横幅の違う長方形を重ねて描いている。
            const float centerX = boxX + BoxWidth * 0.5f;
            for (int step = 0; step < ArrowSteps; ++step)
            {
                const float halfWidth = ArrowTipHalfWidth + static_cast<float>(step) * ArrowStepWidening;
                AddRectangle(centerX - halfWidth, boxY - ArrowTopGap + static_cast<float>(step) * ArrowStepHeight,
                    halfWidth * 2.0f, ArrowStepHeight, Green);
                AddRectangle(centerX - halfWidth, boxY + BoxHeight + ArrowBottomGap - static_cast<float>(step) * ArrowStepHeight,
                    halfWidth * 2.0f, ArrowStepHeight, Green);
            }
        }
    }

    // 下のメッセージ：間違えた直後は「番号が違う」、3回以上間違えると扉の向こうの気配を知らせている
    const std::string_view message = wrongRate > 0.0f
        ? (mistakes >= KeypadLock::WarningMistakeCount ? "扉の向こうで何かが動いた" : "番号が違う")
        : "壁の数字を懐中電灯で探す";
    AddCenteredText(panelY + MessageY, message, MessagePixelSize, wrongRate > 0.0f ? Red : Pale);
    AddCenteredText(panelY + HelpY,
        "←→ 桁  ↑↓ 数字  E 決定  Q 戻る", HelpPixelSize, Dim);
    Flush();
}

namespace HidingLayout
{
    // 扉の内側の暗さと、すき間の縁の色（影が近いほど赤を足す）
    const Color DarkColor(0.0f, 0.0f, 0.0f, 0.94f);
    const Color EdgeColor(0.10f, 0.02f, 0.02f, 0.55f);
    constexpr float EdgeDangerRed = 0.35f;
    // すき間：数、高さとすき間の間（画面の高さに対する割合）、呼吸の揺れ（速さと画面の高さに対する割合）、
    // 最初のすき間の位置（画面の高さに対する割合）、左右の端の幅（画面の幅に対する割合）、縁の太さ
    constexpr int SlitCount = 4;
    constexpr float SlitHeightRatio = 0.055f;
    constexpr float SlitGapRatio = 0.035f;
    constexpr float BreathRate = 1.6f;
    constexpr float BreathRatio = 0.004f;
    constexpr float FirstSlitRatio = 0.36f;
    constexpr float SlitInsetRatio = 0.12f;
    constexpr float EdgeWidth = 2.0f;
    // 「外に出る」の文字：大きさ、下からの位置、色
    constexpr float PromptPixelSize = 2.2f;
    constexpr float PromptBottom = 90.0f;
    const Color PromptColor(0.62f, 0.70f, 0.66f, 0.80f);
    // 息のゲージを出す条件（これより減っていたら出す）、ゲージの幅・下からの位置・太さ・枠の太さ
    constexpr float FullBreath = 0.999f;
    constexpr float MeterWidth = 260.0f;
    constexpr float MeterBottom = 140.0f;
    constexpr float MeterHeight = 10.0f;
    constexpr float MeterFrame = 2.0f;
    // 聞かれそうなときの枠の点滅（速さ）と、枠の色（普段の色と、聞かれそうなときに変わる量）
    constexpr float WarningBlinkRate = 18.0f;
    const Color FrameBaseColor(0.30f, 0.30f, 0.28f, 0.85f);
    constexpr float FrameWarningRed = 0.6f;
    constexpr float FrameWarningFade = 0.2f;
    // ゲージの地と中身の色（息が少ない・普段）、少ないとみなす割合
    const Color MeterBackColor(0.02f, 0.03f, 0.03f, 0.95f);
    const Color LowBreathColor(0.85f, 0.30f, 0.20f, 0.95f);
    const Color BreathColor(0.45f, 0.72f, 0.80f, 0.95f);
    constexpr float LowBreathRate = 0.25f;
    // 「息を止める」の文字：大きさ、ゲージの上からの距離、色（止めている・いない）
    constexpr float BreathPixelSize = 2.0f;
    constexpr float BreathTextGap = 26.0f;
    const Color HoldingColor(0.62f, 0.78f, 0.84f, 0.90f);
    const Color HoldPromptColor(0.86f, 0.62f, 0.52f, 0.90f);
}

void Hud::DrawHidingView(float elapsedSeconds, float dangerRate, float breathLeft, bool holding, float exposure)
{
    using namespace HidingLayout;
    m_Vertices.clear();
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float danger = (std::clamp)(dangerRate, 0.0f, 1.0f);
    const Color edge(EdgeColor.R() + danger * EdgeDangerRed, EdgeColor.G(), EdgeColor.B(), EdgeColor.A());

    // 画面の中央付近に4本の横長のすき間を残し、それ以外を扉の内側の暗さで覆っている。
    // 呼吸に合わせて、すき間がわずかに上下している。
    const float slitHeight = screenHeight * SlitHeightRatio;
    const float slitGap = screenHeight * SlitGapRatio;
    const float breath = std::sin(elapsedSeconds * BreathRate) * screenHeight * BreathRatio;
    const float firstSlitY = screenHeight * FirstSlitRatio + breath;
    const float slitInset = screenWidth * SlitInsetRatio;

    // すき間とすき間の間を上から順に暗く塗り、すき間の左右の端も塗っている。影が近いほど、すき間の縁を赤くしている
    float coveredY = 0.0f;
    for (int index = 0; index < SlitCount; ++index)
    {
        const float slitY = firstSlitY + static_cast<float>(index) * (slitHeight + slitGap);
        AddRectangle(0.0f, coveredY, screenWidth, slitY - coveredY, DarkColor);
        AddRectangle(0.0f, slitY, slitInset, slitHeight, DarkColor);
        AddRectangle(screenWidth - slitInset, slitY, slitInset, slitHeight, DarkColor);
        AddRectangle(slitInset, slitY, screenWidth - slitInset * 2.0f, EdgeWidth, edge);
        AddRectangle(slitInset, slitY + slitHeight - EdgeWidth, screenWidth - slitInset * 2.0f, EdgeWidth, edge);
        coveredY = slitY + slitHeight;
    }
    AddRectangle(0.0f, coveredY, screenWidth, screenHeight - coveredY, DarkColor);

    const std::string_view prompt = Input::IsControllerConnected()
        ? "A 外に出る"
        : "E 外に出る";
    const float promptWidth =
        static_cast<float>(CountDisplayedCharacters(prompt)) * PromptPixelSize * GlyphAdvance;
    AddText((screenWidth - promptWidth) * 0.5f, screenHeight - PromptBottom,
        prompt, PromptPixelSize, PromptColor);

    // 影が近いときと、息を止めているときだけ、息のゲージと「息を止める」操作を出している。
    // 聞かれそうになるほど、ゲージの枠が赤く点滅する
    if (danger > 0.0f || holding || breathLeft < FullBreath)
    {
        const float meterX = (screenWidth - MeterWidth) * 0.5f;
        const float meterY = screenHeight - MeterBottom;
        const float warning = (std::clamp)(exposure, 0.0f, 1.0f);
        const float blink = 0.5f + 0.5f * std::sin(elapsedSeconds * WarningBlinkRate);
        const Color frame(FrameBaseColor.R() + warning * blink * FrameWarningRed, FrameBaseColor.G() - warning * FrameWarningFade,
            FrameBaseColor.B() - warning * FrameWarningFade, FrameBaseColor.A());
        AddRectangle(meterX - MeterFrame, meterY - MeterFrame, MeterWidth + MeterFrame * 2.0f, MeterHeight + MeterFrame * 2.0f, frame);
        AddRectangle(meterX, meterY, MeterWidth, MeterHeight, MeterBackColor);
        const float remaining = (std::clamp)(breathLeft, 0.0f, 1.0f);
        const Color fill = remaining < LowBreathRate ? LowBreathColor : BreathColor;
        AddRectangle(meterX, meterY, MeterWidth * remaining, MeterHeight, fill);

        const std::string_view breathPrompt = holding
            ? "息を止めている"
            : (Input::IsControllerConnected() ? "RB 長押しで息を止める" : "SPACE 長押しで息を止める");
        const float breathWidth =
            static_cast<float>(CountDisplayedCharacters(breathPrompt)) * BreathPixelSize * GlyphAdvance;
        AddText((screenWidth - breathWidth) * 0.5f, meterY - BreathTextGap, breathPrompt, BreathPixelSize,
            holding ? HoldingColor : HoldPromptColor);
    }
    Flush();
}

namespace QuietRecoveryLayout
{
    // パネル：左端の下限、右端からの距離、上の位置（右上の「記録」の下端108のすぐ下）、幅と高さ、地の色
    constexpr float MinimumX = 12.0f;
    constexpr float RightMargin = 330.0f;
    constexpr float TopY = 116.0f;
    constexpr float PanelWidth = 296.0f;
    constexpr float PanelHeight = 76.0f;
    const Color PanelColor(0.005f, 0.012f, 0.015f, 0.85f);
    // 文字の色（影が近すぎる・普段）
    const Color TooCloseColor(0.96f, 0.38f, 0.25f, 1.0f);
    const Color NormalColor(0.64f, 0.84f, 0.78f, 1.0f);
    // 文字：字下げ、1行目・2行目の位置と大きさ
    constexpr float TextIndent = 10.0f;
    constexpr float FirstLineY = 10.0f;
    constexpr float FirstLinePixelSize = 1.6f;
    constexpr float SecondLineY = 31.0f;
    constexpr float SecondLinePixelSize = 1.4f;
    // ゲージ：位置、長さ、太さ、地の色。呼吸を整えている間に満ちる秒数
    constexpr float GaugeY = 58.0f;
    constexpr float GaugeWidth = 276.0f;
    constexpr float GaugeHeight = 6.0f;
    const Color GaugeBackColor(0.09f, 0.14f, 0.14f, 1.0f);
    constexpr float CooldownSeconds = 10.0f;
}

// 息を殺して影をやり過ごす操作の進み具合（成功・影が近すぎる・呼吸を整えている間・止まっている時間）を出している
void Hud::DrawQuietRecovery(float progressRate, float cooldown, bool success, bool tooClose)
{
    using namespace QuietRecoveryLayout;
    m_Vertices.clear();
    const float x = (std::max)(MinimumX, GetCanvasWidth() - RightMargin);
    // 右上の「記録」（Hud::Drawで描いている。下端は108＋m_TopRightOffset）のすぐ下に置いている。
    const float y = TopY + m_TopRightOffset;
    const Color color = tooClose ? TooCloseColor : NormalColor;
    AddRectangle(x, y, PanelWidth, PanelHeight, PanelColor);
    const std::string_view message = success ? "気配が遠のいた" :
        (tooClose ? "影が近い 距離を取るか光で対処" :
        (cooldown > 0.0f ? "呼吸を整えている" : "消灯して2秒止まると気配を抑える"));
    AddText(x + TextIndent, y + FirstLineY, message, FirstLinePixelSize, color);
    AddText(x + TextIndent, y + SecondLineY,
        success ? "静かに歩いて探索を続ける" : "移動や点灯で静止の計測はやり直し", SecondLinePixelSize, color);
    AddRectangle(x + TextIndent, y + GaugeY, GaugeWidth, GaugeHeight, GaugeBackColor);
    // ゲージ：成功なら満タン、呼吸を整えている間は10秒で回復、それ以外は止まっている時間の進み具合
    const float fill = success ? 1.0f : (cooldown > 0.0f ?
        1.0f - cooldown / CooldownSeconds : progressRate);
    AddRectangle(x + TextIndent, y + GaugeY, GaugeWidth * (std::clamp)(fill, 0.0f, 1.0f), GaugeHeight, color);
    Flush();
}

namespace PauseLayout
{
    // パネルの大きさ、左の縁の幅、上の線の高さ
    constexpr float PanelWidth = 660.0f;
    constexpr float PanelHeight = 600.0f;
    constexpr float AccentWidth = 6.0f;
    constexpr float TopLineHeight = 2.0f;
    // 色：画面全体の影、パネルの地、選んでいる項目と見出し、普段の文字
    const Color ShadeColor(0.0f, 0.004f, 0.004f, 0.78f);
    const Color PanelColor(0.012f, 0.022f, 0.018f, 0.96f);
    const Color Green(0.48f, 0.90f, 0.58f, 0.96f);
    const Color Pale(0.72f, 0.82f, 0.74f, 0.92f);
    // 見出しと、今の状態の行：位置と大きさ
    constexpr float TitleY = 34.0f;
    constexpr float TitlePixelSize = 5.0f;
    constexpr float StatusY = 78.0f;
    constexpr float SmallPixelSize = 2.0f;
    // 設定の行：1行目の位置、行の間隔、文字の大きさ
    constexpr float SettingFirstY = 108.0f;
    constexpr float SettingSpacing = 40.0f;
    constexpr float SettingPixelSize = 2.5f;
    // 操作の説明の行：1行目の位置と行の間隔
    constexpr float HelpFirstY = 350.0f;
    constexpr float HelpSpacing = 42.0f;
    // ポーズ画面の階表示の上限。設定の段階はGameSettings側とそろえる。
    constexpr int MaxFloorNumber = 2;
}

// ポーズメニュー：今の階・経過時間・捕まった回数と、6つの設定（選んでいる項目は緑）、操作の説明を並べている
void Hud::DrawPause(
    int brightnessLevel,
    int effectLevel,
    int lookSensitivityLevel,
    int volumeLevel,
    bool guideEnabled,
    int resolutionLevel,
    int selectedSetting,
    int floorNumber,
    float runTimeSeconds,
    int caughtCount)
{
    using namespace PauseLayout;
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float panelX = (screenWidth - PanelWidth) * 0.5f;
    const float panelY = (screenHeight - PanelHeight) * 0.5f;

    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, ShadeColor);
    AddRectangle(panelX, panelY, PanelWidth, PanelHeight, PanelColor);
    AddRectangle(panelX, panelY, AccentWidth, PanelHeight, Green);
    AddRectangle(panelX, panelY, PanelWidth, TopLineHeight, Green);

    AddCenteredText(panelY + TitleY, "ポーズ", TitlePixelSize, Green);
    const int runSeconds = (std::max)(
        0, static_cast<int>(runTimeSeconds));
    // 階の番号が0以下なら、研究所に入る前（外の森・導入）として表示している
    const std::string place = floorNumber <= 0
        ? std::string("研究所の外")
        : std::to_string((std::min)(floorNumber, MaxFloorNumber)) + "階";
    const std::string runStatus =
        "現在 " + place +
        "   経過 " + std::to_string(runSeconds / SecondsPerMinute) + "分 " +
        std::to_string(runSeconds % SecondsPerMinute) + "秒   捕まった回数 " +
        std::to_string((std::max)(caughtCount, 0));
    AddCenteredText(panelY + StatusY, runStatus, SmallPixelSize, Pale);
    const int safeBrightnessLevel =
        (std::clamp)(brightnessLevel, 0, Core::GameSettings::MaxBrightnessLevel);
    const int safeEffectLevel = (std::clamp)(effectLevel, Core::GameSettings::LowEffectLevel, Core::GameSettings::HighEffectLevel);
    const int safeSensitivityLevel =
        (std::clamp)(lookSensitivityLevel, 0, Core::GameSettings::MaxLookSensitivityLevel);
    const int safeVolumeLevel = (std::clamp)(volumeLevel, 0, Core::GameSettings::MaxVolumeLevel);
    using Setting = Core::PauseMenu::Item;
    const int safeSelectedSetting =
        (std::clamp)(selectedSetting, 0, static_cast<int>(Setting::Count) - 1);
    // 設定の1行を、上からの順番の位置に、選んでいれば緑で描いている
    const auto addSetting = [&](Setting setting, std::string_view text)
    {
        const int row = static_cast<int>(setting);
        AddCenteredText(panelY + SettingFirstY + static_cast<float>(row) * SettingSpacing,
            text, SettingPixelSize, safeSelectedSetting == row ? Green : Pale);
    };
    addSetting(Setting::Brightness, "明るさ " + std::to_string(safeBrightnessLevel + 1) + " OF " +
        std::to_string(Core::GameSettings::MaxBrightnessLevel + 1));
    constexpr std::string_view effectNames[] =
    {
        "軽量 FPS優先", "標準", "高品質"
    };
    static_assert(std::size(effectNames) == Core::GameSettings::MaxEffectLevel + 1);
    addSetting(Setting::Effect, "画面効果 " + std::string(effectNames[safeEffectLevel]));
    addSetting(Setting::LookSensitivity, "視点速度 " + std::to_string(safeSensitivityLevel + 1) + " OF " +
        std::to_string(Core::GameSettings::MaxLookSensitivityLevel + 1));
    // UIには操作の目安の割合を出す。音声に掛ける倍率とは別の表。
    constexpr int volumePercent[] = { 0, 25, 50, 75, 100 };
    static_assert(std::size(volumePercent) == Core::GameSettings::MaxVolumeLevel + 1);
    addSetting(Setting::Volume, safeVolumeLevel == 0
        ? "音量 ミュート"
        : "音量 " + std::to_string(volumePercent[safeVolumeLevel]) + "%");
    addSetting(Setting::Guide, guideEnabled ? "目的表示 あり" : "目的表示 なし");
    // 描画解像度は次回の起動から反映されるため、起動時と違う段階を選んでいるときは、そのことを表示している。
    constexpr std::string_view resolutionNames[] = { "自動", "100%", "75%", "67%" };
    static_assert(std::size(resolutionNames) == Core::GameSettings::MaxResolutionLevel + 1);
    const int safeResolutionLevel = (std::clamp)(resolutionLevel, 0, Core::GameSettings::MaxResolutionLevel);
    addSetting(Setting::Resolution,
        "描画解像度 " + std::string(resolutionNames[safeResolutionLevel]) +
        (safeResolutionLevel == Application::GetLaunchResolutionLevel()
            ? "  " + std::to_string(Application::GetWidth()) + "x" +
                std::to_string(Application::GetHeight())
            : std::string("  次回起動から")));
    // 操作の説明は、コントローラーがつながっているかで切り替えている（1行目だけ小さい字）
    const bool controller = Input::IsControllerConnected();
    const std::string_view helpLines[] =
    {
        controller ? "十字キーで選択と調整" : "矢印キーで選択と調整",
        controller ? "START ゲームに戻る" : "ESC または P ゲームに戻る",
        controller
            ? (floorNumber <= 0 ? "Y この場面をやり直す" : "Y この階をやり直す")
            : (floorNumber <= 0 ? "R この場面をやり直す" : "R この階をやり直す"),
        controller ? "B タイトルへ戻る" : "T タイトルへ戻る",
        controller ? "BACK ゲーム終了" : "Q ゲーム終了",
    };
    for (std::size_t line = 0; line < std::size(helpLines); ++line)
    {
        AddCenteredText(panelY + HelpFirstY + static_cast<float>(line) * HelpSpacing,
            helpLines[line], line == 0 ? SmallPixelSize : SettingPixelSize, Pale);
    }

    Flush();
}

namespace SignalTextLayout
{
    // 文字の乱れ（導入と無線の字幕で共通）：sin を使う決まった式で 0〜1 の値を作る係数、1秒あたりの刻み、
    // 横のずれを始めるノイズの強さと幅、色をにじませるノイズの強さとずれ、にじむ赤
    constexpr float HashFrequency = 12.9898f;
    constexpr float HashAmplitude = 43758.5453f;
    constexpr float NoiseStepsPerSecond = 24.0f;
    constexpr float JitterNoiseLevel = 0.1f;
    constexpr float JitterWidth = 22.0f;
    constexpr float SplitNoiseLevel = 0.3f;
    constexpr float SplitOffset = 4.0f;
    const Color SplitRed(0.95f, 0.18f, 0.16f, 0.35f);
    // 本文の白っぽい文字の色
    const Color LineColor(0.86f, 0.90f, 0.86f, 1.0f);
    // 「通信途絶」：本体の赤、にじむ青緑（ノイズを掛ける）とそのずれ、抜ける横の帯（起こすノイズの強さ・乱数の倍率・
    // 左右のはみ出し・太さ（最低）・色）
    const Color SignalLostRed(0.92f, 0.32f, 0.26f, 0.95f);
    const Color SignalLostCyan(0.18f, 0.85f, 0.90f, 0.40f);
    constexpr float SignalLostSplit = 6.0f;
    constexpr float BarNoiseLevel = 0.2f;
    constexpr float BarSeedStep = 2.3f;
    constexpr float BarSeedPerBar = 5.1f;
    constexpr float BarLeftMargin = 10.0f;
    constexpr float BarExtraWidth = 20.0f;
    constexpr float BarHeightBase = 2.0f;
    // 受信の印の明滅（1秒のうち点いている割合）と、印の色
    constexpr float BlinkOn = 0.6f;
    const Color ReceiveMarkColor(0.48f, 0.90f, 0.58f, 0.90f);
    // 背景の走査線の間隔と色
    constexpr float ScanlineSpacing = 6.0f;
    const Color ScanlineColor(0.0f, 0.0f, 0.0f, 0.10f);

    float NoiseHash(float value)
    {
        const float s = std::sin(value * HashFrequency) * HashAmplitude;
        return s - std::floor(s);
    }
}

namespace IntroLayout
{
    // 背景：森を暗く沈める色（目的の画面ほど暗く）
    const Color BackdropColor(0.0f, 0.004f, 0.003f, 0.62f);
    constexpr float MissionBackdropExtra = 0.18f;
    // 受信記録：左端（最小・画面の幅に対する割合）、印の位置と大きさ、見出しの位置・大きさ・色
    constexpr float MinimumLeft = 80.0f;
    constexpr float LeftRatio = 0.16f;
    constexpr float MarkY = 230.0f;
    constexpr float MarkSize = 12.0f;
    constexpr float HeaderOffset = 22.0f;
    constexpr float HeaderY = 222.0f;
    constexpr float HeaderPixelSize = 2.6f;
    const Color HeaderColor(0.40f, 0.70f, 0.50f, 0.85f);
    // 行：1行目の位置、行の間隔、文字の大きさ、本文の不透明度、行ごとのずれの乱数の倍率
    constexpr float FirstLineY = 296.0f;
    constexpr float LineSpacing = 66.0f;
    constexpr float LinePixelSize = 3.6f;
    constexpr float LineAlpha = 0.92f;
    constexpr float LineJitterSeedStep = 3.7f;
    // 「通信途絶」：文字の大きさ、横のずれの幅、位置（受信記録の4行目 y = 494 の下）、帯の本数と太さの幅
    constexpr float SignalLostPixelSize = 5.5f;
    constexpr float SignalLostJitter = 30.0f;
    constexpr float SignalLostY = 586.0f;
    constexpr int SignalLostBars = 4;
    constexpr float SignalLostBarHeightRange = 4.0f;
    const Color SignalLostBarColor(0.0f, 0.004f, 0.003f, 0.9f);
    // 目的のパネル：左右の余白、幅の上限、高さ、中央から上へずらす量、地の色、左の縁の幅と色
    constexpr float MissionSideMargin = 120.0f;
    constexpr float MissionMaxWidth = 900.0f;
    constexpr float MissionPanelHeight = 340.0f;
    constexpr float MissionRaise = 20.0f;
    const Color MissionPanelColor(0.008f, 0.016f, 0.013f, 0.90f);
    constexpr float MissionAccentWidth = 6.0f;
    const Color MissionAccentColor(0.48f, 0.90f, 0.58f, 0.96f);
    // 目的のパネルの行：状況・目的・操作・始める操作の、大きさ・位置・色
    constexpr float LeadPixelSize = 3.0f;
    constexpr float LeadY = 44.0f;
    const Color LeadColor(0.80f, 0.86f, 0.82f, 0.95f);
    constexpr float GoalPixelSize = 3.6f;
    constexpr float GoalY = 112.0f;
    const Color GoalColor(0.52f, 0.92f, 0.60f, 1.0f);
    constexpr float ControlsPixelSize = 2.7f;
    constexpr float ControlsY = 186.0f;
    const Color ControlsColor(0.62f, 0.76f, 0.68f, 0.90f);
    constexpr float StartPixelSize = 3.0f;
    constexpr float StartY = 262.0f;
    // 右下の飛ばし方：大きさ、右端と下からの距離、色
    constexpr float SkipPixelSize = 1.8f;
    constexpr float SkipRightMargin = 60.0f;
    constexpr float SkipBottom = 70.0f;
    const Color SkipColor(0.50f, 0.62f, 0.55f, 0.75f);
}

// 1面の前の導入：後ろの森を暗く沈め、受信記録（見出しと、1行ずつタイプされる3行）を左寄せで、
// 「通信途絶」を中央に、最後に目的と操作を中央のパネルに描いている。noise（0〜1）で文字を横にずらし、色をにじませている
void Hud::DrawIntro(const IntroView& view)
{
    using namespace SignalTextLayout;
    using namespace IntroLayout;
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float noise = (std::clamp)(view.Noise, 0.0f, 1.0f);
    const auto textWidth = [](std::string_view text, float pixelSize)
    {
        return static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * GlyphAdvance;
    };
    const float noiseStep = std::floor(view.Time * NoiseStepsPerSecond);

    // 玄関から来たら、森での暗転を黒い背景につなぐ。受信記録では後ろの森を暗く沈める。
    const Color background = view.HideBackdrop
        ? Color(0.0f, 0.0f, 0.0f, 1.0f)
        : Color(BackdropColor.R(), BackdropColor.G(), BackdropColor.B(),
            BackdropColor.A() + MissionBackdropExtra * view.MissionAlpha);
    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, background);
    for (float y = 0.0f; y < screenHeight; y += ScanlineSpacing)
    {
        AddRectangle(0.0f, y, screenWidth, 1.0f, ScanlineColor);
    }

    // --- 受信記録：見出し（明滅する受信の印つき）と、タイプされる3行
    const float left = (std::max)(MinimumLeft, screenWidth * LeftRatio);
    if (view.HeaderAlpha > 0.0f)
    {
        const bool blink = std::fmod(view.Time, 1.0f) < BlinkOn;
        if (blink)
        {
            AddRectangle(left, MarkY, MarkSize, MarkSize, Faded(ReceiveMarkColor, view.HeaderAlpha));
        }
        AddText(left + HeaderOffset, HeaderY, view.Header, HeaderPixelSize, Faded(HeaderColor, view.HeaderAlpha));
    }
    if (view.LinesAlpha > 0.0f)
    {
        float y = FirstLineY;
        for (std::size_t index = 0; index < view.Lines.size(); ++index)
        {
            const std::string& line = view.Lines[index];
            if (!line.empty())
            {
                // 乱れているときは、行ごとに少しずつ横へずらしている
                const float jitter = noise > JitterNoiseLevel
                    ? (NoiseHash(noiseStep + static_cast<float>(index) * LineJitterSeedStep) - 0.5f) * JitterWidth * noise : 0.0f;
                if (noise > SplitNoiseLevel)
                {
                    AddText(left + jitter - SplitOffset, y, line, LinePixelSize, Faded(SplitRed, noise));
                }
                AddText(left + jitter, y, line, LinePixelSize, Faded(LineColor, LineAlpha * view.LinesAlpha));
            }
            y += LineSpacing;
        }
    }
    // 「通信途絶」：中央に赤く出し、色のにじみと横の帯で崩している
    if (view.SignalLostAlpha > 0.0f)
    {
        const float width = textWidth(view.SignalLost, SignalLostPixelSize);
        const float x = (screenWidth - width) * 0.5f + (NoiseHash(noiseStep) - 0.5f) * SignalLostJitter * noise;
        AddText(x - SignalLostSplit * noise, SignalLostY, view.SignalLost, SignalLostPixelSize,
            Faded(SignalLostCyan, noise * view.SignalLostAlpha));
        AddText(x, SignalLostY, view.SignalLost, SignalLostPixelSize, Faded(SignalLostRed, view.SignalLostAlpha));
        if (noise > BarNoiseLevel)
        {
            for (int bar = 0; bar < SignalLostBars; ++bar)
            {
                const float seed = noiseStep * BarSeedStep + static_cast<float>(bar) * BarSeedPerBar;
                AddRectangle(x - BarLeftMargin, SignalLostY + NoiseHash(seed) * SignalLostPixelSize * GlyphRows,
                    width + BarExtraWidth, BarHeightBase + NoiseHash(seed + 1.0f) * SignalLostBarHeightRange,
                    SignalLostBarColor);
            }
        }
    }

    // --- 目的の画面：中央のパネルに、状況・目的・操作・始める操作を並べている
    if (view.MissionAlpha > 0.0f)
    {
        const float alpha = view.MissionAlpha;
        const float panelWidth = (std::min)(screenWidth - MissionSideMargin, MissionMaxWidth);
        const float panelX = (screenWidth - panelWidth) * 0.5f;
        const float panelY = (screenHeight - MissionPanelHeight) * 0.5f - MissionRaise;
        AddRectangle(panelX, panelY, panelWidth, MissionPanelHeight, Faded(MissionPanelColor, alpha));
        AddRectangle(panelX, panelY, MissionAccentWidth, MissionPanelHeight, Faded(MissionAccentColor, alpha));

        const auto centered = [&](std::string_view text, float size, float y, const Color& color)
        {
            AddText((screenWidth - textWidth(text, size)) * 0.5f, y, text, size, color);
        };
        centered(view.MissionLead, LeadPixelSize, panelY + LeadY, Faded(LeadColor, alpha));
        centered(view.MissionGoal, GoalPixelSize, panelY + GoalY, Faded(GoalColor, alpha));
        const bool controller = Input::IsControllerConnected();
        centered(controller ? "Y でライト　A で調べる　迷ったら LB でヒント" : "F でライト　E で調べる　迷ったら H でヒント",
            ControlsPixelSize, panelY + ControlsY, Faded(ControlsColor, alpha));
        // 始める操作は、タイトル画面の「開始」と同じ明滅にしている
        const float pulse = std::sin(view.Time * TitleLayout::PromptPulseRate) * 0.5f + 0.5f;
        const Color& base = TitleLayout::PromptBaseColor;
        const Color& range = TitleLayout::PromptPulseRange;
        centered(controller ? "A で始める" : "ENTER で始める", StartPixelSize, panelY + StartY,
            Color(base.R() + pulse * range.R(), base.G() + pulse * range.G(), base.B() + pulse * range.B(),
                (base.A() + pulse * range.A()) * alpha));
    }
    else if (!view.HideBackdrop)
    {
        // 受信記録の間は、右下に飛ばし方を小さく出している
        const std::string_view skip = Input::IsControllerConnected() ? "A で飛ばす" : "ENTER で飛ばす";
        AddText(screenWidth - textWidth(skip, SkipPixelSize) - SkipRightMargin, screenHeight - SkipBottom, skip, SkipPixelSize,
            SkipColor);
    }

    Flush();
}

namespace RadioLayout
{
    // 帯：左右の余白、幅の上限、高さ、下からの位置、地の色、左の縁の幅
    constexpr float SideMargin = 120.0f;
    constexpr float MaxWidth = 1060.0f;
    constexpr float PanelHeight = 104.0f;
    constexpr float PanelBottom = 250.0f;
    const Color PanelColor(0.0f, 0.006f, 0.004f, 0.62f);
    constexpr float AccentWidth = 4.0f;
    // 印と見出しの色（無線は緑、録音は琥珀色）と、縁・印・見出しの不透明度
    const Color RecordingAccent(0.95f, 0.66f, 0.26f, 1.0f);
    const Color RadioAccent(0.48f, 0.90f, 0.58f, 1.0f);
    const Color RecordingHeader(0.78f, 0.58f, 0.32f, 1.0f);
    const Color RadioHeader(0.40f, 0.70f, 0.50f, 1.0f);
    constexpr float AccentAlpha = 0.85f;
    constexpr float MarkAlpha = 0.90f;
    constexpr float HeaderAlpha = 0.85f;
    // 印の位置と大きさ、見出しの位置と大きさ、1行の位置と大きさ、本文の不透明度
    constexpr float MarkX = 24.0f;
    constexpr float MarkY = 20.0f;
    constexpr float MarkSize = 10.0f;
    constexpr float HeaderX = 42.0f;
    constexpr float HeaderY = 13.0f;
    constexpr float HeaderPixelSize = 2.2f;
    constexpr float LineY = 50.0f;
    constexpr float LinePixelSize = 3.2f;
    constexpr float LineAlpha = 0.95f;
    // 「通信途絶」：文字の大きさ、下からの位置、帯の本数と太さの幅、帯の色
    constexpr float SignalLostPixelSize = 4.4f;
    constexpr float SignalLostBottom = 228.0f;
    constexpr int SignalLostBars = 3;
    constexpr float SignalLostBarHeightRange = 3.0f;
    const Color SignalLostBarColor(0.0f, 0.004f, 0.003f, 0.85f);
}

// 無線と録音の字幕（外の森の無線、1面の録音と無線）：画面の下の中央に、暗い帯と、受信の印つきの見出し、タイプされる1行を出している。
// 「通信途絶」は帯の上に赤く出す。noise（0〜1）で文字を横にずらし、色をにじませている
void Hud::DrawRadio(const RadioView& view)
{
    using namespace SignalTextLayout;
    using namespace RadioLayout;
    if (view.LineAlpha <= 0.0f && view.SignalLostAlpha <= 0.0f)
    {
        return;
    }
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float noise = (std::clamp)(view.Noise, 0.0f, 1.0f);
    const auto textWidth = [](std::string_view text, float pixelSize)
    {
        return static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * GlyphAdvance;
    };
    const float noiseStep = std::floor(view.Time * NoiseStepsPerSecond);
    const float jitter = noise > JitterNoiseLevel ? (NoiseHash(noiseStep) - 0.5f) * JitterWidth * noise : 0.0f;

    if (view.LineAlpha > 0.0f)
    {
        const float alpha = view.LineAlpha;
        const float panelWidth = (std::min)(screenWidth - SideMargin, MaxWidth);
        const float panelX = (screenWidth - panelWidth) * 0.5f;
        const float panelY = screenHeight - PanelBottom;
        // 無線は緑、録音は琥珀色の印にしている
        const Color accent = view.Recording ? RecordingAccent : RadioAccent;
        const Color headerColor = view.Recording ? RecordingHeader : RadioHeader;
        AddRectangle(panelX, panelY, panelWidth, PanelHeight, Faded(PanelColor, alpha));
        AddRectangle(panelX, panelY, AccentWidth, PanelHeight, Color(accent.x, accent.y, accent.z, AccentAlpha * alpha));
        // 見出し：無線の受信中は印が明滅し、録音の再生中は点いたまま
        if (view.Recording || std::fmod(view.Time, 1.0f) < BlinkOn)
        {
            AddRectangle(panelX + MarkX, panelY + MarkY, MarkSize, MarkSize, Color(accent.x, accent.y, accent.z, MarkAlpha * alpha));
        }
        AddText(panelX + HeaderX, panelY + HeaderY, view.Header, HeaderPixelSize,
            Color(headerColor.x, headerColor.y, headerColor.z, HeaderAlpha * alpha));
        const float lineX = panelX + MarkX + jitter;
        const float lineY = panelY + LineY;
        if (noise > SplitNoiseLevel)
        {
            AddText(lineX - SplitOffset, lineY, view.Line, LinePixelSize, Faded(SplitRed, noise * alpha));
        }
        AddText(lineX, lineY, view.Line, LinePixelSize, Faded(LineColor, LineAlpha * alpha));
    }
    if (view.SignalLostAlpha > 0.0f)
    {
        const float width = textWidth(view.SignalLost, SignalLostPixelSize);
        const float x = (screenWidth - width) * 0.5f + jitter;
        const float y = screenHeight - SignalLostBottom;
        AddText(x - SignalLostSplit * noise, y, view.SignalLost, SignalLostPixelSize,
            Faded(SignalLostCyan, noise * view.SignalLostAlpha));
        AddText(x, y, view.SignalLost, SignalLostPixelSize, Faded(SignalLostRed, view.SignalLostAlpha));
        if (noise > BarNoiseLevel)
        {
            for (int bar = 0; bar < SignalLostBars; ++bar)
            {
                const float seed = noiseStep * BarSeedStep + static_cast<float>(bar) * BarSeedPerBar;
                AddRectangle(x - BarLeftMargin, y + NoiseHash(seed) * SignalLostPixelSize * GlyphRows, width + BarExtraWidth,
                    BarHeightBase + NoiseHash(seed + 1.0f) * SignalLostBarHeightRange, SignalLostBarColor);
            }
        }
    }

    Flush();
}
