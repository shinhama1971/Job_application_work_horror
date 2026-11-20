// ============================================================================
// ファイルの役割: リザルト画面・暗証番号の入力画面・ポーズメニューを描いている。
// 主な技術: 評価の点数の計算と表示、選んでいる項目の強調表示、フェードイン
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
