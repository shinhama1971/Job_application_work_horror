// ============================================================================
// ファイルの役割: 2面の恐怖の進行（影を見たときの照明・最後の停電と追跡・捕まったとき・光の区画・背後の気配・危険度と心拍・誰かの足跡・吊るした電球）を担当している。
// 主な技術: 有限状態機械、照明の演出、距離と視線の判定、時間で進む演出
// 値（場所・秒数・強さ）は Stage2HorrorSettings.h にまとめている。
// ============================================================================

#include "Stage2Scene.h"
#include "MathUtility.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "MirrorSelf.h"
#include "Ground.h"
#include "HangingBulb.h"
#include "TensionPulseFeedback.h"
#include "EventFeedback.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace DirectX::SimpleMath;
#include "Stage2HorrorSettings.h"
using namespace Stage2HorrorSettings;

// 影を見てしまったときの演出を始めている（驚かせる音・画面・振動）
void Stage2Scene::StartObservedScare()
{
    if (!m_ObservedScareSequence.Start())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), ObservedScarePulse);
    EventFeedback::Rumble(ObservedScareRumble);
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
        { Stage2Light::DoorLight, ObservedScareDoorLightStrength },
        { Stage2Light::Light3, ObservedScareLight3Strength },
        { Stage2Light::Light2, ObservedScareLight2Strength },
        { Stage2Light::Light1, ObservedScareLight1Strength }
    };

    int pendingBeat = m_ObservedScareSequence.ConsumePendingBeat();
    while (pendingBeat >= 0)
    {
        const LightBeat& beat = beats[pendingBeat];
        CeilingLight* light = m_Objects.Light(beat.Light);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(ObservedScareFlickerSeconds, beat.Strength);
        }

        game->GetPostProcess()->TriggerBloomPulse(
            ObservedScareBloomBase + beat.Strength * ObservedScareBloomPerStrength,
            ObservedScareBloomSeconds);
        pendingBeat = m_ObservedScareSequence.ConsumePendingBeat();
    }

    if (m_ObservedScareSequence.GetTimer() < ObservedScareSeconds)
    {
        const float intensity =
            m_ObservedScareSequence.GetAtmosphereIntensity();
        game->GetPostProcess()->SetAtmosphere(
            ObservedScareNoiseBase + intensity * ObservedScareNoiseRange,
            ObservedScareVignetteBase + intensity * ObservedScareVignetteRange);
    }
    else
    {
        m_ObservedScareSequence.CompleteIfElapsed();
    }
}

// 照明の区画：廊下を進むと照明を明滅させ、2周目以降は通り過ぎた照明を消している
void Stage2Scene::UpdateLightZones(const Player& player)
{
    if (m_LoopCount >= Stage2FinalLoopCount || m_FinalSequence.IsSequenceActive())
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
        { LightZone1Z, Stage2Light::Light1 },
        { LightZone2Z, Stage2Light::Light2 },
        { LightZone3Z, Stage2Light::Light3 },
        { LightZoneDoorZ, Stage2Light::DoorLight }
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
        const float loopStrength = static_cast<float>(m_LoopCount) * LightZoneLoopStrength;
        const float strength = (std::clamp)(
            LightZoneBaseStrength + loopStrength + static_cast<float>(index) * LightZoneIndexStrength,
            0.0f,
            LightZoneMaxStrength);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(LightZoneFlickerBaseSeconds + loopStrength, strength);
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
            LightZoneBloomBase + strength * LightZoneBloomPerStrength,
            LightZoneBloomSeconds);
        if (m_LoopCount > 0 && index >= 1)
        {
            game->GetPostProcess()->TriggerHorrorPulse(
                LightZonePulseBase + loopStrength * LightZonePulsePerLoop,
                LightZonePulseSeconds);
        }
        Input::SetVibration(LightZoneRumbleBase.Frames + m_LoopCount,
            LightZoneRumbleBase.Strength + loopStrength * LightZoneRumblePerLoop);
    }
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
    float distance = PresenceFarDistance;
    if (m_BehindPresence.IsFollowing())
    {
        Vector3 toPlayer = player.GetPosition() - presence->GetPosition();
        toPlayer.y = 0.0f;
        distance = toPlayer.Length();

        Vector3 toPresence = presence->GetPosition() + Vector3(0.0f, PresenceChestHeight, 0.0f) -
            camera->GetPosition();
        const float viewDistance = toPresence.Length();
        if (viewDistance > MinimumDirectionLength && viewDistance < PresenceLookDistance)
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
        if (backward.LengthSquared() < MinimumDirectionLength)
        {
            m_BehindPresence.Postpone(BehindPresenceRetrySeconds);
            break;
        }
        backward.Normalize();
        const Vector3 spawn = player.GetPosition() + backward * PresenceSpawnDistance;
        if (std::abs(spawn.x) > CorridorHalfWidth ||
            spawn.z < CorridorMinZ || spawn.z > CorridorMaxZ)
        {
            m_BehindPresence.Postpone(BehindPresenceRetrySeconds);
            break;
        }

        presence->SetPosition(spawn.x, FootY, spawn.z);
        presence->SetActive(true);
        presence->EnableGazeScare(BehindPresence::MaxFollowSeconds + PresenceGazeExtraSeconds);
        presence->EnableChase(PresenceCreepSpeed, PresenceChaseStopDistance);
        break;
    }
    case BehindPresence::Event::Seen:
        // 振り向いた瞬間に消し、見間違いだったのかと思わせる程度の反応にとどめている。
        presence->SetActive(false);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), PresenceSeenPulse);
        EventFeedback::Rumble(PresenceSeenRumble);
        break;
    // 気づかずに追いつかれた：照明が一斉に明滅し、危険度が上がる
    case BehindPresence::Event::Reached:
        presence->SetActive(false);
        for (CeilingLight* light : m_Objects.lights)
        {
            EventFeedback::Flicker(*light, PresenceReachedFlicker);
        }
        m_NoiseThreatSystem.SetThreat((std::min)(
            1.0f, m_NoiseThreatSystem.GetThreat() + PresenceReachThreat));
        game->PlayAudioCue(SOUND_CUE_SCARE, PresenceReachedScareSound.Pitch, PresenceReachedScareSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), PresenceReachedPulse);
        EventFeedback::Rumble(PresenceReachedRumble);
        break;
    case BehindPresence::Event::Vanished:
        presence->SetActive(false);
        break;
    }
}

// 今の危険度（0〜1）を求めている。足音の危険度、観察の間違い（必要なとき）、最後の追跡の影、足音の影のうち、一番高いものを使っている
float Stage2Scene::ComputeThreatRate(const Player& player, bool includeMistakes) const
{
    float threatRate = m_NoiseThreatSystem.GetThreat() * NoiseThreatToHeartbeat;
    // 見えない影が近いほど、心拍も速くなる
    threatRate = (std::max)(threatRate, m_UnseenPresence.GetProximity(player.GetPosition()) * UnseenProximityToHeartbeat);
    if (includeMistakes && m_LoopCount > 0 && m_LoopCount <= Stage2AnomalyPlan::PlannedLoopCount)
    {
        const float observationDanger =
            static_cast<float>(m_PuzzleFeedback.GetMistakeCount()) / MistakeCountForFullDanger;
        threatRate = (std::max)(threatRate, observationDanger * MistakeDangerToHeartbeat);
    }
    if (m_FinalSequence.IsPursuitActive())
    {
        const ShadowMan* shadow = m_Objects.shadow;
        if (shadow != nullptr)
        {
            Vector3 toShadow = shadow->GetPosition() - player.GetPosition();
            toShadow.y = 0.0f;
            const float distance = toShadow.Length();
            threatRate = MathUtility::Proximity(distance, PursuitDangerNearDistance, PursuitDangerRange);
        }
    }
    const ShadowMan* noiseShadow = m_Objects.noiseShadow;
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float noiseShadowDanger =
            MathUtility::Proximity(distance, NoiseShadowDangerNearDistance, NoiseShadowDangerRange);
        threatRate = (std::max)(threatRate, noiseShadowDanger);
    }
    return threatRate;
}

void Stage2Scene::UpdateTensionPulse(const Player& player, float deltaTime)
{
    // 観察の間違いの回数は含めていない（一度間違えると心拍が鳴りやまなくなるため）。今まさに迫っている危険だけを使っている。
    const TensionPulse::Output pulse = m_TensionPulse.Update(
        deltaTime, ComputeThreatRate(player, false), player.IsHiding());
    PlayTensionPulse(pulse);
    // 緊張が高いほど、画面の端から視界が狭まり、心拍に合わせて脈打つ（ロッカーの中では、息を殺す緊張でわずかに狭まる）
    ShowFearVision(m_TensionPulse, pulse);

    // 携帯ラジオの雑音：足音で上がる危険度ではなく、実際に近くにいる影（足音の影・最後の追跡の影・背後の気配・見えない影）の近さで鳴らしている
    float presence = 0.0f;
    for (const ShadowMan* figure : { m_Objects.noiseShadow, m_Objects.shadow, m_Objects.presence })
    {
        if (figure == nullptr || !figure->IsActive())
        {
            continue;
        }
        Vector3 toFigure = figure->GetPosition() - player.GetPosition();
        toFigure.y = 0.0f;
        presence = (std::max)(presence,
            MathUtility::Proximity(toFigure.Length(), RadioPresenceNearDistance, RadioPresenceRange));
    }
    // 見えない影の異変の周回では、姿の見えない影の近さも雑音に含めている（手がかりはこの雑音だけ）
    presence = (std::max)(presence, m_UnseenPresence.GetProximity(player.GetPosition()));
    m_RadioStatic.Update(deltaTime, presence, true);
    // 影が近いほど、吊るした裸電球がすきま風にあおられるように大きく揺れる
    for (HangingBulb* bulb : m_Objects.hangingBulbs)
    {
        if (bulb != nullptr)
        {
            bulb->SetDisturbance(m_RadioStatic.GetLevel());
        }
    }
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
    if (leftWater && !m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Unseen) &&
        m_LoopCount >= FootprintFollowerStartLoop && !m_FootprintFollowerUsedThisLoop &&
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
        game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, Vector3(step.Position.x, FootY, step.Position.y),
            FollowerWaterStepSound.Pitch, FollowerWaterStepSound.Volume);
    }
    else if (followerEvent == FootprintFollower::Event::Lit)
    {
        // 照らされて止まった：心拍がひとつ跳ねるだけにしている
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FollowerLitPulse);
        EventFeedback::Rumble(FollowerLitRumble);
    }
    else if (followerEvent == FootprintFollower::Event::Reached)
    {
        // すぐ近くまで来た：近くの照明が揺れ、すぐ後ろで息づかいが聞こえる
        KickHangingBulbs(FollowerReachedBulbKick);
        const Vector3 playerPosition = player.GetPosition();
        CeilingLight* nearbyLight = m_Objects.Light(Stage2NearestLight(playerPosition.z));
        if (nearbyLight != nullptr)
        {
            EventFeedback::Flicker(*nearbyLight, FollowerReachedFlicker);
        }
        Vector3 behind = playerPosition - camera->GetForward() * FollowerBreathBehindDistance;
        behind.y = playerPosition.y;
        game->PlayAudioCueAt(SOUND_CUE_BREATH, behind, FollowerBreathSound.Pitch, FollowerBreathSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FollowerReachedPulse);
        EventFeedback::Rumble(FollowerReachedRumble);
    }
}

// 吊るした裸電球を揺さぶっている（周回の切り替え・異変を見つけたとき・影がすぐ近くまで来たとき）
void Stage2Scene::KickHangingBulbs(float strength)
{
    for (HangingBulb* bulb : m_Objects.hangingBulbs)
    {
        if (bulb != nullptr)
        {
            bulb->Kick(strength);
        }
    }
}
