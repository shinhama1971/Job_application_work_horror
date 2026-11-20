// ============================================================================
// ファイルの役割: プレイ中に重ねる画面（監視映像・章のカード・目的地の方向・2面の状態・隠れている間の視界・息を潜める進み具合・無線の字幕）を描いている。
// 主な技術: 画面ごとの描画関数、3Dの位置から画面の端への矢印、テクスチャを貼った四角形
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
