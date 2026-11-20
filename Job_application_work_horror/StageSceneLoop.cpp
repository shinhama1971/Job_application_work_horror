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
    Vector3 FixtureHalfSize(const Vector3& scale, const Matrix& rotation)
    {
        const Vector3 half(std::abs(scale.x) * 0.5f, std::abs(scale.y) * 0.5f, std::abs(scale.z) * 0.5f);
        return Vector3(
            std::abs(rotation._11) * half.x + std::abs(rotation._21) * half.y + std::abs(rotation._31) * half.z,
            std::abs(rotation._12) * half.x + std::abs(rotation._22) * half.y + std::abs(rotation._32) * half.z,
            std::abs(rotation._13) * half.x + std::abs(rotation._23) * half.y + std::abs(rotation._33) * half.z);
    }

}

void StageScene::SetupBreathingWall()
{
    m_BreathingWalls.clear();
    m_BreathingFixtures.clear();
    for (const auto& [name, wall] : m_Objects.layoutProps)
    {
        if (wall == nullptr)
        {
            continue;
        }
        for (const BreathingWallFace& face : BreathingWallFaces)
        {
            if (name != face.Name)
            {
                continue;
            }
            const Vector3 scale = wall->GetScale();
            const float width = std::abs(face.Normal.z != 0.0f ? scale.x : scale.z);
            const int columns = static_cast<int>(std::ceil(width / BreathingWallColumnWidth));
            wall->EnableBreathing(face.Normal, columns, BreathingWallRows, BreathingWallEdgeWidth);
            m_BreathingWalls.push_back({ wall, face.Normal, face.Group });
            break;
        }
    }
    // 物の位置を動く前に記録する。出現のたびに変位を足して位置がずれないようにする。
    const auto attach = [this](Object& object, const Vector3& anchor) -> bool
    {
        float nearest = (std::numeric_limits<float>::max)();
        Wall* selected = nullptr;
        Vector3 selectedNormal = Vector3::Zero;
        Vector2 sample = Vector2::Zero;
        const Vector3 center = object.GetPosition();
        const Vector3 angles = object.GetRotation();
        const Matrix rotation = Matrix::CreateFromYawPitchRoll(angles.y, angles.x, angles.z);
        const Vector3 half = FixtureHalfSize(object.GetScale(), rotation);
        for (const auto& [wall, normal, group] : m_BreathingWalls)
        {
            const Vector3 wallCenter = wall->GetPosition();
            const Vector3 size = wall->GetScale();
            const bool alongX = normal.z != 0.0f;
            const float plane = (wallCenter + normal * (std::abs(alongX ? size.z : size.x) * 0.5f)).Dot(normal);
            const float faceGap = center.Dot(normal) - plane;
            const float halfDepth = alongX ? half.z : half.x;
            if (faceGap + halfDepth < -BreathingFixtureBackTolerance ||
                faceGap - halfDepth > BreathingFixtureAttachReach)
            {
                continue;
            }

            const float wallAlong = alongX ? wallCenter.x : wallCenter.z;
            const float halfLength = std::abs(alongX ? size.x : size.z) * 0.5f;
            const float halfHeight = std::abs(size.y) * 0.5f;
            const float centerAlong = alongX ? center.x : center.z;
            const float objectHalfLength = alongX ? half.x : half.z;
            if (std::abs(centerAlong - wallAlong) > halfLength + objectHalfLength + BreathingFixtureBackTolerance ||
                std::abs(center.y - wallCenter.y) > halfHeight + half.y + BreathingFixtureBackTolerance)
            {
                continue;
            }

            // 扉は蝶番の位置、照明・小物は取り付けの中心に近い面を選ぶ。
            const float anchorAlong = alongX ? anchor.x : anchor.z;
            const float closestAlong = std::clamp(anchorAlong, wallAlong - halfLength, wallAlong + halfLength);
            const float closestY = std::clamp(anchor.y, wallCenter.y - halfHeight, wallCenter.y + halfHeight);
            const Vector3 closest = alongX ? Vector3(closestAlong, closestY, plane / normal.z)
                : Vector3(plane / normal.x, closestY, closestAlong);
            const float distance = Vector3::DistanceSquared(anchor, closest);
            if (distance >= nearest)
            {
                continue;
            }
            nearest = distance;
            selected = wall;
            selectedNormal = normal;
            const float insetAlong = (std::min)(BreathingFixtureAnchorInset, halfLength * BreathingFixtureInsetLimit);
            const float insetY = (std::min)(BreathingFixtureAnchorInset, halfHeight * BreathingFixtureInsetLimit);
            sample.x = std::clamp(anchorAlong, wallAlong - halfLength + insetAlong, wallAlong + halfLength - insetAlong);
            sample.y = std::clamp(anchor.y, wallCenter.y - halfHeight + insetY, wallCenter.y + halfHeight - insetY);
        }

        if (selected == nullptr)
        {
            return false;
        }
        m_BreathingFixtures.push_back({ &object, selected, center, selectedNormal, sample });
        return true;
    };

    Core::Game& game = *Core::Game::GetInstance();
    for (const std::string& name : m_Objects.objectNames)
    {
        Door* door = game.GetObj<Door>(name);
        if (door == nullptr)
        {
            continue;
        }
        const Vector3 angles = door->GetRotation();
        const Matrix rotation = Matrix::CreateFromYawPitchRoll(angles.y, angles.x, angles.z);
        const Vector3 fromHinge(std::abs(door->GetScale().x) * 0.5f, 0.0f, 0.0f);
        if (!attach(*door, door->GetPosition() - Vector3::TransformNormal(fromHinge, rotation)))
        {
            continue;
        }
        if (Wall* lintel = game.GetObj<Wall>(name + "Lintel"))
        {
            BreathingFixture fixture = m_BreathingFixtures.back();
            fixture.Fixture = lintel;
            fixture.BasePosition = lintel->GetPosition();
            m_BreathingFixtures.push_back(fixture);
        }
    }
    for (CeilingLight* light : game.GetObjects<CeilingLight>())
    {
        attach(*light, light->GetPosition());
    }
    for (FuseBox* panel : game.GetObjects<FuseBox>())
    {
        attach(*panel, panel->GetPosition());
    }
    for (FlashlightWriting* writing : game.GetObjects<FlashlightWriting>())
    {
        attach(*writing, writing->GetPosition());
    }
    for (const auto& [name, prop] : m_Objects.layoutProps)
    {
        if (prop != nullptr && !prop->GetVisualModel().empty())
        {
            attach(*prop, prop->GetPosition());
        }
    }
}

void StageScene::UpdateBreathingFixtures(bool restore)
{
    for (const auto& fixture : m_BreathingFixtures)
    {
        const float offset = restore ? 0.0f : fixture.Surface->SampleBreathingFace(fixture.Sample.x, fixture.Sample.y);
        const Vector3 position = fixture.BasePosition + fixture.Normal * offset;
        if (auto* door = dynamic_cast<Door*>(fixture.Fixture))
        {
            // 蝶番と当たり判定が使う閉じた位置も、一緒に動かす。
            door->SetPosition(position.x, position.y, position.z);
        }
        else
        {
            fixture.Fixture->SetPosition(position);
        }
    }
}


void StageScene::UpdateCorridorFlood(Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    const bool drained = m_WestWing.GetStep() >= Stage1WestWing::Step::FindFuse;
    m_CorridorFloodCues.clear();
    m_CorridorFlood.Update(deltaTime, player.GetPosition(), drained, IsAmbientSoundAllowed(), m_CorridorFloodCues);
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
            game->PlayAudioCueAt(SOUND_CUE_DRAIN, cue.Position, CorridorFloodRushSound.Pitch, CorridorFloodRushSound.Volume);
            EventFeedback::Rumble(CorridorFloodRushRumble);
            break;
        case Stage1CorridorFlood::CueType::Caught:
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

    // 追いつかれたら、画面の下から赤い水面が上がってきて、視界が水に呑まれる
    if (const float submerge = m_CorridorFlood.GetSubmerge(); submerge > 0.0f)
    {
        game->GetPostProcess()->SetSubmerge(submerge);
    }

    // 床の浸水範囲は、西棟の水を抜いた後は使っていないので、水がある間だけこの演出が使っている
    Ground* ground = m_Objects.ground;
    if (ground == nullptr)
    {
        return;
    }
    if (m_CorridorFlood.HasWater())
    {
        ground->SetFloodRegion(
            Vector2(Stage1CorridorFlood::CorridorMinX, m_CorridorFlood.GetFrontZ()),
            Vector2(Stage1CorridorFlood::CorridorMaxX, Stage1CorridorFlood::CorridorEndZ));
        m_CorridorFloodOwnsWater = true;
    }
    else if (m_CorridorFloodOwnsWater)
    {
        // 水が引き切ったら、広さ0の範囲にして水をなくしている
        ground->SetFloodRegion(
            Vector2(Stage1CorridorFlood::CorridorMinX, Stage1CorridorFlood::CorridorEndZ),
            Vector2(Stage1CorridorFlood::CorridorMaxX, Stage1CorridorFlood::CorridorEndZ));
        m_CorridorFloodOwnsWater = false;
    }
    ground->SetFloodRedness(m_CorridorFlood.GetRedness());
}

float StageScene::GetBreathingGroupTarget(Stage1BreathingGroup group) const
{
    // 3本目のヒューズを拾った後は、建物全体（どの組も）が大きく息をしている
    const bool allFuses = Core::Game::GetInstance()->GetItemCount() >= Core::GameState::RequiredFuseCount;
    const bool thirdLoop = m_CorridorLoopCount >= BreathingWallWholeFaceCompletedLoops;
    switch (group)
    {
    case Stage1BreathingGroup::Corridor:
        if (m_CorridorLoopCount <= 0) return 0.0f;
        if (allFuses) return BreathingBuildingAmplitude;
        if (m_CorridorLoopCount == 1) return BreathingWallFirstLoopAmplitude;
        return m_CorridorLoopCount > BreathingWallWholeFaceCompletedLoops
            ? BreathingWallLaterLoopAmplitude : BreathingWallSecondLoopAmplitude;
    case Stage1BreathingGroup::Hall:
        if (allFuses) return BreathingBuildingAmplitude;
        return thirdLoop ? BreathingHallAmplitude : 0.0f;
    case Stage1BreathingGroup::Building:
        return allFuses ? BreathingBuildingAmplitude : 0.0f;
    case Stage1BreathingGroup::Count:
        break;
    }
    return 0.0f;
}

void StageScene::UpdateBreathingWall(float deltaTime)
{
    if (m_BreathingWalls.empty())
    {
        return;
    }

    // 組ごとの膨らむ量を、目標へ少しずつ近づけている（急に動き出さないように）
    const float step = BreathingWallAmplitudeRate * deltaTime;
    for (std::size_t index = 0; index < Stage1BreathingGroupCount; ++index)
    {
        float& amplitude = m_BreathingGroupAmplitudes[index];
        const float target = GetBreathingGroupTarget(static_cast<Stage1BreathingGroup>(index));
        amplitude = std::clamp(target, amplitude - step, amplitude + step);
    }
    m_BreathingWallTime += deltaTime;

    // 3周目からは面全体が動く。廊下以外の組（中央ホール・外周）は、動き始めから面全体で息をする。
    // 懐中電灯で照らしても止めない（どこを照らしても、壁は息を続けている）
    const bool thirdLoop = m_CorridorLoopCount >= BreathingWallWholeFaceCompletedLoops;
    float largestAmplitude = 0.0f;
    for (const auto& [wall, normal, group] : m_BreathingWalls)
    {
        const float amplitude = m_BreathingGroupAmplitudes[static_cast<std::size_t>(group)];
        largestAmplitude = (std::max)(largestAmplitude, amplitude);
        wall->SetBreathing(m_BreathingWallTime, amplitude, thirdLoop || group != Stage1BreathingGroup::Corridor);
    }

    UpdateBreathingFixtures();

    const int cycle = static_cast<int>(m_BreathingWallTime / SIGNAL_LOST_BREATHING_WALL_CYCLE_SECONDS);
    const bool newCycle = cycle != m_BreathingWallCycle;
    m_BreathingWallCycle = cycle;
    if (!newCycle || largestAmplitude <= 0.0f || !IsAmbientSoundAllowed())
    {
        return;
    }

    // 両側で鳴ると位置が分からなくなるので、プレイヤーの側から聞ける一番近い、動いている面を1枚だけ選ぶ。
    Core::Game* game = Core::Game::GetInstance();
    const Vector3 eye = game->GetCamera()->GetPosition();
    Vector3 closestSound = Vector3::Zero;
    float closestDistance = BreathingWallSoundDistance;
    for (const auto& [wall, normal, group] : m_BreathingWalls)
    {
        if (m_BreathingGroupAmplitudes[static_cast<std::size_t>(group)] <= 0.0f)
        {
            continue;
        }
        const Vector3 center = wall->GetPosition();
        const Vector3 scale = wall->GetScale();
        const bool normalAlongZ = normal.z != 0.0f;
        const float halfDepth = std::abs(normalAlongZ ? scale.z : scale.x) * 0.5f;
        if ((eye - center).Dot(normal) < halfDepth)
        {
            continue;
        }
        Vector3 point = center + normal * (halfDepth + BreathingWallSoundFaceOffset);
        const float halfWidth = std::abs(normalAlongZ ? scale.x : scale.z) * 0.5f;
        const float centerAlong = normalAlongZ ? center.x : center.z;
        const float eyeAlong = normalAlongZ ? eye.x : eye.z;
        (normalAlongZ ? point.x : point.z) = std::clamp(eyeAlong, centerAlong - halfWidth, centerAlong + halfWidth);
        const float distance = (point - eye).Length();
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestSound = point;
        }
    }
    if (closestDistance < BreathingWallSoundDistance)
    {
        game->PlayAudioCueAt(SOUND_CUE_BREATH, closestSound, BreathingWallSound.Pitch, BreathingWallSound.Volume);
    }
}
