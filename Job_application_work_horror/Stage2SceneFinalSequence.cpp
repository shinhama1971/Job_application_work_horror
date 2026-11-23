// ============================================================================
// ファイルの役割: 2面の最終追跡と、捕まったときの演出を進める。
// ============================================================================

#include "Stage2Scene.h"
#include "Stage2HorrorSettings.h"
#include "MathUtility.h"
#include "CeilingLight.h"
#include "Door.h"
#include "EventFeedback.h"
#include "ExitTrigger.h"
#include "Game.h"
#include "Ground.h"
#include "Input.h"
#include "Player.h"
#include "PostProcess.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;
using namespace Stage2HorrorSettings;

// 最後の停電の演出を始めている：足音の影を消し、スタミナを満タンにして、背後72に追ってくる影を出している
void Stage2Scene::StartFinalSequence()
{
    m_FinalSequence.Start();
    KickHangingBulbs(FinalStartBulbKick);
    m_NoiseThreatSystem.SetThreat(0.0f);
    m_NoiseThreatSystem.SetWarningTimer(0.0f);
    m_Notices.loop = Stage2NoticeSeconds;

    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    ShadowMan* shadow = m_Objects.shadow;
    ShadowMan* noiseShadow = m_Objects.noiseShadow;
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
            FootY,
            playerPosition.z - PursuitStartBehind);
        shadow->SetActive(false);
        shadow->SetActive(true);
        // 捕まった回数が多いほど影を遅くしている（1回1.5ずつ、最大2回分）
        const float retryAssist = static_cast<float>((std::min)(
            game->GetCaughtCount(), PursuitRetrySlowdownMaxCatches)) * PursuitRetrySlowdown;
        shadow->EnableChase(PursuitSpeed - retryAssist, PursuitStopDistance);
        shadow->EnableGazeScare(PursuitGazeScareSeconds);
        // 追跡中に影を見てしまうと、照明が一斉に明滅する罰を出している（続けては出さない）
        shadow->SetOnObserved(
            [this]()
            {
                if (!m_FinalSequence.TryTriggerGazePenalty())
                {
                    return;
                }

                // 追跡の最中なので、知らせは短く（走りながらでも一目で読める長さ）
                constexpr float gazePenaltyNoticeSeconds = 1.65f;
                m_Notices.loop = gazePenaltyNoticeSeconds;
                Core::Game* game = Core::Game::GetInstance();
                for (CeilingLight* light : m_Objects.lights)
                {
                    if (light != nullptr)
                    {
                        EventFeedback::Flicker(*light, GazePenaltyFlicker);
                    }
                }
                EventFeedback::HorrorPulse(*game->GetPostProcess(), GazePenaltyPulse);
                EventFeedback::BloomPulse(*game->GetPostProcess(), GazePenaltyBloom);
                EventFeedback::Rumble(GazePenaltyRumble);
            });
    }
    // 影が通り過ぎて消した照明を、やり直したときのために点け直し、影の足跡を数え直している
    for (CeilingLight* light : m_Objects.lights)
    {
        if (light != nullptr)
        {
            light->SetForcedOff(false);
        }
    }
    if (shadow != nullptr)
    {
        m_PursuitLastShadowPosition = shadow->GetPosition();
    }
    m_PursuitStepDistance = 0.0f;
    m_PursuitStepSide = 1.0f;
    // 壁の傷を全部見せ、強く驚かせている
    RevealScratchPieces(0, Stage2ScratchCount, FinalStartScratchEmission);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), FinalStartPulse);
    EventFeedback::Rumble(FinalStartRumble);
}

// 最後の停電の演出を進め、終わったら出口の扉の鍵を外している
void Stage2Scene::UpdateFinalSequence(float deltaTime)
{
    if (!m_FinalSequence.IsSequenceActive() || m_FinalDoorReady)
    {
        return;
    }

    m_FinalSequence.AdvanceSequence(deltaTime);
    Core::Game* game = Core::Game::GetInstance();

    int pendingBeat = m_FinalSequence.ConsumePendingBeat();
    while (pendingBeat >= 0)
    {
        // 最後の演出の拍ごとに、入口側から出口の扉の上へ順に非常灯（赤）を点けている。
        CeilingLight* light = m_Objects.lights[static_cast<std::size_t>(pendingBeat)];
        if (light != nullptr)
        {
            light->SetEmergencyLight(
                true,
                FinalBeatEmergencyOffsetBase + static_cast<float>(pendingBeat) * FinalBeatEmergencyOffsetStep);
            EventFeedback::Flicker(*light, FinalBeatFlicker);
        }
        EventFeedback::BloomPulse(*game->GetPostProcess(), FinalBeatBloom);
        pendingBeat = m_FinalSequence.ConsumePendingBeat();
    }

    // 最初の1.65秒は、画面を強く明滅させている
    constexpr float FinalFlickerSeconds = 1.65f;
    constexpr float FinalFlickerRadiansPerSecond = 22.0f;
    constexpr float FinalFlickerMinimumExposure = Effect::PostProcess::MinimumSceneExposure;
    constexpr float FinalFlickerExposureAmplitude = 0.17f;
    if (m_FinalSequence.GetSequenceTimer() < FinalFlickerSeconds)
    {
        const float pulse =
            std::sin(m_FinalSequence.GetSequenceTimer() * FinalFlickerRadiansPerSecond) *
                0.5f + 0.5f;
        game->GetPostProcess()->SetExposure(FinalFlickerMinimumExposure + pulse * FinalFlickerExposureAmplitude);
        game->GetPostProcess()->SetAtmosphere(FinalFlickerNoise, FinalFlickerVignette);
        game->GetPostProcess()->SetLensDistortionStrength(
            FinalFlickerDistortionBase + pulse * FinalFlickerDistortionRange);
        game->GetPostProcess()->SetVolumetricIntensity(
            FinalFlickerVolumetricBase + pulse * FinalFlickerVolumetricRange);
        return;
    }

    // 演出が終わったら、出口の扉の鍵を外し、出口を調べられるようにし、扉のランプを緑にしている
    m_FinalDoorReady = true;
    m_FinalSequenceArmed = false;
    m_Notices.loop = Stage2LongNoticeSeconds;

    Door* finalDoor = m_Objects.door;
    if (finalDoor != nullptr)
    {
        finalDoor->SetLocked(false);
    }

    ExitTrigger* exit = m_Objects.exit;
    if (exit != nullptr)
    {
        exit->SetInteractionEnabled(true);
    }

    Wall* doorIndicator = m_Objects.doorIndicator;
    if (doorIndicator != nullptr)
    {
        doorIndicator->SetAppearance(
            DoorIndicatorOpenDiffuse, DoorIndicatorOpenEmission, DoorIndicatorOpenShininess);
    }

    EventFeedback::BloomPulse(*game->GetPostProcess(), FinalDoorReadyBloom);
    EventFeedback::Rumble(FinalDoorReadyRumble);
}

// 最後の追跡：影に追いつかれたら捕まり、近いほど画面を乱し、振動を強くしている
void Stage2Scene::UpdateFinalPursuit(float deltaTime)
{
    if (!m_FinalSequence.IsPursuitActive())
    {
        ShadowMan* shadow =
            m_Objects.shadow;
        if (shadow != nullptr)
        {
            shadow->SetActive(false);
        }

        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    ShadowMan* shadow = m_Objects.shadow;
    if (player == nullptr || shadow == nullptr)
    {
        return;
    }

    const Vector3 offset = player->GetPosition() - shadow->GetPosition();
    const float horizontalDistance =
        std::sqrt(offset.x * offset.x + offset.z * offset.z);
    // ここまで追いつかれたら捕まっている
    if (horizontalDistance <= PursuitCatchDistance)
    {
        StartCaughtSequence(*player, CaughtSequence::Reason::FinalPursuit);
        return;
    }

    // 追ってくる影は、裸足の濡れた足跡を残していく（左右の足を交互に、歩いた向きに合わせている）
    {
        Vector3 moved = shadow->GetPosition() - m_PursuitLastShadowPosition;
        moved.y = 0.0f;
        const float travel = moved.Length();
        m_PursuitLastShadowPosition = shadow->GetPosition();
        m_PursuitStepDistance += travel;
        Ground* ground = m_Objects.ground;
        if (ground != nullptr && travel > MinimumDirectionLength && m_PursuitStepDistance >= FootprintFollower::Stride)
        {
            m_PursuitStepDistance -= FootprintFollower::Stride;
            const float yaw = std::atan2(moved.x, moved.z);
            const Vector2 right(std::cos(yaw), -std::sin(yaw));
            const Vector2 position = Vector2(shadow->GetPosition().x, shadow->GetPosition().z) +
                right * (FootprintFollower::SideOffset * m_PursuitStepSide);
            ground->AddFootprint(position, yaw, m_PursuitStepSide, true, 1.0f);
            m_PursuitStepSide = -m_PursuitStepSide;
        }
    }
    // 影が通り過ぎた照明は、ぱちっと消える（背後から闇が迫ってくる）
    for (CeilingLight* light : m_Objects.lights)
    {
        if (light != nullptr && !light->IsForcedOff() &&
            shadow->GetPosition().z > light->GetPosition().z + PursuitLightPassMargin)
        {
            light->SetForcedOff(true);
            game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, light->GetPosition(),
                PursuitLightOffSound.Pitch, PursuitLightOffSound.Volume);
        }
    }

    // 近さ（0〜1）：近いほど1に近づく
    const float proximity = MathUtility::Proximity(horizontalDistance, PursuitNearDistance, PursuitProximityRange);
    const float dangerPulse =
        std::sin(m_VisualTimer * (PursuitPulseSpeedBase + proximity * PursuitPulseSpeedRange)) *
        0.5f + 0.5f;

    game->GetPostProcess()->SetCorridorTension(
        PursuitTensionBase + proximity * PursuitTensionRange);
    if (m_FinalDoorReady)
    {
        game->GetPostProcess()->SetAtmosphere(
            PursuitNoiseBase + proximity * PursuitNoiseRange + dangerPulse * PursuitNoisePulse,
            PursuitVignetteBase + proximity * PursuitVignetteRange);
        game->GetPostProcess()->SetLensDistortionStrength(
            PursuitDistortionBase + proximity * PursuitDistortionRange +
            dangerPulse * proximity * PursuitDistortionPulse);
        game->GetPostProcess()->SetVolumetricIntensity(
            PursuitVolumetricBase + proximity * PursuitVolumetricRange);
        game->GetPostProcess()->SetFilmGradeStrength(
            PursuitFilmGradeBase + proximity * PursuitFilmGradeRange);
        game->GetPostProcess()->SetLensDirtStrength(
            PursuitLensDirtBase + proximity * PursuitLensDirtRange);
        game->GetPostProcess()->SetExposure(
            1.0f - proximity * PursuitExposureDrop -
            dangerPulse * proximity * PursuitExposurePulse);
    }

    // 影を見てしまった罰の間は、画面を暗く乱している
    if (m_FinalSequence.HasGazePenalty())
    {
        const float penalty = m_FinalSequence.GetGazePenaltyRate();
        game->GetPostProcess()->SetAtmosphere(
            GazePenaltyNoiseBase + penalty * GazePenaltyNoiseRange,
            GazePenaltyVignetteBase + penalty * GazePenaltyVignetteRange);
        game->GetPostProcess()->SetLensDistortionStrength(
            GazePenaltyDistortionBase + penalty * GazePenaltyDistortionRange);
        game->GetPostProcess()->SetLensDirtStrength(GazePenaltyLensDirtBase + penalty * GazePenaltyLensDirtRange);
        constexpr float PenaltyMinimumExposure = 0.91f;
        constexpr float PenaltyExposureRange = 0.06f;
        game->GetPostProcess()->SetExposure(PenaltyMinimumExposure + (1.0f - penalty) * PenaltyExposureRange);
    }

    // 近いほど短い間隔で、振動と画面の脈動を出している
    if (!m_FinalSequence.AdvancePursuitPulse(deltaTime))
    {
        return;
    }

    const int vibrationFrames =
        PursuitRumbleBase.Frames + static_cast<int>(proximity * static_cast<float>(PursuitRumbleRange.Frames));
    Input::SetVibration(
        vibrationFrames,
        PursuitRumbleBase.Strength + proximity * PursuitRumbleRange.Strength);
    if (proximity > PursuitPulseMinProximity)
    {
        game->GetPostProcess()->TriggerHorrorPulse(
            PursuitPulseBase.Strength + proximity * PursuitPulseStrengthRange,
            PursuitPulseBase.Seconds);
    }
    m_FinalSequence.SchedulePursuitPulse(proximity);
}

// 捕まった演出を始めている（追跡・息を潜める操作・隠れている状態を止め、影を消している）
void Stage2Scene::StartCaughtSequence(
    Player& player,
    CaughtSequence::Reason reason)
{
    if (!m_CaughtSequence.Start(reason))
    {
        return;
    }

    m_QuietRecovery.Reset();
    m_FinalSequence.StopForCaught();
    // ロッカーの中で捕まった場合も、外へ出た扱いにしてから入口へ戻している。
    player.ForceExitHiding();
    m_HiddenStalkerTimer = -1.0f;
    player.SetCanControl(false);

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterCaught();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    ShadowMan* shadow = m_Objects.shadow;
    if (shadow != nullptr)
    {
        shadow->SetActive(false);
    }
    ShadowMan* noiseShadow = m_Objects.noiseShadow;
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
    // 捕まっている間はUpdateBehindPresenceが呼ばれないため、ここで背後の気配も消している。
    m_Objects.presence->SetActive(false);
    m_BehindPresence.Postpone(BehindPresence::MaxInterval);

    EventFeedback::HorrorPulse(*game->GetPostProcess(), CaughtPulse);
    EventFeedback::BloomPulse(*game->GetPostProcess(), CaughtBloom);
    EventFeedback::Rumble(CaughtRumble);
}

// 捕まった演出を進め、暗転した後に入口へ戻している
void Stage2Scene::UpdateCaughtSequence(Player& player, float deltaTime)
{
    m_CaughtSequence.Advance(deltaTime);
    Core::Game* game = Core::Game::GetInstance();

    if (!m_CaughtSequence.IsReadyToRecover())
    {
        const float darkness = m_CaughtSequence.GetFadeRate();
        game->GetPostProcess()->SetExposure(
            CaughtExposureBase - darkness * CaughtExposureDrop);
        game->GetPostProcess()->SetAtmosphere(
            CaughtNoiseBase + darkness * CaughtNoiseRange,
            CaughtVignetteBase + darkness * CaughtVignetteRange);
        game->GetPostProcess()->SetLensDistortionStrength(
            CaughtDistortionBase + darkness * CaughtDistortionRange);
        return;
    }

    // 足音の影に捕まった場合は今の周回を残し、入口へ戻して再挑戦させている。
    player.SetPosition(EntrancePosition);
    player.RestoreStamina();
    player.SetCanControl(true);
    const bool wasNoiseCatch = m_CaughtSequence.WasNoiseStalker();
    m_CaughtSequence.Complete();
    // 最後の追跡で捕まった場合は、停電の演出をもう一度最初からにして、扉を閉め直している
    if (!wasNoiseCatch)
    {
        m_FinalSequence.ResetSequenceForRetry();
        m_FinalSequenceArmed = true;
        m_FinalDoorReady = false;
    }
    else
    {
        m_NoiseThreatSystem.SetThreat(NoiseCatchResetThreat);
        m_NoiseThreatSystem.SetStalkerCooldown(NoiseCatchStalkerCooldown);
        m_NoiseThreatSystem.SetStalkerNoticeTimer(NoiseCatchStalkerNoticeSeconds);
    }
    m_Notices.loop = Stage2ImportantNoticeSeconds;

    Door* door = m_Objects.door;
    if (door != nullptr && !wasNoiseCatch)
    {
        door->ResetClosed(FinalDoorRetryOpeningPhase);
        door->SetLocked(true);
    }

    ExitTrigger* exit = m_Objects.exit;
    if (exit != nullptr && !wasNoiseCatch)
    {
        exit->SetInteractionEnabled(false);
    }

    Wall* indicator = m_Objects.doorIndicator;
    if (indicator != nullptr && !wasNoiseCatch)
    {
        indicator->SetAppearance(
            DoorIndicatorLockedDiffuse, DoorIndicatorLockedEmission, DoorIndicatorLockedShininess);
    }

    EventFeedback::HorrorPulse(*game->GetPostProcess(), CaughtRecoveredPulse);
    EventFeedback::Rumble(CaughtRecoveredRumble);
}
