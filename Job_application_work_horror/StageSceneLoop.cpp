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
    return endDoor != nullptr &&
        endDoor->IsOpen() &&
        position.x > Stage1EndDoorX &&
        std::abs(position.z - Stage1EndDoorZ) < Stage1EndDoorHalfWidth + EndDoorPassMargin;
}

// ループ廊下を1周進めている：開始地点の奥のドアの裏側へ移し、周回の数に応じて印・扉・照明・ヒューズ・影を変えている
void StageScene::AdvanceCorridorLoop(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    // 最後のドアは、前の周回の開き方で開き始めている。奥のドアも同じ開き方で開けて、続きに見せている。
    const int openingPhase = (std::min)(m_CorridorLoopCount, MaxCorridorLoopPhase);
    ++m_CorridorLoopCount;

    if (m_CorridorLoopCount == 1 && m_Objects.facilityMap != nullptr)
    {
        m_Objects.facilityMap->ShowAlternate();
    }

    // 最後のドアに対する立ち位置と向きを、そのまま奥のドアの裏側へ写している。
    // 最後のドアを通る向き（+x）が奥のドアを通る向き（+z）になるよう、水平に90度回している（(x, z) → (-z, x)）。
    // 同じフレームの後半でPlayerがカメラを更新するため、移った瞬間も1フレームもずれない。
    const Vector3 position = player.GetPosition();
    const float alongDoor = position.x - Stage1EndDoorX;
    const float acrossDoor = position.z - Stage1EndDoorZ;
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

void StageScene::SetupBreathingWall()
{
    m_BreathingWall = nullptr;
    for (const auto& [name, wall] : m_Objects.layoutProps)
    {
        if (name == BreathingWallName && wall != nullptr)
        {
            wall->EnableBreathing(BreathingWallPositiveZFace, BreathingWallColumns, BreathingWallRows, BreathingWallEdgeWidth);
            m_BreathingWall = wall;
            break;
        }
    }
}

void StageScene::UpdateBreathingWall(const Player& player, float deltaTime)
{
    if (m_BreathingWall == nullptr)
    {
        return;
    }
    // 1周するまでは普通の壁。周回が進むほど大きく膨らむ（急に動き出さないよう、少しずつ近づけている）
    const float target =
        m_CorridorLoopCount <= 0 ? 0.0f
        : m_CorridorLoopCount == 1 ? BreathingWallFirstLoopAmplitude
        : m_CorridorLoopCount == 2 ? BreathingWallSecondLoopAmplitude
        : BreathingWallLaterLoopAmplitude;
    const float step = BreathingWallAmplitudeRate * deltaTime;
    m_BreathingWallAmplitude = std::clamp(target, m_BreathingWallAmplitude - step, m_BreathingWallAmplitude + step);
    m_BreathingWallTime += deltaTime;
    m_BreathingWall->SetBreathing(m_BreathingWallTime, m_BreathingWallAmplitude);

    // 息を吐き始める所（呼吸の1回の始まり）ごとに、壁の中から息の音を1回鳴らしている
    const int cycle = static_cast<int>(m_BreathingWallTime / SIGNAL_LOST_BREATHING_WALL_CYCLE_SECONDS);
    const bool newCycle = cycle != m_BreathingWallCycle;
    m_BreathingWallCycle = cycle;
    if (!newCycle || m_BreathingWallAmplitude <= 0.0f || !IsAmbientSoundAllowed())
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    const Vector3 playerPosition = player.GetPosition();
    const Vector3 soundPosition(
        std::clamp(playerPosition.x, BreathingWallSoundMinX, BreathingWallSoundMaxX),
        BreathingWallSoundY, BreathingWallSoundZ);
    Vector3 toSound = soundPosition - game->GetCamera()->GetPosition();
    const float distance = toSound.Length();
    if (distance > BreathingWallSoundDistance)
    {
        return;
    }
    toSound /= (std::max)(distance, 0.001f);
    const bool lit = player.IsFlashlightOn() && game->GetCamera()->GetForward().Dot(toSound) > BreathingWallLitFacing;
    if (!lit)
    {
        game->PlayAudioCueAt(SOUND_CUE_BREATH, soundPosition, BreathingWallSound.Pitch, BreathingWallSound.Volume);
    }
}
