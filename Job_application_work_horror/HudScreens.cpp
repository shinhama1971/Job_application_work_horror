// ============================================================================
// ファイルの役割: タイトル、結果、目的ガイド、ポーズなどの画面別UIを描画します。
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

    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());
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
    bool newBestTime,
    bool newBestCaught)
{
    m_Vertices.clear();

    const float reveal = (std::clamp)(revealAmount, 0.0f, 1.0f);
    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());
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
    const float panelHeight = 560.0f;
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
    const int safeEvidenceCollected = (std::clamp)(evidenceCollected, 0, 3);
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
        "残された記録 " + std::to_string(safeEvidenceCollected) + " / 3";
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
        safeEvidenceCollected >= 3
            ? Color(0.92f, 0.78f, 0.34f, 0.96f * reveal)
            : pale);

    std::string_view achievement = "";
    if (safeEvidenceCollected >= 3 && safeCaughtCount == 0 &&
        safePuzzleMistakes == 0)
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
            panelY + 448.0f,
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
        panelY + 508.0f,
        prompt,
        promptPixelSize,
        pale);

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
    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());
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

    const float screenWidth = static_cast<float>(Application::GetWidth());
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

void Hud::DrawPause(
    int brightnessLevel,
    int effectLevel,
    int lookSensitivityLevel,
    int volumeLevel,
    int selectedSetting,
    int floorNumber,
    float runTimeSeconds,
    int caughtCount)
{
    m_Vertices.clear();

    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());
    const float panelWidth = 660.0f;
    const float panelHeight = 520.0f;
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
        "FLOOR " + std::to_string((std::clamp)(floorNumber, 1, 2)) +
        "   TIME " + std::to_string(runSeconds / 60) + " MIN " +
        std::to_string(runSeconds % 60) + " SEC   CAUGHT " +
        std::to_string((std::max)(caughtCount, 0));
    addCenteredText(panelY + 78.0f, runStatus, 2.0f, pale);
    const int safeBrightnessLevel =
        (std::clamp)(brightnessLevel, 0, 4);
    const int safeEffectLevel = (std::clamp)(effectLevel, 0, 2);
    const int safeSensitivityLevel =
        (std::clamp)(lookSensitivityLevel, 0, 4);
    const int safeVolumeLevel = (std::clamp)(volumeLevel, 0, 4);
    const int safeSelectedSetting =
        (std::clamp)(selectedSetting, 0, 3);
    const std::string brightnessText =
        "明るさ " + std::to_string(safeBrightnessLevel + 1) +
        " OF 5";
    addCenteredText(panelY + 108.0f,
        brightnessText, 2.5f,
        safeSelectedSetting == 0 ? green : pale);
    const std::string effectText =
        "画面効果 " + std::to_string(safeEffectLevel + 1) +
        " OF 3";
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
    if (Input::IsControllerConnected())
    {
        addCenteredText(panelY + 270.0f,
            "十字キーで選択と調整", 2.0f, pale);
        addCenteredText(panelY + 312.0f,
            "START ゲームに戻る", 2.5f, pale);
        addCenteredText(panelY + 354.0f,
            "Y この階をやり直す", 2.5f, pale);
        addCenteredText(panelY + 396.0f,
            "B タイトルへ戻る", 2.5f, pale);
        addCenteredText(panelY + 438.0f,
            "BACK ゲーム終了", 2.5f, pale);
    }
    else
    {
        addCenteredText(panelY + 270.0f,
            "矢印キーで選択と調整", 2.0f, pale);
        addCenteredText(panelY + 312.0f,
            "ESC または P ゲームに戻る", 2.5f, pale);
        addCenteredText(panelY + 354.0f,
            "R この階をやり直す", 2.5f, pale);
        addCenteredText(panelY + 396.0f,
            "T タイトルへ戻る", 2.5f, pale);
        addCenteredText(panelY + 438.0f,
            "Q ゲーム終了", 2.5f, pale);
    }

    Flush();
}

