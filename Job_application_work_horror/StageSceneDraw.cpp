// ============================================================================
// ファイルの役割: 1面のHUD（目的・目的地の方向・章のカード・暗転・暗証番号の入力・ポーズ）と、監視映像の描画を担当している。
// 主な技術: 状態に応じたUIの重ね方、描く順番の制御、目的表示の選択を別の関数に任せる設計
// ============================================================================

#include "StageScene.h"
#include "Game.h"
#include "Input.h"

#include "Player.h"
#include "Ground.h"
#include "Wall.h"
#include "Item.h"
#include "Door.h"
#include "FuseBox.h"
#include "CeilingLight.h"
#include "ExitTrigger.h"
#include "BatteryItem.h"
#include "ScreenDustOverlay.h"
#include "ScareTrigger.h"
#include "ShadowMan.h"
#include "KeyItem.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

void StageScene::RenderOffscreen()
{
    // 監視映像を見ている間だけ、選んだカメラの視点を本描画の前に描いている。
    m_Surveillance.RenderFeeds();
}

// 1面のHUDを、下から順に重ねて描いている
void StageScene::Draw(Camera* camera)
{
    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;

    if (player == nullptr)
    {
        return;
    }

    // 監視映像を見ている間は、映像の画面（とポーズ）だけを描いている
    if (m_Surveillance.IsViewing())
    {
        m_Surveillance.DrawFeed(m_Hud);
        if (game->IsPaused())
        {
            m_Hud.DrawPause(
                game->GetBrightnessLevel(),
                game->GetEffectLevel(),
                game->GetLookSensitivityLevel(),
                game->GetVolumeLevel(),
                game->IsGuideEnabled(),
                game->GetResolutionLevel(),
                game->GetPauseSettingIndex(),
                1,
                game->GetRunTimeSeconds(),
                game->GetCaughtCount());
        }
        return;
    }

    FuseBox* exitPowerPanel = m_Objects.exitPowerPanel;
    const bool exitPowerActivated =
        exitPowerPanel != nullptr && exitPowerPanel->IsActivated();

    const int fuseCount = game->GetItemCount();
    ExitTrigger* exitTrigger = m_Objects.exitTrigger;
    // どの目的・知らせ・ヒントを出すかの優先順位は、SelectStage1Objectiveにまとめている。
    const std::string objectiveText = SelectStage1Objective(MakeObjectiveInput());

    // 目的表示なしの設定では、何をすべきかを説明しない静かな画面にしている。
    m_Hud.Draw(
        *player,
        fuseCount,
        m_InteractionSystem.GetPrompt(),
        game->IsGuideEnabled() ? std::string_view(objectiveText) : std::string_view{});

    // 目的地の方向：始まりの4.2秒の後、進み具合に合わせて次に行く場所を指している（普段は出口）
    if (game->IsGuideEnabled() &&
        m_StageVisualTimer >= 4.20f &&
        (exitTrigger == nullptr || !exitTrigger->IsEscaping()))
    {
        Vector3 guideTarget(0.0f, -99.0f, 315.0f);
        if (m_HiddenRoom.IsTrapped() && m_Objects.hiddenRoom.key != nullptr)
        {
            // 隠し部屋に閉じ込められている間は、鍵の場所を指している。
            guideTarget = m_Objects.hiddenRoom.key->GetPosition();
        }
        // 電力が戻った後は、出口の送電盤、次に出口の扉を指している
        else if (game->IsPowerRestored() && !exitPowerActivated)
        {
            guideTarget = Vector3(145.0f, -90.0f, 270.0f);
        }
        else if (game->IsPowerRestored())
        {
            guideTarget = Vector3(202.0f, -74.0f, 307.5f);
        }
        // ヒューズの位置はプレイごとに変わるため、配置したObjectから読んでいる。
        else if (fuseCount <= 0 && m_Objects.firstFuse != nullptr)
        {
            guideTarget = m_Objects.firstFuse->GetPosition();
        }
        else if (fuseCount == 1 && m_CorridorLoopCount >= 1 && m_Objects.secondFuse != nullptr)
        {
            guideTarget = m_Objects.secondFuse->GetPosition();
        }
        else if (fuseCount == 2 && m_CorridorLoopCount >= 2)
        {
            // 3本目は西棟の奥。鍵 → 西側の扉 → ヒューズの順に指している。
            guideTarget = m_WestWing.GetGuideTarget();
        }
        // ヒューズが3本そろったら、配電盤を指している
        else if (fuseCount >= 3)
        {
            guideTarget = Vector3(-180.0f, -90.0f, 35.0f);
        }
        m_Hud.DrawObjectiveGuide(
            *camera,
            player->GetPosition(),
            guideTarget);
    }

    // 始まりの0.65秒は黒からだんだん明るくし、4.2秒までは章のカードを出している
    if (m_StageVisualTimer < 0.65f)
    {
        const float fade = 1.0f - m_StageVisualTimer / 0.65f;
        m_Hud.DrawBlink(fade * fade);
    }
    if (m_StageVisualTimer < 4.20f)
    {
        m_Hud.DrawChapterCard(
            "1階", "ヒューズを集めて電力を復旧する", m_StageVisualTimer);
    }
    // 監視カメラの確認で捕まったときは、画面を暗くしている
    if (m_Surveillance.IsCaughtActive())
    {
        m_Hud.DrawBlink(m_Surveillance.GetCaughtFadeRate() * 0.96f);
    }
    // 暗証番号の入力画面を開いていれば描いている
    m_KeypadDoor.Draw(m_Hud);

    // ポーズ中はポーズメニューを重ねている（1階として表示）
    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->IsGuideEnabled(),
            game->GetResolutionLevel(),
            game->GetPauseSettingIndex(),
            1,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
}

// 目的表示の文章を選ぶための状態を、各仕組みから読み取って集めている。
Stage1ObjectiveInput StageScene::MakeObjectiveInput() const
{
    const Core::Game* game = Core::Game::GetInstance();
    Stage1ObjectiveInput input;
    input.fuseCount = game->GetItemCount();
    input.corridorLoopCount = m_CorridorLoopCount;
    input.progressHintSeconds = m_ProgressHintTimer;

    input.powerRestored = game->IsPowerRestored();
    input.powerRestoreActive = m_PowerSequence.IsRestoreActive();
    input.powerRestoreSeconds = m_PowerSequence.GetRestoreTimer();
    input.exitPowerActivated =
        m_Objects.exitPowerPanel != nullptr && m_Objects.exitPowerPanel->IsActivated();
    input.exitPowerReady = m_PowerSequence.IsExitComplete();
    input.exitDoorOpen = m_Objects.exitDoor != nullptr && m_Objects.exitDoor->IsOpen();
    input.escaping =
        m_Objects.exitTrigger != nullptr && m_Objects.exitTrigger->IsEscaping();

    if (m_Objects.player != nullptr)
    {
        m_Surveillance.FillObjectiveInput(input, *m_Objects.player);
    }

    input.fuseWatcherState = m_FuseWatcherState;
    input.storageScarePhase = m_StorageScarePhase;
    input.exitOmenSeconds = m_ExitOmenSequence.GetTimer();
    input.scareLightNoticeSeconds = m_ScareLightSequence.GetNoticeTimer();

    input.chargerNotice = m_ChargerNoticeTimer > 0.0f;
    input.evidenceNotice = m_EvidenceNoticeTimer > 0.0f;
    input.storageScareNotice = m_StorageScareNoticeTimer > 0.0f;
    input.fuseNotice = m_FuseNoticeTimer > 0.0f;
    input.fuseWatcherNotice = m_FuseWatcherNoticeTimer > 0.0f;
    input.loopNotice = m_LoopNoticeTimer > 0.0f;
    input.hiddenRoomText = m_HiddenRoom.GetObjectiveText();
    input.archiveStalkerText = m_ArchiveStalkerNoticeTimer > 0.0f
        ? m_ArchiveStalkerNotice
        : std::string_view{};
    input.westWingStep = static_cast<int>(m_WestWing.GetStep());
    return input;
}
