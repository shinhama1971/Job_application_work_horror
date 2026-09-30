// ============================================================================
// ファイルの役割: 2面の目的、ヒント、異変フィードバックの描画を管理します。
// 主な技術: マルチパス描画、描画順制御、シャドウマップ
// ============================================================================

#include "Stage2Scene.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace DirectX::SimpleMath;

#include "Stage2SceneConstants.h"


void Stage2Scene::Draw(Camera* camera)
{
    (void)camera;
    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    if (player == nullptr)
    {
        return;
    }

    ExitTrigger* exit = m_Objects.exit;
    Door* finalDoor = m_Objects.door;
    FuseBox* confirmationPanel =
        m_Objects.confirmationPanel;
    const bool confirmationPending =
        confirmationPanel != nullptr &&
        !confirmationPanel->IsActivated() &&
        IsRequiredAnomalyFound();
    // どの目的・通知・ヒントを出すかの優先順位はSelectStage2Objectiveにまとめています。
    const std::string_view objective =
        SelectStage2Objective(MakeObjectiveInput(confirmationPending));

    // 目的表示なしの設定では、何をすべきかを説明しない静かな画面にします。
    m_Hud.Draw(*player, -1, m_InteractionSystem.GetPrompt(),
        game->IsGuideEnabled() ? objective : std::string_view{});
    float threatRate = 0.0f;
    threatRate = (std::max)(
        threatRate, m_NoiseThreatSystem.GetThreat() * 0.78f);
    if (m_LoopCount == 1 || m_LoopCount == 2)
    {
        const float observationDanger =
            static_cast<float>(m_PuzzleFeedback.GetMistakeCount()) / 3.0f;
        threatRate = (std::max)(threatRate, observationDanger * 0.72f);
    }
    if (m_FinalSequence.IsPursuitActive())
    {
        ShadowMan* shadow = m_Objects.shadow;
        if (shadow != nullptr)
        {
            Vector3 toShadow = shadow->GetPosition() - player->GetPosition();
            toShadow.y = 0.0f;
            const float distance = toShadow.Length();
            threatRate = 1.0f - (std::clamp)(
                (distance - 18.0f) / 92.0f, 0.0f, 1.0f);
        }
    }
    ShadowMan* noiseShadow = m_Objects.noiseShadow;
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player->GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float noiseShadowDanger = 1.0f - (std::clamp)(
            (distance - 14.0f) / 72.0f, 0.0f, 1.0f);
        threatRate = (std::max)(threatRate, noiseShadowDanger);
    }
    if (game->IsGuideEnabled() &&
        m_VisualTimer >= 4.20f &&
        !m_CaughtSequence.IsActive() &&
        (exit == nullptr || !exit->IsEscaping()))
    {
        m_Hud.DrawStage2Status(
            m_LoopCount, threatRate, m_FinalDoorReady,
            m_SignalPuzzle.GetStep(),
            m_LoopCount >= 3 && !m_SignalPuzzle.IsComplete());
        if (m_LoopCount < 3 && !m_ObservedScareSequence.IsActive() &&
            !m_FinalSequence.IsPursuitActive() &&
            !m_FinalSequence.IsSequenceActive() &&
            m_LoopTransitionTimer < 0.0f &&
            (m_NoiseThreatSystem.GetThreat() > 0.25f ||
                m_QuietRecovery.progress > 0.0f ||
                m_QuietRecovery.successNotice > 0.0f || m_QuietRecovery.cooldown > 0.0f))
        {
            m_Hud.DrawQuietRecovery(
                m_QuietRecovery.progress / QuietRecovery::RequiredSeconds,
                m_QuietRecovery.cooldown, m_QuietRecovery.successNotice > 0.0f,
                m_QuietRecovery.tooClose);
        }
    }
    if (m_VisualTimer >= 4.20f &&
        !m_CaughtSequence.IsActive() &&
        (exit == nullptr || !exit->IsEscaping()))
    {
        Vector3 guideTarget = finalDoor != nullptr
            ? finalDoor->GetPosition()
            : Vector3(0.0f, -74.0f, 140.0f);
        const Stage2Anomaly requiredAnomaly = m_AnomalyPlan.GetRequired(m_LoopCount);
        if (requiredAnomaly != Stage2Anomaly::None && !IsRequiredAnomalyFound())
        {
            // 探すべき異変の場所を指します（偽ドア・時計・肖像画）。
            guideTarget =
                requiredAnomaly == Stage2Anomaly::FalseDoor ? Vector3(-38.3f, -72.0f, 70.0f) :
                requiredAnomaly == Stage2Anomaly::Clock ? Vector3(-38.0f, -70.0f, -25.0f) :
                Vector3(38.72f, -69.0f, -25.0f);
        }
        else if (confirmationPending)
        {
            guideTarget = Vector3(35.5f, -90.0f, 112.0f);
        }
        else if (m_LoopCount >= 3 && !m_SignalPuzzle.IsComplete())
        {
            if (m_SignalPuzzle.GetStep() == 0)
            {
                guideTarget = Vector3(35.5f, -90.0f, 82.0f);
            }
            else if (m_SignalPuzzle.GetStep() == 1)
            {
                guideTarget = Vector3(-35.5f, -90.0f, 18.0f);
            }
            else
            {
                guideTarget = Vector3(35.5f, -90.0f, -108.0f);
            }
        }
        m_Hud.DrawObjectiveGuide(
            *camera, player->GetPosition(), guideTarget);
    }
    if (m_VisualTimer < 0.65f)
    {
        const float fade = 1.0f - m_VisualTimer / 0.65f;
        m_Hud.DrawBlink(fade * fade);
    }
    if (m_VisualTimer < 4.20f)
    {
        m_Hud.DrawChapterCard(
            "2階",
            "廊下の変化を見逃さない",
            m_VisualTimer);
    }
    else if (m_LoopTransitionTimer >= 0.0f)
    {
        constexpr std::string_view cycleTitles[] =
        {
            "1回目を通過",
            "2回目を通過",
            "3回目を通過"
        };
        constexpr std::string_view cycleSubtitles[] =
        {
            "同じ廊下へ戻ってきた",
            "何かが移動している",
            "信号復旧を開始する"
        };
        const size_t cycleIndex = static_cast<size_t>((std::clamp)(
            m_LoopCount - 1, 0, 2));
        m_Hud.DrawChapterCard(
            cycleTitles[cycleIndex],
            cycleSubtitles[cycleIndex],
            m_LoopTransitionTimer);
    }
    if (m_LoopBlinkTimer > 0.0f)
    {
        const float blinkRate =
            (std::clamp)(m_LoopBlinkTimer / 0.28f, 0.0f, 1.0f);
        m_Hud.DrawBlink(blinkRate * blinkRate * 0.90f);
    }

    if (m_CaughtSequence.IsActive())
    {
        const float caughtFade = m_CaughtSequence.GetFadeRate();
        m_Hud.DrawBlink(caughtFade * 0.96f);
    }

    if (exit != nullptr && exit->IsEscaping())
    {
        const float fadeRate = (std::clamp)(
            (exit->GetEscapeProgress() - 0.36f) / 0.64f,
            0.0f, 1.0f);
        const float smoothFade =
            fadeRate * fadeRate * (3.0f - 2.0f * fadeRate);
        m_Hud.DrawBlink(smoothFade * 0.90f);
    }

    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->IsGuideEnabled(),
            game->GetPauseSettingIndex(),
            2,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
}

// 目的表示の文章を選ぶための状態を、各仕組みから読み取って集めます。
Stage2ObjectiveInput Stage2Scene::MakeObjectiveInput(bool confirmationPending) const
{
    Stage2ObjectiveInput input;
    input.loopCount = m_LoopCount;
    input.progressHintSeconds = m_ProgressHintTimer;
    input.controllerConnected = Input::IsControllerConnected();

    input.finalDoorOpen = m_Objects.door != nullptr && m_Objects.door->IsOpen();
    input.escaping = m_Objects.exit != nullptr && m_Objects.exit->IsEscaping();
    input.finalDoorReady = m_FinalDoorReady;
    input.confirmationPending = confirmationPending;
    input.confirmationHandledThisLoop = m_ConfirmationHandledThisLoop;

    input.requiredAnomaly = m_AnomalyPlan.GetRequired(m_LoopCount);
    input.requiredAnomalyFound = IsRequiredAnomalyFound();
    input.falseDoorObserved = m_FalseDoorAnomaly.WasObserved();
    input.portraitStaring = m_PortraitAnomaly.IsStaring();

    input.signalPuzzleComplete = m_SignalPuzzle.IsComplete();
    input.signalPuzzleStep = m_SignalPuzzle.GetStep();
    input.puzzleFeedbackVisible = m_PuzzleFeedback.IsVisible();
    input.puzzleFeedbackType = m_PuzzleFeedback.GetType();
    input.puzzleMistakeCount = m_PuzzleFeedback.GetMistakeCount();

    input.caught = m_CaughtSequence.IsActive();
    input.presenceTooClose = m_QuietRecovery.tooClose;
    input.quietRecoverySucceeded = m_QuietRecovery.successNotice > 0.0f;
    input.quietRecoveryInProgress = m_QuietRecovery.progress > 0.0f;
    input.noiseShadowActive =
        m_Objects.noiseShadow != nullptr && m_Objects.noiseShadow->IsActive();
    input.finalGazePenalty = m_FinalSequence.HasGazePenalty();
    input.finalSequenceActive = m_FinalSequence.IsSequenceActive();
    input.finalPursuitActive = m_FinalSequence.IsPursuitActive();
    input.finalSequenceArmed = m_FinalSequenceArmed;

    input.observedScareNotice = m_ObservedScareSequence.GetNoticeTimer() > 0.0f;
    input.chargerNotice = m_Notices.charger > 0.0f;
    input.evidenceNotice = m_Notices.evidence > 0.0f;
    input.signalNotice = m_Notices.signal > 0.0f;
    input.stalkerNotice = m_NoiseThreatSystem.GetStalkerNoticeTimer() > 0.0f;
    input.wetStepNotice = m_NoiseThreatSystem.GetWetStepNoticeTimer() > 0.0f;
    input.noiseWarning = m_NoiseThreatSystem.GetWarningTimer() > 0.0f;
    input.scratchNotice = m_ScratchAnomaly.GetNoticeTimer() > 0.0f;
    input.falseDoorNotice = m_FalseDoorAnomaly.GetNoticeTimer() > 0.0f;
    input.clockNotice = m_ClockAnomaly.GetNoticeTimer() > 0.0f;
    input.portraitNotice = m_PortraitAnomaly.GetNoticeTimer() > 0.0f;
    input.loopNotice = m_Notices.loop > 0.0f;
    return input;
}
