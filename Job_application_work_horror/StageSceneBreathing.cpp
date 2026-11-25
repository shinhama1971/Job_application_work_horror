// ============================================================================
// ファイルの役割: 1面の呼吸する壁：壁の面を格子にし、扉・照明・小物を一番近い壁に取り付け、組ごとの呼吸の強さを進めている。
// （StageSceneLoop.cpp から分けた。説明は元のファイルとヘッダーを参照）
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
    EnableBreathingWallFaces();

    Core::Game& game = *Core::Game::GetInstance();
    for (const std::string& name : m_Objects.objectNames)
    {
        Door* door = game.GetObj<Door>(name);
        if (door == nullptr)
        {
            continue;
        }
        // 扉は蝶番の位置で、取り付ける壁を選ぶ
        const Vector3 angles = door->GetRotation();
        const Matrix rotation = Matrix::CreateFromYawPitchRoll(angles.y, angles.x, angles.z);
        const Vector3 fromHinge(std::abs(door->GetScale().x) * 0.5f, 0.0f, 0.0f);
        if (!AttachBreathingFixture(*door, door->GetPosition() - Vector3::TransformNormal(fromHinge, rotation)))
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
        AttachBreathingFixture(*light, light->GetPosition());
    }
    for (FuseBox* panel : game.GetObjects<FuseBox>())
    {
        AttachBreathingFixture(*panel, panel->GetPosition());
    }
    for (FlashlightWriting* writing : game.GetObjects<FlashlightWriting>())
    {
        AttachBreathingFixture(*writing, writing->GetPosition());
    }
    for (const auto& [name, prop] : m_Objects.layoutProps)
    {
        if (prop != nullptr && !prop->GetVisualModel().empty())
        {
            AttachBreathingFixture(*prop, prop->GetPosition());
        }
    }
}

void StageScene::EnableBreathingWallFaces()
{
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
}

// 物の位置を動く前に記録する。出現のたびに変位を足して位置がずれないようにする。
bool StageScene::AttachBreathingFixture(Object& object, const Vector3& anchor)
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
    // 呼吸は世界の時計で進めている（時間が止まった部屋にいる間は、壁も息を止める）
    m_BreathingWallTime += Application::GetWorldDeltaTime();

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
