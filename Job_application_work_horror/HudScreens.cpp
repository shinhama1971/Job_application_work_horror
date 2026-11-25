// ============================================================================
// ファイルの役割: プレイ中に重ねる画面（監視映像・章のカード・目的地の方向）を描いている。2面の状態・隠れている間の視界・無線の字幕は HudStage2.cpp。
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
