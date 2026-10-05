// ============================================================================
// ファイルの役割: タイトル、結果、目的ガイド、ポーズなどの画面別UIを描画します。
// 主な技術: 画面状態機械、レスポンシブ配置、入力フォーカス
// ============================================================================

#include "Hud.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <windows.h>

#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;

namespace
{
    size_t CountDisplayedCharacters(std::string_view text)
    {
        size_t count = 0;
        for (unsigned char byte : text)
        {
            if ((byte & 0xC0u) != 0x80u)
            {
                ++count;
            }
        }
        return count;
    }
}

void Hud::DrawTitle(
    float time,
    bool hasClearRecord,
    float bestClearTimeSeconds,
    int bestCaughtCount)
{
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const Color background(0.002f, 0.004f, 0.004f, 1.0f);
    const Color dimGreen(0.08f, 0.20f, 0.14f, 0.50f);
    const Color titleGreen(0.48f, 0.90f, 0.58f, 0.96f);
    const float pulse = std::sin(time * 2.1f) * 0.5f + 0.5f;
    const Color promptColor(
        0.58f + pulse * 0.16f,
        0.70f + pulse * 0.18f,
        0.62f + pulse * 0.14f,
        0.62f + pulse * 0.34f);

    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, background);

    for (float y = 0.0f; y < screenHeight; y += 7.0f)
    {
        AddRectangle(
            0.0f,
            y,
            screenWidth,
            1.0f,
            Color(0.08f, 0.16f, 0.11f, 0.075f));
    }

    const float panelWidth = (std::min)(screenWidth * 0.76f, 980.0f);
    const float panelHeight = 430.0f;
    const float panelX = (screenWidth - panelWidth) * 0.5f;
    const float panelY = (screenHeight - panelHeight) * 0.5f;
    AddRectangle(panelX, panelY, panelWidth, panelHeight,
        Color(0.008f, 0.016f, 0.013f, 0.96f));
    AddRectangle(panelX, panelY, 6.0f, panelHeight, titleGreen);
    AddRectangle(panelX, panelY, panelWidth, 2.0f, dimGreen);
    AddRectangle(panelX, panelY + panelHeight - 2.0f,
        panelWidth, 2.0f, dimGreen);

    constexpr std::string_view title = "通信途絶";
    constexpr float titlePixelSize = 9.0f;
    const float titleWidth =
        static_cast<float>(CountDisplayedCharacters(title)) *
        titlePixelSize * 6.0f;
    AddText(
        (screenWidth - titleWidth) * 0.5f,
        panelY + 72.0f,
        title,
        titlePixelSize,
        titleGreen);

    constexpr std::string_view subtitle = "終わらない廊下から脱出する";
    constexpr float subtitlePixelSize = 3.0f;
    const float subtitleWidth =
        static_cast<float>(CountDisplayedCharacters(subtitle)) *
        subtitlePixelSize * 6.0f;
    AddText((screenWidth - subtitleWidth) * 0.5f,
        panelY + 148.0f, subtitle, subtitlePixelSize,
        Color(0.42f, 0.58f, 0.48f, 0.74f));

    const bool controller = Input::IsControllerConnected();
    const std::string_view controls1 = controller
        ? "左スティック 移動  右スティック 視点  A 調べる"
        : "WASD 移動  マウス 視点  E 調べる";
    const std::string_view controls2 = controller
        ? "左スティック押し込み 走る  Y ライト"
        : "SHIFT 走る  F ライト";
    const std::string_view controls3 = controller
        ? "LB ヒント  START ポーズ"
        : "H ヒント  ESC または P ポーズ";
    constexpr float controlsPixelSize = 2.0f;
    const Color controlsColor(0.48f, 0.66f, 0.55f, 0.82f);
    const float controls1Width =
        static_cast<float>(CountDisplayedCharacters(controls1)) *
        controlsPixelSize * 6.0f;
    const float controls2Width =
        static_cast<float>(CountDisplayedCharacters(controls2)) *
        controlsPixelSize * 6.0f;
    const float controls3Width =
        static_cast<float>(CountDisplayedCharacters(controls3)) *
        controlsPixelSize * 6.0f;
    AddText((screenWidth - controls1Width) * 0.5f,
        panelY + 198.0f, controls1, controlsPixelSize, controlsColor);
    AddText((screenWidth - controls2Width) * 0.5f,
        panelY + 224.0f, controls2, controlsPixelSize, controlsColor);
    AddText((screenWidth - controls3Width) * 0.5f,
        panelY + 250.0f, controls3, controlsPixelSize, controlsColor);

    if (hasClearRecord)
    {
        const int bestSeconds = (std::max)(
            0, static_cast<int>(bestClearTimeSeconds + 0.5f));
        const std::string recordText =
            "最短記録 " + std::to_string(bestSeconds / 60) +
            "分 " + std::to_string(bestSeconds % 60) +
            "秒   最少捕獲 " +
            std::to_string((std::max)(bestCaughtCount, 0)) + "回";
        constexpr float recordPixelSize = 2.0f;
        const float recordWidth =
            static_cast<float>(CountDisplayedCharacters(recordText)) *
            recordPixelSize * 6.0f;
        AddText(
            (screenWidth - recordWidth) * 0.5f,
            panelY + 286.0f,
            recordText,
            recordPixelSize,
            Color(0.72f, 0.78f, 0.48f, 0.88f));
    }

    const std::string_view guidance = Input::IsControllerConnected()
        ? "目的表示に従う  迷ったらLBでヒント"
        : "目的表示に従う  迷ったらHでヒント";
    constexpr float guidancePixelSize = 1.7f;
    const float guidanceWidth =
        static_cast<float>(CountDisplayedCharacters(guidance)) *
        guidancePixelSize * 6.0f;
    AddText((screenWidth - guidanceWidth) * 0.5f,
        panelY + 310.0f, guidance, guidancePixelSize, controlsColor);

    const std::string_view prompt = Input::IsControllerConnected()
        ? "Aでゲーム開始"
        : "ENTERでゲーム開始";
    constexpr float promptPixelSize = 3.0f;
    const float promptWidth =
        static_cast<float>(CountDisplayedCharacters(prompt)) *
        promptPixelSize * 6.0f;
    AddText((screenWidth - promptWidth) * 0.5f,
        panelY + 338.0f, prompt, promptPixelSize, promptColor);

    const std::string_view quitPrompt = Input::IsControllerConnected()
        ? "Bでゲーム終了"
        : "Qでゲーム終了";
    constexpr float quitPixelSize = 2.0f;
    const float quitWidth =
        static_cast<float>(CountDisplayedCharacters(quitPrompt)) *
        quitPixelSize * 6.0f;
    AddText((screenWidth - quitWidth) * 0.5f,
        panelY + 382.0f, quitPrompt, quitPixelSize, controlsColor);

    Flush();
}

void Hud::DrawResult(
    float revealAmount,
    float clearTimeSeconds,
    int caughtCount,
    int anomaliesHandled,
    int puzzleMistakes,
    int chargersUsed,
    int evidenceCollected,
    int wallWritingsRead,
    bool hiddenRoomEscaped,
    std::string_view stage2Anomalies,
    bool newBestTime,
    bool newBestCaught)
{
    m_Vertices.clear();

    const float reveal = (std::clamp)(revealAmount, 0.0f, 1.0f);
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const Color background(0.004f, 0.007f, 0.006f, 1.0f);
    const Color panel(0.018f, 0.028f, 0.025f, 0.94f * reveal);
    const Color green(0.48f, 0.90f, 0.56f, 0.96f * reveal);
    const Color pale(0.74f, 0.82f, 0.76f, 0.78f * reveal);

    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, background);

    for (float y = 0.0f; y < screenHeight; y += 8.0f)
    {
        AddRectangle(
            0.0f,
            y,
            screenWidth,
            1.0f,
            Color(0.10f, 0.16f, 0.12f, 0.075f * reveal));
    }

    const float panelWidth = (std::min)(screenWidth * 0.72f, 860.0f);
    // 下段に「今回の発見」（壁の文字・隠し部屋・今回の異変）を並べるため、縦に広げています。
    const float panelHeight = 680.0f;
    const float panelX = (screenWidth - panelWidth) * 0.5f;
    const float panelY = (screenHeight - panelHeight) * 0.5f;
    AddRectangle(panelX, panelY, panelWidth, panelHeight, panel);
    AddRectangle(panelX, panelY, 6.0f, panelHeight, green);
    AddRectangle(panelX, panelY, panelWidth, 2.0f, green);

    constexpr std::string_view title = "脱出成功";
    constexpr float titlePixelSize = 7.0f;
    const float titleWidth =
        static_cast<float>(CountDisplayedCharacters(title)) *
        titlePixelSize * 6.0f;
    AddText(
        (screenWidth - titleWidth) * 0.5f,
        panelY + 48.0f,
        title,
        titlePixelSize,
        green);

    constexpr std::string_view floorText = "2階をクリア";
    constexpr float floorPixelSize = 3.0f;
    const float floorWidth =
        static_cast<float>(CountDisplayedCharacters(floorText)) * floorPixelSize * 6.0f;
    AddText(
        (screenWidth - floorWidth) * 0.5f,
        panelY + 116.0f,
        floorText,
        floorPixelSize,
        pale);

    const int clearSeconds = (std::max)(
        0, static_cast<int>(clearTimeSeconds + 0.5f));
    const int minutes = clearSeconds / 60;
    const int seconds = clearSeconds % 60;
    const int safeCaughtCount = (std::max)(caughtCount, 0);
    const int safeAnomaliesHandled = (std::max)(anomaliesHandled, 0);
    const int safePuzzleMistakes = (std::max)(puzzleMistakes, 0);
    const int safeChargersUsed = (std::max)(chargersUsed, 0);
    constexpr int totalEvidence = Core::GameState::TotalEvidenceCount;
    constexpr int totalWritings = Core::GameState::TotalWallWritingCount;
    const int safeEvidenceCollected = (std::clamp)(evidenceCollected, 0, totalEvidence);
    const int safeWritingsRead = (std::clamp)(wallWritingsRead, 0, totalWritings);
    const int timePenalty = (std::min)(
        (std::max)(clearSeconds - 360, 0) / 15, 40);
    const int caughtPenalty = (std::min)(safeCaughtCount * 15, 45);
    const int mistakePenalty = (std::min)(safePuzzleMistakes * 5, 25);
    const int anomalyBonus = (std::min)(safeAnomaliesHandled * 2, 8);
    const int evidenceBonus = safeEvidenceCollected * 3;
    const int performanceScore = (std::clamp)(
        100 - timePenalty - caughtPenalty - mistakePenalty +
            anomalyBonus + evidenceBonus,
        0,
        100);
    const char rank = performanceScore >= 90
        ? 'S'
        : (performanceScore >= 75
            ? 'A'
            : (performanceScore >= 55 ? 'B' : 'C'));
    const std::string rankText = "評価 " + std::string(1, rank);
    const Color rankColor = rank == 'S'
        ? Color(0.92f, 0.78f, 0.34f, 0.98f * reveal)
        : (rank == 'A'
            ? green
            : (rank == 'B'
                ? pale
                : Color(0.74f, 0.42f, 0.34f, 0.92f * reveal)));
    constexpr float rankPixelSize = 5.0f;
    const float rankWidth =
        static_cast<float>(CountDisplayedCharacters(rankText)) * rankPixelSize * 6.0f;
    AddText(
        (screenWidth - rankWidth) * 0.5f,
        panelY + 158.0f,
        rankText,
        rankPixelSize,
        rankColor);

    const std::string timeText =
        "クリア時間 " + std::to_string(minutes) + "分 " +
        std::to_string(seconds) + "秒";
    const std::string caughtText =
        "捕まった回数 " + std::to_string(safeCaughtCount) + "回";
    const std::string anomalyText =
        "怪異への対処 " + std::to_string(safeAnomaliesHandled) + "回";
    const std::string mistakeText =
        "観察の失敗 " + std::to_string(safePuzzleMistakes) + "回";
    const std::string chargerText =
        "充電器の使用 " + std::to_string(safeChargersUsed) + "回";
    const std::string evidenceText =
        "残された記録 " + std::to_string(safeEvidenceCollected) + " / " +
        std::to_string(totalEvidence);
    constexpr float statPixelSize = 2.5f;
    const float timeWidth =
        static_cast<float>(CountDisplayedCharacters(timeText)) * statPixelSize * 6.0f;
    const float caughtWidth =
        static_cast<float>(CountDisplayedCharacters(caughtText)) * statPixelSize * 6.0f;
    const float anomalyWidth =
        static_cast<float>(CountDisplayedCharacters(anomalyText)) * statPixelSize * 6.0f;
    const float mistakeWidth =
        static_cast<float>(CountDisplayedCharacters(mistakeText)) * statPixelSize * 6.0f;
    const float chargerWidth =
        static_cast<float>(CountDisplayedCharacters(chargerText)) * statPixelSize * 6.0f;
    const float evidenceWidth =
        static_cast<float>(CountDisplayedCharacters(evidenceText)) * statPixelSize * 6.0f;
    AddText(
        (screenWidth - timeWidth) * 0.5f,
        panelY + 232.0f,
        timeText,
        statPixelSize,
        pale);
    AddText(
        (screenWidth - caughtWidth) * 0.5f,
        panelY + 266.0f,
        caughtText,
        statPixelSize,
        pale);
    AddText(
        (screenWidth - anomalyWidth) * 0.5f,
        panelY + 300.0f,
        anomalyText,
        statPixelSize,
        pale);
    AddText(
        (screenWidth - mistakeWidth) * 0.5f,
        panelY + 334.0f,
        mistakeText,
        statPixelSize,
        pale);
    AddText(
        (screenWidth - chargerWidth) * 0.5f,
        panelY + 368.0f,
        chargerText,
        statPixelSize,
        pale);
    AddText(
        (screenWidth - evidenceWidth) * 0.5f,
        panelY + 402.0f,
        evidenceText,
        statPixelSize,
        safeEvidenceCollected >= totalEvidence
            ? Color(0.92f, 0.78f, 0.34f, 0.96f * reveal)
            : pale);

    // 今回の発見。プレイごとに変わる要素と任意の探索を並べ、もう一度遊ぶ理由にします。
    const Color gold(0.92f, 0.78f, 0.34f, 0.96f * reveal);
    const Color dim(0.50f, 0.58f, 0.52f, 0.80f * reveal);
    const auto addCenteredStat = [this, screenWidth](
        float y, std::string_view text, float pixelSize, const Color& color)
    {
        const float width =
            static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * 6.0f;
        AddText((screenWidth - width) * 0.5f, y, text, pixelSize, color);
    };
    AddRectangle(panelX + 60.0f, panelY + 446.0f, panelWidth - 120.0f, 1.0f, dim);
    addCenteredStat(panelY + 458.0f, "今回の発見", 2.2f, dim);
    const std::string writingText =
        "壁の文字 " + std::to_string(safeWritingsRead) + " / " + std::to_string(totalWritings);
    addCenteredStat(panelY + 490.0f, writingText, statPixelSize,
        safeWritingsRead >= totalWritings ? gold : pale);
    addCenteredStat(panelY + 524.0f,
        hiddenRoomEscaped ? "隠し部屋 脱出した" : "隠し部屋 見つけていない", statPixelSize,
        hiddenRoomEscaped ? gold : pale);
    if (!stage2Anomalies.empty())
    {
        addCenteredStat(panelY + 558.0f,
            "2階の異変 " + std::string(stage2Anomalies), statPixelSize, pale);
    }

    std::string_view achievement = "";
    if (safeEvidenceCollected >= totalEvidence && safeWritingsRead >= totalWritings &&
        hiddenRoomEscaped && safeCaughtCount == 0 && safePuzzleMistakes == 0)
    {
        achievement = "完全探索で脱出";
    }
    else if (newBestTime && newBestCaught)
    {
        achievement = "自己記録を更新";
    }
    else if (newBestTime)
    {
        achievement = "最速クリア記録を更新";
    }
    else if (newBestCaught)
    {
        achievement = "最少捕獲記録を更新";
    }
    else if (safeCaughtCount == 0)
    {
        achievement = "一度も捕まらず脱出";
    }
    if (!achievement.empty())
    {
        constexpr float achievementPixelSize = 2.2f;
        const float achievementWidth = static_cast<float>(
            CountDisplayedCharacters(achievement)) *
            achievementPixelSize * 6.0f;
        AddText(
            (screenWidth - achievementWidth) * 0.5f,
            panelY + 598.0f,
            achievement,
            achievementPixelSize,
            Color(0.92f, 0.78f, 0.34f, 0.96f * reveal));
    }

    const std::string_view prompt = Input::IsControllerConnected()
        ? "A タイトルへ   X もう一度"
        : "ENTER タイトルへ   R もう一度";
    constexpr float promptPixelSize = 3.0f;
    const float promptWidth =
        static_cast<float>(CountDisplayedCharacters(prompt)) *
        promptPixelSize * 6.0f;
    AddText(
        (screenWidth - promptWidth) * 0.5f,
        panelY + 638.0f,
        prompt,
        promptPixelSize,
        pale);

    Flush();
}

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
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float availableHeight = (std::max)(screenHeight - 156.0f, 300.0f);
    const float feedWidth = (std::min)({
        screenWidth * 0.90f,
        1280.0f,
        availableHeight * 16.0f / 9.0f });
    const float feedHeight = feedWidth * 9.0f / 16.0f;
    const float feedX = (screenWidth - feedWidth) * 0.5f;
    const float feedY = (screenHeight - feedHeight) * 0.5f + 8.0f;

    m_Vertices.clear();
    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight,
        Color(0.0f, 0.004f, 0.003f, 0.94f));
    Flush();

    DrawTextureRectangle(
        feed, textureShader, feedX, feedY, feedWidth, feedHeight);

    m_Vertices.clear();
    const Color green(0.38f, 0.94f, 0.55f, 0.96f);
    const Color pale(0.76f, 0.88f, 0.80f, 0.94f);
    const Color dark(0.005f, 0.018f, 0.012f, 0.88f);
    AddRectangle(feedX - 5.0f, feedY - 5.0f,
        feedWidth + 10.0f, 5.0f, green);
    AddRectangle(feedX - 5.0f, feedY + feedHeight,
        feedWidth + 10.0f, 5.0f, green);
    AddRectangle(feedX - 5.0f, feedY, 5.0f, feedHeight, green);
    AddRectangle(feedX + feedWidth, feedY, 5.0f, feedHeight, green);
    AddRectangle(feedX, feedY, feedWidth, 62.0f, dark);
    AddText(feedX + 18.0f, feedY + 10.0f,
        std::string(cameraLabel) +
            (showingReference ? "   基準映像" : "   LIVE"),
        2.5f, green);
    AddText(feedX + 18.0f, feedY + 37.0f,
        showingReference
            ? "異常発生前の映像   C / Y でライブへ戻る"
            : (zoomed
                ? "暗視補正 ON   拡大中   Z / X で標準画角へ"
                : "暗視補正 ON   C / Y 基準映像   Z / X 拡大"),
        1.9f, Color(0.66f, 0.85f, 0.71f, 0.95f));

    // 右上に巡回の進み具合と、捕獲までの残り猶予を表示します。
    const std::string status =
        "巡回 " + std::to_string(roundsCleared) + " / " +
        std::to_string(requiredRounds);
    constexpr float statusSize = 2.0f;
    const float statusWidth =
        static_cast<float>(CountDisplayedCharacters(status)) * statusSize * 6.0f;
    const float warningBoxSize = 12.0f;
    const float warningWidth =
        static_cast<float>(mistakesUntilCaught) * (warningBoxSize + 6.0f);
    const float statusRight = feedX + feedWidth - 18.0f;
    AddText(statusRight - warningWidth - 16.0f - statusWidth,
        feedY + 10.0f, status, statusSize, pale);
    for (int warning = 0; warning < mistakesUntilCaught; ++warning)
    {
        const bool used = warning < mistakes;
        AddRectangle(
            statusRight - warningWidth +
                static_cast<float>(warning) * (warningBoxSize + 6.0f),
            feedY + 13.0f, warningBoxSize, warningBoxSize,
            used
                ? Color(1.0f, 0.24f, 0.18f, 0.96f)
                : Color(0.20f, 0.34f, 0.26f, 0.80f));
    }

    // 下端にカメラの並びを表示し、切り替えられることを伝えます。
    const float indicatorWidth = 28.0f;
    const float indicatorGap = 8.0f;
    const float indicatorsWidth =
        static_cast<float>(cameraCount) * (indicatorWidth + indicatorGap) -
        indicatorGap;
    for (int camera = 0; camera < cameraCount; ++camera)
    {
        AddRectangle(
            (screenWidth - indicatorsWidth) * 0.5f +
                static_cast<float>(camera) * (indicatorWidth + indicatorGap),
            feedY + feedHeight - 14.0f, indicatorWidth, 5.0f,
            camera == cameraIndex ? green : Color(0.20f, 0.34f, 0.26f, 0.70f));
    }

    for (int line = 0; line < 8; ++line)
    {
        const float offset = std::fmod(
            static_cast<float>(line * 73) + elapsedSeconds * 46.0f,
            (std::max)(feedHeight - 69.0f, 1.0f));
        AddRectangle(feedX, feedY + 64.0f + offset,
            feedWidth, 1.0f,
            Color(0.38f, 0.85f, 0.52f, 0.10f));
    }

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
    constexpr float promptSize = 2.8f;
    const float promptWidth =
        static_cast<float>(CountDisplayedCharacters(prompt)) *
        promptSize * 6.0f;
    AddRectangle(feedX, feedY + feedHeight + 18.0f,
        feedWidth, 54.0f, dark);
    AddText((screenWidth - promptWidth) * 0.5f,
        feedY + feedHeight + 34.0f, prompt, promptSize,
        wrongReportVisible
            ? Color(1.0f, 0.30f, 0.22f, 0.96f)
            : (reportReady ? pale : green));
    Flush();
}

void Hud::DrawChapterCard(
    std::string_view chapter,
    std::string_view subtitle,
    float elapsedSeconds)
{
    constexpr float fadeInDuration = 0.45f;
    constexpr float fadeOutStart = 3.15f;
    constexpr float totalDuration = 4.20f;
    float visibility = 1.0f;
    if (elapsedSeconds < fadeInDuration)
    {
        visibility = elapsedSeconds / fadeInDuration;
    }
    else if (elapsedSeconds > fadeOutStart)
    {
        visibility =
            (totalDuration - elapsedSeconds) /
            (totalDuration - fadeOutStart);
    }
    visibility = (std::clamp)(visibility, 0.0f, 1.0f);
    if (visibility <= 0.0f)
    {
        return;
    }

    m_Vertices.clear();
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float panelWidth = (std::min)(screenWidth * 0.72f, 760.0f);
    const float panelHeight = 142.0f;
    const float panelX = (screenWidth - panelWidth) * 0.5f;
    const float panelY = screenHeight * 0.5f - 88.0f;
    const Color shade(0.0f, 0.004f, 0.003f, 0.22f * visibility);
    const Color panel(0.008f, 0.016f, 0.013f, 0.88f * visibility);
    const Color accent(0.48f, 0.88f, 0.57f, 0.94f * visibility);
    const Color pale(0.70f, 0.78f, 0.72f, 0.86f * visibility);

    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, shade);
    AddRectangle(panelX, panelY, panelWidth, panelHeight, panel);
    AddRectangle(panelX, panelY, 5.0f, panelHeight, accent);
    AddRectangle(panelX, panelY, panelWidth, 2.0f, accent);

    constexpr float chapterPixelSize = 4.0f;
    constexpr float subtitlePixelSize = 2.5f;
    const float chapterWidth =
        static_cast<float>(chapter.size()) * chapterPixelSize * 6.0f;
    const float subtitleWidth =
        static_cast<float>(subtitle.size()) * subtitlePixelSize * 6.0f;
    AddText((screenWidth - chapterWidth) * 0.5f,
        panelY + 28.0f, chapter, chapterPixelSize, accent);
    AddText((screenWidth - subtitleWidth) * 0.5f,
        panelY + 91.0f, subtitle, subtitlePixelSize, pale);
    Flush();
}

void Hud::DrawObjectiveGuide(
    const Camera& camera,
    const Vector3& origin,
    const Vector3& target)
{
    Vector3 toTarget = target - origin;
    toTarget.y = 0.0f;
    const float distance = toTarget.Length();
    if (distance <= 0.001f)
    {
        return;
    }
    toTarget /= distance;

    Vector3 forward = camera.GetForward();
    forward.y = 0.0f;
    const float forwardLength = forward.Length();
    if (forwardLength <= 0.001f)
    {
        return;
    }
    forward /= forwardLength;

    std::string_view direction = "前方";
    const float facing = forward.Dot(toTarget);
    const float side = forward.z * toTarget.x - forward.x * toTarget.z;
    if (distance < 28.0f)
    {
        direction = "近く";
    }
    else if (facing < -0.55f)
    {
        direction = "後方";
    }
    else if (facing < 0.65f)
    {
        direction = side >= 0.0f ? "右" : "左";
    }

    const std::string text =
        "目的地 " + std::string(direction) +
        (Input::IsControllerConnected() ? "   LB ヒント" : "   H ヒント");
    constexpr float pixelSize = 2.0f;
    const float textWidth =
        static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * 6.0f;
    const float panelWidth = textWidth + 28.0f;
    const float panelX = 34.0f;
    const float panelY = 76.0f;

    m_Vertices.clear();
    AddRectangle(panelX, panelY, panelWidth, 30.0f,
        Color(0.004f, 0.010f, 0.008f, 0.72f));
    AddRectangle(panelX, panelY, 3.0f, 30.0f,
        Color(0.42f, 0.72f, 0.48f, 0.82f));
    AddText(panelX + 14.0f, panelY + 8.0f, text, pixelSize,
        Color(0.58f, 0.76f, 0.62f, 0.88f));
    Flush();
}

void Hud::DrawStage2Status(
    int completedLoops,
    float threatRate,
    bool exitReady,
    int signalStep,
    bool signalActive)
{
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    constexpr float panelWidth = 214.0f;
    constexpr float panelHeight = 58.0f;
    const float panelX = screenWidth - panelWidth - 34.0f;
    constexpr float panelY = 28.0f;
    const Color dark(0.004f, 0.010f, 0.008f, 0.72f);
    const Color inactive(0.12f, 0.15f, 0.14f, 0.88f);
    const Color active = exitReady
        ? Color(0.46f, 0.90f, 0.54f, 0.96f)
        : Color(0.80f, 0.66f, 0.38f, 0.94f);

    AddRectangle(panelX, panelY, panelWidth, panelHeight, dark);
    AddRectangle(panelX, panelY, 3.0f, panelHeight, active);
    AddText(panelX + 14.0f, panelY + 8.0f,
        exitReady ? "出口が開いた" :
            (signalActive ? "信号復旧 青 黄 赤" : "廊下の繰り返し"),
        1.8f, active);

    const int safeCompletedLoops = (std::clamp)(completedLoops, 0, 3);
    constexpr float segmentWidth = 52.0f;
    constexpr float segmentGap = 7.0f;
    for (int index = 0; index < 3; ++index)
    {
        const Color signalColors[] =
        {
            Color(0.12f, 0.52f, 0.96f, 0.96f),
            Color(0.96f, 0.68f, 0.14f, 0.96f),
            Color(0.94f, 0.18f, 0.10f, 0.96f)
        };
        const bool segmentComplete = signalActive
            ? index < (std::clamp)(signalStep, 0, 3)
            : index < safeCompletedLoops;
        Color segmentColor = segmentComplete
            ? (signalActive ? signalColors[index] : active)
            : inactive;
        if (signalActive && index == (std::clamp)(signalStep, 0, 2))
        {
            segmentColor = Color(
                signalColors[index].x * 0.58f,
                signalColors[index].y * 0.58f,
                signalColors[index].z * 0.58f,
                0.96f);
        }
        AddRectangle(
            panelX + 14.0f + static_cast<float>(index) *
                (segmentWidth + segmentGap),
            panelY + 35.0f,
            segmentWidth,
            10.0f,
            segmentColor);
    }

    const float safeThreatRate = (std::clamp)(threatRate, 0.0f, 1.0f);
    if (safeThreatRate > 0.0f)
    {
        constexpr float dangerPanelY = panelY + panelHeight + 8.0f;
        const Color dangerColor(
            0.78f + safeThreatRate * 0.18f,
            0.24f - safeThreatRate * 0.14f,
            0.14f - safeThreatRate * 0.08f,
            0.96f);
        AddRectangle(panelX, dangerPanelY, panelWidth, 40.0f, dark);
        AddRectangle(panelX, dangerPanelY, 3.0f, 40.0f, dangerColor);
        AddText(panelX + 14.0f, dangerPanelY + 7.0f,
            "危険", 1.8f, dangerColor);
        AddRectangle(panelX + 88.0f, dangerPanelY + 12.0f,
            108.0f, 10.0f, inactive);
        AddRectangle(panelX + 88.0f, dangerPanelY + 12.0f,
            108.0f * safeThreatRate, 10.0f, dangerColor);
    }

    Flush();
}

// 画面中央に4桁の入力盤を出します。選択中の桁は緑の枠で示し、上下の三角で増減できることを伝えます。
void Hud::DrawKeypad(
    const std::array<int, 4>& entered,
    int cursor,
    float wrongRate,
    int mistakes)
{
    m_Vertices.clear();
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    constexpr float panelWidth = 440.0f;
    constexpr float panelHeight = 300.0f;
    const float panelX = (screenWidth - panelWidth) * 0.5f;
    const float panelY = (screenHeight - panelHeight) * 0.5f;

    const Color green(0.48f, 0.90f, 0.58f, 0.96f);
    const Color pale(0.72f, 0.82f, 0.74f, 0.92f);
    const Color dim(0.30f, 0.38f, 0.33f, 0.90f);
    const Color red(0.96f, 0.22f, 0.14f, 0.96f);
    // 不正解の直後は枠を赤く点滅させます。
    const bool wrongFlash = wrongRate > 0.0f &&
        static_cast<int>(wrongRate * 7.0f) % 2 == 0;
    const Color frame = wrongFlash ? red : green;

    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, Color(0.0f, 0.004f, 0.004f, 0.55f));
    AddRectangle(panelX, panelY, panelWidth, panelHeight, Color(0.012f, 0.020f, 0.017f, 0.96f));
    AddRectangle(panelX, panelY, panelWidth, 3.0f, frame);
    AddRectangle(panelX, panelY + panelHeight - 3.0f, panelWidth, 3.0f, frame);

    const auto addCenteredText = [this, screenWidth](
        float y, std::string_view text, float pixelSize, const Color& color)
    {
        const float width =
            static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * 6.0f;
        AddText((screenWidth - width) * 0.5f, y, text, pixelSize, color);
    };
    addCenteredText(panelY + 20.0f, "暗証番号", 3.4f, pale);

    constexpr float boxWidth = 70.0f;
    constexpr float boxHeight = 92.0f;
    constexpr float boxGap = 20.0f;
    constexpr float digitPixelSize = 9.0f;
    const float rowWidth = boxWidth * 4.0f + boxGap * 3.0f;
    const float rowX = (screenWidth - rowWidth) * 0.5f;
    const float boxY = panelY + 88.0f;
    for (int index = 0; index < 4; ++index)
    {
        const float boxX = rowX + static_cast<float>(index) * (boxWidth + boxGap);
        const bool selected = index == cursor;
        const Color border = selected ? frame : dim;
        const float thickness = selected ? 4.0f : 2.0f;
        AddRectangle(boxX, boxY, boxWidth, boxHeight, Color(0.005f, 0.010f, 0.008f, 1.0f));
        AddRectangle(boxX, boxY, boxWidth, thickness, border);
        AddRectangle(boxX, boxY + boxHeight - thickness, boxWidth, thickness, border);
        AddRectangle(boxX, boxY, thickness, boxHeight, border);
        AddRectangle(boxX + boxWidth - thickness, boxY, thickness, boxHeight, border);

        const char digitText[2] = {
            static_cast<char>('0' + entered[static_cast<std::size_t>(index)]), '\0' };
        AddText(boxX + (boxWidth - digitPixelSize * 5.0f) * 0.5f,
            boxY + (boxHeight - digitPixelSize * 7.0f) * 0.5f,
            digitText, digitPixelSize, selected ? green : pale);

        if (selected)
        {
            // 上下の三角（増減できる合図）を、横幅の違う長方形を重ねて描きます。
            const float centerX = boxX + boxWidth * 0.5f;
            for (int step = 0; step < 4; ++step)
            {
                const float halfWidth = 2.0f + static_cast<float>(step) * 3.0f;
                AddRectangle(centerX - halfWidth, boxY - 22.0f + static_cast<float>(step) * 3.0f,
                    halfWidth * 2.0f, 3.0f, green);
                AddRectangle(centerX - halfWidth, boxY + boxHeight + 19.0f - static_cast<float>(step) * 3.0f,
                    halfWidth * 2.0f, 3.0f, green);
            }
        }
    }

    const std::string_view message = wrongRate > 0.0f
        ? (mistakes >= 3 ? "扉の向こうで何かが動いた" : "番号が違う")
        : "壁の数字を懐中電灯で探す";
    addCenteredText(panelY + 222.0f, message, 2.4f, wrongRate > 0.0f ? red : pale);
    addCenteredText(panelY + 256.0f,
        "←→ 桁  ↑↓ 数字  E 決定  Q 戻る", 2.0f, dim);
    Flush();
}

void Hud::DrawHidingView(float elapsedSeconds, float dangerRate)
{
    m_Vertices.clear();
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float danger = (std::clamp)(dangerRate, 0.0f, 1.0f);
    const Color dark(0.0f, 0.0f, 0.0f, 0.94f);
    const Color edge(0.10f + danger * 0.35f, 0.02f, 0.02f, 0.55f);

    // 画面の中央付近に4本の横長の隙間を残し、それ以外を扉の内側の暗さで覆います。
    // 呼吸に合わせて、隙間がわずかに上下します。
    constexpr int SlitCount = 4;
    const float slitHeight = screenHeight * 0.055f;
    const float slitGap = screenHeight * 0.035f;
    const float breath = std::sin(elapsedSeconds * 1.6f) * screenHeight * 0.004f;
    const float firstSlitY = screenHeight * 0.36f + breath;
    const float slitInset = screenWidth * 0.12f;

    float coveredY = 0.0f;
    for (int index = 0; index < SlitCount; ++index)
    {
        const float slitY = firstSlitY + static_cast<float>(index) * (slitHeight + slitGap);
        AddRectangle(0.0f, coveredY, screenWidth, slitY - coveredY, dark);
        AddRectangle(0.0f, slitY, slitInset, slitHeight, dark);
        AddRectangle(screenWidth - slitInset, slitY, slitInset, slitHeight, dark);
        AddRectangle(slitInset, slitY, screenWidth - slitInset * 2.0f, 2.0f, edge);
        AddRectangle(slitInset, slitY + slitHeight - 2.0f, screenWidth - slitInset * 2.0f, 2.0f, edge);
        coveredY = slitY + slitHeight;
    }
    AddRectangle(0.0f, coveredY, screenWidth, screenHeight - coveredY, dark);

    const std::string_view prompt = Input::IsControllerConnected()
        ? "A 外に出る"
        : "E 外に出る";
    constexpr float promptPixelSize = 2.2f;
    const float promptWidth =
        static_cast<float>(CountDisplayedCharacters(prompt)) * promptPixelSize * 6.0f;
    AddText((screenWidth - promptWidth) * 0.5f, screenHeight - 90.0f,
        prompt, promptPixelSize, Color(0.62f, 0.70f, 0.66f, 0.80f));
    Flush();
}

void Hud::DrawQuietRecovery(float progressRate, float cooldown, bool success, bool tooClose)
{
    m_Vertices.clear();
    const float x = (std::max)(12.0f, GetCanvasWidth() - 330.0f);
    constexpr float y = 150.0f;
    const Color color = tooClose ? Color(0.96f, 0.38f, 0.25f, 1.0f)
        : Color(0.64f, 0.84f, 0.78f, 1.0f);
    AddRectangle(x, y, 296.0f, 76.0f, Color(0.005f, 0.012f, 0.015f, 0.85f));
    const std::string_view message = success ? "気配が遠のいた" :
        (tooClose ? "影が近い 距離を取るか光で対処" :
        (cooldown > 0.0f ? "呼吸を整えている" : "消灯して2秒止まると気配を抑える"));
    AddText(x + 10.0f, y + 10.0f, message, 1.6f, color);
    AddText(x + 10.0f, y + 31.0f,
        success ? "静かに歩いて探索を続ける" : "移動や点灯で静止の計測はやり直し", 1.4f, color);
    AddRectangle(x + 10.0f, y + 58.0f, 276.0f, 6.0f, Color(0.09f, 0.14f, 0.14f, 1.0f));
    const float fill = success ? 1.0f : (cooldown > 0.0f ?
        1.0f - cooldown / 10.0f : progressRate);
    AddRectangle(x + 10.0f, y + 58.0f, 276.0f * (std::clamp)(fill, 0.0f, 1.0f), 6.0f, color);
    Flush();
}

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
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float panelWidth = 660.0f;
    const float panelHeight = 600.0f;
    const float panelX = (screenWidth - panelWidth) * 0.5f;
    const float panelY = (screenHeight - panelHeight) * 0.5f;
    const Color shade(0.0f, 0.004f, 0.004f, 0.78f);
    const Color panel(0.012f, 0.022f, 0.018f, 0.96f);
    const Color green(0.48f, 0.90f, 0.58f, 0.96f);
    const Color pale(0.72f, 0.82f, 0.74f, 0.92f);

    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, shade);
    AddRectangle(panelX, panelY, panelWidth, panelHeight, panel);
    AddRectangle(panelX, panelY, 6.0f, panelHeight, green);
    AddRectangle(panelX, panelY, panelWidth, 2.0f, green);

    const auto addCenteredText = [this, screenWidth](
        float y,
        std::string_view text,
        float pixelSize,
        const Color& color)
    {
        const float width =
            static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * 6.0f;
        AddText((screenWidth - width) * 0.5f,
            y, text, pixelSize, color);
    };

    addCenteredText(panelY + 34.0f, "ポーズ", 5.0f, green);
    const int runSeconds = (std::max)(
        0, static_cast<int>(runTimeSeconds));
    const std::string runStatus =
        "現在 " + std::to_string((std::clamp)(floorNumber, 1, 2)) +
        "階   経過 " + std::to_string(runSeconds / 60) + "分 " +
        std::to_string(runSeconds % 60) + "秒   捕まった回数 " +
        std::to_string((std::max)(caughtCount, 0));
    addCenteredText(panelY + 78.0f, runStatus, 2.0f, pale);
    const int safeBrightnessLevel =
        (std::clamp)(brightnessLevel, 0, 4);
    const int safeEffectLevel = (std::clamp)(effectLevel, 0, 2);
    const int safeSensitivityLevel =
        (std::clamp)(lookSensitivityLevel, 0, 4);
    const int safeVolumeLevel = (std::clamp)(volumeLevel, 0, 4);
    const int safeSelectedSetting =
        (std::clamp)(selectedSetting, 0, 5);
    const std::string brightnessText =
        "明るさ " + std::to_string(safeBrightnessLevel + 1) +
        " OF 5";
    addCenteredText(panelY + 108.0f,
        brightnessText, 2.5f,
        safeSelectedSetting == 0 ? green : pale);
    constexpr std::string_view effectNames[] =
    {
        "軽量 FPS優先", "標準", "高品質"
    };
    const std::string effectText =
        "画面効果 " + std::string(effectNames[safeEffectLevel]);
    addCenteredText(panelY + 148.0f,
        effectText, 2.5f,
        safeSelectedSetting == 1 ? green : pale);
    const std::string sensitivityText =
        "視点速度 " +
        std::to_string(safeSensitivityLevel + 1) + " OF 5";
    addCenteredText(panelY + 188.0f,
        sensitivityText, 2.5f,
        safeSelectedSetting == 2 ? green : pale);
    constexpr int volumePercent[] = { 0, 25, 50, 75, 100 };
    const std::string volumeText = safeVolumeLevel == 0
        ? "音量 ミュート"
        : "音量 " + std::to_string(volumePercent[safeVolumeLevel]) + "%";
    addCenteredText(panelY + 228.0f,
        volumeText, 2.5f,
        safeSelectedSetting == 3 ? green : pale);
    addCenteredText(panelY + 268.0f,
        guideEnabled ? "目的表示 あり" : "目的表示 なし", 2.5f,
        safeSelectedSetting == 4 ? green : pale);
    // 描画解像度は次回起動から反映されるため、起動時と違う段階を選んでいるときはそのことを表示します。
    constexpr std::string_view resolutionNames[] = { "自動", "100%", "75%", "67%" };
    const int safeResolutionLevel = (std::clamp)(resolutionLevel, 0, 3);
    const std::string resolutionText =
        "描画解像度 " + std::string(resolutionNames[safeResolutionLevel]) +
        (safeResolutionLevel == Application::GetLaunchResolutionLevel()
            ? "  " + std::to_string(Application::GetWidth()) + "x" +
                std::to_string(Application::GetHeight())
            : std::string("  次回起動から"));
    addCenteredText(panelY + 308.0f,
        resolutionText, 2.5f,
        safeSelectedSetting == 5 ? green : pale);
    if (Input::IsControllerConnected())
    {
        addCenteredText(panelY + 350.0f,
            "十字キーで選択と調整", 2.0f, pale);
        addCenteredText(panelY + 392.0f,
            "START ゲームに戻る", 2.5f, pale);
        addCenteredText(panelY + 434.0f,
            "Y この階をやり直す", 2.5f, pale);
        addCenteredText(panelY + 476.0f,
            "B タイトルへ戻る", 2.5f, pale);
        addCenteredText(panelY + 518.0f,
            "BACK ゲーム終了", 2.5f, pale);
    }
    else
    {
        addCenteredText(panelY + 350.0f,
            "矢印キーで選択と調整", 2.0f, pale);
        addCenteredText(panelY + 392.0f,
            "ESC または P ゲームに戻る", 2.5f, pale);
        addCenteredText(panelY + 434.0f,
            "R この階をやり直す", 2.5f, pale);
        addCenteredText(panelY + 476.0f,
            "T タイトルへ戻る", 2.5f, pale);
        addCenteredText(panelY + 518.0f,
            "Q ゲーム終了", 2.5f, pale);
    }

    Flush();
}
