// ============================================================================
// ファイルの役割: 1面のループ廊下（周回・最後のドアから開始地点へのつながり・周回ごとの変化・振り返ると伸びる廊下・呼吸する壁）を担当している。
// 主な技術: 見えない所での位置の移し替え、周回ごとの段階、ビュー行列での見え方の引き伸ばし、頂点シェーダーで動かす壁
// 値（場所・秒数・強さ）は Stage1EventSettings.h にまとめている。
// ============================================================================

#include "StageScene.h"
#include "Stage1EventSettings.h"
#include "CaptureMode.h"
#include "Stage1FuseProgress.h"
#include "ShadowFigureGeometry.h"
#include "MathUtility.h"
#include "Application.h"
#include "Game.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "Input.h"

#include "Player.h"
#include "Ground.h"
#include "Wall.h"
#include "Item.h"
#include "Door.h"
#include "FuseBox.h"
#include "CeilingLight.h"
#include "ExitTrigger.h"
#include "BatteryItem.h"
#include "ScreenDustOverlay.h"
#include "ScareTrigger.h"
#include "ShadowMan.h"
#include "FlashlightWriting.h"
#include "Camera.h"
#include "TensionPulseFeedback.h"
#include "VoiceRecorder.h"
#include "EventFeedback.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

using namespace DirectX::SimpleMath;
using namespace Stage1EventSettings;

// ループ廊下：電力が戻る前に、突き当たりの最後のドアを開けたら、その瞬間に開始地点の奥のドアの裏側へ移して次の周回を始めている
void StageScene::UpdateCorridorLoop(Player& player)
{
    const float deltaTime = Application::GetDeltaTime();
    if (m_LoopCooldown > 0.0f)
    {
        m_LoopCooldown -= deltaTime;
    }
    if (m_LoopNoticeTimer > 0.0f)
    {
        m_LoopNoticeTimer -= deltaTime;
    }

    Core::Game* game = Core::Game::GetInstance();

    // 開始地点の奥のドア：通ってきたプレイヤーが部屋へ入り、ドアに背を向けたら、音を立てて閉めて鍵をかけている
    Door* startDoor = m_Objects.startDoor;
    if (startDoor != nullptr && startDoor->IsOpen())
    {
        const bool walkedIntoRoom = player.GetPosition().z > Stage1StartDoorZ + StartDoorCloseWalkIn;
        const bool facingAway = game->GetCamera()->GetForward().z > StartDoorCloseFacingZ;
        if (walkedIntoRoom && facingAway)
        {
            startDoor->ResetClosed(0);
            startDoor->SetLocked(true);
            game->PlayAudioCueAt(SOUND_CUE_DOOR, startDoor->GetPosition());
        }
    }

    if (startDoor != nullptr)
    {
        // 既存の遮蔽判定を使い、壁越しにライトを向けただけでは閉じない。
        const bool lit = IsLitByFlashlight(player, startDoor->GetPosition());
        startDoor->SetHauntedGap(m_CorridorLoopCount >= StartDoorHauntedGapLoop && !game->IsPowerRestored(), lit);
    }
    if (game->IsPowerRestored() || m_LoopCooldown > 0.0f)
    {
        return;
    }

    // 2周目以降：開始地点の奥のドアが閉まった後、部屋にいる間だけ、ドアの向こうから誰かが叩く（2〜4回続けて叩き、間を空けてまた叩く）。
    // 3周目からは、叩く合間に取っ手がガタガタ揺れる（通ってきたドアの向こうに、何かがついて来ている）。
    if (m_CorridorLoopCount >= StartDoorKnockLoop && startDoor != nullptr && startDoor->IsLocked())
    {
        const Vector3 position = player.GetPosition();
        const bool inStartRoom = position.z < StartRoomMaxZ && std::abs(position.x) < StartRoomHalfWidth;
        if (!inStartRoom)
        {
            // 部屋の外では鳴らさず、戻ってきてから少し間を置いて鳴らしている
            m_StartDoorKnocksLeft = 0;
            m_StartDoorKnockTimer = (std::max)(m_StartDoorKnockTimer, StartDoorKnockReturnDelay);
        }
        else if ((m_StartDoorKnockTimer -= deltaTime) <= 0.0f)
        {
            if (m_StartDoorKnocksLeft <= 0)
            {
                ++m_StartDoorKnockEvents;
                const bool rattle = m_CorridorLoopCount >= StartDoorRattleLoop && (m_StartDoorKnockEvents % 2) == 0;
                if (rattle)
                {
                    // 鍵のかかったドアを調べたときと同じく、取っ手が揺れて音が鳴り、画面がわずかに脈打つ
                    startDoor->Interact(player);
                    m_StartDoorKnockTimer = StartDoorKnockPauseBase + std::fmod(static_cast<float>(m_StartDoorKnockEvents) * StartDoorKnockPauseStep, StartDoorKnockPauseRange);
                }
                else
                {
                    m_StartDoorKnocksLeft = StartDoorKnockMinCount + m_StartDoorKnockEvents % StartDoorKnockCountCycle;
                }
            }
            if (m_StartDoorKnocksLeft > 0)
            {
                // 拳で木の扉を叩くような鈍い音。続けて叩く間は短く、叩き終えたら長く間を空けている
                --m_StartDoorKnocksLeft;
                game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, startDoor->GetPosition(),
                    StartDoorKnockBasePitch + static_cast<float>(m_StartDoorKnocksLeft) * StartDoorKnockPitchStep,
                    StartDoorKnockVolume);
                m_StartDoorKnockTimer = m_StartDoorKnocksLeft > 0
                    ? StartDoorKnockBurstInterval
                    : StartDoorKnockPauseBase + std::fmod(static_cast<float>(m_StartDoorKnockEvents) * StartDoorKnockPauseStep, StartDoorKnockPauseRange);
            }
        }
    }

    Door* endDoor = m_Objects.exitDoor;
    if (endDoor != nullptr && endDoor->IsOpening())
    {
        AdvanceCorridorLoop(player);
    }
}

// 突き当たりの最後のドアが開いていて、プレイヤーがドアの位置を越えて向こう側へ出たか
bool StageScene::IsThroughEndDoor(const Vector3& position) const
{
    const Door* endDoor = m_Objects.exitDoor;
    if (endDoor == nullptr || !endDoor->IsOpen())
    {
        return false;
    }
    const Vector3 doorPosition = endDoor->GetPosition();
    return position.x > doorPosition.x &&
        std::abs(position.z - doorPosition.z) < Stage1EndDoorHalfWidth + EndDoorPassMargin;
}

// ループ廊下を1周進めている：開始地点の奥のドアの裏側へ移し、周回の数に応じて印・扉・照明・ヒューズ・影を変えている
void StageScene::AdvanceCorridorLoop(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    // 最後のドアは、前の周回の開き方で開き始めている。奥のドアも同じ開き方で開けて、続きに見せている。
    const int openingPhase = (std::min)(m_CorridorLoopCount, MaxCorridorLoopPhase);
    ++m_CorridorLoopCount;

    // 最後のドアに対する立ち位置と向きを、そのまま奥のドアの裏側へ写している。
    // 最後のドアを通る向き（+x）が奥のドアを通る向き（+z）になるよう、水平に90度回している（(x, z) → (-z, x)）。
    // 同じフレームの後半でPlayerがカメラを更新するため、移った瞬間も1フレームもずれない。
    const Vector3 position = player.GetPosition();
    const Vector3 exitPosition = m_Objects.exitDoor->GetPosition();
    const float alongDoor = position.x - exitPosition.x;
    const float acrossDoor = position.z - exitPosition.z;
    player.SetPosition(Vector3(
        Stage1StartDoorX - acrossDoor, position.y, Stage1StartDoorZ + alongDoor));
    Camera* camera = game->GetCamera();
    camera->SetCameraDirection(camera->GetCameraDirection() - DirectX::XM_PIDIV2);

    // 奥のドアを開け始めている。開いた向こうに開始地点が見える。
    Door* startDoor = m_Objects.startDoor;
    if (startDoor != nullptr)
    {
        startDoor->ResetClosed(openingPhase);
        startDoor->StartOpeningQuietly();
        game->PlayAudioCueAt(SOUND_CUE_DOOR, startDoor->GetPosition());
    }
    m_LoopCooldown = CorridorLoopCooldownSeconds;
    m_ProgressHintTimer = 0.0f;
    m_LoopNoticeTimer = CorridorLoopNoticeSeconds;
    // 周回が進むほど、振り返った廊下が大きく伸びる
    m_CorridorStretch.SetLoopCount(m_CorridorLoopCount);
    // 2周目からは、戻った開始地点の部屋が、音で輪郭が浮かぶ真っ暗な部屋になる（一度だけ）
    m_EchoRoom.ArmOnLoopArrival(m_CorridorLoopCount);
    // 1周目から、右の倉庫（時間が止まった部屋）の天井から水が滴り、床に水たまりが広がっていく
    // （時間が止まると、落ちる途中の水滴と、広がる波が宙で止まって見える）
    if (m_CorridorLoopCount == 1 && m_Objects.ground != nullptr)
    {
        m_Objects.ground->AddLeak(TimeStopLeakCenter, TimeStopLeakRadius, TimeStopLeakGrowSeconds);
    }

    const int loopPhase = m_CorridorLoopCount < MaxCorridorLoopPhase
        ? m_CorridorLoopCount
        : MaxCorridorLoopPhase;

    UpdateCorridorLoopMarks(loopPhase);

    // 開始地点へ戻すときに、廊下の扉も閉じた状態に戻している。同じ境目をもう一度開けさせることで、
    // 各周回が意図された繰り返しとして感じられるようにしている。
    Door* loopDoor = m_Objects.loopDoor;
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(loopPhase);
    }
    // 最後のドアは閉じた状態に戻し、周回に合わせた開き方に変えている（次の周回でもう一度開けさせる）
    Door* endDoor = m_Objects.exitDoor;
    if (endDoor != nullptr)
    {
        endDoor->ResetClosed(loopPhase);
    }

    ApplyCorridorLoopChanges(loopPhase);
}

// 開始地点の部屋の壁の文字と、周回の数だけの壁の赤い印を出し、周回が進むほど強い演出をしている
void StageScene::UpdateCorridorLoopMarks(int loopPhase)
{
    Core::Game* game = Core::Game::GetInstance();
    // 開始地点の部屋に、この周回の壁の文字をじわりと浮かばせている（戻ってくるたびに、部屋の文字が1つずつ増えていく）
    if (m_CorridorLoopCount >= 1 && m_CorridorLoopCount <= static_cast<int>(m_Objects.lapWritings.size()))
    {
        FlashlightWriting* lapWriting = m_Objects.lapWritings[static_cast<std::size_t>(m_CorridorLoopCount - 1)];
        if (lapWriting != nullptr)
        {
            lapWriting->SetTargetPresence(1.0f);
        }
    }

    // 周回の数だけ、壁の赤い印を出している
    for (int markerIndex = 1; markerIndex <= MaxCorridorLoopPhase; ++markerIndex)
    {
        Wall* marker = m_Objects.loopMarkers[static_cast<std::size_t>(markerIndex - 1)];
        if (marker != nullptr)
        {
            marker->SetVisible(markerIndex <= loopPhase);
            const float intensity =
                LoopMarkerGlowBase + static_cast<float>(markerIndex) * LoopMarkerGlowStep;
            marker->SetAppearance(
                LoopMarkerDiffuse,
                Color(intensity, LoopMarkerGlowGreen, LoopMarkerGlowBlue, 1.0f),
                LoopMarkerShininess);
        }
    }
    const float phase = static_cast<float>(loopPhase);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), PulseSpec{
        CorridorLoopPulseBase.Strength + phase * CorridorLoopPulseStep.Strength,
        CorridorLoopPulseBase.Seconds + phase * CorridorLoopPulseStep.Seconds });
    EventFeedback::Rumble(RumbleSpec{
        CorridorLoopRumbleBase.Frames + loopPhase * CorridorLoopRumbleStep.Frames,
        CorridorLoopRumbleBase.Strength + loopPhase * CorridorLoopRumbleStep.Strength });
}

// 周回ごとに廊下を変えている（1周目：2本目のヒューズ、2周目：西棟と影、3周目以降：照明の配置と背後の人影）
void StageScene::ApplyCorridorLoopChanges(int loopPhase)
{
    Core::Game* game = Core::Game::GetInstance();
    CeilingLight* entranceLight =
        m_Objects.CeilingLightAt(Stage1LightNumber::StartRoom);
    CeilingLight* middleLight =
        m_Objects.CeilingLightAt(Stage1LightNumber::MiddleCorridor);
    CeilingLight* cornerLight =
        m_Objects.CeilingLightAt(Stage1LightNumber::NorthCorner);

    // 1周目：左の倉庫に2本目のヒューズを出している
    if (loopPhase == 1)
    {
        Item* secondFuse = m_Objects.secondFuse;
        if (secondFuse != nullptr && !secondFuse->IsCollected())
        {
            secondFuse->SetActive(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(false, MiddleLoop1EmergencyOffset);
        }
    }
    else if (loopPhase == Stage1FuseProgress::LoopsToUnlockLastFuse)
    {
        // 2周目：3本目のヒューズは西棟の奥にある。右の倉庫に西棟の鍵を出し、鍵で西棟の扉を開けさせている。
        // ヒューズは、西棟の排水バルブで水を抜いたときにStage1WestWingが出している。
        m_WestWing.Activate();

        // 開始地点の部屋の照明が、この周回から故障したように不規則に消えるようになる
        if (entranceLight != nullptr)
        {
            entranceLight->SetFaulted(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, MiddleLoop2EmergencyOffset);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(true, CornerLoop2EmergencyOffset);
        }

        // 中央の廊下に影を出している
        game->RequestAddObject<ShadowMan>(
            [](ShadowMan& shadow)
            {
                shadow.SetPosition(CorridorShadowPosition.x, CorridorShadowPosition.y, CorridorShadowPosition.z);
            });
    }
    // 3周目以降：照明の配置を変えている
    else
    {
        if (entranceLight != nullptr)
        {
            entranceLight->SetEmergencyLight(false, 0.0f);
        }
        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, MiddleIdleEmergencyOffset);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(false, CornerIdleEmergencyOffset);
        }

        // 3周目だけ、壁の文字の警告どおり、プレイヤーの背後に一度だけ人影を出している（見ると照明の連鎖が始まる）。
        if (m_CorridorLoopCount == BehindShadowLoop)
        {
            game->RequestAddObject<ShadowMan>(
                [this](ShadowMan& shadow)
                {
                    shadow.SetPosition(Stage1StartDoorX, FootY, Stage1StartDoorZ - StartDoorShadowBehind);
                    shadow.EnableGazeScare();
                    shadow.SetOnObserved(
                        [this]()
                        {
                            StartScareLightSequence();
                        });
                });
        }
    }
}

void StageScene::UpdateCorridorStretch(const Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    Camera* camera = game->GetCamera();
    const Vector3 position = player.GetPosition();
    if (m_CorridorStretch.Update(deltaTime, position, camera->GetForward(), IsAmbientSoundAllowed()))
    {
        game->PlayAudioCueAt(SOUND_CUE_DOOR, position - Vector3(0.0f, 0.0f, CorridorStretchSoundDistance),
            CorridorStretchSound.Pitch, CorridorStretchSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), CorridorStretchPulse);
        EventFeedback::Rumble(CorridorStretchRumble);
    }
    camera->SetWorldStretchZ(m_CorridorStretch.GetStretchZ());
}

namespace
{
}


bool StageScene::IsCorridorFloodReady() const
{
    return m_WestWing.GetStep() >= Stage1WestWing::Step::FindFuse ||
        Core::Game::GetInstance()->GetItemCount() >= Core::GameState::RequiredFuseCount;
}

void StageScene::UpdateCorridorFlood(Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    const ExitTrigger* exit = m_Objects.exitTrigger;
    // 物音用の条件には、通過中にも残り続ける影や演出が含まれる。濁流は操作を妨げる場面だけ待たせる。
    const bool canStart = player.CanControl() && !m_Surveillance.IsViewing() &&
        !m_Surveillance.IsCaughtActive() && !m_HiddenRoom.IsTrapped() && !m_CeilingStalker.IsActive() &&
        !m_TimeStopRoom.IsActive() && !m_EchoRoom.IsDark() && (exit == nullptr || !exit->IsEscaping());
    m_CorridorFloodCues.clear();
    m_CorridorFlood.Update(deltaTime, player.GetPosition(), IsCorridorFloodReady(), canStart, m_CorridorFloodCues);
    for (const Stage1CorridorFlood::Cue& cue : m_CorridorFloodCues)
    {
        switch (cue.Type)
        {
        case Stage1CorridorFlood::CueType::LightsOut:
            // 廊下の照明を一斉に消している（すでに消えている照明は、戻すときにも触らない）
            m_CorridorFloodLights.clear();
            for (CeilingLight* light : game->GetObjects<CeilingLight>())
            {
                const Vector3 position = light->GetPosition();
                if (light->IsForcedOff() ||
                    position.x < Stage1CorridorFlood::CorridorMinX - CorridorFloodLightMargin ||
                    position.x > Stage1CorridorFlood::CorridorMaxX + CorridorFloodLightMargin ||
                    position.z < Stage1CorridorFlood::CorridorEntranceZ - CorridorFloodLightMargin ||
                    position.z > Stage1CorridorFlood::CorridorEndZ + CorridorFloodLightMargin)
                {
                    continue;
                }
                light->SetForcedOff(true);
                m_CorridorFloodLights.push_back(light);
            }
            game->PlayAudioCue(SOUND_CUE_POWER, CorridorFloodLightsOutSound.Pitch, CorridorFloodLightsOutSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), CorridorFloodLightsOutPulse);
            EventFeedback::Rumble(CorridorFloodLightsOutRumble);
            break;
        case Stage1CorridorFlood::CueType::Rush:
            game->PlayAudioCueAt(SOUND_CUE_DRAIN, cue.Position, CorridorFloodRushSound.Pitch,
                CorridorFloodRushSound.Volume * cue.Strength);
            EventFeedback::Rumble(CorridorFloodRushRumble);
            break;
        case Stage1CorridorFlood::CueType::Drip:
            // 天井から水滴が落ちる：天井の高さから水音を鳴らし、床に水があれば小さな波を立てる
            game->PlayAudioCueAt(SOUND_CUE_WATER_DROP,
                Vector3(cue.Position.x, CorridorFloodDripSoundY, cue.Position.z), CorridorFloodDripSound.Pitch,
                CorridorFloodDripSound.Volume * (1.0f + cue.Strength * CorridorFloodDripVolumeGain));
            if (m_Objects.ground != nullptr)
            {
                m_Objects.ground->AddRippleImpulse(cue.Position, CorridorFloodDripRippleRadius, CorridorFloodDripRippleStrength);
            }
            break;
        case Stage1CorridorFlood::CueType::Caught:
            game->GetPostProcess()->TriggerLensMoisture(CorridorFloodImpactMoisture, CorridorFloodImpactMoistureSeconds);
            game->PlayAudioCueAt(SOUND_CUE_WATER_RUN, cue.Position,
                CorridorFloodCaughtSplash.Pitch, CorridorFloodCaughtSplash.Volume);
            game->PlayAudioCue(SOUND_CUE_SCARE, CorridorFloodCaughtScare.Pitch, CorridorFloodCaughtScare.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), CorridorFloodCaughtPulse);
            EventFeedback::Rumble(CorridorFloodCaughtRumble);
            break;
        case Stage1CorridorFlood::CueType::Restart:
        {
            // その周回をやり直す：開始地点の部屋へ戻している（周回の数はそのまま）
            const Vector3 position = player.GetPosition();
            player.SetPosition(Vector3(CorridorFloodRestartX, position.y, CorridorFloodRestartZ));
            game->GetCamera()->SetCameraDirection(CorridorFloodRestartYaw);
            break;
        }
        case Stage1CorridorFlood::CueType::LightsOn:
            for (CeilingLight* light : m_CorridorFloodLights)
            {
                light->SetForcedOff(false);
            }
            m_CorridorFloodLights.clear();
            break;
        }
    }

    // 地鳴り：水が近づくほど、視界が細かく揺れ、コントローラーが震える（傾く部屋の傾きに足している）
    if (const float tremor = m_CorridorFlood.GetTremor(); tremor > 0.0f)
    {
        const float shake = std::sin(m_StageVisualTimer * CorridorFloodTremorRateA) +
            std::sin(m_StageVisualTimer * CorridorFloodTremorRateB) * 0.5f;
        game->GetCamera()->SetViewRoll(m_TiltRoom.GetRoll() + shake * CorridorFloodTremorRoll * tremor);
        m_CorridorFloodRumbleTimer -= deltaTime;
        if (m_CorridorFloodRumbleTimer <= 0.0f)
        {
            m_CorridorFloodRumbleTimer += CorridorFloodTremorRumbleInterval;
            EventFeedback::Rumble({ CorridorFloodTremorRumbleFrames, CorridorFloodTremorRumbleStrength * tremor });
        }
    }

    // 追いつかれたら、画面の下から赤い水面が上がってきて、視界が水に呑まれる
    if (const float submerge = m_CorridorFlood.GetSubmerge(); submerge > 0.0f)
    {
        game->GetPostProcess()->SetSubmerge(submerge);
    }

    UpdateFloodWallStain(deltaTime);

    // 床の浸水範囲を使い回す。暗証番号ルートで西棟を排水していない場合も、濁流後は元の水位に戻す。
    Ground* ground = m_Objects.ground;
    if (ground == nullptr)
    {
        return;
    }
    const float shallowFrontZ = m_CorridorFlood.GetShallowFrontZ();
    if (shallowFrontZ < Stage1CorridorFlood::CorridorEndZ)
    {
        ground->SetFloodRegion(
            Vector2(Stage1CorridorFlood::CorridorMinX, shallowFrontZ),
            Vector2(Stage1CorridorFlood::CorridorMaxX, Stage1CorridorFlood::CorridorEndZ));
        m_CorridorFloodOwnsWater = true;
    }
    else if (m_CorridorFloodOwnsWater)
    {
        m_WestWing.ApplyFloodRegion();
        m_CorridorFloodOwnsWater = false;
    }
    const float redness = m_CorridorFlood.GetRedness();
    ground->SetFloodRedness(m_CorridorFloodOwnsWater ? redness : 0.0f);
    ground->SetCorridorFloodInlet(Vector4(m_CorridorFlood.GetInletWaterMinX(), Stage1CorridorFlood::InletMinZ,
        m_CorridorFlood.GetInletWaterMaxX(), Stage1CorridorFlood::InletMaxZ), redness);
    const auto front = m_CorridorFlood.GetWaveFront();
    ground->SetCorridorFloodFront(front.Distance, front.Strength);
}


// 濁流が通った壁の濡れた跡：水が届いた一番手前の z を覚え、水がある間は濃くし、水が引いた後はゆっくり乾かしている
void StageScene::UpdateFloodWallStain(float deltaTime)
{
    const float frontZ = m_CorridorFlood.GetShallowFrontZ();
    const bool water = frontZ < Stage1CorridorFlood::CorridorEndZ;
    if (water)
    {
        m_FloodStainMinZ = (std::min)(m_FloodStainMinZ, frontZ);
        m_FloodStainRedness = (std::max)(m_FloodStainRedness, m_CorridorFlood.GetRedness());
        m_FloodStain = (std::min)(1.0f, m_FloodStain + deltaTime * CorridorFloodStainRiseRate);
    }
    else
    {
        m_FloodStain = (std::max)(0.0f, m_FloodStain - deltaTime / CorridorFloodStainDrySeconds);
    }
    Renderer::SetFloodWallStain(Vector4(m_FloodStainMinZ, CorridorFloodStainHeight, m_FloodStainRedness, m_FloodStain));
}
