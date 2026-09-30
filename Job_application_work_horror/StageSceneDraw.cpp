// ============================================================================
// ファイルの役割: 1面のHUD、目的表示、進行フィードバックの描画を管理します。
// 主な技術: 描画パス分離、シャドウマップ、透明描画順
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
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

void StageScene::RenderOffscreen()
{
    // 監視映像を見ている間だけ、選んだカメラの視点を本描画の前に描きます。
    m_Surveillance.RenderFeeds();
}

void StageScene::Draw(Camera* camera)
{
    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;

    if (player == nullptr)
    {
        return;
    }

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
    // どの目的・通知・ヒントを出すかの優先順位はSelectStage1Objectiveにまとめています。
    const std::string objectiveText = SelectStage1Objective(MakeObjectiveInput());

    // 目的表示なしの設定では、何をすべきかを説明しない静かな画面にします。
    m_Hud.Draw(
        *player,
        fuseCount,
        m_InteractionSystem.GetPrompt(),
        game->IsGuideEnabled() ? std::string_view(objectiveText) : std::string_view{});

    if (game->IsGuideEnabled() &&
        m_StageVisualTimer >= 4.20f &&
        (exitTrigger == nullptr || !exitTrigger->IsEscaping()))
    {
        Vector3 guideTarget(0.0f, -99.0f, 315.0f);
        if (game->IsPowerRestored() && !exitPowerActivated)
        {
            guideTarget = Vector3(145.0f, -90.0f, 270.0f);
        }
        else if (game->IsPowerRestored())
        {
            guideTarget = Vector3(202.0f, -74.0f, 307.5f);
        }
        // ヒューズの位置はプレイごとに変わるため、配置したObjectから読みます。
        else if (fuseCount <= 0 && m_Objects.firstFuse != nullptr)
        {
            guideTarget = m_Objects.firstFuse->GetPosition();
        }
        else if (fuseCount == 1 && m_CorridorLoopCount >= 1 && m_Objects.secondFuse != nullptr)
        {
            guideTarget = m_Objects.secondFuse->GetPosition();
        }
        else if (fuseCount == 2 && m_CorridorLoopCount >= 2 && m_Objects.thirdFuse != nullptr)
        {
            guideTarget = m_Objects.thirdFuse->GetPosition();
        }
        else if (fuseCount >= 3)
        {
            guideTarget = Vector3(-180.0f, -90.0f, 35.0f);
        }
        m_Hud.DrawObjectiveGuide(
            *camera,
            player->GetPosition(),
            guideTarget);
    }

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
    if (m_Surveillance.IsCaughtActive())
    {
        m_Hud.DrawBlink(m_Surveillance.GetCaughtFadeRate() * 0.96f);
    }
    m_KeypadDoor.Draw(m_Hud);

    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->IsGuideEnabled(),
            game->GetPauseSettingIndex(),
            1,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
}

// 目的表示の文章を選ぶための状態を、各仕組みから読み取って集めます。
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
    return input;
}
