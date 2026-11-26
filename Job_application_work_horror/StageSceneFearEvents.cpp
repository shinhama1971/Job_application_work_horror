// ============================================================================
// ファイルの役割: 1面の静かな時間に起こす小さな恐怖の出来事（Stage1FearEvents）を、音・照明・懐中電灯・人影・画面効果へ移している。
// 周回ごとに部屋が腐っていく見た目（壁のカビ・垂れ跡・血の跡・水の跡が広がる。Renderer::SetRoomDecay）もここで進めている。
// いつ・何を起こすかは Stage1FearEvents が決め、ここでは起こせるか（後ろの扉・前の照明・懐中電灯）を調べて渡している。
// ============================================================================

#include "StageScene.h"
#include "Stage1EventSettings.h"
#include "Camera.h"
#include "CaptureMode.h"
#include "CeilingLight.h"
#include "Door.h"
#include "EventFeedback.h"
#include "Game.h"
#include "Player.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "ShadowMan.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace DirectX::SimpleMath;
using namespace Stage1EventSettings;

namespace
{
    // 上から見た向き（長さ1）。真上・真下を向いているときは0を返している
    Vector3 FlatDirection(const Vector3& vector)
    {
        Vector3 flat(vector.x, 0.0f, vector.z);
        const float length = flat.Length();
        return length > MinimumLitDistance ? flat / length : Vector3::Zero;
    }
}

void StageScene::UpdateFearEvents(Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    // 周回ごとに、開始地点へ戻ったときには部屋が少しずつ腐っている（変わらなければ送り直さない）
    Renderer::SetRoomDecay(static_cast<float>(m_CorridorLoopCount) * RoomDecayPerLoop);

    m_FearEventCues.clear();
    m_FearEvents.Update(BuildFearEventInput(player, deltaTime), m_FearEventCues);
    for (const Stage1FearEvents::Cue& cue : m_FearEventCues)
    {
        HandleFearEventCue(player, cue);
    }

    const float otherworld = m_FearEvents.GetOtherworld();
    if (otherworld > 0.0f)
    {
        game->GetPostProcess()->SetOtherworld(otherworld, m_FearEvents.GetOtherworldSeed());
    }
}

Stage1FearEvents::Input StageScene::BuildFearEventInput(const Player& player, float deltaTime) const
{
    Stage1FearEvents::Input input;
    input.DeltaTime = deltaTime;
    // 自動撮影・計測では、道順の見た目を毎回同じにするため起こさない
    input.Calm = !Tools::CaptureMode::IsActive() && IsAmbientSoundAllowed() &&
        !m_FearEvents.IsActive() && ComputeThreatRate(player) < FearEventMaxThreat;
    input.FlashlightOn = player.IsFlashlightOn();

    const Vector3 position = player.GetPosition();
    const Vector3 forward = FlatDirection(Core::Game::GetInstance()->GetCamera()->GetForward());

    // 後ろにある閉じた扉のうち、一番近いもの（開始地点の奥の扉は、周回の演出で叩いているので使わない）
    float nearestDoor = FearKnockMaxDistance;
    for (const Door* door : m_StalkerDoors)
    {
        if (door == nullptr || door == m_Objects.startDoor || door->IsOpen() || door->IsOpening())
        {
            continue;
        }
        const Vector3 toDoor(door->GetPosition().x - position.x, 0.0f, door->GetPosition().z - position.z);
        const float distance = toDoor.Length();
        if (distance < FearKnockMinDistance || distance > nearestDoor ||
            forward.Dot(toDoor / distance) > FearKnockBehindDot)
        {
            continue;
        }
        nearestDoor = distance;
        input.KnockAvailable = true;
        input.KnockPosition = door->GetPosition();
    }

    // 前にあって点いている天井の照明（奥から手前の順）
    std::vector<std::pair<float, int>> ahead;
    for (int number = 1; number <= StageObjects::CeilingLightCount; ++number)
    {
        const CeilingLight* light = m_Objects.CeilingLightAt(number);
        if (light == nullptr || light->IsForcedOff() || light->GetBrightness() < FearLightMinBrightness)
        {
            continue;
        }
        const Vector3 toLight(light->GetPosition().x - position.x, 0.0f, light->GetPosition().z - position.z);
        const float distance = toLight.Length();
        if (distance < FearLightMinDistance || distance > FearLightMaxDistance ||
            forward.Dot(toLight / distance) < FearLightAheadDot)
        {
            continue;
        }
        ahead.emplace_back(distance, number);
    }
    std::sort(ahead.begin(), ahead.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& entry : ahead)
    {
        input.LightsAhead.push_back(entry.second);
    }
    return input;
}

void StageScene::HandleFearEventCue(Player& player, const Stage1FearEvents::Cue& cue)
{
    Core::Game* game = Core::Game::GetInstance();
    CeilingLight* light = cue.LightNumber > 0 ? m_Objects.CeilingLightAt(cue.LightNumber) : nullptr;
    switch (cue.Type)
    {
    case Stage1FearEvents::CueType::Knock:
        // 通り過ぎた扉の内側から、拳で叩く鈍い音（開始地点の扉のノックと同じ音）
        game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, cue.Position, cue.Pitch, cue.Volume * FearKnockVolumeGain);
        break;
    case Stage1FearEvents::CueType::FlashlightSputter:
        player.SputterFlashlight(cue.Seconds);
        break;
    case Stage1FearEvents::CueType::FlashlightOut:
        player.SetFlashlightBlocked(true);
        game->PlayAudioCue(SOUND_CUE_FLASHLIGHT, FearFlashlightOutSound.Pitch, FearFlashlightOutSound.Volume);
        break;
    case Stage1FearEvents::CueType::FlashlightBack:
        player.SetFlashlightBlocked(false);
        player.SetFlashlightOn(true);
        ShowFearFigure(player);
        break;
    case Stage1FearEvents::CueType::FigureHide:
        if (ShadowMan* figure = m_Objects.fearFigure; figure != nullptr && figure->IsActive())
        {
            // 懐中電灯が一瞬ちらつく間に、人影は消えている
            player.SputterFlashlight(FearFigureHideSputterSeconds);
            figure->SetExternallyControlled(false);
            figure->SetActive(false);
        }
        break;
    case Stage1FearEvents::CueType::LightOff:
        if (light != nullptr)
        {
            light->SetForcedOff(true);
            game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, light->GetPosition(), FearLightOffSound.Pitch, FearLightOffSound.Volume);
        }
        break;
    case Stage1FearEvents::CueType::LightOn:
        if (light != nullptr)
        {
            light->SetForcedOff(false);
            EventFeedback::Flicker(*light, FearLightRestoreFlicker);
        }
        break;
    case Stage1FearEvents::CueType::OtherworldFlash:
        game->PlayAudioCue(SOUND_CUE_SCARE, FearOtherworldSound.Pitch, FearOtherworldSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FearOtherworldPulse);
        break;
    default:
        break;
    }
}

// 懐中電灯の光の中で、間に壁や扉がない所を、遠い候補から選んで人影を立たせている（見える所がなければ出さない）
void StageScene::ShowFearFigure(const Player& player)
{
    ShadowMan* figure = m_Objects.fearFigure;
    if (figure == nullptr)
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    const Vector3 forward = FlatDirection(game->GetCamera()->GetForward());
    const Vector3 position = player.GetPosition();
    for (const float distance : FearFigureDistances)
    {
        const Vector3 foot(position.x + forward.x * distance, FootY, position.z + forward.z * distance);
        if (!IsLitByFlashlight(player, foot + Vector3(0.0f, FearFigureLitHeight, 0.0f)))
        {
            continue;
        }
        figure->SetPosition(foot.x, foot.y, foot.z);
        figure->SetActive(true);
        // 時間切れや追う動きはせず、こちらを向いて立っているだけにしている（消すのは FigureHide の合図）
        figure->SetExternallyControlled(true);
        game->PlayAudioCueAt(SOUND_CUE_SCARE, foot, FearFigureSound.Pitch, FearFigureSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FearFigurePulse);
        EventFeedback::Rumble(FearFigureRumble);
        return;
    }
}

void StageScene::RestoreFearEvents()
{
    for (const int number : m_FearEvents.GetLightsOff())
    {
        if (CeilingLight* light = m_Objects.CeilingLightAt(number))
        {
            light->SetForcedOff(false);
        }
    }
    if (ShadowMan* figure = m_Objects.fearFigure)
    {
        figure->SetExternallyControlled(false);
        figure->SetActive(false);
    }
    if (Player* player = m_Objects.player)
    {
        player->SetFlashlightBlocked(false);
    }
}
