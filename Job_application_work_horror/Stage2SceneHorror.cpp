// ============================================================================
// ファイルの役割: 2面の偽ドア異変、視線演出、停電、追跡、捕獲イベントを管理します。
// 主な技術: 有限状態機械、動的照明、距離判定、時間演出
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
    // 偽ドアを見つける周回（Stage2AnomalyPlanが決める）だけ判定します。
    if (!m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::FalseDoor) ||
        m_FalseDoorAnomaly.HasMoved())
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
        m_FalseDoorAnomaly.MarkObserved();
        m_PuzzleFeedback.Clear();
        return;
    }

    if (!m_FalseDoorAnomaly.WasObserved() ||
        (facing > 0.30f && distance < 118.0f))
    {
        return;
    }

    m_FalseDoorAnomaly.MarkMoved();
    SetFalseDoorState(true, true);

    m_Notices.loop = 2.8f;

    CeilingLight* doorLight =
        m_Objects.Light(Stage2Light::DoorLight);
    if (doorLight != nullptr)
    {
        doorLight->TriggerEventFlicker(0.90f, 0.76f);
    }
    game->GetPostProcess()->TriggerBloomPulse(0.58f, 0.24f);

    CeilingLight* oldDoorLight =
        m_Objects.Light(Stage2Light::Light3);
    if (oldDoorLight != nullptr)
    {
        oldDoorLight->TriggerEventFlicker(0.72f, 0.68f);
    }
    game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.34f);
    Input::SetVibration(8, 0.20f);
}

void Stage2Scene::StartObservedScare()
{
    if (!m_ObservedScareSequence.Start())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    game->GetPostProcess()->TriggerHorrorPulse(0.72f, 0.52f);
    Input::SetVibration(15, 0.34f);
}

void Stage2Scene::UpdateObservedScare(float deltaTime)
{
    if (!m_ObservedScareSequence.IsActive())
    {
        return;
    }

    m_ObservedScareSequence.Advance(deltaTime);
    Core::Game* game = Core::Game::GetInstance();

    struct LightBeat
    {
        Stage2Light Light;
        float Strength;
    };

    constexpr LightBeat beats[] =
    {
        { Stage2Light::DoorLight, 0.94f },
        { Stage2Light::Light3, 0.90f },
        { Stage2Light::Light2, 0.86f },
        { Stage2Light::Light1, 0.80f }
    };

    int pendingBeat = m_ObservedScareSequence.ConsumePendingBeat();
    while (pendingBeat >= 0)
    {
        const LightBeat& beat = beats[pendingBeat];
        CeilingLight* light = m_Objects.Light(beat.Light);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.82f, beat.Strength);
        }

        game->GetPostProcess()->TriggerBloomPulse(
            0.26f + beat.Strength * 0.25f,
            0.16f);
        pendingBeat = m_ObservedScareSequence.ConsumePendingBeat();
    }

    if (m_ObservedScareSequence.GetTimer() < 1.35f)
    {
        const float intensity =
            m_ObservedScareSequence.GetAtmosphereIntensity();
        game->GetPostProcess()->SetAtmosphere(
            0.30f + intensity * 0.16f,
            0.76f + intensity * 0.12f);
    }
    else
    {
        m_ObservedScareSequence.CompleteIfElapsed();
    }
}

void Stage2Scene::StartFinalSequence()
{
    m_FinalSequence.Start();
    m_NoiseThreatSystem.SetThreat(0.0f);
    m_NoiseThreatSystem.SetWarningTimer(0.0f);
    m_Notices.loop = 2.8f;

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
                if (!m_FinalSequence.TryTriggerGazePenalty())
                {
                    return;
                }

                m_Notices.loop = 1.65f;
                Core::Game* game = Core::Game::GetInstance();
                for (CeilingLight* light : m_Objects.lights)
                {
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
    if (!m_FinalSequence.IsSequenceActive() || m_FinalDoorReady)
    {
        return;
    }

    m_FinalSequence.AdvanceSequence(deltaTime);
    Core::Game* game = Core::Game::GetInstance();

    int pendingBeat = m_FinalSequence.ConsumePendingBeat();
    while (pendingBeat >= 0)
    {
        // 最終演出の拍は入口側から出口扉の上へ順に非常灯を点けます。
        CeilingLight* light = m_Objects.lights[static_cast<std::size_t>(pendingBeat)];
        if (light != nullptr)
        {
            light->SetEmergencyLight(
                true,
                9.0f + static_cast<float>(pendingBeat) * 0.7f);
            light->TriggerEventFlicker(1.0f, 0.96f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.18f);
        pendingBeat = m_FinalSequence.ConsumePendingBeat();
    }

    if (m_FinalSequence.GetSequenceTimer() < 1.65f)
    {
        const float pulse =
            std::sin(m_FinalSequence.GetSequenceTimer() * 22.0f) *
                0.5f + 0.5f;
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
    m_Notices.loop = 3.0f;

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
            Color(0.025f, 0.22f, 0.055f, 1.0f),
            Color(0.005f, 0.30f, 0.025f, 1.0f), 30.0f);
    }

    game->GetPostProcess()->TriggerBloomPulse(0.88f, 0.62f);
    Input::SetVibration(9, 0.24f);
}

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
    if (horizontalDistance <= 31.5f)
    {
        StartCaughtSequence(*player, CaughtSequence::Reason::FinalPursuit);
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

    if (m_FinalSequence.HasGazePenalty())
    {
        const float penalty = m_FinalSequence.GetGazePenaltyRate();
        game->GetPostProcess()->SetAtmosphere(
            0.43f + penalty * 0.12f,
            0.88f + penalty * 0.08f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.66f + penalty * 0.14f);
        game->GetPostProcess()->SetLensDirtStrength(0.32f + penalty * 0.36f);
        game->GetPostProcess()->SetExposure(0.91f + (1.0f - penalty) * 0.06f);
    }

    if (!m_FinalSequence.AdvancePursuitPulse(deltaTime))
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
    m_FinalSequence.SchedulePursuitPulse(proximity);
}

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
    // ロッカーの中で捕まった場合も、外へ出た扱いにしてからチェックポイントへ戻します。
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
    // 捕獲中はUpdateBehindPresenceが呼ばれないため、ここで背後の気配も消します。
    m_Objects.presence->SetActive(false);
    m_BehindPresence.Postpone(BehindPresence::MaxInterval);

    game->GetPostProcess()->TriggerHorrorPulse(1.0f, 0.72f);
    game->GetPostProcess()->TriggerBloomPulse(0.18f, 0.16f);
    Input::SetVibration(24, 0.72f);
}

void Stage2Scene::UpdateCaughtSequence(Player& player, float deltaTime)
{
    m_CaughtSequence.Advance(deltaTime);
    Core::Game* game = Core::Game::GetInstance();

    if (!m_CaughtSequence.IsReadyToRecover())
    {
        const float darkness = m_CaughtSequence.GetFadeRate();
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
    const bool wasNoiseCatch = m_CaughtSequence.WasNoiseStalker();
    m_CaughtSequence.Complete();
    if (!wasNoiseCatch)
    {
        m_FinalSequence.ResetSequenceForRetry();
        m_FinalSequenceArmed = true;
        m_FinalDoorReady = false;
    }
    else
    {
        m_NoiseThreatSystem.SetThreat(0.18f);
        m_NoiseThreatSystem.SetStalkerCooldown(7.0f);
        m_NoiseThreatSystem.SetStalkerNoticeTimer(3.0f);
    }
    m_Notices.loop = 3.2f;

    Door* door = m_Objects.door;
    if (door != nullptr && !wasNoiseCatch)
    {
        door->ResetClosed(3);
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
            Color(0.24f, 0.012f, 0.008f, 1.0f),
            Color(0.18f, 0.001f, 0.0f, 1.0f),
            24.0f);
    }

    game->GetPostProcess()->TriggerHorrorPulse(0.34f, 0.42f);
    Input::SetVibration(8, 0.18f);
}

void Stage2Scene::RevealScratchPieces(int first, int last, float emission)
{
    first = (std::clamp)(first, 0, Stage2ScratchCount);
    last = (std::clamp)(last, first, Stage2ScratchCount);

    for (int index = first; index < last; ++index)
    {
        Wall* scratch = m_Objects.scratches[static_cast<std::size_t>(index)];
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
    if (m_LoopCount >= 3 || m_FinalSequence.IsSequenceActive())
    {
        return;
    }

    struct LightZone
    {
        float TriggerZ;
        Stage2Light Light;
    };
    constexpr LightZone zones[] =
    {
        { -88.0f, Stage2Light::Light1 },
        { -14.0f, Stage2Light::Light2 },
        {  60.0f, Stage2Light::Light3 },
        { 108.0f, Stage2Light::DoorLight }
    };
    constexpr int zoneCount =
        static_cast<int>(sizeof(zones) / sizeof(zones[0]));

    Core::Game* game = Core::Game::GetInstance();
    for (int index = 0; index < zoneCount; ++index)
    {
        if (!m_LightZoneProgress.TryEnter(
            index, player.GetPosition().z, zones[index].TriggerZ))
        {
            continue;
        }
        CeilingLight* light = m_Objects.Light(zones[index].Light);
        const float loopStrength = static_cast<float>(m_LoopCount) * 0.17f;
        const float strength = (std::clamp)(
            0.40f + loopStrength + static_cast<float>(index) * 0.035f,
            0.0f,
            0.92f);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.48f + loopStrength, strength);
        }

        // 2周目以降はプレイヤーの背後から照明を消し、暗闇が迫るように見せます。
        // 次の周回開始時に照明を戻し、同じ廊下を再利用できる状態にします。
        if (m_LoopCount > 0 && index > 0)
        {
            CeilingLight* lightBehind = m_Objects.Light(zones[index - 1].Light);
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

    if (!m_ScratchAnomaly.ConsumeUpdateInterval(deltaTime))
    {
        return;
    }

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
        (m_FinalSequence.IsSequenceActive() && !m_FinalDoorReady)
            ? 0.16f : 0.0f;
    const float emission =
        0.07f + static_cast<float>(m_LoopCount) * 0.035f +
        flashlightResponse * (0.10f + heartbeat * 0.16f) + finalBoost;

    const int visibleCount = m_ScratchAnomaly.GetVisiblePieceCount(
        m_LoopCount, Stage2ScratchCount);
    RevealScratchPieces(0, visibleCount, emission);

    if (m_ScratchAnomaly.TryTriggerScare(
        m_LoopCount, player.IsFlashlightOn(), distance, facing))
    {
        game->GetPostProcess()->TriggerHorrorPulse(
            0.24f + static_cast<float>(m_LoopCount) * 0.08f,
            0.32f);
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.24f);
        Input::SetVibration(7, 0.20f);
    }
}

// 肖像画を見つける周回（Stage2AnomalyPlanが決める）だけ判定します。
// 懐中電灯で照らしたまま見つめ続けると、閉じていた目がゆっくり浮かび、やがて開きます。
// 途中で目を離すと、目は消えて最初からやり直しです。
void Stage2Scene::UpdatePortraitAnomaly(const Player& player, float deltaTime)
{
    if (!m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Portrait) ||
        m_PortraitAnomaly.HasChangedThisLoop())
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
    const bool wasStaring = m_PortraitAnomaly.IsStaring();
    if (!m_PortraitAnomaly.UpdateStare(lookingAtPortrait, deltaTime))
    {
        // 見つめている間は目がかすかに浮かび、目を離すと消えます。
        const float stareRate = m_PortraitAnomaly.GetStareRate();
        if (m_PortraitAnomaly.IsStaring() || wasStaring)
        {
            for (Wall* eye : m_Objects.portraitEyes)
            {
                if (eye != nullptr)
                {
                    eye->SetVisible(m_PortraitAnomaly.IsStaring());
                    eye->SetAppearance(
                        Color(0.05f + stareRate * 0.08f, 0.004f, 0.002f, 1.0f),
                        Color(stareRate * stareRate * 0.05f, 0.0f, 0.0f, 1.0f),
                        28.0f);
                }
            }
        }
        return;
    }

    // 見つめ続けた: 目が開き、奥の確認スイッチが押せるようになります。
    m_Notices.loop = 2.8f;
    constexpr float emission = 0.28f;
    for (Wall* eye : m_Objects.portraitEyes)
    {
        if (eye != nullptr)
        {
            eye->SetVisible(true);
            eye->SetAppearance(
                Color(0.18f, 0.008f, 0.003f, 1.0f),
                Color(emission, 0.002f, 0.0f, 1.0f),
                28.0f);
        }
    }

    Wall* portrait = m_Objects.portrait;
    if (portrait != nullptr)
    {
        portrait->SetAppearance(
            Color(0.10f, 0.014f, 0.009f, 1.0f),
            Color(emission * 0.12f, 0.0f, 0.0f, 1.0f),
            12.0f);
    }

    // 肖像画は時計の向かい（z=-25）にあるため、近くの照明と奥の扉の照明を揺らします。
    CeilingLight* nearbyLight = m_Objects.Light(Stage2Light::Light2);
    if (nearbyLight != nullptr)
    {
        nearbyLight->TriggerEventFlicker(0.82f, 0.72f);
    }
    CeilingLight* doorLight = m_Objects.Light(Stage2Light::DoorLight);
    if (doorLight != nullptr)
    {
        doorLight->TriggerEventFlicker(0.90f, 0.76f);
    }
    game->GetPostProcess()->TriggerHorrorPulse(0.36f, 0.30f);
    Input::SetVibration(6, 0.17f);
}

// ----------------------------------------------------------------------------
// 壁の向こうのノック
// ノックを見つける周回（Stage2AnomalyPlanが決める）だけ、壁の裏から叩く音を立体音響で鳴らします。
// 出どころの壁の前で立ち止まり、壁の方を向いて耳を澄ますと見つけたことになります。
// ----------------------------------------------------------------------------
namespace
{
    constexpr float KnockListenDistance = 24.0f;    // 出どころの壁の前とみなす距離
    constexpr float KnockListenFacing = 0.60f;      // 壁の方を向いているとみなす内積

    Vector3 GetKnockSpot(int index)
    {
        const float* spot = Stage2KnockSpots[index];
        return Vector3(spot[0], spot[1], spot[2]);
    }
}

// 音の出どころの壁の手前（廊下の内側）の位置です。耳を澄ます場所と、案内の矢印の先に使います。
Vector3 Stage2Scene::GetKnockListenPoint() const
{
    const Vector3 spot = GetKnockSpot(m_KnockingAnomaly.GetSpot());
    return Vector3(spot.x > 0.0f ? 38.0f : -38.0f, spot.y, spot.z);
}

void Stage2Scene::UpdateKnockingAnomaly(const Player& player, float deltaTime)
{
    if (!m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Knocking) ||
        m_KnockingAnomaly.WasFound())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 spot = GetKnockSpot(m_KnockingAnomaly.GetSpot());
    if (m_KnockingAnomaly.UpdateKnock(deltaTime))
    {
        // 配管の音より低くし、木の扉を拳で叩くような鈍い音にします。
        std::uniform_real_distribution<float> pitch(0.58f, 0.68f);
        game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, spot, pitch(m_PresenceRandom), 1.6f);
    }

    // 壁の手前（廊下の内側）の位置と、プレイヤーとの距離・向きで「耳を澄ませているか」を判定します。
    const Vector3 listenPoint = GetKnockListenPoint();
    const Camera* camera = game->GetCamera();
    Vector3 toSpot = listenPoint - camera->GetPosition();
    toSpot.y = 0.0f;
    const float distance = toSpot.Length();
    Vector3 forward = camera->GetForward();
    forward.y = 0.0f;
    const bool facing = distance > 0.001f && forward.LengthSquared() > 0.0001f &&
        forward.Dot(toSpot / distance) / forward.Length() > KnockListenFacing;
    const bool listening = distance < KnockListenDistance && facing &&
        !player.IsMovingHorizontally();
    if (!m_KnockingAnomaly.UpdateListen(listening, deltaTime))
    {
        return;
    }

    // 聞き当てた: 壁のすぐ向こうで一度だけ強く叩き、音が止まります。
    game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, spot, 0.52f, 2.2f);
    m_Notices.loop = 2.8f;
    CeilingLight* nearbyLight = m_Objects.Light(Stage2NearestLight(spot.z));
    if (nearbyLight != nullptr)
    {
        nearbyLight->TriggerEventFlicker(0.82f, 0.72f);
    }
    CeilingLight* doorLight = m_Objects.Light(Stage2Light::DoorLight);
    if (doorLight != nullptr)
    {
        doorLight->TriggerEventFlicker(0.90f, 0.76f);
    }
    game->GetPostProcess()->TriggerHorrorPulse(0.34f, 0.32f);
    Input::SetVibration(7, 0.18f);
}

// ----------------------------------------------------------------------------
// 背後の気配
// 視界の外に人影を出し、見ていない間だけ近づけます。振り向けば消え、
// 気づかずに背後まで近づかれると足音の危険度が一気に上がります。
// ----------------------------------------------------------------------------
namespace
{
    constexpr float PresenceSpawnDistance = 58.0f;
    constexpr float PresenceCreepSpeed = 17.0f;     // 歩く速さ(30)より遅く、止まれば迫る速さ
    constexpr float PresenceLookDistance = 170.0f;
    constexpr float PresenceLookAlignment = 0.88f;
    constexpr float PresenceReachThreat = 0.35f;
    // 廊下の内側に収め、壁の中に出現しないようにします。
    constexpr float CorridorHalfWidth = 30.0f;
    constexpr float CorridorMinZ = -150.0f;
    constexpr float CorridorMaxZ = 130.0f;
}

bool Stage2Scene::IsBehindPresenceAllowed() const
{
    // 他の人影や大きな演出と重ねず、周回そのものの静けさの中でだけ出します。
    const bool otherFigureActive =
        (m_Objects.shadow != nullptr && m_Objects.shadow->IsActive()) ||
        (m_Objects.noiseShadow != nullptr && m_Objects.noiseShadow->IsActive());
    const bool hiding = m_Objects.player != nullptr && m_Objects.player->IsHiding();
    return m_LoopCount >= 1 &&
        !otherFigureActive &&
        !hiding &&
        m_LoopTransitionTimer < 0.0f &&
        !m_ObservedScareSequence.IsActive() &&
        !m_FinalSequence.IsSequenceActive() &&
        !m_FinalSequence.IsPursuitActive() &&
        !m_CaughtSequence.IsActive();
}

void Stage2Scene::UpdateBehindPresence(Player& player, float deltaTime)
{
    ShadowMan* presence = m_Objects.presence;
    Core::Game* game = Core::Game::GetInstance();
    const Camera* camera = game->GetCamera();

    bool looking = false;
    float distance = 1000.0f;
    if (m_BehindPresence.IsFollowing())
    {
        Vector3 toPlayer = player.GetPosition() - presence->GetPosition();
        toPlayer.y = 0.0f;
        distance = toPlayer.Length();

        Vector3 toPresence = presence->GetPosition() + Vector3(0.0f, 17.0f, 0.0f) -
            camera->GetPosition();
        const float viewDistance = toPresence.Length();
        if (viewDistance > 0.001f && viewDistance < PresenceLookDistance)
        {
            toPresence /= viewDistance;
            looking = camera->GetForward().Dot(toPresence) > PresenceLookAlignment;
        }
    }

    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    const BehindPresence::Event presenceEvent = m_BehindPresence.Update(
        deltaTime, IsBehindPresenceAllowed(), looking, distance,
        roll(m_PresenceRandom));

    switch (presenceEvent)
    {
    case BehindPresence::Event::None:
        break;
    case BehindPresence::Event::Spawn:
    {
        // カメラの真後ろへ置きます。廊下の外に出る場合は、次の機会に回します。
        Vector3 backward = -camera->GetForward();
        backward.y = 0.0f;
        if (backward.LengthSquared() < 0.001f)
        {
            m_BehindPresence.Postpone(3.0f);
            break;
        }
        backward.Normalize();
        const Vector3 spawn = player.GetPosition() + backward * PresenceSpawnDistance;
        if (std::abs(spawn.x) > CorridorHalfWidth ||
            spawn.z < CorridorMinZ || spawn.z > CorridorMaxZ)
        {
            m_BehindPresence.Postpone(3.0f);
            break;
        }

        presence->SetPosition(spawn.x, -99.0f, spawn.z);
        presence->SetActive(true);
        presence->EnableGazeScare(BehindPresence::MaxFollowSeconds + 10.0f);
        presence->EnableChase(PresenceCreepSpeed, 8.0f);
        break;
    }
    case BehindPresence::Event::Seen:
        // 振り向いた瞬間に消し、見間違いだったのかと思わせる程度の反応にとどめます。
        presence->SetActive(false);
        game->GetPostProcess()->TriggerHorrorPulse(0.14f, 0.18f);
        Input::SetVibration(2, 0.05f);
        break;
    case BehindPresence::Event::Reached:
        presence->SetActive(false);
        for (CeilingLight* light : m_Objects.lights)
        {
            light->TriggerEventFlicker(0.60f, 0.86f);
        }
        m_NoiseThreatSystem.SetThreat((std::min)(
            1.0f, m_NoiseThreatSystem.GetThreat() + PresenceReachThreat));
        game->PlayAudioCue(SOUND_CUE_SCARE, 0.58f);
        game->GetPostProcess()->TriggerHorrorPulse(0.62f, 0.42f);
        Input::SetVibration(10, 0.26f);
        break;
    case BehindPresence::Event::Vanished:
        presence->SetActive(false);
        break;
    }
}
