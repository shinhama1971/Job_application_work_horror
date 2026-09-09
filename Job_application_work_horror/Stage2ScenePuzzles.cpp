// ============================================================================
// ファイルの役割: 2面の足音危険度、時計、信号パズル、偽ドア状態を管理します。
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
    if (m_FinalSequenceTimer >= 0.0f || m_FinalPursuitTimer > 0.0f)
    {
        m_QuietRecovery.Reset();
        m_NoiseThreat = (std::max)(0.0f, m_NoiseThreat - deltaTime * 0.8f);
        ShadowMan* noiseShadow = Core::Game::GetInstance()->GetObj<ShadowMan>(
            "Stage2NoiseShadow");
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        return;
    }

    const bool signalStealthActive =
        m_LoopCount >= 3 && !m_SignalPuzzleComplete;
    if (m_LoopCount >= 3 && !signalStealthActive)
    {
        m_NoiseThreat = (std::max)(0.0f, m_NoiseThreat - deltaTime * 0.8f);
        ShadowMan* noiseShadow = Core::Game::GetInstance()->GetObj<ShadowMan>(
            "Stage2NoiseShadow");
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        return;
    }

    const float surfacePulse = player.GetSurfaceNoisePulse();
    if (surfacePulse > 0.0f)
    {
        m_NoiseThreat = (std::min)(1.0f,
            m_NoiseThreat + surfacePulse * (signalStealthActive ? 0.18f : 0.11f));
        if (surfacePulse >= 0.60f)
        {
            m_WetStepNoticeTimer = 1.8f;
        }
    }

    const float change = player.IsSprinting()
        ? deltaTime * (signalStealthActive ? 0.48f : 0.36f)
        : -deltaTime * (signalStealthActive ? 0.12f : 0.22f);
    m_NoiseThreat = (std::clamp)(m_NoiseThreat + change, 0.0f, 1.0f);

    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* noiseShadow =
        game->GetObj<ShadowMan>("Stage2NoiseShadow");
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float proximity = 1.0f - (std::clamp)(
            (distance - 14.0f) / 72.0f, 0.0f, 1.0f);
        m_NoiseThreat = (std::max)(m_NoiseThreat, proximity * 0.92f);

        if (distance <= 15.5f)
        {
            m_NoiseCatch = true;
            StartCaughtSequence(player);
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
        m_ObservedScareTimer < 0.0f && m_LoopTransitionTimer < 0.0f &&
        (m_NoiseThreat > 0.05f || m_QuietRecovery.progress > 0.0f ||
            (noiseShadow != nullptr && noiseShadow->IsActive()));
    if (m_QuietRecovery.Update(deltaTime, quietEligible,
        !player.IsMovingHorizontally(), !player.IsFlashlightOn(), nearbyThreat))
    {
        m_NoiseThreat = (std::max)(0.0f, m_NoiseThreat - 0.45f);
        if (noiseShadow != nullptr) noiseShadow->SetActive(false);
        m_NoiseStalkerCooldown = (std::max)(m_NoiseStalkerCooldown, 8.0f);
        m_NoiseWarningTimer = 0.0f;
        // 成功時は強いフラッシュを避け、視界の落ち着きで成功を伝えます。
        game->GetPostProcess()->TriggerHorrorPulse(0.08f, 0.16f);
        game->GetPostProcess()->TriggerBloomPulse(0.18f, 0.16f);
        Input::SetVibration(2, 0.06f);
    }

    const bool canSpawnNoiseStalker =
        m_NoiseThreat >= 0.74f &&
        m_NoiseStalkerCooldown <= 0.0f &&
        m_FinalSequenceTimer < 0.0f &&
        m_FinalPursuitTimer <= 0.0f &&
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
        noiseShadow->EnableChase(13.0f + m_NoiseThreat * 6.0f, 12.0f);
        noiseShadow->EnableGazeScare(7.5f);
        noiseShadow->SetOnObserved([this]()
        {
            Core::Game* currentGame = Core::Game::GetInstance();
            ShadowMan* currentShadow =
                currentGame->GetObj<ShadowMan>("Stage2NoiseShadow");
            if (currentShadow != nullptr)
            {
                currentShadow->SetActive(false);
            }
            m_NoiseThreat = (std::max)(0.10f, m_NoiseThreat - 0.38f);
            m_NoiseStalkerCooldown = 6.0f;
            m_NoiseStalkerNoticeTimer = 2.4f;
        });
        m_NoiseStalkerCooldown = 8.5f;
        m_NoiseStalkerNoticeTimer = 2.8f;
        m_NoiseWarningTimer = (std::max)(m_NoiseWarningTimer, 2.8f);
        game->PlayAudioCue(SOUND_CUE_SCARE, 0.78f);
        game->GetPostProcess()->TriggerHorrorPulse(0.42f, 0.38f);
        Input::SetVibration(8, 0.22f);
    }

    if (signalStealthActive && m_NoiseThreat >= 0.98f)
    {
        ResetSignalPuzzle();
        RegisterPuzzleMistake(4);
        m_PuzzleFeedbackType = 4;
        m_PuzzleFeedbackTimer = 2.8f;
        m_SignalNoticeTimer = 3.0f;
        m_NoiseWarningTimer = 3.0f;
        m_NoiseThreat = 0.30f;
        m_NoiseEventCooldown = 2.8f;

        game->GetPostProcess()->TriggerHorrorPulse(0.72f, 0.48f);
        game->GetPostProcess()->TriggerBloomPulse(0.90f, 0.24f);
        Input::SetVibration(13, 0.30f);
        return;
    }
    if (m_NoiseThreat < 0.70f || m_NoiseEventCooldown > 0.0f)
    {
        return;
    }

    const float playerZ = player.GetPosition().z;
    const char* reactionLightName = playerZ < -55.0f
        ? "Stage2Light1"
        : (playerZ < 18.0f
            ? "Stage2Light2"
            : (playerZ < 88.0f ? "Stage2Light3" : "CeilingLight4"));

    CeilingLight* reactionLight =
        game->GetObj<CeilingLight>(reactionLightName);
    if (reactionLight != nullptr)
    {
        reactionLight->TriggerEventFlicker(
            0.48f + m_NoiseThreat * 0.48f,
            0.46f + m_NoiseThreat * 0.42f);
    }

    m_NoiseWarningTimer = 2.1f;
    m_NoiseEventCooldown = 2.35f;
    game->GetPostProcess()->TriggerHorrorPulse(
        0.10f + m_NoiseThreat * 0.16f,
        0.22f);
    Input::SetVibration(
        3 + static_cast<int>(m_NoiseThreat * 4.0f),
        0.08f + m_NoiseThreat * 0.08f);
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
    m_NoticeTimer = 3.0f;
    m_LightZoneMask = 0;
    m_ScratchScareTriggered = false;
    m_PortraitObserved = false;
    m_PortraitChangedThisLoop = false;
    m_FalseDoorObserved = false;
    m_FalseDoorMoved = false;
    m_ClockObservedThisLoop = false;
    m_ConfirmationHandledThisLoop = false;
    ResetSignalPuzzle();
    m_PuzzleFeedbackTimer = 0.0f;
    m_PuzzleFeedbackType = 0;
    m_PuzzleMistakeCount = 0;
    m_NoiseThreat = 0.12f;
    m_NoiseEventCooldown = 1.0f;
    m_NoiseWarningTimer = 0.0f;
    m_NoiseStalkerCooldown = 2.0f;
    m_NoiseStalkerNoticeTimer = 0.0f;
    ShadowMan* noiseShadow =
        game->GetObj<ShadowMan>("Stage2NoiseShadow");
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
    FuseBox* confirmationPanel =
        game->GetObj<FuseBox>("Stage2ConfirmationPanel");
    if (confirmationPanel != nullptr)
    {
        confirmationPanel->ResetActivation();
        confirmationPanel->SetManualInteractionAllowed(false);
    }
    player.SetPosition(Vector3(0.0f, -99.0f, -125.0f));
    Door* loopDoor = game->GetObj<Door>("Stage2Door");
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(m_LoopCount);
        // Each repeated hallway has one change that must be noticed before
        // the familiar door will open. This turns the loop into observation
        // gameplay instead of a straight walk through the same corridor.
        loopDoor->SetLocked(m_LoopCount > 0);
    }

    constexpr const char* corridorLightNames[] =
    {
        "Stage2Light1", "Stage2Light2",
        "Stage2Light3", "CeilingLight4"
    };
    for (const char* lightName : corridorLightNames)
    {
        CeilingLight* light = game->GetObj<CeilingLight>(lightName);
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

    const char* cycleMarkNames[] =
    {
        "Stage2CycleMark1",
        "Stage2CycleMark2",
        "Stage2CycleMark3"
    };
    for (int markIndex = 0; markIndex < 3; ++markIndex)
    {
        Wall* cycleMark =
            game->GetObj<Wall>(cycleMarkNames[markIndex]);
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

    Wall* loopMark = game->GetObj<Wall>("Stage2LoopMark");
    Wall* portrait = game->GetObj<Wall>("Stage2Portrait");
    CeilingLight* light2 = game->GetObj<CeilingLight>("Stage2Light2");
    CeilingLight* light3 = game->GetObj<CeilingLight>("Stage2Light3");

    if (m_LoopCount == 1)
    {
        SetFalseDoorState(true, false);
        CeilingLight* failingLight =
            game->GetObj<CeilingLight>("Stage2Light2");
        if (failingLight != nullptr)
        {
            failingLight->SetFaulted(true);
        }
        RevealScratchPieces(0, 3, 0.10f);
        BatteryItem* battery = game->GetObj<BatteryItem>("Stage2Battery");
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
            game->GetObj<CeilingLight>("Stage2Light1");
        CeilingLight* farLight =
            game->GetObj<CeilingLight>("Stage2Light3");
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
        ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
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
        // The last loop now requires restoring the three signal panels.
        // The chase is armed only after the player has explored the corridor
        // and entered the visible colour sequence correctly.
        m_FinalSequenceArmed = false;
        CeilingLight* doorLight = game->GetObj<CeilingLight>("CeilingLight4");
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(1.5f, 0.94f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.64f, 0.58f);
    }
}

void Stage2Scene::ConfigureClockForLoop()
{
    if (m_LoopCount == 1)
    {
        m_ClockHourAngle = 1.18f;
        m_ClockMinuteAngle = -2.34f;
    }
    else if (m_LoopCount == 2)
    {
        m_ClockHourAngle = -0.62f;
        m_ClockMinuteAngle = 2.72f;
    }
    else if (m_LoopCount >= 3)
    {
        m_ClockHourAngle = 3.14159265f;
        m_ClockMinuteAngle = 3.14159265f;

        Core::Game* game = Core::Game::GetInstance();
        Wall* face = game->GetObj<Wall>("Stage2ClockFace");
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
    if (m_LoopCount == 0)
    {
        m_ClockMinuteAngle += deltaTime * 0.035f;
        m_ClockHourAngle += deltaTime * 0.0029f;
    }
    else if (m_LoopCount == 2)
    {
        // The reverse motion is deliberately slow enough to be noticed only
        // when the player compares the hands against the previous loop.
        m_ClockMinuteAngle -= deltaTime * 0.82f;
        m_ClockHourAngle -= deltaTime * 0.068f;
    }

    float displayedHourAngle = m_ClockHourAngle;
    float displayedMinuteAngle = m_ClockMinuteAngle;
    if (m_LoopCount == 2)
    {
        constexpr float clockStep = 0.105f;
        displayedHourAngle =
            std::floor(m_ClockHourAngle / clockStep) * clockStep;
        displayedMinuteAngle =
            std::floor(m_ClockMinuteAngle / clockStep) * clockStep;
    }

    Core::Game* game = Core::Game::GetInstance();
    Wall* hourHand = game->GetObj<Wall>("Stage2ClockHourHand");
    Wall* minuteHand = game->GetObj<Wall>("Stage2ClockMinuteHand");
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
        m_ClockObservedThisLoop)
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

    Player* player = game->GetObj<Player>("Player");
    if (m_LoopCount == 2 && player != nullptr && player->IsFlashlightOn())
    {
        RegisterPuzzleMistake(2);
        return;
    }

    m_ClockObservedThisLoop = true;
    m_ClockNoticeTimer = 2.6f;

    if (m_LoopCount == 2)
    {
        m_NoticeTimer = 2.8f;

        CeilingLight* doorLight =
            game->GetObj<CeilingLight>("CeilingLight4");
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(0.90f, 0.76f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.58f, 0.24f);
    }

    CeilingLight* clockLight =
        game->GetObj<CeilingLight>("Stage2Light2");
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
    if (m_PuzzleFeedbackTimer > 0.0f)
    {
        return;
    }

    m_PuzzleFeedbackType = type;
    m_PuzzleFeedbackTimer = 2.2f;
    m_PuzzleMistakeCount = (std::min)(m_PuzzleMistakeCount + 1, 3);

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterPuzzleMistake();
    CeilingLight* warningLight = game->GetObj<CeilingLight>(
        type == 1 ? "Stage2Light3" : "Stage2Light2");
    const float mistakeRate =
        static_cast<float>(m_PuzzleMistakeCount) / 3.0f;
    if (warningLight != nullptr)
    {
        warningLight->TriggerEventFlicker(
            0.48f + mistakeRate * 0.52f,
            0.42f + mistakeRate * 0.48f);
        if (m_PuzzleMistakeCount >= 3)
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
        2 + m_PuzzleMistakeCount * 2,
        0.06f + mistakeRate * 0.12f);
}

void Stage2Scene::ResetSignalPuzzle()
{
    m_SignalStep = 0;
    m_SignalPuzzleComplete = false;
    m_SignalAccepted[0] = false;
    m_SignalAccepted[1] = false;
    m_SignalAccepted[2] = false;
    if (m_LoopCount >= 3)
    {
        m_FinalSequenceArmed = false;
    }

    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* signalShadow = game->GetObj<ShadowMan>("Stage2Shadow");
    if (signalShadow != nullptr && m_FinalPursuitTimer <= 0.0f)
    {
        signalShadow->SetActive(false);
    }
    constexpr const char* terminalNames[] =
    {
        "Stage2SignalTerminalBlue",
        "Stage2SignalTerminalAmber",
        "Stage2SignalTerminalRed"
    };
    for (const char* terminalName : terminalNames)
    {
        FuseBox* terminal = game->GetObj<FuseBox>(terminalName);
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
    constexpr const char* terminalNames[] =
    {
        "Stage2SignalTerminalBlue",
        "Stage2SignalTerminalAmber",
        "Stage2SignalTerminalRed"
    };
    constexpr const char* markerNames[] =
    {
        "Stage2SignalMarkerBlue",
        "Stage2SignalMarkerAmber",
        "Stage2SignalMarkerRed"
    };
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

    const bool puzzleActive = m_LoopCount >= 3 && !m_SignalPuzzleComplete;
    for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
    {
        FuseBox* terminal = game->GetObj<FuseBox>(terminalNames[signalIndex]);
        if (terminal != nullptr)
        {
            terminal->SetManualInteractionAllowed(puzzleActive);
        }
    }

    if (puzzleActive)
    {
        for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
        {
            FuseBox* terminal = game->GetObj<FuseBox>(terminalNames[signalIndex]);
            if (terminal == nullptr || !terminal->IsActivated() ||
                m_SignalAccepted[signalIndex])
            {
                continue;
            }

            if (signalIndex != m_SignalStep)
            {
                ResetSignalPuzzle();
                RegisterPuzzleMistake(3);
                m_SignalNoticeTimer = 2.8f;
                m_NoiseThreat = 0.92f;
                game->GetPostProcess()->TriggerHorrorPulse(0.62f, 0.42f);
                Input::SetVibration(11, 0.24f);
                break;
            }

            m_SignalAccepted[signalIndex] = true;
            ++m_SignalStep;
            m_PuzzleFeedbackType = 0;
            const float retryAssist = static_cast<float>((std::min)(
                m_PuzzleMistakeCount, 2));
            m_NoiseThreat = (std::min)(
                1.0f,
                m_NoiseThreat + 0.12f - retryAssist * 0.025f);
            m_SignalNoticeTimer = 2.4f;
            m_ProgressHintTimer = 0.0f;
            game->GetPostProcess()->TriggerBloomPulse(0.52f, 0.20f);
            Input::SetVibration(4 + m_SignalStep * 2, 0.10f);

            CeilingLight* responseLight = game->GetObj<CeilingLight>(
                signalIndex == 0 ? "Stage2Light3" :
                signalIndex == 1 ? "Stage2Light2" : "Stage2Light1");
            if (responseLight != nullptr)
            {
                responseLight->TriggerEventFlicker(0.54f, 0.48f);
            }
            ApplySignalLightingState();

            if (m_SignalStep >= 3)
            {
                m_SignalPuzzleComplete = true;
                m_FinalSequenceArmed = true;
                m_NoticeTimer = 3.2f;
                ApplySignalLightingState();
                game->RegisterAnomalyHandled();
                ShadowMan* signalShadow =
                    game->GetObj<ShadowMan>("Stage2Shadow");
                if (signalShadow != nullptr)
                {
                    signalShadow->SetActive(false);
                }
                for (const char* terminalName : terminalNames)
                {
                    FuseBox* completedTerminal =
                        game->GetObj<FuseBox>(terminalName);
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
                Player* player = game->GetObj<Player>("Player");
                ShadowMan* signalShadow =
                    game->GetObj<ShadowMan>("Stage2Shadow");
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
                        m_PuzzleMistakeCount, 2));
                    const float spawnDistance =
                        78.0f - m_SignalStep * 7.0f + assistLevel * 13.0f;
                    signalShadow->SetPosition(
                        playerPosition.x + backward.x * spawnDistance,
                        -99.0f,
                        playerPosition.z + backward.z * spawnDistance);
                    signalShadow->SetActive(false);
                    signalShadow->SetActive(true);
                    signalShadow->EnableChase(
                        13.0f + static_cast<float>(m_SignalStep) * 2.0f -
                            assistLevel * 1.6f,
                        30.0f);
                    signalShadow->EnableGazeScare(24.0f);
                    signalShadow->SetOnObserved(
                        [this]()
                        {
                            Core::Game* currentGame =
                                Core::Game::GetInstance();
                            ShadowMan* currentShadow =
                                currentGame->GetObj<ShadowMan>("Stage2Shadow");
                            if (currentShadow != nullptr)
                            {
                                currentShadow->SetActive(false);
                            }
                            m_NoiseThreat = (std::max)(0.12f, m_NoiseThreat - 0.24f);
                            m_PuzzleFeedbackType = 6;
                            m_PuzzleFeedbackTimer = 2.0f;
                            m_SignalNoticeTimer = 2.4f;
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
        Wall* marker = game->GetObj<Wall>(markerNames[signalIndex]);
        if (marker == nullptr)
        {
            continue;
        }

        if (m_SignalPuzzleComplete || m_SignalAccepted[signalIndex])
        {
            marker->SetAppearance(
                Color(0.025f, 0.14f, 0.055f, 1.0f),
                Color(0.02f, 0.42f + pulse * 0.12f, 0.08f, 1.0f),
                58.0f);
        }
        else
        {
            const bool isTarget = puzzleActive && signalIndex == m_SignalStep;
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

    Core::Game* game = Core::Game::GetInstance();
    constexpr const char* restorationLights[] =
    {
        "Stage2Light3",
        "Stage2Light2",
        "Stage2Light1"
    };
    constexpr float flickerOffsets[] = { 2.8f, 1.4f, 0.2f };

    for (int lightIndex = 0; lightIndex < 3; ++lightIndex)
    {
        CeilingLight* light =
            game->GetObj<CeilingLight>(restorationLights[lightIndex]);
        if (light == nullptr)
        {
            continue;
        }

        const bool restored =
            m_SignalPuzzleComplete || lightIndex < m_SignalStep;
        light->SetForcedOff(false);
        light->SetFaulted(!restored);
        light->SetEmergencyLight(!restored, flickerOffsets[lightIndex]);
    }

    CeilingLight* exitLight = game->GetObj<CeilingLight>("CeilingLight4");
    if (exitLight != nullptr)
    {
        exitLight->SetForcedOff(false);
        exitLight->SetFaulted(!m_SignalPuzzleComplete);
        exitLight->SetEmergencyLight(
            !m_SignalPuzzleComplete,
            4.1f);
        if (m_SignalPuzzleComplete)
        {
            exitLight->TriggerEventFlicker(1.10f, 0.82f);
        }
    }
}

void Stage2Scene::UpdateSignalStalker()
{
    if (m_LoopCount < 3 || m_SignalPuzzleComplete || m_SignalStep <= 0)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
    if (player == nullptr || shadow == nullptr || !shadow->IsActive())
    {
        return;
    }

    Vector3 toShadow = shadow->GetPosition() - player->GetPosition();
    toShadow.y = 0.0f;
    const float distance = toShadow.Length();
    const float proximity = 1.0f - (std::clamp)((distance - 30.0f) / 90.0f, 0.0f, 1.0f);
    m_NoiseThreat = (std::max)(m_NoiseThreat, proximity * 0.78f);

    if (distance > 35.0f)
    {
        return;
    }

    shadow->SetActive(false);
    ResetSignalPuzzle();
    RegisterPuzzleMistake(5);
    m_PuzzleFeedbackType = 5;
    m_PuzzleFeedbackTimer = 2.8f;
    m_SignalNoticeTimer = 3.0f;
    m_NoiseThreat = 0.38f;
    m_NoiseEventCooldown = 2.8f;
    game->GetPostProcess()->TriggerHorrorPulse(0.88f, 0.48f);
    game->GetPostProcess()->TriggerBloomPulse(0.16f, 0.16f);
    Input::SetVibration(15, 0.34f);
}

void Stage2Scene::SetFalseDoorState(bool visible, bool rightSide)
{
    Core::Game* game = Core::Game::GetInstance();
    const float surfaceX = rightSide ? 39.3f : -39.3f;
    const float frameX = rightSide ? 39.0f : -39.0f;
    const float handleX = rightSide ? 38.3f : -38.3f;
    const float centerZ = rightSide ? -88.0f : 70.0f;

    Wall* panel = game->GetObj<Wall>("Stage2FalseDoorPanel");
    Wall* frameNear = game->GetObj<Wall>("Stage2FalseDoorFrameNear");
    Wall* frameFar = game->GetObj<Wall>("Stage2FalseDoorFrameFar");
    Wall* frameTop = game->GetObj<Wall>("Stage2FalseDoorFrameTop");
    Wall* handle = game->GetObj<Wall>("Stage2FalseDoorHandle");
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

    for (const char* name : Stage2FalseDoorNames)
    {
        Wall* piece = game->GetObj<Wall>(name);
        if (piece != nullptr)
        {
            piece->SetVisible(visible);
        }
    }
}
