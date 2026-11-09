// ============================================================================
// ファイルの役割: タイトル・リザルト・監視映像・章のカード・目的地の方向・暗証番号・隠れている間の視界・ポーズなど、画面ごとのUIを描いている。
// 主な技術: 画面ごとの描画関数、画面の大きさに合わせた中央・端の配置、選んでいる項目の強調表示
// ============================================================================

#include "Hud.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
    // UTF-8の文字列で、画面に出る文字の数を数えている（中央に置くための幅の計算に使っている）
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
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const Color dimGreen(0.08f, 0.20f, 0.14f, 0.55f);
    const Color titleGreen(0.48f, 0.90f, 0.58f, 0.96f);
    const Color textColor(0.50f, 0.68f, 0.57f, 0.86f);
    const Color faintColor(0.36f, 0.50f, 0.42f, 0.72f);
    // 「開始」の文字と、選んでいる面の縁を、ゆっくり明滅させている
    const float pulse = std::sin(time * 2.1f) * 0.5f + 0.5f;
    const Color promptColor(
        0.58f + pulse * 0.16f,
        0.70f + pulse * 0.18f,
        0.62f + pulse * 0.14f,
        0.62f + pulse * 0.34f);
    noise = (std::clamp)(noise, 0.0f, 1.0f);
    // 0〜1の乱数（同じ値からは同じ結果）。ノイズのずれ方を、時間の刻みごとに変えるのに使っている
    const auto hash = [](float value)
    {
        const float s = std::sin(value * 12.9898f) * 43758.5453f;
        return s - std::floor(s);
    };
    const auto textWidth = [](std::string_view text, float pixelSize)
    {
        return static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * 6.0f;
    };

    // 左側の暗い帯：左端はほぼ黒、右へ行くほど透明にして、後ろの森が右側に見えるようにしている
    constexpr float bandWidth = 560.0f;
    constexpr float fadeWidth = 260.0f;
    AddRectangle(0.0f, 0.0f, bandWidth - fadeWidth, screenHeight, Color(0.0f, 0.004f, 0.003f, 0.80f));
    constexpr int fadeSteps = 26;
    for (int step = 0; step < fadeSteps; ++step)
    {
        const float t = (static_cast<float>(step) + 0.5f) / static_cast<float>(fadeSteps);
        AddRectangle(bandWidth - fadeWidth + fadeWidth * static_cast<float>(step) / fadeSteps, 0.0f,
            fadeWidth / fadeSteps + 0.5f, screenHeight, Color(0.0f, 0.004f, 0.003f, 0.80f * (1.0f - t) * (1.0f - t)));
    }
    // 画面全体の薄い走査線（古いモニターや監視映像のような見た目）
    for (float y = 0.0f; y < screenHeight; y += 6.0f)
    {
        AddRectangle(0.0f, y, screenWidth, 1.0f, Color(0.0f, 0.0f, 0.0f, 0.10f));
    }
    // 左端の緑の線
    AddRectangle(56.0f, 120.0f, 3.0f, screenHeight - 240.0f, Color(0.30f, 0.62f, 0.40f, 0.70f));

    // --- 題名：通常は静かに表示し、ノイズのときに文字を横へずらし、色をにじませ、横の帯を抜いている
    constexpr float left = 84.0f;
    constexpr std::string_view title = "通信途絶";
    constexpr float titlePixelSize = 11.0f;
    constexpr float titleY = 150.0f;
    const float titleHeight = titlePixelSize * 7.0f;
    const float noiseStep = std::floor(time * 24.0f);
    const float jitter = noise > 0.0f ? (hash(noiseStep) - 0.5f) * 28.0f * noise : 0.0f;
    if (noise > 0.05f)
    {
        // 色のにじみ（赤と青緑を、左右に少しずらして重ねている）
        const float split = 4.0f + 10.0f * noise;
        AddText(left + jitter - split, titleY, title, titlePixelSize, Color(0.95f, 0.18f, 0.16f, 0.55f * noise));
        AddText(left + jitter + split, titleY + 1.0f, title, titlePixelSize, Color(0.18f, 0.85f, 0.90f, 0.45f * noise));
    }
    // 本体の文字。乱れている間は、ときどき一瞬暗くしている（電波が途切れる）
    const float dropout = (noise > 0.3f && hash(noiseStep + 5.1f) < 0.35f) ? 0.35f : 1.0f;
    AddText(left + jitter, titleY, title, titlePixelSize,
        Color(titleGreen.R(), titleGreen.G(), titleGreen.B(), titleGreen.A() * dropout));
    if (noise > 0.05f)
    {
        // 横の帯が抜ける：題名の上に、背景と同じ暗い横線を数本重ね、その少し横に緑の線の切れ端を置いている
        const int bars = 2 + static_cast<int>(noise * 5.0f);
        const float titleWidth = textWidth(title, titlePixelSize);
        for (int bar = 0; bar < bars; ++bar)
        {
            const float seed = noiseStep * 3.1f + static_cast<float>(bar) * 7.7f;
            const float barY = titleY + hash(seed) * titleHeight;
            const float barHeight = 2.0f + hash(seed + 1.3f) * 6.0f;
            AddRectangle(left - 10.0f, barY, titleWidth + 40.0f, barHeight, Color(0.0f, 0.004f, 0.003f, 0.92f));
            const float shift = (hash(seed + 2.9f) - 0.5f) * 60.0f * noise;
            AddRectangle(left + shift + hash(seed + 4.4f) * titleWidth * 0.6f, barY, 30.0f + hash(seed + 6.1f) * 90.0f,
                (std::max)(1.0f, barHeight * 0.5f), Color(0.48f, 0.90f, 0.58f, 0.55f * noise));
        }
    }
    // 英語の題名と、ひとことの説明
    AddText(left + 4.0f, titleY + titleHeight + 18.0f, "SIGNAL LOST", 3.0f, Color(0.40f, 0.70f, 0.50f, 0.80f));
    AddText(left + 4.0f, titleY + titleHeight + 54.0f, "終わらない廊下から脱出する", 2.4f, textColor);

    // --- 始める面の選択：縦に3つ並べ、選んでいる面の枠を明るくし、その下に説明を出している
    constexpr std::string_view stageNames[] = { "1面  森から研究所へ", "2面  ループする廊下", "1面  森を飛ばして始める" };
    constexpr std::string_view stageNotes[] =
    {
        "雨の森を歩いて研究所に入る  クリアの記録が残る",
        "2面から遊ぶ  クリアしても記録には残らない",
        "受信記録を見て、研究所の中から始める"
    };
    constexpr int stageCount = 3;
    const int stage = (std::clamp)(selectedStage, 0, stageCount - 1);
    constexpr float stageBoxWidth = 340.0f;
    constexpr float stageBoxHeight = 42.0f;
    constexpr float stageBoxGap = 12.0f;
    constexpr float stageTextPixelSize = 2.4f;
    constexpr float stageTop = 370.0f;
    for (int index = 0; index < stageCount; ++index)
    {
        const bool selected = index == stage;
        const float boxY = stageTop + static_cast<float>(index) * (stageBoxHeight + stageBoxGap);
        AddRectangle(left, boxY, stageBoxWidth, stageBoxHeight,
            selected ? Color(0.05f, 0.14f, 0.09f, 0.88f) : Color(0.01f, 0.02f, 0.015f, 0.55f));
        if (selected)
        {
            // 選んでいる面：明滅する縁と、左の太い印
            AddRectangle(left, boxY, stageBoxWidth, 2.0f, promptColor);
            AddRectangle(left, boxY + stageBoxHeight - 2.0f, stageBoxWidth, 2.0f, promptColor);
            AddRectangle(left + stageBoxWidth - 2.0f, boxY, 2.0f, stageBoxHeight, promptColor);
            AddRectangle(left, boxY, 6.0f, stageBoxHeight, titleGreen);
        }
        else
        {
            AddRectangle(left, boxY, 2.0f, stageBoxHeight, dimGreen);
        }
        AddText(left + 22.0f, boxY + (stageBoxHeight - stageTextPixelSize * 7.0f) * 0.5f,
            stageNames[index], stageTextPixelSize, selected ? titleGreen : faintColor);
    }
    const float afterStages = stageTop + stageCount * (stageBoxHeight + stageBoxGap);
    AddText(left + 4.0f, afterStages + 4.0f, stageNotes[stage], 1.7f, textColor);

    // 開始と終了の操作
    const bool controller = Input::IsControllerConnected();
    const std::string_view prompt = controller ? "Aでゲーム開始" : "ENTERでゲーム開始";
    AddText(left + 4.0f, afterStages + 44.0f, prompt, 3.0f, promptColor);
    AddText(left + 4.0f, afterStages + 84.0f,
        controller ? "上下で面を選ぶ  Bでゲーム終了" : "上下で面を選ぶ  Qでゲーム終了", 1.8f, faintColor);

    // クリアの記録があれば、最短タイムと最少の捕まった回数を出している
    if (hasClearRecord)
    {
        const int bestSeconds = (std::max)(0, static_cast<int>(bestClearTimeSeconds + 0.5f));
        const std::string recordText =
            "最短記録 " + std::to_string(bestSeconds / 60) + "分 " + std::to_string(bestSeconds % 60) +
            "秒   最少捕獲 " + std::to_string((std::max)(bestCaughtCount, 0)) + "回";
        AddText(left + 4.0f, afterStages + 116.0f, recordText, 1.8f, Color(0.72f, 0.78f, 0.48f, 0.88f));
    }

    // 操作の説明（左下）。コントローラーがつながっていれば、コントローラーのボタン名で表示している
    const std::string_view controls[] =
    {
        controller ? "左スティック 移動  右スティック 視点  A 調べる" : "WASD 移動  マウス 視点  E 調べる",
        controller ? "左スティック押し込み 走る  Y ライト" : "SHIFT 走る  F ライト",
        controller ? "LB ヒント  START ポーズ" : "H ヒント  ESC または P ポーズ",
    };
    for (int line = 0; line < 3; ++line)
    {
        AddText(left + 4.0f, screenHeight - 118.0f + static_cast<float>(line) * 24.0f, controls[line], 1.7f, faintColor);
    }

    // 右上：監視映像のような受信表示（「受信中」と、明滅する印と、時間）。ノイズのときは「信号なし」にしている
    const bool lost = noise > 0.45f;
    const std::string_view status = lost ? "信号なし" : "受信中...";
    const int clock = static_cast<int>(time);
    char timeText[16];
    std::snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", 2 + clock / 3600, (14 + clock / 60) % 60, (37 + clock) % 60);
    const float statusX = screenWidth - 250.0f;
    const bool blink = std::fmod(time, 1.0f) < 0.6f;
    if (blink || lost)
    {
        AddRectangle(statusX, 46.0f, 10.0f, 10.0f, lost ? Color(0.92f, 0.22f, 0.18f, 0.90f) : Color(0.48f, 0.90f, 0.58f, 0.90f));
    }
    AddText(statusX + 20.0f, 43.0f, status, 2.0f, lost ? Color(0.92f, 0.40f, 0.32f, 0.90f) : textColor);
    AddText(statusX + 20.0f, 70.0f, "CH-03", 1.8f, faintColor);
    AddText(statusX + 100.0f, 70.0f, timeText, 1.8f, faintColor);

    Flush();
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
    // 下段に「今回の発見」（壁の文字・隠し部屋・今回の異変）を並べるため、縦に広げている。
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

    // 評価の点数：100点から、6分を超えた時間（15秒ごとに1点、最大40点）・捕まった回数（1回15点、最大45点）・
    // 観察の失敗（1回5点、最大25点）を引き、異変への対処（1回2点、最大8点）と残された記録（1つ3点）を足している
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
    // 90点以上でS、75点以上でA、55点以上でB、それ未満はC
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

    // 成績の各行（タイム・捕まった回数・異変への対処・観察の失敗・充電器・残された記録）
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

    // 今回の発見。プレイごとに変わる要素と任意の探索を並べ、もう一度遊ぶ理由にしている。
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

    // 達成の一言：全部見つけて一度も失敗しない「完全探索」を最優先にし、次に記録の更新、捕まらなかったことの順で選んでいる
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
    // 映像は16:9で、画面の90%・幅1280・上下の文字が入る高さのうち、最も小さい大きさにしている
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

    // 背景を暗くし、映像のテクスチャを貼っている（映像はHUDの頂点とは別に描いている）
    m_Vertices.clear();
    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight,
        Color(0.0f, 0.004f, 0.003f, 0.94f));
    Flush();

    DrawTextureRectangle(
        feed, textureShader, feedX, feedY, feedWidth, feedHeight);

    // 映像の上に重ねる枠と、カメラの名前・操作の説明
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

    // 右上に巡回の進み具合と、捕まるまでに許される失敗の回数（使った分は赤）を表示している。
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

    // 下端にカメラの並びを表示し、切り替えられることを伝えている。
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

    // 映像の上を流れる薄い横線（監視カメラのノイズらしさ）
    for (int line = 0; line < 8; ++line)
    {
        const float offset = std::fmod(
            static_cast<float>(line * 73) + elapsedSeconds * 46.0f,
            (std::max)(feedHeight - 69.0f, 1.0f));
        AddRectangle(feedX, feedY + 64.0f + offset,
            feedWidth, 1.0f,
            Color(0.38f, 0.85f, 0.52f, 0.10f));
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

// 章の始まりのカード：0.45秒で現れ、3.15秒から薄れ始め、4.2秒で消えている
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

    // 章の名前は英字の想定なので、文字数（バイト数）から幅を出して中央に置いている
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

// 目的地の方向を、左上の小さなパネルに「前方・右・左・後方・近く」の言葉で出している
void Hud::DrawObjectiveGuide(
    const Camera& camera,
    const Vector3& origin,
    const Vector3& target)
{
    // 水平面だけで、プレイヤーの向きと目的地への向きを比べている
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

    // 内積で前か後ろか、外積のy成分で右か左かを決めている（28より近ければ「近く」）
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

// 2面の右上の状態パネル：周回の進み具合（信号盤の段階に入ったら青・黄・赤）と、足音の危険度のゲージ
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

    // 3つの区画：周回の数、信号盤の段階なら終えた色まで点け、次に押す色は暗くして示している
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

    // 危険度がある間だけ、その下に危険のゲージを出している
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

// 画面の中央に4桁の入力盤を出している。選んでいる桁は緑の枠で示し、上下の三角で増減できることを伝えている。
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
    // 不正解の直後は、枠を赤く点滅させている。
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
    // 4つの桁の箱と数字を描いている（選んでいる桁は枠を太くしている）
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
            // 上下の三角（増減できる合図）を、横幅の違う長方形を重ねて描いている。
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

    // 下のメッセージ：間違えた直後は「番号が違う」、3回以上間違えると扉の向こうの気配を知らせている
    const std::string_view message = wrongRate > 0.0f
        ? (mistakes >= 3 ? "扉の向こうで何かが動いた" : "番号が違う")
        : "壁の数字を懐中電灯で探す";
    addCenteredText(panelY + 222.0f, message, 2.4f, wrongRate > 0.0f ? red : pale);
    addCenteredText(panelY + 256.0f,
        "←→ 桁  ↑↓ 数字  E 決定  Q 戻る", 2.0f, dim);
    Flush();
}

void Hud::DrawHidingView(float elapsedSeconds, float dangerRate, float breathLeft, bool holding, float exposure)
{
    m_Vertices.clear();
    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float danger = (std::clamp)(dangerRate, 0.0f, 1.0f);
    const Color dark(0.0f, 0.0f, 0.0f, 0.94f);
    const Color edge(0.10f + danger * 0.35f, 0.02f, 0.02f, 0.55f);

    // 画面の中央付近に4本の横長のすき間を残し、それ以外を扉の内側の暗さで覆っている。
    // 呼吸に合わせて、すき間がわずかに上下している。
    constexpr int SlitCount = 4;
    const float slitHeight = screenHeight * 0.055f;
    const float slitGap = screenHeight * 0.035f;
    const float breath = std::sin(elapsedSeconds * 1.6f) * screenHeight * 0.004f;
    const float firstSlitY = screenHeight * 0.36f + breath;
    const float slitInset = screenWidth * 0.12f;

    // すき間とすき間の間を上から順に暗く塗り、すき間の左右の端も塗っている。影が近いほど、すき間の縁を赤くしている
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

    // 影が近いときと、息を止めているときだけ、息のゲージと「息を止める」操作を出している。
    // 聞かれそうになるほど、ゲージの枠が赤く点滅する
    if (danger > 0.0f || holding || breathLeft < 0.999f)
    {
        const float meterWidth = 260.0f;
        const float meterX = (screenWidth - meterWidth) * 0.5f;
        const float meterY = screenHeight - 140.0f;
        const float warning = (std::clamp)(exposure, 0.0f, 1.0f);
        const float blink = 0.5f + 0.5f * std::sin(elapsedSeconds * 18.0f);
        const Color frame(0.30f + warning * blink * 0.6f, 0.30f - warning * 0.2f, 0.28f - warning * 0.2f, 0.85f);
        AddRectangle(meterX - 2.0f, meterY - 2.0f, meterWidth + 4.0f, 14.0f, frame);
        AddRectangle(meterX, meterY, meterWidth, 10.0f, Color(0.02f, 0.03f, 0.03f, 0.95f));
        const float remaining = (std::clamp)(breathLeft, 0.0f, 1.0f);
        const Color fill = remaining < 0.25f
            ? Color(0.85f, 0.30f, 0.20f, 0.95f)
            : Color(0.45f, 0.72f, 0.80f, 0.95f);
        AddRectangle(meterX, meterY, meterWidth * remaining, 10.0f, fill);

        const std::string_view breathPrompt = holding
            ? "息を止めている"
            : (Input::IsControllerConnected() ? "RB 長押しで息を止める" : "SPACE 長押しで息を止める");
        constexpr float breathPixelSize = 2.0f;
        const float breathWidth =
            static_cast<float>(CountDisplayedCharacters(breathPrompt)) * breathPixelSize * 6.0f;
        AddText((screenWidth - breathWidth) * 0.5f, meterY - 26.0f, breathPrompt, breathPixelSize,
            holding ? Color(0.62f, 0.78f, 0.84f, 0.90f) : Color(0.86f, 0.62f, 0.52f, 0.90f));
    }
    Flush();
}

// 息を殺して影をやり過ごす操作の進み具合（成功・影が近すぎる・呼吸を整えている間・止まっている時間）を出している
void Hud::DrawQuietRecovery(float progressRate, float cooldown, bool success, bool tooClose)
{
    m_Vertices.clear();
    const float x = (std::max)(12.0f, GetCanvasWidth() - 330.0f);
    // 右上の「記録」（Hud::Drawで描いている。下端は108＋m_TopRightOffset）のすぐ下に置いている。
    const float y = 116.0f + m_TopRightOffset;
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
    // ゲージ：成功なら満タン、呼吸を整えている間は10秒で回復、それ以外は止まっている時間の進み具合
    const float fill = success ? 1.0f : (cooldown > 0.0f ?
        1.0f - cooldown / 10.0f : progressRate);
    AddRectangle(x + 10.0f, y + 58.0f, 276.0f * (std::clamp)(fill, 0.0f, 1.0f), 6.0f, color);
    Flush();
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
    // 描画解像度は次回の起動から反映されるため、起動時と違う段階を選んでいるときは、そのことを表示している。
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
    // 操作の説明は、コントローラーがつながっているかで切り替えている
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

// 技術デモの操作パネル。左の端に半透明の黒い板を置き、選んでいる項目を緑の帯と「< >」で示している
void Hud::DrawTechDemoPanel(
    const std::vector<TechDemoRow>& rows,
    const std::vector<std::string>& infoLines)
{
    m_Vertices.clear();

    const Color green(0.45f, 0.95f, 0.55f, 1.0f);
    const Color pale(0.84f, 0.88f, 0.86f, 0.95f);
    const Color dim(0.58f, 0.63f, 0.60f, 0.92f);
    constexpr float panelX = 24.0f;
    constexpr float panelY = 140.0f;
    constexpr float panelWidth = 500.0f;
    constexpr float rowTop = 86.0f;
    constexpr float rowHeight = 30.0f;
    constexpr float infoLineHeight = 26.0f;
    const float infoTop = rowTop + rowHeight * static_cast<float>(rows.size()) + 14.0f;
    const float panelHeight = infoTop + infoLineHeight * static_cast<float>(infoLines.size()) + 12.0f;

    AddRectangle(panelX, panelY, panelWidth, panelHeight, Color(0.0f, 0.0f, 0.0f, 0.74f));
    AddRectangle(panelX, panelY, 4.0f, panelHeight, green);
    AddText(panelX + 18.0f, panelY + 14.0f, "技術デモ", 2.6f, green);
    AddText(panelX + 18.0f, panelY + 52.0f, "↑↓ 項目   ←→ 切り替え   F2 閉じる", 1.8f, dim);

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const TechDemoRow& row = rows[index];
        const float rowY = panelY + rowTop + rowHeight * static_cast<float>(index);
        if (row.Selected)
        {
            AddRectangle(panelX + 8.0f, rowY - 5.0f, panelWidth - 16.0f, rowHeight - 2.0f,
                Color(0.30f, 0.80f, 0.40f, 0.16f));
        }
        const Color& color = row.Selected ? green : pale;
        AddText(panelX + 18.0f, rowY, row.Name, 2.0f, color);
        const std::string variant = row.Selected
            ? "< " + std::string(row.Variant) + " >"
            : std::string(row.Variant);
        AddText(panelX + 250.0f, rowY, variant, 2.0f, color);
    }

    // 区切りの線と、選んだ項目の説明・GPU時間
    AddRectangle(panelX + 14.0f, panelY + infoTop - 8.0f, panelWidth - 28.0f, 1.0f, dim);
    for (std::size_t index = 0; index < infoLines.size(); ++index)
    {
        AddText(panelX + 18.0f, panelY + infoTop + infoLineHeight * static_cast<float>(index),
            infoLines[index], 1.8f, pale);
    }

    Flush();
}

// 1面の前の導入：後ろの森を暗く沈め、受信記録（見出しと、1行ずつタイプされる3行）を左寄せで、
// 「通信途絶」を中央に、最後に目的と操作を中央のパネルに描いている。noise（0〜1）で文字を横にずらし、色をにじませている
void Hud::DrawIntro(const IntroView& view)
{
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float noise = (std::clamp)(view.Noise, 0.0f, 1.0f);
    const auto hash = [](float value)
    {
        const float s = std::sin(value * 12.9898f) * 43758.5453f;
        return s - std::floor(s);
    };
    const auto textWidth = [](std::string_view text, float pixelSize)
    {
        return static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * 6.0f;
    };
    const float noiseStep = std::floor(view.Time * 24.0f);

    // 後ろの森を暗く沈め（目的の画面ではさらに暗く）、薄い走査線を重ねている
    const float shade = 0.62f + 0.18f * view.MissionAlpha;
    AddRectangle(0.0f, 0.0f, screenWidth, screenHeight, Color(0.0f, 0.004f, 0.003f, shade));
    for (float y = 0.0f; y < screenHeight; y += 6.0f)
    {
        AddRectangle(0.0f, y, screenWidth, 1.0f, Color(0.0f, 0.0f, 0.0f, 0.10f));
    }

    // --- 受信記録：見出し（明滅する受信の印つき）と、タイプされる3行
    const float left = (std::max)(80.0f, screenWidth * 0.16f);
    if (view.HeaderAlpha > 0.0f)
    {
        const bool blink = std::fmod(view.Time, 1.0f) < 0.6f;
        if (blink)
        {
            AddRectangle(left, 230.0f, 12.0f, 12.0f, Color(0.48f, 0.90f, 0.58f, 0.90f * view.HeaderAlpha));
        }
        AddText(left + 22.0f, 222.0f, view.Header, 2.6f, Color(0.40f, 0.70f, 0.50f, 0.85f * view.HeaderAlpha));
    }
    if (view.LinesAlpha > 0.0f)
    {
        float y = 296.0f;
        for (std::size_t index = 0; index < view.Lines.size(); ++index)
        {
            const std::string& line = view.Lines[index];
            if (!line.empty())
            {
                // 乱れているときは、行ごとに少しずつ横へずらしている
                const float jitter = noise > 0.1f ? (hash(noiseStep + static_cast<float>(index) * 3.7f) - 0.5f) * 22.0f * noise : 0.0f;
                if (noise > 0.3f)
                {
                    AddText(left + jitter - 4.0f, y, line, 3.6f, Color(0.95f, 0.18f, 0.16f, 0.35f * noise));
                }
                AddText(left + jitter, y, line, 3.6f, Color(0.86f, 0.90f, 0.86f, 0.92f * view.LinesAlpha));
            }
            y += 66.0f;
        }
    }
    // 「通信途絶」：中央に赤く出し、色のにじみと横の帯で崩している
    if (view.SignalLostAlpha > 0.0f)
    {
        const float size = 5.5f;
        const float width = textWidth(view.SignalLost, size);
        const float x = (screenWidth - width) * 0.5f + (hash(noiseStep) - 0.5f) * 30.0f * noise;
        // 受信記録の4行目（y = 494）の下に出している
        const float y = 586.0f;
        AddText(x - 6.0f * noise, y, view.SignalLost, size, Color(0.18f, 0.85f, 0.90f, 0.40f * noise * view.SignalLostAlpha));
        AddText(x, y, view.SignalLost, size, Color(0.92f, 0.32f, 0.26f, 0.95f * view.SignalLostAlpha));
        if (noise > 0.2f)
        {
            for (int bar = 0; bar < 4; ++bar)
            {
                const float seed = noiseStep * 2.3f + static_cast<float>(bar) * 5.1f;
                AddRectangle(x - 10.0f, y + hash(seed) * size * 7.0f, width + 20.0f, 2.0f + hash(seed + 1.0f) * 4.0f,
                    Color(0.0f, 0.004f, 0.003f, 0.9f));
            }
        }
    }

    // --- 目的の画面：中央のパネルに、状況・目的・操作・始める操作を並べている
    if (view.MissionAlpha > 0.0f)
    {
        const float alpha = view.MissionAlpha;
        const float panelWidth = (std::min)(screenWidth - 120.0f, 900.0f);
        const float panelHeight = 340.0f;
        const float panelX = (screenWidth - panelWidth) * 0.5f;
        const float panelY = (screenHeight - panelHeight) * 0.5f - 20.0f;
        AddRectangle(panelX, panelY, panelWidth, panelHeight, Color(0.008f, 0.016f, 0.013f, 0.90f * alpha));
        AddRectangle(panelX, panelY, 6.0f, panelHeight, Color(0.48f, 0.90f, 0.58f, 0.96f * alpha));

        const auto centered = [&](std::string_view text, float size, float y, const Color& color)
        {
            AddText((screenWidth - textWidth(text, size)) * 0.5f, y, text, size, color);
        };
        centered(view.MissionLead, 3.0f, panelY + 44.0f, Color(0.80f, 0.86f, 0.82f, 0.95f * alpha));
        centered(view.MissionGoal, 3.6f, panelY + 112.0f, Color(0.52f, 0.92f, 0.60f, alpha));
        const bool controller = Input::IsControllerConnected();
        centered(controller ? "Y でライト　A で調べる　迷ったら LB でヒント" : "F でライト　E で調べる　迷ったら H でヒント",
            2.7f, panelY + 186.0f, Color(0.62f, 0.76f, 0.68f, 0.90f * alpha));
        const float pulse = std::sin(view.Time * 2.1f) * 0.5f + 0.5f;
        centered(controller ? "A で始める" : "ENTER で始める", 3.0f, panelY + 262.0f,
            Color(0.58f + pulse * 0.16f, 0.70f + pulse * 0.18f, 0.62f + pulse * 0.14f, (0.62f + pulse * 0.34f) * alpha));
    }
    else
    {
        // 受信記録の間は、右下に飛ばし方を小さく出している
        const std::string_view skip = Input::IsControllerConnected() ? "A で飛ばす" : "ENTER で飛ばす";
        AddText(screenWidth - textWidth(skip, 1.8f) - 60.0f, screenHeight - 70.0f, skip, 1.8f,
            Color(0.50f, 0.62f, 0.55f, 0.75f));
    }

    Flush();
}

// 外の森を歩いている間の無線の字幕：画面の下の中央に、暗い帯と、受信の印つきの見出し、タイプされる1行を出している。
// 「通信途絶」は帯の上に赤く出す。noise（0〜1）で文字を横にずらし、色をにじませている
void Hud::DrawRadio(const RadioView& view)
{
    if (view.LineAlpha <= 0.0f && view.SignalLostAlpha <= 0.0f)
    {
        return;
    }
    m_Vertices.clear();

    const float screenWidth = GetCanvasWidth();
    const float screenHeight = GetCanvasHeight();
    const float noise = (std::clamp)(view.Noise, 0.0f, 1.0f);
    const auto hash = [](float value)
    {
        const float s = std::sin(value * 12.9898f) * 43758.5453f;
        return s - std::floor(s);
    };
    const auto textWidth = [](std::string_view text, float pixelSize)
    {
        return static_cast<float>(CountDisplayedCharacters(text)) * pixelSize * 6.0f;
    };
    const float noiseStep = std::floor(view.Time * 24.0f);
    const float jitter = noise > 0.1f ? (hash(noiseStep) - 0.5f) * 22.0f * noise : 0.0f;

    if (view.LineAlpha > 0.0f)
    {
        const float alpha = view.LineAlpha;
        const float panelWidth = (std::min)(screenWidth - 120.0f, 1060.0f);
        const float panelHeight = 104.0f;
        const float panelX = (screenWidth - panelWidth) * 0.5f;
        const float panelY = screenHeight - 250.0f;
        AddRectangle(panelX, panelY, panelWidth, panelHeight, Color(0.0f, 0.006f, 0.004f, 0.62f * alpha));
        AddRectangle(panelX, panelY, 4.0f, panelHeight, Color(0.48f, 0.90f, 0.58f, 0.85f * alpha));
        // 見出し：受信中は印が明滅する
        if (std::fmod(view.Time, 1.0f) < 0.6f)
        {
            AddRectangle(panelX + 24.0f, panelY + 20.0f, 10.0f, 10.0f, Color(0.48f, 0.90f, 0.58f, 0.90f * alpha));
        }
        AddText(panelX + 42.0f, panelY + 13.0f, view.Header, 2.2f, Color(0.40f, 0.70f, 0.50f, 0.85f * alpha));
        const float lineX = panelX + 24.0f + jitter;
        const float lineY = panelY + 50.0f;
        if (noise > 0.3f)
        {
            AddText(lineX - 4.0f, lineY, view.Line, 3.2f, Color(0.95f, 0.18f, 0.16f, 0.35f * noise * alpha));
        }
        AddText(lineX, lineY, view.Line, 3.2f, Color(0.86f, 0.90f, 0.86f, 0.95f * alpha));
    }
    if (view.SignalLostAlpha > 0.0f)
    {
        const float size = 4.4f;
        const float width = textWidth(view.SignalLost, size);
        const float x = (screenWidth - width) * 0.5f + jitter;
        const float y = screenHeight - 228.0f;
        AddText(x - 6.0f * noise, y, view.SignalLost, size, Color(0.18f, 0.85f, 0.90f, 0.40f * noise * view.SignalLostAlpha));
        AddText(x, y, view.SignalLost, size, Color(0.92f, 0.32f, 0.26f, 0.95f * view.SignalLostAlpha));
        if (noise > 0.2f)
        {
            for (int bar = 0; bar < 3; ++bar)
            {
                const float seed = noiseStep * 2.3f + static_cast<float>(bar) * 5.1f;
                AddRectangle(x - 10.0f, y + hash(seed) * size * 7.0f, width + 20.0f, 2.0f + hash(seed + 1.0f) * 3.0f,
                    Color(0.0f, 0.004f, 0.003f, 0.85f));
            }
        }
    }

    Flush();
}