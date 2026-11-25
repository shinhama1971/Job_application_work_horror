// ============================================================================
// ファイルの役割: 2面のHUD（状態の表示・隠れている間の視界・息を潜める進み具合）と、無線の字幕を描いている。
// （HudScreens.cpp から分けた）
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
    static_assert(std::size(SignalColors) == Stage2SignalTerminalCount);
    // 次に押す信号盤の色の暗さ（色に掛ける割合）と、そのときの不透明度
    constexpr float NextSignalDim = 0.58f;
    constexpr float NextSignalAlpha = 0.96f;
    // 左の縁の幅、文字の位置（字下げ・上）と大きさ
    constexpr float AccentWidth = 3.0f;
    constexpr float TextIndent = 14.0f;
    constexpr float TextTop = 8.0f;
    constexpr float TextPixelSize = 1.8f;
    // 区画の幅とすき間、上からの位置、高さ
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
    const int segmentCount = signalActive ? Stage2SignalTerminalCount : Stage2FinalLoopCount;
    const int safeCompletedLoops = (std::clamp)(completedLoops, 0, Stage2FinalLoopCount);
    for (int index = 0; index < segmentCount; ++index)
    {
        const bool segmentComplete = signalActive
            ? index < (std::clamp)(signalStep, 0, Stage2SignalTerminalCount)
            : index < safeCompletedLoops;
        Color segmentColor = segmentComplete
            ? (signalActive ? SignalColors[index] : active)
            : InactiveColor;
        if (signalActive && index == (std::clamp)(signalStep, 0, Stage2SignalTerminalCount - 1))
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
