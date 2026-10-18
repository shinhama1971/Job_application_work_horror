// ============================================================================
// ファイルの役割: 2面の怖い演出（偽の扉・影を見たときの照明・最後の停電と追跡・捕まったとき・傷・肖像画・ノック・背後の気配・心拍）を担当している。
// 主な技術: 有限状態機械、照明の演出、距離と視線の判定、時間で進む演出
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
#include "MirrorSelf.h"
#include "Ground.h"
#include "TensionPulseFeedback.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace DirectX::SimpleMath;

#include "Stage2SceneConstants.h"


// 偽の扉の異変：ライトで照らして見た後、目を離した隙に反対側（右）の壁へ移している
void Stage2Scene::UpdateFalseDoorAnomaly(const Player& player)
{
    // 偽の扉を見つける周回（Stage2AnomalyPlanが決める）だけ判定している。
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

    // 近く（105未満）で正面（内積0.88超）から見たとき、ライトが点いていれば「見た」、消えていれば間違いとして数えている
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

    // 見た後で、目をそらす（内積0.30以下）か離れたら、右の壁へ移して照明と画面で知らせている
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

// 影を見てしまったときの演出を始めている（驚かせる音・画面・振動）
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

// 影を見たときの演出を進めている：奥の扉の照明から入口の照明へ、順に明滅させている
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

// 最後の停電の演出を始めている：足音の影を消し、スタミナを満タンにして、背後72に追ってくる影を出している
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
        // 捕まった回数が多いほど影を遅くしている（1回1.5ずつ、最大2回分）
        const float retryAssist = static_cast<float>((std::min)(
            game->GetCaughtCount(), 2)) * 1.5f;
        shadow->EnableChase(18.0f - retryAssist, 30.0f);
        shadow->EnableGazeScare(8.0f);
        // 追跡中に影を見てしまうと、照明が一斉に明滅する罰を出している（続けては出さない）
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
    RevealScratchPieces(0, Stage2ScratchCount, 0.34f);
    game->GetPostProcess()->TriggerHorrorPulse(0.82f, 0.72f);
    Input::SetVibration(18, 0.42f);
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
                9.0f + static_cast<float>(pendingBeat) * 0.7f);
            light->TriggerEventFlicker(1.0f, 0.96f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.18f);
        pendingBeat = m_FinalSequence.ConsumePendingBeat();
    }

    // 最初の1.65秒は、画面を強く明滅させている
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

    // 演出が終わったら、出口の扉の鍵を外し、出口を調べられるようにし、扉のランプを緑にしている
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
    // 31.5以内まで追いつかれたら捕まっている
    if (horizontalDistance <= 31.5f)
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
        if (ground != nullptr && travel > 0.001f && m_PursuitStepDistance >= FootprintFollower::Stride)
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
            shadow->GetPosition().z > light->GetPosition().z + 4.0f)
        {
            light->SetForcedOff(true);
            game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, light->GetPosition(), 0.55f, 0.9f);
        }
    }

    // 近さ（0〜1）：30〜102の距離で、近いほど1に近づく
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

    // 影を見てしまった罰の間は、画面を暗く乱している
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

    // 近いほど短い間隔で、振動と画面の脈動を出している
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

    game->GetPostProcess()->TriggerHorrorPulse(1.0f, 0.72f);
    game->GetPostProcess()->TriggerBloomPulse(0.18f, 0.16f);
    Input::SetVibration(24, 0.72f);
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
            0.92f - darkness * 0.38f);
        game->GetPostProcess()->SetAtmosphere(
            0.46f + darkness * 0.18f,
            0.90f + darkness * 0.08f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.72f + darkness * 0.18f);
        return;
    }

    // 足音の影に捕まった場合は今の周回を残し、入口へ戻して再挑戦させている。
    player.SetPosition(Vector3(0.0f, -99.0f, -125.0f));
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

// 指定の範囲の傷を表示し、少しずつ色を変えて赤く光らせている
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
        // 新しく出る傷は、焦げた縁が広がるように壁へ浮かび上がらせている
        if (!scratch->IsVisible())
        {
            scratch->DissolveIn(1.4f);
        }
        scratch->SetAppearance(
            Color(0.20f + variation, 0.008f, 0.004f, 1.0f),
            Color(emission + variation, 0.001f, 0.0f, 1.0f),
            18.0f);
    }
}

// 照明の区画：廊下を進むと照明を明滅させ、2周目以降は通り過ぎた照明を消している
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

        // 2周目以降はプレイヤーの背後から照明を消し、暗闇が迫るように見せている。
        // 次の周回の始まりに照明を戻し、同じ廊下を使い直せる状態にしている。
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

// 壁の傷：周回が進むほど増え、ライトで照らすと心臓の鼓動のように赤く脈打っている
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

    // 見ている度合い（正面ほど1）と近さ（24〜119）から、照らしている強さを求めている
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

    // 周回に合わせた数の傷を見せ、2周目以降に近くで正面から照らしたら一度だけ驚かせている
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

// 血の手形を見つける周回（Stage2AnomalyPlanが決める）だけ判定している。
// 壁に浮かんだ最後の手形を懐中電灯で照らし続けると、奥の反対側の壁に次の手形が「ぺたっ」と現れる。
// 4つ目まで照らすと見つけたことになる。手形の形と見え方は、毎フレーム壁のシェーダーへ渡している。
void Stage2Scene::UpdateHandprintAnomaly(const Player& player, float deltaTime)
{
    const bool active = m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Handprint);
    Core::Game* game = Core::Game::GetInstance();

    // 最後に現れた手形を照らしているか（ライトが点いていて、近く（110未満）で正面（内積0.93超）から見ている）
    bool litLatest = false;
    if (active && player.IsFlashlightOn())
    {
        Vector3 toPrint = m_HandprintAnomaly.GetLatestPosition() - game->GetCamera()->GetPosition();
        const float distance = toPrint.Length();
        if (distance > 0.001f)
        {
            toPrint /= distance;
            litLatest = distance < 110.0f && game->GetCamera()->GetForward().Dot(toPrint) > 0.93f;
        }
    }

    const HandprintAnomaly::Event handprintEvent = m_HandprintAnomaly.Update(deltaTime, active, litLatest);
    if (handprintEvent == HandprintAnomaly::Event::NextAppeared)
    {
        // 次の手形が現れた所で、濡れた手を壁に押し付けたような低い水音を鳴らし、近くの照明を一瞬揺らしている
        const Vector3 nextPrint = m_HandprintAnomaly.GetLatestPosition();
        game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, nextPrint, 0.55f, 0.9f);
        CeilingLight* nearbyLight = m_Objects.Light(Stage2NearestLight(nextPrint.z));
        if (nearbyLight != nullptr)
        {
            nearbyLight->TriggerEventFlicker(0.45f, 0.30f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.14f, 0.22f);
        Input::SetVibration(3, 0.08f);
    }
    else if (handprintEvent == HandprintAnomaly::Event::Found)
    {
        // 最後の手形を照らした：奥の確認スイッチが押せるようになる
        m_Notices.loop = 2.8f;
        game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, m_HandprintAnomaly.GetLatestPosition(), 0.62f, 1.4f);
        game->GetPostProcess()->TriggerHorrorPulse(0.30f, 0.40f);
        game->GetPostProcess()->TriggerBloomPulse(0.40f, 0.20f);
        Input::SetVibration(7, 0.18f);
    }

    // 壁のシェーダーへ、手形の場所と見え方を渡している（この周回でなければ描かない）
    std::array<Vector4, HandprintAnomaly::PrintCount> prints{};
    for (int index = 0; index < HandprintAnomaly::PrintCount; ++index)
    {
        const DirectX::XMFLOAT3& position = HandprintAnomaly::Positions[static_cast<std::size_t>(index)];
        prints[static_cast<std::size_t>(index)] = Vector4(
            position.x, position.y, position.z, m_HandprintAnomaly.GetVisibility(index));
    }
    Renderer::SetWallHandprints(prints.data(), active ? HandprintAnomaly::PrintCount : 0);
}

// 鏡を見つける周回（Stage2AnomalyPlanが決める）だけ判定している。
// 左の壁の鏡をのぞくと、すぐ後ろに黒い人影が映っている（人影は鏡に映す景色を描くときだけ描かれる）。
// 鏡で見た後に振り返り、人影が立っていた所を見ると、そこには誰もいない。それで見つけたことになる。
void Stage2Scene::UpdateMirrorAnomaly(const Player& player, float deltaTime)
{
    const bool active = m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Mirror);
    Core::Game* game = Core::Game::GetInstance();
    const Camera* camera = game->GetCamera();
    const MirrorAnomaly::Event mirrorEvent = m_MirrorAnomaly.Update(
        deltaTime, active, camera->GetPosition(), camera->GetForward(), player.GetPosition());

    // 鏡の異変の周回で、まだ見つけていない間は、鏡の中の自分だけが少し遅れて動く
    if (m_Objects.mirrorSelf != nullptr)
    {
        m_Objects.mirrorSelf->SetDelaySeconds(active && !m_MirrorAnomaly.WasFound() ? 0.45f : 0.0f);
    }

    // 鏡の中の人影を、異変の周回で、まだ見つけていない間だけ表示している
    ShadowMan* figure = m_Objects.mirrorFigure;
    if (figure != nullptr)
    {
        const bool showFigure = active && m_MirrorAnomaly.IsFigureVisible();
        if (showFigure != figure->IsActive())
        {
            figure->SetActive(showFigure);
        }
        if (showFigure)
        {
            const Vector3 position = m_MirrorAnomaly.GetFigurePosition();
            figure->SetPosition(position.x, position.y, position.z);
        }
    }

    if (mirrorEvent == MirrorAnomaly::Event::Seen)
    {
        // 鏡の中の人影に気づいた：すぐ後ろから、かすかな息づかいが聞こえ、心拍がひとつ跳ねる
        game->PlayAudioCueAt(SOUND_CUE_BREATH, m_MirrorAnomaly.GetFigurePosition(), 0.82f, 0.42f);
        game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.30f);
        Input::SetVibration(3, 0.10f);
    }
    else if (mirrorEvent == MirrorAnomaly::Event::Found)
    {
        // 振り返ると誰もいない：鏡の上の照明が一瞬揺れ、奥の確認スイッチが押せるようになる
        m_Notices.loop = 2.8f;
        CeilingLight* mirrorLight = m_Objects.Light(Stage2NearestLight(MirrorAnomaly::CenterZ));
        if (mirrorLight != nullptr)
        {
            mirrorLight->TriggerEventFlicker(0.80f, 0.70f);
        }
        game->PlayAudioCueAt(
            SOUND_CUE_PIPE_KNOCK,
            Vector3(MirrorAnomaly::SurfaceX, MirrorAnomaly::CenterY, MirrorAnomaly::CenterZ), 0.70f, 0.50f);
        game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.42f);
        Input::SetVibration(6, 0.16f);
    }
}

// 肖像画を見つける周回（Stage2AnomalyPlanが決める）だけ判定している。
// 懐中電灯で照らしたまま見つめ続けると、絵の中の閉じていた目がかすかに開きかけ、やがて見開く。
// 途中で目を離すと、目は閉じて最初からやり直しにしている。
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
    // ライトが点いていて、近く（105未満）で正面（内積0.91超）から見ているか
    const bool lookingAtPortrait =
        player.IsFlashlightOn() && distance < 105.0f && facing > 0.91f;
    // 見つめている間は、絵の中の閉じた目がかすかに開きかけ、目を離すと閉じる（目の見え方は UpdatePortraitEyes）。
    if (!m_PortraitAnomaly.UpdateStare(lookingAtPortrait, deltaTime))
    {
        return;
    }

    // 見つめ続けた: 絵の中で目が見開かれ、赤く光る瞳がこちらを追い始める。奥の確認スイッチが押せるようになる。
    m_Notices.loop = 2.8f;
    constexpr float emission = 0.28f;
    Wall* portrait = m_Objects.portrait;
    if (portrait != nullptr)
    {
        portrait->SetAppearance(
            Color(0.10f, 0.014f, 0.009f, 1.0f),
            Color(emission * 0.12f, 0.0f, 0.0f, 1.0f),
            12.0f);
    }

    // 肖像画は時計の向かい（z=-25）にあるため、近くの照明と奥の扉の照明を揺らしている。
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

// 肖像画の絵の中の目：普段は閉じている。肖像画の異変の周回で見つめている間はかすかに開きかけ（目を離すとすぐ閉じる）、
// 見つめ続けて異変を見つけると見開かれ、赤く光る瞳が、見ている人の方へ動いて目で追い続ける。
void Stage2Scene::UpdatePortraitEyes(float deltaTime)
{
    Wall* portrait = m_Objects.portrait;
    if (portrait == nullptr)
    {
        return;
    }
    const bool required = m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Portrait);
    // 最後の停電と追跡の間も、目を見開いて、逃げる人を目で追う
    const bool finalEvent = m_FinalSequence.IsSequenceActive() || m_FinalSequence.IsPursuitActive();
    const bool opened = (required && m_PortraitAnomaly.HasChangedThisLoop()) || finalEvent;
    const float targetOpen = opened
        ? 1.0f
        : (required && m_PortraitAnomaly.IsStaring() ? 0.30f * m_PortraitAnomaly.GetStareRate() : 0.0f);
    // 開くときはゆっくり、閉じるときはすばやく（目を離した瞬間に閉じる）
    const float openSpeed = targetOpen > m_PortraitEyeOpen ? 1.4f : 5.0f;
    m_PortraitEyeOpen = targetOpen > m_PortraitEyeOpen
        ? (std::min)(targetOpen, m_PortraitEyeOpen + openSpeed * deltaTime)
        : (std::max)(targetOpen, m_PortraitEyeOpen - openSpeed * deltaTime);
    m_PortraitEyeGlow = (std::clamp)(m_PortraitEyeGlow + (opened ? 1.0f : -3.0f) * deltaTime, 0.0f, 1.0f);

    // 瞳の向き：絵の目の高さ・位置から見たカメラの方向（絵の面からの距離で割った横と縦のずれ）
    const Vector3 eyeCenter(38.9f, -67.5f, -25.0f);
    const Vector3 toCamera = Core::Game::GetInstance()->GetCamera()->GetPosition() - eyeCenter;
    const float depth = (std::max)(-toCamera.x, 1.0f);
    const float gazeX = (std::clamp)(toCamera.z / depth, -1.0f, 1.0f);
    const float gazeY = (std::clamp)(toCamera.y / depth, -1.0f, 1.0f);
    portrait->SetPortraitEyes(m_PortraitEyeOpen, gazeX, gazeY, m_PortraitEyeGlow);
}

// ----------------------------------------------------------------------------
// 壁の向こうのノック
// ノックを見つける周回（Stage2AnomalyPlanが決める）だけ、壁の裏から叩く音を立体音響で鳴らしている。
// 出どころの壁の前で立ち止まり、壁の方を向いて耳を澄ますと見つけたことになる。
// ----------------------------------------------------------------------------
namespace
{
    constexpr float KnockListenDistance = 24.0f;    // 出どころの壁の前とみなす距離
    constexpr float KnockListenFacing = 0.60f;      // 壁の方を向いているとみなす内積

    // 音の出どころの番号から位置を作っている
    Vector3 GetKnockSpot(int index)
    {
        const float* spot = Stage2KnockSpots[index];
        return Vector3(spot[0], spot[1], spot[2]);
    }
}

// 音の出どころの壁の手前（廊下の内側）の位置を返している。耳を澄ます場所と、案内の矢印の先に使っている。
Vector3 Stage2Scene::GetKnockListenPoint() const
{
    const Vector3 spot = GetKnockSpot(m_KnockingAnomaly.GetSpot());
    return Vector3(spot.x > 0.0f ? 38.0f : -38.0f, spot.y, spot.z);
}

// ノックの異変：決まったリズムで壁の裏から叩く音を鳴らし、聞き当てたかを判定している
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
        // 配管の音より低くし、木の扉を拳で叩くような鈍い音にしている。
        std::uniform_real_distribution<float> pitch(0.58f, 0.68f);
        game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, spot, pitch(m_PresenceRandom), 1.6f);
    }

    // 壁の手前（廊下の内側）の位置と、プレイヤーとの距離・向きで「耳を澄ませているか」を判定している（立ち止まっていることも条件）。
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

    // 聞き当てた: 壁のすぐ向こうで一度だけ強く叩き、音が止まる。
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
// 視界の外に人影を出し、見ていない間だけ近づけている。振り向けば消え、
// 気づかずに背後まで近づかれると、足音の危険度が一気に上がる。
// ----------------------------------------------------------------------------
namespace
{
    // 出す距離（背後58）、近づく速さ、見たと判定する距離と内積、追いつかれたときに上がる危険度
    constexpr float PresenceSpawnDistance = 58.0f;
    constexpr float PresenceCreepSpeed = 17.0f;     // 歩く速さ（30）より遅く、止まれば迫ってくる速さ
    constexpr float PresenceLookDistance = 170.0f;
    constexpr float PresenceLookAlignment = 0.88f;
    constexpr float PresenceReachThreat = 0.35f;
    // 廊下の内側に収め、壁の中に出ないようにしている。
    constexpr float CorridorHalfWidth = 30.0f;
    constexpr float CorridorMinZ = -150.0f;
    constexpr float CorridorMaxZ = 130.0f;
}

bool Stage2Scene::IsBehindPresenceAllowed() const
{
    // ほかの人影や大きな演出と重ねず、周回そのものの静けさの中でだけ出している。
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

// 背後の気配：見ているかと距離を調べ、BehindPresenceが返す出来事ごとに、影を出す・消す・驚かせる
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
        // カメラの真後ろへ置いている。廊下の外に出る場合は、次の機会（3秒後）に回している。
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
        // 振り向いた瞬間に消し、見間違いだったのかと思わせる程度の反応にとどめている。
        presence->SetActive(false);
        game->GetPostProcess()->TriggerHorrorPulse(0.14f, 0.18f);
        Input::SetVibration(2, 0.05f);
        break;
    // 気づかずに追いつかれた：照明が一斉に明滅し、危険度が上がる
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

// 今の危険度（0〜1）を求めている。足音の危険度、観察の間違い（必要なとき）、最後の追跡の影、足音の影のうち、一番高いものを使っている
float Stage2Scene::ComputeThreatRate(const Player& player, bool includeMistakes) const
{
    float threatRate = m_NoiseThreatSystem.GetThreat() * 0.78f;
    if (includeMistakes && (m_LoopCount == 1 || m_LoopCount == 2))
    {
        const float observationDanger =
            static_cast<float>(m_PuzzleFeedback.GetMistakeCount()) / 3.0f;
        threatRate = (std::max)(threatRate, observationDanger * 0.72f);
    }
    if (m_FinalSequence.IsPursuitActive())
    {
        const ShadowMan* shadow = m_Objects.shadow;
        if (shadow != nullptr)
        {
            Vector3 toShadow = shadow->GetPosition() - player.GetPosition();
            toShadow.y = 0.0f;
            const float distance = toShadow.Length();
            threatRate = 1.0f - (std::clamp)(
                (distance - 18.0f) / 92.0f, 0.0f, 1.0f);
        }
    }
    const ShadowMan* noiseShadow = m_Objects.noiseShadow;
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float noiseShadowDanger = 1.0f - (std::clamp)(
            (distance - 14.0f) / 72.0f, 0.0f, 1.0f);
        threatRate = (std::max)(threatRate, noiseShadowDanger);
    }
    return threatRate;
}

void Stage2Scene::UpdateTensionPulse(const Player& player, float deltaTime)
{
    // 観察の間違いの回数は含めていない（一度間違えると心拍が鳴りやまなくなるため）。今まさに迫っている危険だけを使っている。
    PlayTensionPulse(m_TensionPulse.Update(
        deltaTime, ComputeThreatRate(player, false), player.IsHiding()));
}

// 誰かの濡れた足跡：2周目以降、その周回で初めて水たまりから出ると、少し遅れて同じ水たまりから
// 裸足の濡れた足跡が一歩ずつ現れ、プレイヤーの方へ近づいてくる（姿は見えず、足跡と水音だけ）。
// 一番新しい足跡を懐中電灯で照らすとそこで止まり、照らさずにすぐ近くまで来させると、照明が揺れて止まる。
void Stage2Scene::UpdateFootprintFollower(const Player& player, float deltaTime)
{
    Ground* ground = m_Objects.ground;
    if (ground == nullptr)
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();

    Vector2 waterExit;
    const bool leftWater = ground->ConsumeWaterExit(waterExit);
    if (leftWater && m_LoopCount >= 2 && !m_FootprintFollowerUsedThisLoop &&
        !m_FootprintFollower.IsActive() && !m_FinalSequence.IsSequenceActive())
    {
        m_FootprintFollower.Start(waterExit);
        m_FootprintFollowerUsedThisLoop = true;
    }

    const Camera* camera = game->GetCamera();
    FootprintFollower::Step step;
    const FootprintFollower::Event followerEvent = m_FootprintFollower.Update(
        deltaTime, player.GetPosition(), camera->GetPosition(), camera->GetForward(),
        player.IsFlashlightOn(), step);
    if (followerEvent == FootprintFollower::Event::Step)
    {
        // 裸足の濡れた足跡を床に残し、その場所から、ぺたりと濡れた足音を鳴らしている
        ground->AddFootprint(step.Position, step.Yaw, step.Side, true, 1.0f);
        game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, Vector3(step.Position.x, -99.0f, step.Position.y), 0.78f, 0.55f);
    }
    else if (followerEvent == FootprintFollower::Event::Lit)
    {
        // 照らされて止まった：心拍がひとつ跳ねるだけにしている
        game->GetPostProcess()->TriggerHorrorPulse(0.14f, 0.26f);
        Input::SetVibration(2, 0.08f);
    }
    else if (followerEvent == FootprintFollower::Event::Reached)
    {
        // すぐ近くまで来た：近くの照明が揺れ、すぐ後ろで息づかいが聞こえる
        const Vector3 playerPosition = player.GetPosition();
        CeilingLight* nearbyLight = m_Objects.Light(Stage2NearestLight(playerPosition.z));
        if (nearbyLight != nullptr)
        {
            nearbyLight->TriggerEventFlicker(0.70f, 0.60f);
        }
        Vector3 behind = playerPosition - camera->GetForward() * 10.0f;
        behind.y = playerPosition.y;
        game->PlayAudioCueAt(SOUND_CUE_BREATH, behind, 0.85f, 0.45f);
        game->GetPostProcess()->TriggerHorrorPulse(0.30f, 0.40f);
        Input::SetVibration(5, 0.14f);
    }
}
