// ============================================================================
// ファイルの役割: タイトル画面（題名・面の選択・操作と記録・受信表示）と、1面の前の導入の画面を描いている。
// 主な技術: 画面の大きさに合わせた配置、電波が乱れたような文字のずれと色のにじみ、選んでいる項目の強調表示
// ============================================================================

#include "Hud.h"
#include "HudText.h"
#include "TitleStageChoices.h"
#include "StoryText.h"
#include "Stage2SceneConstants.h"

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
using namespace HudText;

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

    // 「開始」の文字と、選んでいる面の縁を、ゆっくり明滅させている
    const float pulse = std::sin(time * PromptPulseRate) * 0.5f + 0.5f;
    const Color promptColor(
        PromptBaseColor.R() + pulse * PromptPulseRange.R(),
        PromptBaseColor.G() + pulse * PromptPulseRange.G(),
        PromptBaseColor.B() + pulse * PromptPulseRange.B(),
        PromptBaseColor.A() + pulse * PromptPulseRange.A());
    noise = (std::clamp)(noise, 0.0f, 1.0f);

    DrawTitleBackground();
    DrawTitleLogo(time, noise);
    const float afterStages = DrawTitleStageMenu(selectedStage, promptColor);
    DrawTitleFooter(afterStages, promptColor, hasClearRecord, bestClearTimeSeconds, bestCaughtCount);
    DrawTitleStatus(time, noise);

    Flush();
}

// 左側の暗い帯・画面全体の薄い走査線・左端の緑の線
void Hud::DrawTitleBackground()
{
    using namespace TitleLayout;
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();

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
}

// 題名：通常は静かに表示し、ノイズのときに文字を横へずらし、色をにじませ、横の帯を抜いている
void Hud::DrawTitleLogo(float time, float noise)
{
    using namespace TitleLayout;
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
}

// 始める面の選択：縦に並べ（TitleStageChoices の数だけ）、選んでいる面の枠を明るくし、その下に説明を出している。
// 並びの下の端の高さを返している（その下に操作と記録を並べる）
float Hud::DrawTitleStageMenu(int selectedStage, const Color& promptColor)
{
    using namespace TitleLayout;
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
    return afterStages;
}

// 開始と終了の操作・クリアの記録・左下の操作の説明
void Hud::DrawTitleFooter(float afterStages, const Color& promptColor,
    bool hasClearRecord, float bestClearTimeSeconds, int bestCaughtCount)
{
    using namespace TitleLayout;
    const float screenHeight = GetCanvasHeight();

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
}

// 右上：監視映像のような受信表示（「受信中」と、明滅する印と、時間）。ノイズのときは「信号なし」にしている
void Hud::DrawTitleStatus(float time, float noise)
{
    using namespace TitleLayout;
    const float screenWidth = GetCanvasWidth();

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
