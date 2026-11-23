// ============================================================================
// ファイルの役割: 1面の書類保管室、時間が止まる部屋、反響する部屋と、濡れた足跡の演出を更新する。
// ============================================================================

#include "StageScene.h"
#include "Stage1EventSettings.h"
#include "ShadowFigureGeometry.h"
#include "Application.h"
#include "Camera.h"
#include "CeilingLight.h"
#include "Door.h"
#include "EventFeedback.h"
#include "Game.h"
#include "Ground.h"
#include "Player.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <algorithm>

using namespace DirectX::SimpleMath;
using namespace Stage1EventSettings;

// 動きのルールはStage1LightStalkerが決め、ここでは照らされているかの判定と、表示・音・電池の変化を行っている。
bool StageScene::UpdateArchiveStalker(Player& player, float deltaTime)
{
    ShadowMan* shadow = m_Objects.archiveStalker;
    if (shadow == nullptr)
    {
        return false;
    }
    Core::Game* game = Core::Game::GetInstance();
    m_ArchiveStalkerNoticeTimer = (std::max)(0.0f, m_ArchiveStalkerNoticeTimer - deltaTime);

    Stage1LightStalker::FrameInput input;
    input.PlayerPosition = player.GetPosition();
    // 電力が戻った後と、隠し部屋に閉じ込められている間は出さない
    input.Enabled = !game->IsPowerRestored() && !m_HiddenRoom.IsTrapped();
    // 視線判定と同じ胸の位置を使い、影の見た目に合わせる。
    input.Lit = m_ArchiveStalker.IsActive() &&
        IsLitByFlashlight(player, shadow->GetPosition() + Vector3(0.0f, ShadowFigureGeometry::ChestHeight, 0.0f));
    input.DeltaTime = deltaTime;

    // 影は棚・壁・閉じた扉を通り抜けず、押し戻されて沿うように回り込む
    const Stage1LightStalker::FrameResult result = m_ArchiveStalker.Update(
        input,
        [this](Vector3& position, float radius)
        {
            for (const Wall* wall : m_StalkerWalls)
            {
                wall->ResolveCollision(position, radius);
            }
            for (const Door* door : m_StalkerDoors)
            {
                door->ResolveCollision(position, radius);
            }
        });

    // 影の位置と表示を、ルールの結果に合わせている
    if (result.Visible)
    {
        shadow->SetPosition(result.Position.x, result.Position.y, result.Position.z);
    }
    if (shadow->IsActive() != result.Visible)
    {
        shadow->SetActive(result.Visible);
    }

    // 現れたとき：棚の奥から低い音を鳴らし、気配のする方へ視線を誘う。
    if (result.Appeared)
    {
        game->PlayAudioCueAt(
            SOUND_CUE_PIPE_KNOCK, result.Position + Vector3(0.0f, ArchiveStalkerKnockHeight, 0.0f),
            ArchiveStalkerKnockSound.Pitch, ArchiveStalkerKnockSound.Volume);
        m_ArchiveStalkerNotice = result.FirstAppearance
            ? "棚の奥で、何かが動いた"
            : "また棚の間に何かいる";
        m_ArchiveStalkerNoticeTimer = result.FirstAppearance ? ArchiveStalkerFirstNoticeSeconds : ArchiveStalkerNoticeSeconds;
    }

    // 光が外れて近づいている間は、影の足元から重い足音を鳴らしている（立体音響で、見えなくても方向が分かる）
    if (result.Step)
    {
        game->PlayAudioCueAt(
            SOUND_CUE_FOOTSTEP, result.Position + Vector3(0.0f, ArchiveStalkerStepHeight, 0.0f),
            ArchiveStalkerStepSound.Pitch, ArchiveStalkerStepSound.Volume);
    }

    // 触れられたとき：懐中電灯の電池を奪っている（5%は残し、完全には空にしない）
    if (result.Caught)
    {
        constexpr float BatteryPenalty = 30.0f;
        const float taken = (std::min)(
            BatteryPenalty, (std::max)(0.0f, player.GetBattery() - ArchiveStalkerBatteryFloor));
        player.AddBattery(-taken);
        game->PlayAudioCue(SOUND_CUE_SCARE, ArchiveStalkerCaughtSound.Pitch, ArchiveStalkerCaughtSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), ArchiveStalkerCaughtPulse);
        EventFeedback::Rumble(ArchiveStalkerCaughtRumble);
        m_ArchiveStalkerNotice = "影に触れられた 電池を奪われた";
        m_ArchiveStalkerNoticeTimer = ArchiveStalkerCaughtNoticeSeconds;
    }
    return result.Visible && input.Lit;
}

// ----------------------------------------------------------------------------
// 誰かの濡れた足跡
// 西棟の水から初めて乾いた床へ出ると、少し遅れて同じ水から裸足の濡れた足跡が一歩ずつ現れ、
// プレイヤーの足跡のすぐ横に並んで近づいてくる（姿は見えず、足跡と水音だけ。1回のプレイで1回だけ）。
// 一番新しい足跡を懐中電灯で照らすとそこで止まり、照らさずにすぐ近くまで来させると、すぐ後ろで息づかいが聞こえる。
// ----------------------------------------------------------------------------
void StageScene::UpdateStrangeRooms(const Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    const Vector3 position = player.GetPosition();
    ShadowMan* figure = m_Objects.timeStopFigure;

    m_TimeStopCues.clear();
    // 人影を出してよいか：時間が止まっている間は、ほかの演出を止めている（IsAmbientSoundAllowed が false になる）ので、そのときは出してよいことにしている
    m_TimeStopRoom.Update(deltaTime, position, m_CorridorLoopCount, IsAmbientSoundAllowed() || m_TimeStopRoom.IsActive(),
        m_TimeStopCues);
    for (const Stage1TimeStopRoom::Cue& cue : m_TimeStopCues)
    {
        switch (cue.Type)
        {
        case Stage1TimeStopRoom::CueType::Freeze:
            game->PlayAudioCue(SOUND_CUE_POWER, TimeStopFreezeSound.Pitch, TimeStopFreezeSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), TimeStopFreezePulse);
            break;
        case Stage1TimeStopRoom::CueType::Resume:
            game->PlayAudioCue(SOUND_CUE_POWER, TimeStopResumeSound.Pitch, TimeStopResumeSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), TimeStopResumePulse);
            // 部屋を出たら、人影も止まった世界と一緒に消える
            if (figure != nullptr)
            {
                figure->SetActive(false);
            }
            break;
        case Stage1TimeStopRoom::CueType::SpawnFigure:
            if (figure != nullptr)
            {
                figure->SetPosition(cue.Position.x, cue.Position.y, cue.Position.z);
                figure->SetActive(true);
                figure->EnableGazeScare(TimeStopFigureLifetime);
                figure->EnableChase(TimeStopFigureSpeed, TimeStopFigureStopDistance);
                // 照らして崩したら、止まっていた時間が一気に戻る
                figure->SetOnObserved([this]()
                {
                    BreakTimeStop();
                });
            }
            break;
        }
    }

    // 人影に触れられたら、驚かせて、時間を一気に戻している
    if (figure != nullptr && figure->IsActive())
    {
        const Vector3 toFigure = figure->GetPosition() - position;
        if (Vector2(toFigure.x, toFigure.z).Length() <= TimeStopFigureCatchDistance)
        {
            figure->SetActive(false);
            game->PlayAudioCue(SOUND_CUE_SCARE, TimeStopCaughtSound.Pitch, TimeStopCaughtSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), TimeStopCaughtPulse);
            EventFeedback::Rumble(TimeStopCaughtRumble);
            BreakTimeStop();
        }
    }
    Application::SetWorldTimeScale(m_TimeStopRoom.GetWorldTimeScale());

    // 傾く部屋：奥へ進むほど視界が傾き、暖かい灯りの下ではまっすぐに戻る
    m_TiltRoom.Update(deltaTime, position, m_CorridorLoopCount, m_SafeLights.GetCalm());
    game->GetCamera()->SetViewRoll(m_TiltRoom.GetRoll());
}

void StageScene::UpdateEchoRoom(Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* figure = m_Objects.echoFigure;
    m_EchoRoomCues.clear();
    m_EchoRoom.Update(deltaTime, player.GetPosition(), player.IsSprinting(), m_EchoRoomCues);
    for (const Stage1EchoRoom::Cue& cue : m_EchoRoomCues)
    {
        switch (cue.Type)
        {
        case Stage1EchoRoom::CueType::Darken:
            // 部屋の照明を消し、懐中電灯をちらつかせてから消している
            m_EchoRoomLights.clear();
            for (CeilingLight* light : game->GetObjects<CeilingLight>())
            {
                const Vector3 lightPosition = light->GetPosition();
                if (light->IsForcedOff() ||
                    lightPosition.x < Stage1EchoRoom::MinX - EchoRoomLightMargin ||
                    lightPosition.x > Stage1EchoRoom::MaxX + EchoRoomLightMargin ||
                    lightPosition.z < Stage1EchoRoom::MinZ - EchoRoomLightMargin ||
                    lightPosition.z > Stage1EchoRoom::MaxZ + EchoRoomLightMargin)
                {
                    continue;
                }
                light->SetForcedOff(true);
                m_EchoRoomLights.push_back(light);
            }
            player.SputterFlashlight(EchoRoomFlashlightSputterSeconds);
            player.SetFlashlightBlocked(true);
            game->PlayAudioCue(SOUND_CUE_POWER, EchoRoomDarkenSound.Pitch, EchoRoomDarkenSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), EchoRoomDarkenPulse);
            if (figure != nullptr)
            {
                // 人影の位置は Stage1EchoRoom が決める（プレイヤーの方を向くだけ）
                figure->SetExternallyControlled(true);
                figure->SetActive(true);
            }
            break;
        case Stage1EchoRoom::CueType::FigureStep:
            game->PlayAudioCueAt(SOUND_CUE_FOOTSTEP, cue.Position, EchoFigureStepSound.Pitch, EchoFigureStepSound.Volume);
            break;
        case Stage1EchoRoom::CueType::Caught:
            game->PlayAudioCue(SOUND_CUE_SCARE, EchoRoomCaughtSound.Pitch, EchoRoomCaughtSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), EchoRoomCaughtPulse);
            EventFeedback::Rumble(EchoRoomCaughtRumble);
            break;
        case Stage1EchoRoom::CueType::Restore:
            for (CeilingLight* light : m_EchoRoomLights)
            {
                light->SetForcedOff(false);
            }
            m_EchoRoomLights.clear();
            player.SetFlashlightBlocked(false);
            if (figure != nullptr)
            {
                figure->SetActive(false);
                figure->SetExternallyControlled(false);
            }
            break;
        }
    }

    if (m_EchoRoom.IsDark())
    {
        if (figure != nullptr)
        {
            const Vector3 figurePosition = m_EchoRoom.GetFigurePosition();
            figure->SetPosition(figurePosition.x, figurePosition.y, figurePosition.z);
        }
    }
    const auto& pulses = m_EchoRoom.GetPulses();
    Renderer::SetEchoPulses(pulses.data(), m_EchoRoom.GetPulseStrengths().data(), static_cast<unsigned int>(pulses.size()));
}

void StageScene::BreakTimeStop()
{
    if (!m_TimeStopRoom.IsActive())
    {
        return;
    }
    m_TimeStopRoom.BreakSpell();
    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCue(SOUND_CUE_POWER, TimeStopResumeSound.Pitch, TimeStopResumeSound.Volume);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), TimeStopResumePulse);
}

void StageScene::UpdateFootprintFollower(Player& player)
{
    Ground* ground = m_Objects.ground;
    if (ground == nullptr)
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    const float deltaTime = Application::GetDeltaTime();

    // 水から出た瞬間は毎回知らせが来るので、西棟の水から出たときだけ使っている（ほかの水たまりは足跡が残るだけ）
    Vector2 waterExit;
    if (ground->ConsumeWaterExit(waterExit) && !m_FootprintFollowerUsed &&
        Stage1WestWing::IsInside(Vector3(waterExit.x, player.GetPosition().y, waterExit.y)))
    {
        m_FootprintFollower.Start(waterExit);
        m_FootprintFollowerUsed = true;
    }

    const Camera* camera = game->GetCamera();
    FootprintFollower::Step step;
    const FootprintFollower::Event followerEvent = m_FootprintFollower.Update(
        deltaTime, player.GetPosition(), camera->GetPosition(), camera->GetForward(),
        player.IsFlashlightOn(), step);
    if (followerEvent == FootprintFollower::Event::Step)
    {
        ground->AddFootprint(step.Position, step.Yaw, step.Side, true, 1.0f);
        game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, Vector3(step.Position.x, FootY, step.Position.y),
            FollowerWaterStepSound.Pitch, FollowerWaterStepSound.Volume);
    }
    else if (followerEvent == FootprintFollower::Event::Lit)
    {
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FollowerLitPulse);
        EventFeedback::Rumble(FollowerLitRumble);
    }
    else if (followerEvent == FootprintFollower::Event::Reached)
    {
        const Vector3 playerPosition = player.GetPosition();
        Vector3 behind = playerPosition - camera->GetForward() * FollowerBreathBehindDistance;
        behind.y = playerPosition.y;
        game->PlayAudioCueAt(SOUND_CUE_BREATH, behind, FollowerBreathSound.Pitch, FollowerBreathSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FollowerReachedPulse);
        EventFeedback::Rumble(FollowerReachedRumble);
    }
}
