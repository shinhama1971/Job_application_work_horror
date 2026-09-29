// ============================================================================
// ファイルの役割: 2面の足音危険度、時計、信号パズル、偽ドア状態を管理します。
// 主な技術: 入力列照合、状態機械、環境パズル、進行ゲート
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


void Stage2Scene::UpdateNoiseThreat(Player& player, float deltaTime)
{
    if (m_FinalSequence.IsSequenceActive() ||
        m_FinalSequence.IsPursuitActive())
    {
        m_QuietRecovery.Reset();
        m_NoiseThreatSystem.SetThreat((std::max)(0.0f,
            m_NoiseThreatSystem.GetThreat() - deltaTime * 0.8f));
        ShadowMan* noiseShadow = m_Objects.noiseShadow;
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        return;
    }

    const bool signalStealthActive =
        m_LoopCount >= 3 && !m_SignalPuzzle.IsComplete();
    if (m_LoopCount >= 3 && !signalStealthActive)
    {
        m_NoiseThreatSystem.SetThreat((std::max)(0.0f,
            m_NoiseThreatSystem.GetThreat() - deltaTime * 0.8f));
        ShadowMan* noiseShadow = m_Objects.noiseShadow;
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        return;
    }

    const float surfacePulse = player.GetSurfaceNoisePulse();
    if (surfacePulse > 0.0f)
    {
        m_NoiseThreatSystem.SetThreat((std::min)(1.0f,
            m_NoiseThreatSystem.GetThreat() +
                surfacePulse * (signalStealthActive ? 0.18f : 0.11f)));
        if (surfacePulse >= 0.60f)
        {
            m_NoiseThreatSystem.SetWetStepNoticeTimer(1.8f);
        }
    }

    const float change = player.IsSprinting()
        ? deltaTime * (signalStealthActive ? 0.48f : 0.36f)
        : -deltaTime * (signalStealthActive ? 0.12f : 0.22f);
    m_NoiseThreatSystem.SetThreat((std::clamp)(
        m_NoiseThreatSystem.GetThreat() + change, 0.0f, 1.0f));

    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* noiseShadow =
        m_Objects.noiseShadow;
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float proximity = 1.0f - (std::clamp)(
            (distance - 14.0f) / 72.0f, 0.0f, 1.0f);
        m_NoiseThreatSystem.SetThreat((std::max)(
            m_NoiseThreatSystem.GetThreat(), proximity * 0.92f));

        if (distance <= 15.5f)
        {
            StartCaughtSequence(
                player, CaughtSequence::Reason::NoiseStalker);
            return;
        }
    }

    // 初めの3周では、距離を取って消灯・静止すると足音の影を振り切れます。
    // 信号パズルと最終追跡はそれぞれの対処法を維持します。
    bool nearbyThreat = false;
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 offset = noiseShadow->GetPosition() - player.GetPosition();
        offset.y = 0.0f;
        nearbyThreat = offset.LengthSquared() <= 25.0f * 25.0f;
    }
    const bool quietEligible = m_LoopCount < 3 &&
        !m_ObservedScareSequence.IsActive() &&
        m_LoopTransitionTimer < 0.0f &&
        (m_NoiseThreatSystem.GetThreat() > 0.05f ||
            m_QuietRecovery.progress > 0.0f ||
            (noiseShadow != nullptr && noiseShadow->IsActive()));
    if (m_QuietRecovery.Update(deltaTime, quietEligible,
        !player.IsMovingHorizontally(), !player.IsFlashlightOn(), nearbyThreat))
    {
        m_NoiseThreatSystem.SetThreat((std::max)(
            0.0f, m_NoiseThreatSystem.GetThreat() - 0.45f));
        if (noiseShadow != nullptr) noiseShadow->SetActive(false);
        m_NoiseThreatSystem.SetStalkerCooldown((std::max)(
            m_NoiseThreatSystem.GetStalkerCooldown(), 8.0f));
        m_NoiseThreatSystem.SetWarningTimer(0.0f);
        // 成功時は強いフラッシュを避け、視界の落ち着きで成功を伝えます。
        game->GetPostProcess()->TriggerHorrorPulse(0.08f, 0.16f);
        game->GetPostProcess()->TriggerBloomPulse(0.18f, 0.16f);
        Input::SetVibration(2, 0.06f);
    }

    const bool canSpawnNoiseStalker =
        m_NoiseThreatSystem.GetThreat() >= 0.74f &&
        m_NoiseThreatSystem.GetStalkerCooldown() <= 0.0f &&
        !m_FinalSequence.IsSequenceActive() &&
        !m_FinalSequence.IsPursuitActive() &&
        noiseShadow != nullptr && !noiseShadow->IsActive();
    if (canSpawnNoiseStalker)
    {
        const Vector3 forward = player.GetForward();
        Vector3 spawnPosition = player.GetPosition() - forward * 68.0f;
        spawnPosition.x = (std::clamp)(spawnPosition.x, -30.0f, 30.0f);
        spawnPosition.y = -99.0f;
        spawnPosition.z = (std::clamp)(spawnPosition.z, -145.0f, 128.0f);

        noiseShadow->SetPosition(
            spawnPosition.x, spawnPosition.y, spawnPosition.z);
        noiseShadow->SetActive(true);
        noiseShadow->EnableChase(
            13.0f + m_NoiseThreatSystem.GetThreat() * 6.0f, 12.0f);
        noiseShadow->EnableGazeScare(7.5f);
        noiseShadow->SetOnObserved([this]()
        {
            ShadowMan* currentShadow =
                m_Objects.noiseShadow;
            if (currentShadow != nullptr)
            {
                currentShadow->SetActive(false);
            }
            m_NoiseThreatSystem.SetThreat((std::max)(
                0.10f, m_NoiseThreatSystem.GetThreat() - 0.38f));
            m_NoiseThreatSystem.SetStalkerCooldown(6.0f);
            m_NoiseThreatSystem.SetStalkerNoticeTimer(2.4f);
        });
        m_NoiseThreatSystem.SetStalkerCooldown(8.5f);
        m_NoiseThreatSystem.SetStalkerNoticeTimer(2.8f);
        m_NoiseThreatSystem.SetWarningTimer((std::max)(
            m_NoiseThreatSystem.GetWarningTimer(), 2.8f));
        game->PlayAudioCue(SOUND_CUE_SCARE, 0.78f);
        game->GetPostProcess()->TriggerHorrorPulse(0.42f, 0.38f);
        Input::SetVibration(8, 0.22f);
    }

    if (signalStealthActive && m_NoiseThreatSystem.GetThreat() >= 0.98f)
    {
        ResetSignalPuzzle();
        RegisterPuzzleMistake(4);
        m_PuzzleFeedback.Set(4, 2.8f);
        m_Notices.signal = 3.0f;
        m_NoiseThreatSystem.SetWarningTimer(3.0f);
        m_NoiseThreatSystem.SetThreat(0.30f);
        m_NoiseThreatSystem.SetEventCooldown(2.8f);

        game->GetPostProcess()->TriggerHorrorPulse(0.72f, 0.48f);
        game->GetPostProcess()->TriggerBloomPulse(0.90f, 0.24f);
        Input::SetVibration(13, 0.30f);
        return;
    }
    if (m_NoiseThreatSystem.GetThreat() < 0.70f ||
        m_NoiseThreatSystem.GetEventCooldown() > 0.0f)
    {
        return;
    }

    const float playerZ = player.GetPosition().z;
    const Stage2Light reactionLightId = playerZ < -55.0f
        ? Stage2Light::Light1
        : (playerZ < 18.0f
            ? Stage2Light::Light2
            : (playerZ < 88.0f ? Stage2Light::Light3 : Stage2Light::DoorLight));

    CeilingLight* reactionLight = m_Objects.Light(reactionLightId);
    if (reactionLight != nullptr)
    {
        reactionLight->TriggerEventFlicker(
            0.48f + m_NoiseThreatSystem.GetThreat() * 0.48f,
            0.46f + m_NoiseThreatSystem.GetThreat() * 0.42f);
    }

    m_NoiseThreatSystem.SetWarningTimer(2.1f);
    m_NoiseThreatSystem.SetEventCooldown(2.35f);
    game->GetPostProcess()->TriggerHorrorPulse(
        0.10f + m_NoiseThreatSystem.GetThreat() * 0.16f,
        0.22f);
    Input::SetVibration(
        3 + static_cast<int>(m_NoiseThreatSystem.GetThreat() * 4.0f),
        0.08f + m_NoiseThreatSystem.GetThreat() * 0.08f);
}

void Stage2Scene::AdvanceLoop(Player& player)
{
    m_QuietRecovery.Reset();
    Core::Game* game = Core::Game::GetInstance();
    ++m_LoopCount;
    m_LoopCooldown = 1.0f;
    m_ProgressHintTimer = 0.0f;
    m_GuidancePulseCooldown = 1.2f;
    m_LoopBlinkTimer = 0.28f;
    m_LoopTransitionTimer = 0.0f;
    m_Notices.loop = 3.0f;
    m_LightZoneProgress.Reset();
    m_ScratchAnomaly.ResetProgressForLoop();
    m_PortraitAnomaly.ResetProgressForLoop();
    m_FalseDoorAnomaly.ResetProgressForLoop();
    m_ConfirmationHandledThisLoop = false;
    ResetSignalPuzzle();
    m_PuzzleFeedback.Reset();
    m_NoiseThreatSystem.SetThreat(0.12f);
    m_NoiseThreatSystem.SetEventCooldown(1.0f);
    m_NoiseThreatSystem.SetWarningTimer(0.0f);
    m_NoiseThreatSystem.SetStalkerCooldown(2.0f);
    m_NoiseThreatSystem.SetStalkerNoticeTimer(0.0f);
    ShadowMan* noiseShadow =
        m_Objects.noiseShadow;
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
    FuseBox* confirmationPanel =
        m_Objects.confirmationPanel;
    if (confirmationPanel != nullptr)
    {
        confirmationPanel->ResetActivation();
        confirmationPanel->SetManualInteractionAllowed(false);
    }
    player.SetPosition(Vector3(0.0f, -99.0f, -125.0f));
    Door* loopDoor = m_Objects.door;
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(m_LoopCount);
        // 各周回には発見必須の変化を一つ設け、気付くまで同じ扉を開けません。
        // 同じ廊下を直進するだけでなく、周囲を観察するゲームプレイにします。
        loopDoor->SetLocked(m_LoopCount > 0);
    }

    for (CeilingLight* light : m_Objects.lights)
    {
        if (light != nullptr)
        {
            light->SetForcedOff(false);
        }
    }

    ConfigureClockForLoop();

    game->GetPostProcess()->TriggerHorrorPulse(
        0.26f + static_cast<float>(m_LoopCount) * 0.13f,
        0.38f + static_cast<float>(m_LoopCount) * 0.10f);
    game->GetPostProcess()->TriggerBloomPulse(
        0.46f + static_cast<float>(m_LoopCount) * 0.08f,
        0.26f);
    Input::SetVibration(6 + m_LoopCount * 3, 0.18f);

    for (int markIndex = 0; markIndex < 3; ++markIndex)
    {
        Wall* cycleMark = m_Objects.cycleMarks[static_cast<std::size_t>(markIndex)];
        if (cycleMark == nullptr)
        {
            continue;
        }

        const bool revealed = markIndex < m_LoopCount;
        cycleMark->SetVisible(revealed);
        if (revealed)
        {
            const float emission =
                0.10f + static_cast<float>(m_LoopCount) * 0.055f;
            cycleMark->SetAppearance(
                Color(0.24f, 0.006f, 0.003f, 1.0f),
                Color(emission, 0.001f, 0.0f, 1.0f),
                20.0f);
        }
    }

    Wall* loopMark = m_Objects.loopMark;
    Wall* portrait = m_Objects.portrait;
    CeilingLight* light2 = m_Objects.Light(Stage2Light::Light2);
    CeilingLight* light3 = m_Objects.Light(Stage2Light::Light3);

    if (m_LoopCount == 1)
    {
        SetFalseDoorState(true, false);
        CeilingLight* failingLight =
            m_Objects.Light(Stage2Light::Light2);
        if (failingLight != nullptr)
        {
            failingLight->SetFaulted(true);
        }
        RevealScratchPieces(0, 3, 0.10f);
        BatteryItem* battery = m_Objects.battery;
        if (battery != nullptr)
        {
            battery->SetActive(true);
        }
        if (loopMark != nullptr)
        {
            loopMark->SetVisible(true);
            loopMark->SetAppearance(
                Color(0.28f, 0.008f, 0.004f, 1.0f),
                Color(0.16f, 0.001f, 0.0f, 1.0f), 22.0f);
        }
        if (light2 != nullptr)
        {
            light2->SetEmergencyLight(true, 5.2f);
        }
    }
    else if (m_LoopCount == 2)
    {
        SetFalseDoorState(false, false);
        CeilingLight* entranceLight =
            m_Objects.Light(Stage2Light::Light1);
        CeilingLight* farLight =
            m_Objects.Light(Stage2Light::Light3);
        if (entranceLight != nullptr)
        {
            entranceLight->SetFaulted(true);
        }
        if (farLight != nullptr)
        {
            farLight->SetFaulted(true);
        }
        RevealScratchPieces(3, 9, 0.15f);
        if (portrait != nullptr)
        {
            portrait->SetAppearance(
                Color(0.12f, 0.018f, 0.012f, 1.0f),
                Color(0.045f, 0.0f, 0.0f, 1.0f), 12.0f);
        }
        if (light3 != nullptr)
        {
            light3->SetEmergencyLight(true, 7.1f);
        }
        ShadowMan* shadow = m_Objects.shadow;
        if (shadow != nullptr)
        {
            shadow->SetActive(true);
            shadow->EnableGazeScare(9.0f);
            shadow->SetOnObserved(
                [this]()
                {
                    StartObservedScare();
                });
        }
    }
    else
    {
        SetFalseDoorState(false, false);
        RevealScratchPieces(9, Stage2ScratchCount, 0.24f);
        // 最終周では三つの信号パネルを復旧する必要があります。
        // 廊下を探索して表示された色順を正しく入力した後だけ追跡を開始します。
        m_FinalSequenceArmed = false;
        CeilingLight* doorLight = m_Objects.Light(Stage2Light::DoorLight);
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(1.5f, 0.94f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.64f, 0.58f);
    }
}

void Stage2Scene::ConfigureClockForLoop()
{
    m_ClockAnomaly.ConfigureForLoop(m_LoopCount);
    if (m_LoopCount >= 3)
    {
        Wall* face = m_Objects.clockFace;
        if (face != nullptr)
        {
            face->SetAppearance(
                Color(0.075f, 0.018f, 0.012f, 1.0f),
                Color(0.018f, 0.001f, 0.0f, 1.0f),
                10.0f);
        }
    }
}

void Stage2Scene::UpdateClock(float deltaTime)
{
    // 針の進み方と逆回転時の刻み表示は時計異変自身が管理します。
    m_ClockAnomaly.Update(m_LoopCount, deltaTime);
    const float displayedHourAngle =
        m_ClockAnomaly.GetDisplayedHourAngle(m_LoopCount);
    const float displayedMinuteAngle =
        m_ClockAnomaly.GetDisplayedMinuteAngle(m_LoopCount);

    Wall* hourHand = m_Objects.clockHourHand;
    Wall* minuteHand = m_Objects.clockMinuteHand;
    if (hourHand != nullptr)
    {
        hourHand->SetRotation(Vector3(displayedHourAngle, 0.0f, 0.0f));
    }
    if (minuteHand != nullptr)
    {
        minuteHand->SetRotation(Vector3(displayedMinuteAngle, 0.0f, 0.0f));
    }
}

void Stage2Scene::UpdateClockObservation()
{
    if (m_LoopCount <= 0 || m_LoopCount >= 3 ||
        m_ClockAnomaly.WasObservedThisLoop())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 clockCenter(-38.0f, -70.0f, -25.0f);
    Vector3 cameraToClock =
        clockCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToClock.Length();
    if (distance > 0.001f)
    {
        cameraToClock /= distance;
    }

    const float facing =
        game->GetCamera()->GetForward().Dot(cameraToClock);
    if (distance > 98.0f || facing < 0.91f)
    {
        return;
    }

    Player* player = m_Objects.player;
    if (m_LoopCount == 2 && player != nullptr && player->IsFlashlightOn())
    {
        RegisterPuzzleMistake(2);
        return;
    }

    m_ClockAnomaly.MarkObserved();

    if (m_LoopCount == 2)
    {
        m_Notices.loop = 2.8f;

        CeilingLight* doorLight =
            m_Objects.Light(Stage2Light::DoorLight);
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(0.90f, 0.76f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.58f, 0.24f);
    }

    CeilingLight* clockLight =
        m_Objects.Light(Stage2Light::Light2);
    if (clockLight != nullptr)
    {
        clockLight->TriggerEventFlicker(
            m_LoopCount == 1 ? 0.46f : 0.82f,
            m_LoopCount == 1 ? 0.42f : 0.72f);
    }
    game->GetPostProcess()->TriggerHorrorPulse(
        m_LoopCount == 1 ? 0.14f : 0.28f,
        0.32f);
    Input::SetVibration(m_LoopCount == 1 ? 3 : 6, 0.14f);
}

void Stage2Scene::RegisterPuzzleMistake(int type)
{
    if (!m_PuzzleFeedback.TryRegisterMistake(type))
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterPuzzleMistake();
    CeilingLight* warningLight = m_Objects.Light(
        type == 1 ? Stage2Light::Light3 : Stage2Light::Light2);
    const int mistakeCount = m_PuzzleFeedback.GetMistakeCount();
    const float mistakeRate = static_cast<float>(mistakeCount) / 3.0f;
    if (warningLight != nullptr)
    {
        warningLight->TriggerEventFlicker(
            0.48f + mistakeRate * 0.52f,
            0.42f + mistakeRate * 0.48f);
        if (mistakeCount >= 3)
        {
            warningLight->SetForcedOff(true);
        }
    }

    game->GetPostProcess()->TriggerHorrorPulse(
        0.08f + mistakeRate * 0.24f,
        0.18f + mistakeRate * 0.18f);
    game->GetPostProcess()->TriggerBloomPulse(
        0.14f + mistakeRate * 0.28f,
        0.14f);
    Input::SetVibration(
        2 + mistakeCount * 2,
        0.06f + mistakeRate * 0.12f);
}

void Stage2Scene::ResetSignalPuzzle()
{
    m_SignalPuzzle.Reset();
    if (m_LoopCount >= 3)
    {
        m_FinalSequenceArmed = false;
    }

    ShadowMan* signalShadow = m_Objects.shadow;
    if (signalShadow != nullptr && !m_FinalSequence.IsPursuitActive())
    {
        signalShadow->SetActive(false);
    }
    for (FuseBox* terminal : m_Objects.signalTerminals)
    {
        if (terminal != nullptr)
        {
            terminal->ResetActivation();
            terminal->SetManualInteractionAllowed(m_LoopCount >= 3);
        }
    }
    ApplySignalLightingState();
}

void Stage2Scene::UpdateSignalPuzzle()
{
    Core::Game* game = Core::Game::GetInstance();
    const Color baseDiffuse[] =
    {
        Color(0.015f, 0.055f, 0.13f, 1.0f),
        Color(0.13f, 0.075f, 0.012f, 1.0f),
        Color(0.13f, 0.018f, 0.012f, 1.0f)
    };
    const Color baseEmission[] =
    {
        Color(0.015f, 0.18f, 0.48f, 1.0f),
        Color(0.40f, 0.17f, 0.008f, 1.0f),
        Color(0.42f, 0.018f, 0.008f, 1.0f)
    };

    const bool puzzleActive =
        m_LoopCount >= 3 && !m_SignalPuzzle.IsComplete();
    for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
    {
        FuseBox* terminal = m_Objects.signalTerminals[static_cast<std::size_t>(signalIndex)];
        if (terminal != nullptr)
        {
            terminal->SetManualInteractionAllowed(puzzleActive);
        }
    }

    if (puzzleActive)
    {
        for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
        {
            FuseBox* terminal = m_Objects.signalTerminals[static_cast<std::size_t>(signalIndex)];
            if (terminal == nullptr || !terminal->IsActivated() ||
                m_SignalPuzzle.IsAccepted(signalIndex))
            {
                continue;
            }

            const SignalPuzzle::AcceptResult acceptResult =
                m_SignalPuzzle.Accept(signalIndex);
            if (acceptResult == SignalPuzzle::AcceptResult::WrongOrder)
            {
                if (m_LoopCount >= 3)
                {
                    m_FinalSequenceArmed = false;
                }
                RegisterPuzzleMistake(3);
                m_Notices.signal = 2.8f;
                m_NoiseThreatSystem.SetThreat(0.92f);
                game->GetPostProcess()->TriggerHorrorPulse(0.62f, 0.42f);
                Input::SetVibration(11, 0.24f);
                break;
            }

            m_PuzzleFeedback.SetType(0);
            const float retryAssist = static_cast<float>((std::min)(
                m_PuzzleFeedback.GetMistakeCount(), 2));
            m_NoiseThreatSystem.SetThreat((std::min)(
                1.0f,
                m_NoiseThreatSystem.GetThreat() + 0.12f -
                    retryAssist * 0.025f));
            m_Notices.signal = 2.4f;
            m_ProgressHintTimer = 0.0f;
            game->GetPostProcess()->TriggerBloomPulse(0.52f, 0.20f);
            Input::SetVibration(
                4 + m_SignalPuzzle.GetStep() * 2, 0.10f);

            CeilingLight* responseLight = m_Objects.Light(
                signalIndex == 0 ? Stage2Light::Light3 :
                signalIndex == 1 ? Stage2Light::Light2 : Stage2Light::Light1);
            if (responseLight != nullptr)
            {
                responseLight->TriggerEventFlicker(0.54f, 0.48f);
            }
            ApplySignalLightingState();

            if (acceptResult == SignalPuzzle::AcceptResult::Completed)
            {
                m_FinalSequenceArmed = true;
                m_Notices.loop = 3.2f;
                ApplySignalLightingState();
                game->RegisterAnomalyHandled();
                ShadowMan* signalShadow =
                    m_Objects.shadow;
                if (signalShadow != nullptr)
                {
                    signalShadow->SetActive(false);
                }
                for (FuseBox* completedTerminal : m_Objects.signalTerminals)
                {
                    if (completedTerminal != nullptr)
                    {
                        completedTerminal->SetManualInteractionAllowed(false);
                    }
                }
                game->GetPostProcess()->TriggerBloomPulse(1.05f, 0.44f);
                game->GetPostProcess()->TriggerHorrorPulse(0.34f, 0.34f);
            }
            else
            {
                Player* player = m_Objects.player;
                ShadowMan* signalShadow =
                    m_Objects.shadow;
                if (player != nullptr && signalShadow != nullptr)
                {
                    const Vector3 playerPosition = player->GetPosition();
                    Vector3 backward = -player->GetForward();
                    backward.y = 0.0f;
                    if (backward.LengthSquared() < 0.001f)
                    {
                        backward = Vector3(0.0f, 0.0f, -1.0f);
                    }
                    backward.Normalize();
                    const float assistLevel = static_cast<float>((std::min)(
                        m_PuzzleFeedback.GetMistakeCount(), 2));
                    const float spawnDistance =
                        78.0f - m_SignalPuzzle.GetStep() * 7.0f +
                            assistLevel * 13.0f;
                    signalShadow->SetPosition(
                        playerPosition.x + backward.x * spawnDistance,
                        -99.0f,
                        playerPosition.z + backward.z * spawnDistance);
                    signalShadow->SetActive(false);
                    signalShadow->SetActive(true);
                    signalShadow->EnableChase(
                        13.0f + static_cast<float>(
                            m_SignalPuzzle.GetStep()) * 2.0f -
                            assistLevel * 1.6f,
                        30.0f);
                    signalShadow->EnableGazeScare(24.0f);
                    signalShadow->SetOnObserved(
                        [this]()
                        {
                            Core::Game* currentGame =
                                Core::Game::GetInstance();
                            ShadowMan* currentShadow =
                                m_Objects.shadow;
                            if (currentShadow != nullptr)
                            {
                                currentShadow->SetActive(false);
                            }
                            m_NoiseThreatSystem.SetThreat((std::max)(0.12f,
                                m_NoiseThreatSystem.GetThreat() - 0.24f));
                            m_PuzzleFeedback.Set(6, 2.0f);
                            m_Notices.signal = 2.4f;
                            currentGame->GetPostProcess()->TriggerBloomPulse(0.72f, 0.22f);
                            Input::SetVibration(4, 0.10f);
                        });
                }
            }
            break;
        }
    }

    const float pulse = std::sin(m_VisualTimer * 5.4f) * 0.5f + 0.5f;
    for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
    {
        Wall* marker = m_Objects.signalMarkers[static_cast<std::size_t>(signalIndex)];
        if (marker == nullptr)
        {
            continue;
        }

        if (m_SignalPuzzle.IsComplete() ||
            m_SignalPuzzle.IsAccepted(signalIndex))
        {
            marker->SetAppearance(
                Color(0.025f, 0.14f, 0.055f, 1.0f),
                Color(0.02f, 0.42f + pulse * 0.12f, 0.08f, 1.0f),
                58.0f);
        }
        else
        {
            const bool isTarget = puzzleActive &&
                signalIndex == m_SignalPuzzle.GetStep();
            const float energy = isTarget ? 0.72f + pulse * 0.42f : 0.16f;
            marker->SetAppearance(
                baseDiffuse[signalIndex],
                baseEmission[signalIndex] * energy,
                isTarget ? 54.0f : 24.0f);
        }
    }
}

void Stage2Scene::ApplySignalLightingState()
{
    if (m_LoopCount < 3)
    {
        return;
    }

    constexpr Stage2Light restorationLights[] =
    {
        Stage2Light::Light3,
        Stage2Light::Light2,
        Stage2Light::Light1
    };
    constexpr float flickerOffsets[] = { 2.8f, 1.4f, 0.2f };

    for (int lightIndex = 0; lightIndex < 3; ++lightIndex)
    {
        CeilingLight* light = m_Objects.Light(restorationLights[lightIndex]);
        if (light == nullptr)
        {
            continue;
        }

        const bool restored =
            m_SignalPuzzle.IsComplete() ||
            lightIndex < m_SignalPuzzle.GetStep();
        light->SetForcedOff(false);
        light->SetFaulted(!restored);
        light->SetEmergencyLight(!restored, flickerOffsets[lightIndex]);
    }

    CeilingLight* exitLight = m_Objects.Light(Stage2Light::DoorLight);
    if (exitLight != nullptr)
    {
        exitLight->SetForcedOff(false);
        exitLight->SetFaulted(!m_SignalPuzzle.IsComplete());
        exitLight->SetEmergencyLight(
            !m_SignalPuzzle.IsComplete(),
            4.1f);
        if (m_SignalPuzzle.IsComplete())
        {
            exitLight->TriggerEventFlicker(1.10f, 0.82f);
        }
    }
}

void Stage2Scene::UpdateSignalStalker()
{
    if (m_LoopCount < 3 || m_SignalPuzzle.IsComplete() ||
        m_SignalPuzzle.GetStep() <= 0)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    ShadowMan* shadow = m_Objects.shadow;
    if (player == nullptr || shadow == nullptr || !shadow->IsActive())
    {
        return;
    }

    Vector3 toShadow = shadow->GetPosition() - player->GetPosition();
    toShadow.y = 0.0f;
    const float distance = toShadow.Length();
    const float proximity = 1.0f - (std::clamp)((distance - 30.0f) / 90.0f, 0.0f, 1.0f);
    m_NoiseThreatSystem.SetThreat((std::max)(
        m_NoiseThreatSystem.GetThreat(), proximity * 0.78f));

    if (distance > 35.0f)
    {
        return;
    }

    shadow->SetActive(false);
    ResetSignalPuzzle();
    RegisterPuzzleMistake(5);
    m_PuzzleFeedback.Set(5, 2.8f);
    m_Notices.signal = 3.0f;
    m_NoiseThreatSystem.SetThreat(0.38f);
    m_NoiseThreatSystem.SetEventCooldown(2.8f);
    game->GetPostProcess()->TriggerHorrorPulse(0.88f, 0.48f);
    game->GetPostProcess()->TriggerBloomPulse(0.16f, 0.16f);
    Input::SetVibration(15, 0.34f);
}

void Stage2Scene::SetFalseDoorState(bool visible, bool rightSide)
{
    m_FalseDoorAnomaly.SetVisualState(visible, rightSide);
    const float surfaceX = rightSide ? 39.3f : -39.3f;
    const float frameX = rightSide ? 39.0f : -39.0f;
    const float handleX = rightSide ? 38.3f : -38.3f;
    const float centerZ = rightSide ? -88.0f : 70.0f;

    Wall* panel = m_Objects.falseDoorPanel;
    Wall* frameNear = m_Objects.falseDoorFrameNear;
    Wall* frameFar = m_Objects.falseDoorFrameFar;
    Wall* frameTop = m_Objects.falseDoorFrameTop;
    Wall* handle = m_Objects.falseDoorHandle;
    if (panel == nullptr || frameNear == nullptr || frameFar == nullptr ||
        frameTop == nullptr || handle == nullptr)
    {
        return;
    }

    panel->SetPosition(surfaceX, -76.0f, centerZ);
    frameNear->SetPosition(frameX, -76.0f, centerZ - 12.0f);
    frameFar->SetPosition(frameX, -76.0f, centerZ + 12.0f);
    frameTop->SetPosition(frameX, -54.0f, centerZ);
    handle->SetPosition(handleX, -76.0f, centerZ - 8.0f);

    for (Wall* piece : { panel, frameNear, frameFar, frameTop, handle })
    {
        piece->SetVisible(visible);
    }
}
