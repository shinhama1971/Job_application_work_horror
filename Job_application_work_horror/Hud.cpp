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

void Hud::Init()
{
    m_Shader.Create("shader/hudVS.hlsl", "shader/hudPS.hlsl");

    std::vector<VERTEX_3D> initialVertices(MaxVertices);
    m_VertexBuffer.Create(initialVertices);
    m_Vertices.reserve(MaxVertices);
}

void Hud::Draw(
    const Player& player,
    int fuseCount,
    std::string_view interactionPrompt,
    std::string_view objectiveText)
{
    m_Vertices.clear();

    const Color white(0.88f, 0.92f, 0.90f, 0.85f);
    const Color dark(0.015f, 0.02f, 0.02f, 0.72f);
    const Color inactive(0.16f, 0.19f, 0.18f, 0.85f);
    const Color active(0.72f, 0.86f, 0.66f, 0.95f);
    const bool hasInteractionTarget = !interactionPrompt.empty();
    const bool interactionLocked = hasInteractionTarget && (
        interactionPrompt.find("Requires") != std::string_view::npos ||
        interactionPrompt.find("no power") != std::string_view::npos ||
        interactionPrompt.find("Locked") != std::string_view::npos ||
        interactionPrompt.find("locked") != std::string_view::npos ||
        interactionPrompt.find("必要") != std::string_view::npos ||
        interactionPrompt.find("開かない") != std::string_view::npos);
    const Color reticleColor = interactionLocked
        ? Color(0.92f, 0.28f, 0.20f, 0.96f)
        : (hasInteractionTarget
            ? Color(0.62f, 0.92f, 0.70f, 0.98f)
            : white);

    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());

    if (!objectiveText.empty())
    {
        const float pixelSize = 3.0f;
        const float objectiveWidth =
            static_cast<float>(CountDisplayedCharacters(objectiveText)) *
            18.0f + 24.0f;
        const bool isSafeObjective =
            objectiveText == "ESCAPE" ||
            objectiveText == "ESCAPED" ||
            objectiveText == "POWER RESTORED" ||
            objectiveText == "OPEN THE DOOR" ||
            objectiveText == "LEAVE";
        const Color objectiveColor = isSafeObjective
            ? Color(0.52f, 0.92f, 0.58f, 0.95f)
            : Color(0.88f, 0.82f, 0.62f, 0.95f);

        AddRectangle(34.0f, 28.0f, objectiveWidth, 39.0f, dark);
        AddRectangle(34.0f, 28.0f, 4.0f, 39.0f, objectiveColor);
        AddText(48.0f, 37.0f, objectiveText, pixelSize, objectiveColor);
    }

    // Keep optional exploration visible without competing with the current
    // mandatory objective.  This also makes the result-screen bonus legible
    // before the player reaches the end of the game.
    Core::Game* game = Core::Game::GetInstance();
    const int evidenceCount = game != nullptr
        ? (std::clamp)(game->GetEvidenceCollected(), 0, 3)
        : 0;
    const std::string evidenceText =
        "記録 " + std::to_string(evidenceCount) + " / 3";
    constexpr float evidencePixelSize = 2.0f;
    const float evidenceWidth =
        static_cast<float>(CountDisplayedCharacters(evidenceText)) *
        evidencePixelSize * 6.0f;
    const float evidenceX = screenWidth - evidenceWidth - 52.0f;
    const Color evidenceColor = evidenceCount >= 3
        ? Color(0.92f, 0.78f, 0.34f, 0.96f)
        : Color(0.62f, 0.78f, 0.70f, 0.88f);
    AddRectangle(evidenceX - 14.0f, 74.0f,
        evidenceWidth + 28.0f, 34.0f, dark);
    AddText(evidenceX, 82.0f,
        evidenceText, evidencePixelSize, evidenceColor);

    // Minimal center reticle.
    const float reticleExtent = hasInteractionTarget ? 11.0f : 9.0f;
    AddRectangle(screenWidth * 0.5f - reticleExtent, screenHeight * 0.5f - 1.0f, reticleExtent - 2.0f, 2.0f, reticleColor);
    AddRectangle(screenWidth * 0.5f + 2.0f, screenHeight * 0.5f - 1.0f, reticleExtent - 2.0f, 2.0f, reticleColor);
    AddRectangle(screenWidth * 0.5f - 1.0f, screenHeight * 0.5f - reticleExtent, 2.0f, reticleExtent - 2.0f, reticleColor);
    AddRectangle(screenWidth * 0.5f - 1.0f, screenHeight * 0.5f + 2.0f, 2.0f, reticleExtent - 2.0f, reticleColor);

    // A negative count lets non-fuse stages keep the shared battery HUD.
    if (fuseCount >= 0)
    {
        AddRectangle(34.0f, screenHeight - 112.0f, 112.0f, 36.0f, dark);
        const int clampedFuseCount = std::clamp(fuseCount, 0, 3);
        for (int i = 0; i < 3; ++i)
        {
            AddRectangle(
                46.0f + static_cast<float>(i) * 32.0f,
                screenHeight - 101.0f,
                20.0f,
                14.0f,
                i < clampedFuseCount ? active : inactive
            );
        }
    }

    // Flashlight battery frame and fill.
    const float batteryRate = std::clamp(
        player.GetBattery() / 100.0f, 0.0f, 1.0f);
    Color batteryColor(0.42f, 0.82f, 0.48f, 0.95f);
    if (batteryRate <= 0.2f)
    {
        batteryColor = Color(0.92f, 0.18f, 0.12f, 0.95f);
    }
    else if (batteryRate <= 0.5f)
    {
        batteryColor = Color(0.92f, 0.68f, 0.16f, 0.95f);
    }

    constexpr float barFillX = 104.0f;
    constexpr float barFillWidth = 152.0f;
    AddRectangle(34.0f, screenHeight - 66.0f, 230.0f, 30.0f, dark);
    AddText(43.0f, screenHeight - 58.0f,
        "ライト", 1.5f, white);
    AddRectangle(barFillX, screenHeight - 57.0f,
        barFillWidth, 12.0f, inactive);
    if (batteryRate > 0.0f)
    {
        AddRectangle(barFillX, screenHeight - 57.0f,
            barFillWidth * batteryRate, 12.0f, batteryColor);
    }

    const float staminaRate = std::clamp(
        player.GetStamina() / 100.0f, 0.0f, 1.0f);
    Color staminaColor(0.34f, 0.70f, 0.86f, 0.95f);
    if (staminaRate <= 0.20f)
    {
        staminaColor = Color(0.90f, 0.25f, 0.16f, 0.95f);
    }
    else if (staminaRate <= 0.40f)
    {
        staminaColor = Color(0.90f, 0.62f, 0.16f, 0.95f);
    }

    AddRectangle(34.0f, screenHeight - 32.0f, 230.0f, 24.0f, dark);
    AddText(43.0f, screenHeight - 26.0f,
        "スタミナ", 1.5f, white);
    AddRectangle(barFillX, screenHeight - 25.0f,
        barFillWidth, 10.0f, inactive);
    if (staminaRate > 0.0f)
    {
        AddRectangle(barFillX, screenHeight - 25.0f,
            barFillWidth * staminaRate, 10.0f, staminaColor);
    }

    // Show both the input and the selected action.  The old button-only
    // prompt did not tell the player whether E/A would collect, open or leave.
    if (!interactionPrompt.empty())
    {
        const Color promptColor = interactionLocked
            ? Color(0.85f, 0.22f, 0.18f, 0.95f)
            : white;
        constexpr float promptPixelSize = 2.0f;
        const float promptTextWidth =
            static_cast<float>(CountDisplayedCharacters(interactionPrompt)) *
            promptPixelSize * 6.0f;
        const float panelWidth = 88.0f + promptTextWidth;
        const float panelHeight = 58.0f;
        const float panelX = (screenWidth - panelWidth) * 0.5f;
        const float panelY = screenHeight - 106.0f;

        AddRectangle(panelX, panelY, panelWidth, panelHeight, dark);
        AddRectangle(panelX + 10.0f, panelY + 10.0f,
            44.0f, 38.0f, inactive);
        if (Input::IsControllerConnected())
        {
            AddLetterA(panelX + 20.0f, panelY + 17.0f,
                24.0f, promptColor);
        }
        else
        {
            AddLetterE(panelX + 20.0f, panelY + 17.0f,
                24.0f, promptColor);
        }
        AddText(panelX + 68.0f, panelY + 22.0f,
            interactionPrompt, promptPixelSize, promptColor);
    }

    std::string_view batteryNotice;
    Color batteryNoticeColor(0.52f, 0.92f, 0.58f, 0.96f);
    if (player.GetBatteryNoticeTimer() > 0.0f)
    {
        batteryNotice = "電池を回復した";
    }
    else if (player.GetBattery() <= 20.0f)
    {
        batteryNotice = "電池残量が少ない";
        batteryNoticeColor = Color(0.94f, 0.30f, 0.18f, 0.96f);
    }
    if (!batteryNotice.empty())
    {
        constexpr float noticePixelSize = 2.0f;
        const float noticeWidth =
            static_cast<float>(batteryNotice.size()) *
            noticePixelSize * 6.0f;
        const float noticeX = screenWidth - noticeWidth - 52.0f;
        AddRectangle(noticeX - 14.0f, 28.0f,
            noticeWidth + 28.0f, 34.0f, dark);
        AddText(noticeX, 36.0f,
            batteryNotice, noticePixelSize, batteryNoticeColor);
    }

    Flush();
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

    constexpr std::string_view title = "SIGNAL LOST";
    constexpr float titlePixelSize = 9.0f;
    const float titleWidth =
        static_cast<float>(title.size()) * titlePixelSize * 6.0f;
    AddText(
        (screenWidth - titleWidth) * 0.5f,
        panelY + 72.0f,
        title,
        titlePixelSize,
        titleGreen);

    constexpr std::string_view subtitle = "終わらない廊下";
    constexpr float subtitlePixelSize = 3.0f;
    const float subtitleWidth =
        static_cast<float>(subtitle.size()) * subtitlePixelSize * 6.0f;
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
        static_cast<float>(controls1.size()) * controlsPixelSize * 6.0f;
    const float controls2Width =
        static_cast<float>(controls2.size()) * controlsPixelSize * 6.0f;
    const float controls3Width =
        static_cast<float>(controls3.size()) * controlsPixelSize * 6.0f;
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
            "BEST " + std::to_string(bestSeconds / 60) +
            " MIN " + std::to_string(bestSeconds % 60) +
            " SEC   FEWEST CAUGHT " +
            std::to_string((std::max)(bestCaughtCount, 0));
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

    const std::string_view prompt = Input::IsControllerConnected()
        ? "Aでゲーム開始"
        : "ENTERでゲーム開始";
    constexpr float promptPixelSize = 3.0f;
    const float promptWidth =
        static_cast<float>(prompt.size()) * promptPixelSize * 6.0f;
    AddText((screenWidth - promptWidth) * 0.5f,
        panelY + 338.0f, prompt, promptPixelSize, promptColor);

    const std::string_view quitPrompt = Input::IsControllerConnected()
        ? "Bでゲーム終了"
        : "Qでゲーム終了";
    constexpr float quitPixelSize = 2.0f;
    const float quitWidth =
        static_cast<float>(quitPrompt.size()) * quitPixelSize * 6.0f;
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
    bool exitReady)
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
        exitReady ? "出口が開いた" : "廊下の繰り返し",
        1.8f, active);

    const int safeCompletedLoops = (std::clamp)(completedLoops, 0, 3);
    constexpr float segmentWidth = 52.0f;
    constexpr float segmentGap = 7.0f;
    for (int index = 0; index < 3; ++index)
    {
        AddRectangle(
            panelX + 14.0f + static_cast<float>(index) *
                (segmentWidth + segmentGap),
            panelY + 35.0f,
            segmentWidth,
            10.0f,
            index < safeCompletedLoops ? active : inactive);
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
    int selectedSetting,
    int floorNumber,
    float runTimeSeconds,
    int caughtCount)
{
    m_Vertices.clear();

    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());
    const float panelWidth = 660.0f;
    const float panelHeight = 480.0f;
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
    const int safeSelectedSetting =
        (std::clamp)(selectedSetting, 0, 2);
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
    if (Input::IsControllerConnected())
    {
        addCenteredText(panelY + 230.0f,
            "十字キーで選択と調整", 2.0f, pale);
        addCenteredText(panelY + 276.0f,
            "START ゲームに戻る", 2.5f, pale);
        addCenteredText(panelY + 318.0f,
            "Y この階をやり直す", 2.5f, pale);
        addCenteredText(panelY + 360.0f,
            "B タイトルへ戻る", 2.5f, pale);
        addCenteredText(panelY + 402.0f,
            "BACK ゲーム終了", 2.5f, pale);
    }
    else
    {
        addCenteredText(panelY + 230.0f,
            "矢印キーで選択と調整", 2.0f, pale);
        addCenteredText(panelY + 276.0f,
            "ESC または P ゲームに戻る", 2.5f, pale);
        addCenteredText(panelY + 318.0f,
            "R この階をやり直す", 2.5f, pale);
        addCenteredText(panelY + 360.0f,
            "T タイトルへ戻る", 2.5f, pale);
        addCenteredText(panelY + 402.0f,
            "Q ゲーム終了", 2.5f, pale);
    }

    Flush();
}

void Hud::DrawBlink(float opacity)
{
    const float blinkOpacity = (std::clamp)(opacity, 0.0f, 0.90f);
    if (blinkOpacity <= 0.001f)
    {
        return;
    }

    m_Vertices.clear();
    AddRectangle(
        0.0f,
        0.0f,
        static_cast<float>(Application::GetWidth()),
        static_cast<float>(Application::GetHeight()),
        Color(0.0f, 0.0f, 0.0f, blinkOpacity));
    Flush();
}

void Hud::Uninit()
{
    m_Vertices.clear();
}

void Hud::Flush()
{
    if (m_Vertices.empty())
    {
        return;
    }

    m_VertexBuffer.Modify(m_Vertices);

    Renderer::SetDepthEnable(false);
    Renderer::SetBlendState(BS_ALPHABLEND);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(static_cast<UINT>(m_Vertices.size()), 0);

    Renderer::SetDepthEnable(true);
}

void Hud::AddRectangle(float x, float y, float width, float height, const Color& color)
{
    if (m_Vertices.size() + 6 > MaxVertices)
    {
        return;
    }

    const float screenWidth = static_cast<float>(Application::GetWidth());
    const float screenHeight = static_cast<float>(Application::GetHeight());

    const float left = x / screenWidth * 2.0f - 1.0f;
    const float right = (x + width) / screenWidth * 2.0f - 1.0f;
    const float top = 1.0f - y / screenHeight * 2.0f;
    const float bottom = 1.0f - (y + height) / screenHeight * 2.0f;

    const auto makeVertex = [&color](float px, float py)
    {
        VERTEX_3D vertex{};
        vertex.position = Vector3(px, py, 0.0f);
        vertex.color = color;
        return vertex;
    };

    m_Vertices.push_back(makeVertex(left, top));
    m_Vertices.push_back(makeVertex(right, top));
    m_Vertices.push_back(makeVertex(left, bottom));
    m_Vertices.push_back(makeVertex(left, bottom));
    m_Vertices.push_back(makeVertex(right, top));
    m_Vertices.push_back(makeVertex(right, bottom));
}

void Hud::AddLetterE(float x, float y, float size, const Color& color)
{
    const float stroke = (std::max)(2.0f, size * 0.18f);
    AddRectangle(x, y, stroke, size, color);
    AddRectangle(x, y, size, stroke, color);
    AddRectangle(x, y + size * 0.5f - stroke * 0.5f, size * 0.82f, stroke, color);
    AddRectangle(x, y + size - stroke, size, stroke, color);
}

void Hud::AddLetterA(float x, float y, float size, const Color& color)
{
    const float stroke = (std::max)(2.0f, size * 0.18f);
    AddRectangle(x, y, stroke, size, color);
    AddRectangle(x + size - stroke, y, stroke, size, color);
    AddRectangle(x, y, size, stroke, color);
    AddRectangle(x, y + size * 0.5f - stroke * 0.5f, size, stroke, color);
}

void Hud::AddText(
    float x,
    float y,
    std::string_view text,
    float pixelSize,
    const Color& color)
{
    struct RasterGlyph
    {
        unsigned int width = 0;
        unsigned int height = 0;
        int advance = 0;
        std::vector<unsigned char> bitmap;
    };

    class JapaneseGlyphCache
    {
    public:
        JapaneseGlyphCache()
        {
            m_DC = CreateCompatibleDC(nullptr);
            m_Font = CreateFontW(
                -18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                L"Yu Gothic UI");
            if (m_DC != nullptr && m_Font != nullptr)
            {
                m_OldFont = SelectObject(m_DC, m_Font);
            }
        }

        ~JapaneseGlyphCache()
        {
            if (m_DC != nullptr && m_OldFont != nullptr)
            {
                SelectObject(m_DC, m_OldFont);
            }
            if (m_Font != nullptr)
            {
                DeleteObject(m_Font);
            }
            if (m_DC != nullptr)
            {
                DeleteDC(m_DC);
            }
        }

        const RasterGlyph& Get(wchar_t character)
        {
            const auto found = m_Glyphs.find(character);
            if (found != m_Glyphs.end())
            {
                return found->second;
            }

            RasterGlyph glyph;
            if (m_DC == nullptr || m_Font == nullptr)
            {
                return m_Glyphs.emplace(character, std::move(glyph)).first->second;
            }

            MAT2 transform{};
            transform.eM11.value = 1;
            transform.eM22.value = 1;
            GLYPHMETRICS metrics{};
            const DWORD size = GetGlyphOutlineW(
                m_DC, character, GGO_BITMAP, &metrics,
                0, nullptr, &transform);
            glyph.advance = metrics.gmCellIncX > 0
                ? metrics.gmCellIncX
                : 18;
            if (size != GDI_ERROR && size > 0)
            {
                glyph.width = metrics.gmBlackBoxX;
                glyph.height = metrics.gmBlackBoxY;
                const unsigned int pitch = ((glyph.width + 31u) / 32u) * 4u;
                std::vector<unsigned char> packed(size);
                if (GetGlyphOutlineW(
                    m_DC, character, GGO_BITMAP, &metrics,
                    size, packed.data(), &transform) != GDI_ERROR)
                {
                    glyph.bitmap.resize(glyph.width * glyph.height, 0);
                    for (unsigned int row = 0; row < glyph.height; ++row)
                    {
                        for (unsigned int column = 0; column < glyph.width; ++column)
                        {
                            const unsigned char byte = packed[row * pitch + column / 8u];
                            glyph.bitmap[row * glyph.width + column] =
                                (byte & (0x80u >> (column % 8u))) != 0 ? 1 : 0;
                        }
                    }
                }
            }
            return m_Glyphs.emplace(character, std::move(glyph)).first->second;
        }

    private:
        HDC m_DC = nullptr;
        HFONT m_Font = nullptr;
        HGDIOBJ m_OldFont = nullptr;
        std::unordered_map<wchar_t, RasterGlyph> m_Glyphs;
    };

    const auto getRows = [](char character)
    {
        using Glyph = std::array<std::uint8_t, 7>;

        switch (character)
        {
        case 'A': return Glyph{ 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'B': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
        case 'C': return Glyph{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
        case 'D': return Glyph{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
        case 'E': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
        case 'F': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
        case 'G': return Glyph{ 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E };
        case 'H': return Glyph{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'I': return Glyph{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F };
        case 'J': return Glyph{ 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
        case 'K': return Glyph{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
        case 'L': return Glyph{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
        case 'M': return Glyph{ 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
        case 'N': return Glyph{ 0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11 };
        case 'O': return Glyph{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'P': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
        case 'Q': return Glyph{ 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
        case 'R': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
        case 'S': return Glyph{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
        case 'T': return Glyph{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
        case 'U': return Glyph{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'V': return Glyph{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
        case 'W': return Glyph{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
        case 'X': return Glyph{ 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
        case 'Y': return Glyph{ 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
        case 'Z': return Glyph{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
        case '0': return Glyph{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
        case '1': return Glyph{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
        case '2': return Glyph{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
        case '3': return Glyph{ 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
        case '4': return Glyph{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
        case '5': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E };
        case '6': return Glyph{ 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E };
        case '7': return Glyph{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
        case '8': return Glyph{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
        case '9': return Glyph{ 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E };
        case '-': return Glyph{ 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
        case '\'': return Glyph{ 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 };
        default: return Glyph{};
        }
    };

    static JapaneseGlyphCache japaneseGlyphs;
    float cursorX = x;
    for (size_t textIndex = 0; textIndex < text.size();)
    {
        const unsigned char firstByte =
            static_cast<unsigned char>(text[textIndex]);
        if (firstByte >= 0x80u)
        {
            int sequenceLength = 0;
            if ((firstByte & 0xE0u) == 0xC0u) sequenceLength = 2;
            else if ((firstByte & 0xF0u) == 0xE0u) sequenceLength = 3;
            else sequenceLength = 4;

            if (textIndex + static_cast<size_t>(sequenceLength) > text.size())
            {
                break;
            }

            unsigned int codePoint = firstByte &
                (sequenceLength == 2 ? 0x1Fu :
                 sequenceLength == 3 ? 0x0Fu : 0x07u);
            for (int byteIndex = 1; byteIndex < sequenceLength; ++byteIndex)
            {
                codePoint = (codePoint << 6u) |
                    (static_cast<unsigned char>(text[textIndex + byteIndex]) & 0x3Fu);
            }
            textIndex += static_cast<size_t>(sequenceLength);

            const RasterGlyph& glyph = japaneseGlyphs.Get(
                static_cast<wchar_t>(codePoint));
            // Match the 5x7 Latin font's visual height and advance so mixed
            // Japanese/ASCII text stays inside the existing HUD panels.
            const float glyphPixelSize = pixelSize * 0.34f;
            for (unsigned int row = 0; row < glyph.height; ++row)
            {
                unsigned int column = 0;
                while (column < glyph.width)
                {
                    while (column < glyph.width &&
                        glyph.bitmap[row * glyph.width + column] == 0)
                    {
                        ++column;
                    }
                    const unsigned int runStart = column;
                    while (column < glyph.width &&
                        glyph.bitmap[row * glyph.width + column] != 0)
                    {
                        ++column;
                    }
                    if (column > runStart)
                    {
                        AddRectangle(
                            cursorX + static_cast<float>(runStart) * glyphPixelSize,
                            y + static_cast<float>(row) * glyphPixelSize,
                            static_cast<float>(column - runStart) * glyphPixelSize,
                            glyphPixelSize,
                            color);
                    }
                }
            }
            cursorX += static_cast<float>(glyph.advance) * glyphPixelSize;
            continue;
        }

        char character = static_cast<char>(firstByte);
        ++textIndex;
        if (character >= 'a' && character <= 'z')
        {
            character = static_cast<char>(character - 'a' + 'A');
        }
        const std::array<std::uint8_t, 7> rows = getRows(character);
        for (size_t row = 0; row < rows.size(); ++row)
        {
            for (int column = 0; column < 5; ++column)
            {
                if ((rows[row] & (1u << (4 - column))) != 0)
                {
                    AddRectangle(
                        cursorX + static_cast<float>(column) * pixelSize,
                        y + static_cast<float>(row) * pixelSize,
                        pixelSize,
                        pixelSize,
                        color);
                }
            }
        }

        cursorX += pixelSize * 6.0f;
    }
}
