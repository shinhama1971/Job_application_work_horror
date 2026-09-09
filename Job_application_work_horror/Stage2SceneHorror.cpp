// ============================================================================
// ファイルの役割: 2面の偽ドア異変、視線演出、停電、追跡、捕獲イベントを管理します。
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


void Stage2Scene::UpdateFalseDoorAnomaly(const Player& player)
{
    if (m_LoopCount != 1 || m_FalseDoorMoved)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 doorCenter(-38.3f, -72.0f, 70.0f);
    Vector3 cameraToDoor = doorCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToDoor.Length();
    if (distance > 0.001f)
    {
        cameraToDoor /= distance;
    }

    const float facing = game->GetCamera()->GetForward().Dot(cameraToDoor);
    if (distance < 105.0f && facing > 0.88f)
    {
        if (!player.IsFlashlightOn())
        {
            RegisterPuzzleMistake(1);
            return;
        }
        m_FalseDoorObserved = true;
        m_PuzzleFeedbackTimer = 0.0f;
        m_PuzzleFeedbackType = 0;
        return;
    }

    if (!m_FalseDoorObserved || (facing > 0.30f && distance < 118.0f))
    {
        return;
    }

    m_FalseDoorMoved = true;
    m_FalseDoorNoticeTimer = 2.8f;
    SetFalseDoorState(true, true);

    m_NoticeTimer = 2.8f;

    CeilingLight* doorLight =
        game->GetObj<CeilingLight>("CeilingLight4");
    if (doorLight != nullptr)
    {
        doorLight->TriggerEventFlicker(0.90f, 0.76f);
    }
    game->GetPostProcess()->TriggerBloomPulse(0.58f, 0.24f);

    CeilingLight* oldDoorLight =
        game->GetObj<CeilingLight>("Stage2Light3");
    if (oldDoorLight != nullptr)
    {
        oldDoorLight->TriggerEventFlicker(0.72f, 0.68f);
    }
    game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.34f);
    Input::SetVibration(8, 0.20f);
}

void Stage2Scene::StartObservedScare()
{
    if (m_ObservedScareTimer >= 0.0f)
    {
        return;
    }

    m_ObservedScareTimer = 0.0f;
    m_GazeNoticeTimer = 2.8f;
    m_ObservedScarePhase = 0;

    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    game->GetPostProcess()->TriggerHorrorPulse(0.72f, 0.52f);
    Input::SetVibration(15, 0.34f);
}

void Stage2Scene::UpdateObservedScare(float deltaTime)
{
    if (m_ObservedScareTimer < 0.0f)
    {
        return;
    }

    m_ObservedScareTimer += deltaTime;
    Core::Game* game = Core::Game::GetInstance();

    struct LightBeat
    {
        float Time;
        const char* Name;
        float Strength;
    };

    constexpr LightBeat beats[] =
    {
        { 0.05f, "CeilingLight4", 0.94f },
        { 0.30f, "Stage2Light3", 0.90f },
        { 0.58f, "Stage2Light2", 0.86f },
        { 0.90f, "Stage2Light1", 0.80f }
    };

    constexpr int beatCount =
        static_cast<int>(sizeof(beats) / sizeof(beats[0]));
    while (m_ObservedScarePhase < beatCount &&
        m_ObservedScareTimer >= beats[m_ObservedScarePhase].Time)
    {
        const LightBeat& beat = beats[m_ObservedScarePhase];
        CeilingLight* light = game->GetObj<CeilingLight>(beat.Name);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.82f, beat.Strength);
        }

        game->GetPostProcess()->TriggerBloomPulse(
            0.26f + beat.Strength * 0.25f,
            0.16f);
        ++m_ObservedScarePhase;
    }

    if (m_ObservedScareTimer < 1.35f)
    {
        const float intensity =
            1.0f - (std::min)(m_ObservedScareTimer / 1.35f, 1.0f);
        game->GetPostProcess()->SetAtmosphere(
            0.30f + intensity * 0.16f,
            0.76f + intensity * 0.12f);
    }
    else
    {
        m_ObservedScareTimer = -1.0f;
    }
}

void Stage2Scene::StartFinalSequence()
{
    m_FinalSequenceTimer = 0.0f;
    m_FinalPursuitTimer = 7.0f;
    m_PursuitPulseTimer = 0.12f;
    m_PursuitGazePenaltyTimer = 0.0f;
    m_NoiseThreat = 0.0f;
    m_NoiseWarningTimer = 0.0f;
    m_FinalSequencePhase = 0;
    m_NoticeTimer = 2.8f;

    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
    ShadowMan* noiseShadow = game->GetObj<ShadowMan>("Stage2NoiseShadow");
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
    if (player != nullptr)
    {
        player->RestoreStamina();
    }
    if (player != nullptr && shadow != nullptr)
    {
        const Vector3 playerPosition = player->GetPosition();
        shadow->SetPosition(
            playerPosition.x,
            -99.0f,
            playerPosition.z - 72.0f);
        shadow->SetActive(false);
        shadow->SetActive(true);
        const float retryAssist = static_cast<float>((std::min)(
            game->GetCaughtCount(), 2)) * 1.5f;
        shadow->EnableChase(18.0f - retryAssist, 30.0f);
        shadow->EnableGazeScare(8.0f);
        shadow->SetOnObserved(
            [this]()
            {
                if (m_FinalPursuitTimer <= 0.0f ||
                    m_PursuitGazePenaltyTimer > 0.0f)
                {
                    return;
                }

                m_PursuitGazePenaltyTimer = 1.45f;
                m_NoticeTimer = 1.65f;
                Core::Game* game = Core::Game::GetInstance();
                const char* lightNames[] =
                {
                    "Stage2Light1", "Stage2Light2",
                    "Stage2Light3", "CeilingLight4"
                };
                for (const char* lightName : lightNames)
                {
                    CeilingLight* light =
                        game->GetObj<CeilingLight>(lightName);
                    if (light != nullptr)
                    {
                        light->TriggerEventFlicker(0.72f, 0.92f);
                    }
                }
                game->GetPostProcess()->TriggerHorrorPulse(0.92f, 0.64f);
                game->GetPostProcess()->TriggerBloomPulse(0.62f, 0.22f);
                Input::SetVibration(16, 0.45f);
            });
    }
    RevealScratchPieces(0, Stage2ScratchCount, 0.34f);
    game->GetPostProcess()->TriggerHorrorPulse(0.82f, 0.72f);
    Input::SetVibration(18, 0.42f);
}

void Stage2Scene::UpdateFinalSequence(float deltaTime)
{
    if (m_FinalSequenceTimer < 0.0f || m_FinalDoorReady)
    {
        return;
    }

    m_FinalSequenceTimer += deltaTime;
    Core::Game* game = Core::Game::GetInstance();

    struct FinalBeat
    {
        float Time;
        const char* LightName;
    };
    constexpr FinalBeat beats[] =
    {
        { 0.05f, "Stage2Light1" },
        { 0.30f, "Stage2Light2" },
        { 0.58f, "Stage2Light3" },
        { 0.90f, "CeilingLight4" }
    };
    constexpr int beatCount =
        static_cast<int>(sizeof(beats) / sizeof(beats[0]));

    while (m_FinalSequencePhase < beatCount &&
        m_FinalSequenceTimer >= beats[m_FinalSequencePhase].Time)
    {
        CeilingLight* light = game->GetObj<CeilingLight>(
            beats[m_FinalSequencePhase].LightName);
        if (light != nullptr)
        {
            light->SetEmergencyLight(
                true,
                9.0f + static_cast<float>(m_FinalSequencePhase) * 0.7f);
            light->TriggerEventFlicker(1.0f, 0.96f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.18f);
        ++m_FinalSequencePhase;
    }

    if (m_FinalSequenceTimer < 1.65f)
    {
        const float pulse =
            std::sin(m_FinalSequenceTimer * 22.0f) * 0.5f + 0.5f;
        game->GetPostProcess()->SetExposure(0.76f + pulse * 0.17f);
        game->GetPostProcess()->SetAtmosphere(0.43f, 0.88f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.68f + pulse * 0.08f);
        game->GetPostProcess()->SetVolumetricIntensity(
            0.62f + pulse * 0.12f);
        return;
    }

    m_FinalDoorReady = true;
    m_FinalSequenceArmed = false;
    m_NoticeTimer = 3.0f;

    Door* finalDoor = game->GetObj<Door>("Stage2Door");
    if (finalDoor != nullptr)
    {
        finalDoor->SetLocked(false);
    }

    ExitTrigger* exit = game->GetObj<ExitTrigger>("Stage2Exit");
    if (exit != nullptr)
    {
        exit->SetInteractionEnabled(true);
    }

    Wall* doorIndicator = game->GetObj<Wall>("Stage2DoorIndicator");
    if (doorIndicator != nullptr)
    {
        doorIndicator->SetAppearance(
            Color(0.025f, 0.22f, 0.055f, 1.0f),
            Color(0.005f, 0.30f, 0.025f, 1.0f), 30.0f);
    }

    game->GetPostProcess()->TriggerBloomPulse(0.88f, 0.62f);
    Input::SetVibration(9, 0.24f);
}

void Stage2Scene::UpdateFinalPursuit(float deltaTime)
{
    if (m_FinalPursuitTimer <= 0.0f)
    {
        ShadowMan* shadow =
            Core::Game::GetInstance()->GetObj<ShadowMan>("Stage2Shadow");
        if (shadow != nullptr)
        {
            shadow->SetActive(false);
        }

        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
    if (player == nullptr || shadow == nullptr)
    {
        return;
    }

    const Vector3 offset = player->GetPosition() - shadow->GetPosition();
    const float horizontalDistance =
        std::sqrt(offset.x * offset.x + offset.z * offset.z);
    if (horizontalDistance <= 31.5f)
    {
        StartCaughtSequence(*player);
        return;
    }

    const float proximity = 1.0f - (std::clamp)(
        (horizontalDistance - 30.0f) / 72.0f,
        0.0f,
        1.0f);
    const float dangerPulse =
        std::sin(m_VisualTimer * (6.0f + proximity * 5.0f)) *
        0.5f + 0.5f;

    game->GetPostProcess()->SetCorridorTension(
        0.74f + proximity * 0.16f);
    if (m_FinalDoorReady)
    {
        game->GetPostProcess()->SetAtmosphere(
            0.30f + proximity * 0.085f + dangerPulse * 0.018f,
            0.76f + proximity * 0.135f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.46f + proximity * 0.18f +
            dangerPulse * proximity * 0.025f);
        game->GetPostProcess()->SetVolumetricIntensity(
            0.50f + proximity * 0.12f);
        game->GetPostProcess()->SetFilmGradeStrength(
            0.78f + proximity * 0.18f);
        game->GetPostProcess()->SetLensDirtStrength(
            0.20f + proximity * 0.22f);
        game->GetPostProcess()->SetExposure(
            1.0f - proximity * 0.055f -
            dangerPulse * proximity * 0.025f);
    }

    if (m_PursuitGazePenaltyTimer > 0.0f)
    {
        const float penalty = (std::clamp)(
            m_PursuitGazePenaltyTimer / 1.45f, 0.0f, 1.0f);
        game->GetPostProcess()->SetAtmosphere(
            0.43f + penalty * 0.12f,
            0.88f + penalty * 0.08f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.66f + penalty * 0.14f);
        game->GetPostProcess()->SetLensDirtStrength(0.32f + penalty * 0.36f);
        game->GetPostProcess()->SetExposure(0.91f + (1.0f - penalty) * 0.06f);
    }

    m_PursuitPulseTimer -= deltaTime;
    if (m_PursuitPulseTimer > 0.0f)
    {
        return;
    }

    const int vibrationFrames =
        3 + static_cast<int>(proximity * 7.0f);
    Input::SetVibration(
        vibrationFrames,
        0.08f + proximity * 0.17f);
    if (proximity > 0.34f)
    {
        game->GetPostProcess()->TriggerHorrorPulse(
            0.045f + proximity * 0.075f,
            0.16f);
    }
    m_PursuitPulseTimer = 0.72f - proximity * 0.40f;
}

void Stage2Scene::StartCaughtSequence(Player& player)
{
    if (m_CaughtTimer >= 0.0f)
    {
        return;
    }

    m_CaughtTimer = 0.0f;
    m_QuietRecovery.Reset();
    m_FinalPursuitTimer = 0.0f;
    m_PursuitPulseTimer = 0.0f;
    m_PursuitGazePenaltyTimer = 0.0f;
    player.SetCanControl(false);

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterCaught();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
    if (shadow != nullptr)
    {
        shadow->SetActive(false);
    }
    ShadowMan* noiseShadow = game->GetObj<ShadowMan>("Stage2NoiseShadow");
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }

    game->GetPostProcess()->TriggerHorrorPulse(1.0f, 0.72f);
    game->GetPostProcess()->TriggerBloomPulse(0.18f, 0.16f);
    Input::SetVibration(24, 0.72f);
}

void Stage2Scene::UpdateCaughtSequence(Player& player, float deltaTime)
{
    m_CaughtTimer += deltaTime;
    Core::Game* game = Core::Game::GetInstance();

    if (m_CaughtTimer < 1.15f)
    {
        const float darkness = (std::clamp)(
            m_CaughtTimer / 0.34f, 0.0f, 1.0f);
        game->GetPostProcess()->SetExposure(
            0.92f - darkness * 0.38f);
        game->GetPostProcess()->SetAtmosphere(
            0.46f + darkness * 0.18f,
            0.90f + darkness * 0.08f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.72f + darkness * 0.18f);
        return;
    }

    // 騒音追跡で捕まった場合は現在の周回を保持し、入口へ戻して再挑戦させます。
    player.SetPosition(Vector3(0.0f, -99.0f, -125.0f));
    player.RestoreStamina();
    player.SetCanControl(true);
    m_CaughtTimer = -1.0f;
    const bool wasNoiseCatch = m_NoiseCatch;
    if (!wasNoiseCatch)
    {
        m_FinalSequenceTimer = -1.0f;
        m_FinalSequencePhase = 0;
        m_FinalSequenceArmed = true;
        m_FinalDoorReady = false;
    }
    else
    {
        m_NoiseThreat = 0.18f;
        m_NoiseStalkerCooldown = 7.0f;
        m_NoiseStalkerNoticeTimer = 3.0f;
        m_NoiseCatch = false;
    }
    m_NoticeTimer = 3.2f;

    Door* door = game->GetObj<Door>("Stage2Door");
    if (door != nullptr && !wasNoiseCatch)
    {
        door->ResetClosed(3);
        door->SetLocked(true);
    }

    ExitTrigger* exit = game->GetObj<ExitTrigger>("Stage2Exit");
    if (exit != nullptr && !wasNoiseCatch)
    {
        exit->SetInteractionEnabled(false);
    }

    Wall* indicator = game->GetObj<Wall>("Stage2DoorIndicator");
    if (indicator != nullptr && !wasNoiseCatch)
    {
        indicator->SetAppearance(
            Color(0.24f, 0.012f, 0.008f, 1.0f),
            Color(0.18f, 0.001f, 0.0f, 1.0f),
            24.0f);
    }

    game->GetPostProcess()->TriggerHorrorPulse(0.34f, 0.42f);
    Input::SetVibration(8, 0.18f);
}

void Stage2Scene::RevealScratchPieces(int first, int last, float emission)
{
    Core::Game* game = Core::Game::GetInstance();
    first = (std::clamp)(first, 0, Stage2ScratchCount);
    last = (std::clamp)(last, first, Stage2ScratchCount);

    for (int index = first; index < last; ++index)
    {
        Wall* scratch = game->GetObj<Wall>(Stage2ScratchNames[index]);
        if (scratch == nullptr)
        {
            continue;
        }

        const float variation = static_cast<float>(index % 3) * 0.018f;
        scratch->SetVisible(true);
        scratch->SetAppearance(
            Color(0.20f + variation, 0.008f, 0.004f, 1.0f),
            Color(emission + variation, 0.001f, 0.0f, 1.0f),
            18.0f);
    }
}

void Stage2Scene::UpdateLightZones(const Player& player)
{
    if (m_LoopCount >= 3 || m_FinalSequenceTimer >= 0.0f)
    {
        return;
    }

    struct LightZone
    {
        float TriggerZ;
        const char* LightName;
    };
    constexpr LightZone zones[] =
    {
        { -88.0f, "Stage2Light1" },
        { -14.0f, "Stage2Light2" },
        {  60.0f, "Stage2Light3" },
        { 108.0f, "CeilingLight4" }
    };
    constexpr int zoneCount =
        static_cast<int>(sizeof(zones) / sizeof(zones[0]));

    Core::Game* game = Core::Game::GetInstance();
    for (int index = 0; index < zoneCount; ++index)
    {
        const unsigned int zoneBit = 1u << index;
        if ((m_LightZoneMask & zoneBit) != 0u ||
            player.GetPosition().z <= zones[index].TriggerZ)
        {
            continue;
        }

        m_LightZoneMask |= zoneBit;
        CeilingLight* light = game->GetObj<CeilingLight>(zones[index].LightName);
        const float loopStrength = static_cast<float>(m_LoopCount) * 0.17f;
        const float strength = (std::clamp)(
            0.40f + loopStrength + static_cast<float>(index) * 0.035f,
            0.0f,
            0.92f);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.48f + loopStrength, strength);
        }

        // From the second pass onward, darkness closes behind the player.
        // The next loop restores the fixtures so the corridor can repeat.
        if (m_LoopCount > 0 && index > 0)
        {
            CeilingLight* lightBehind = game->GetObj<CeilingLight>(
                zones[index - 1].LightName);
            if (lightBehind != nullptr)
            {
                lightBehind->SetForcedOff(true);
            }
        }

        game->GetPostProcess()->TriggerBloomPulse(
            0.20f + strength * 0.20f,
            0.15f);
        if (m_LoopCount > 0 && index >= 1)
        {
            game->GetPostProcess()->TriggerHorrorPulse(
                0.08f + loopStrength * 0.24f,
                0.16f);
        }
        Input::SetVibration(2 + m_LoopCount, 0.08f + loopStrength * 0.18f);
    }
}

void Stage2Scene::UpdateScratchMessage(
    const Player& player,
    float deltaTime)
{
    if (m_LoopCount <= 0)
    {
        return;
    }

    m_ScratchUpdateAccumulator += deltaTime;
    if (m_ScratchUpdateAccumulator < 0.05f)
    {
        return;
    }
    m_ScratchUpdateAccumulator = 0.0f;

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 messageCenter(39.45f, -71.0f, 31.0f);
    Vector3 cameraToMessage =
        messageCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToMessage.Length();
    if (distance > 0.001f)
    {
        cameraToMessage /= distance;
    }

    const float facing = (std::max)(
        game->GetCamera()->GetForward().Dot(cameraToMessage),
        0.0f);
    const float proximity = 1.0f - (std::clamp)(
        (distance - 24.0f) / 95.0f,
        0.0f,
        1.0f);
    const float gaze = (std::clamp)(
        (facing - 0.62f) / 0.30f,
        0.0f,
        1.0f);
    const float flashlightResponse =
        player.IsFlashlightOn() ? gaze * proximity : 0.0f;
    const float heartbeat =
        std::sin(m_VisualTimer * (2.4f + m_LoopCount * 0.45f)) *
        0.5f + 0.5f;
    const float finalBoost =
        (m_FinalSequenceTimer >= 0.0f && !m_FinalDoorReady) ? 0.16f : 0.0f;
    const float emission =
        0.07f + static_cast<float>(m_LoopCount) * 0.035f +
        flashlightResponse * (0.10f + heartbeat * 0.16f) + finalBoost;

    int visibleCount = Stage2ScratchCount;
    if (m_LoopCount == 1)
    {
        visibleCount = 3;
    }
    else if (m_LoopCount == 2)
    {
        visibleCount = 9;
    }
    RevealScratchPieces(0, visibleCount, emission);

    if (!m_ScratchScareTriggered && m_LoopCount >= 2 &&
        player.IsFlashlightOn() && distance < 92.0f && facing > 0.90f)
    {
        m_ScratchScareTriggered = true;
        m_ScratchNoticeTimer = 2.2f;
        game->GetPostProcess()->TriggerHorrorPulse(
            0.24f + static_cast<float>(m_LoopCount) * 0.08f,
            0.32f);
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.24f);
        Input::SetVibration(7, 0.20f);
    }
}

void Stage2Scene::UpdatePortraitAnomaly(const Player& player)
{
    if (m_LoopCount <= 0 || m_LoopCount >= 3 ||
        m_PortraitChangedThisLoop)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 portraitCenter(38.72f, -69.0f, -25.0f);
    Vector3 cameraToPortrait =
        portraitCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToPortrait.Length();
    if (distance > 0.001f)
    {
        cameraToPortrait /= distance;
    }

    const float facing =
        game->GetCamera()->GetForward().Dot(cameraToPortrait);
    const bool lookingAtPortrait =
        player.IsFlashlightOn() && distance < 105.0f && facing > 0.91f;
    if (lookingAtPortrait)
    {
        m_PortraitObserved = true;
        return;
    }

    if (!m_PortraitObserved || (facing > 0.55f && distance < 112.0f))
    {
        return;
    }

    m_PortraitChangedThisLoop = true;
    m_PortraitNoticeTimer = 2.4f;

    const float emission = m_LoopCount == 1 ? 0.10f : 0.28f;
    const char* eyeNames[] =
    {
        "Stage2PortraitEyeLeft",
        "Stage2PortraitEyeRight"
    };
    for (const char* name : eyeNames)
    {
        Wall* eye = game->GetObj<Wall>(name);
        if (eye != nullptr)
        {
            eye->SetVisible(true);
            eye->SetAppearance(
                Color(0.18f, 0.008f, 0.003f, 1.0f),
                Color(emission, 0.002f, 0.0f, 1.0f),
                28.0f);
        }
    }

    Wall* portrait = game->GetObj<Wall>("Stage2Portrait");
    if (portrait != nullptr)
    {
        portrait->SetAppearance(
            Color(0.10f, 0.014f, 0.009f, 1.0f),
            Color(emission * 0.12f, 0.0f, 0.0f, 1.0f),
            12.0f);
    }

    CeilingLight* nearbyLight = game->GetObj<CeilingLight>(
        m_LoopCount == 1 ? "Stage2Light2" : "Stage2Light3");
    if (nearbyLight != nullptr)
    {
        nearbyLight->TriggerEventFlicker(
            0.62f + static_cast<float>(m_LoopCount) * 0.18f,
            0.58f + static_cast<float>(m_LoopCount) * 0.12f);
    }
    game->GetPostProcess()->TriggerHorrorPulse(
        0.18f + static_cast<float>(m_LoopCount) * 0.10f,
        0.30f);
    Input::SetVibration(6, 0.17f);
}
