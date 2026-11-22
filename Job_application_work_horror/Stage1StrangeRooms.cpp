// ============================================================================
// ファイルの役割: 1面の「おかしな部屋」2つの進行。
// （説明はStage1StrangeRooms.hを参照）
// ============================================================================

#include "Stage1StrangeRooms.h"

#include "MathUtility.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // --- 時間が止まった部屋
    // 入ってから世界の時計が止まりきるまでの秒数（ゆっくり止まっていく）と、出てから戻りきるまでの秒数
    constexpr float FreezeSeconds = 1.4f;
    constexpr float ResumeSeconds = 0.35f;
    // 世界の時計がこれより遅くなったら、止まりきったとみなす
    constexpr float FrozenScale = 0.001f;
    // 止まりきってから人影を出すまでの秒数（静けさに気づく間）
    constexpr float FigureDelaySeconds = 2.0f;
    // 人影を出す所：部屋の4つの隅のうち、プレイヤーから一番遠い所を、壁からこの距離だけ内側へ
    constexpr float FigureCornerInset = 12.0f;
    // 人影の足もとの高さ（床 y=-100 のわずか上）
    constexpr float FigureFootY = -99.0f;

    // --- 傾く部屋
    // 入口（北の壁の中ほど）。ここからの距離が遠いほど傾く
    const Vector2 TiltEntrance(-160.0f, -184.0f);
    // 傾き始める距離と、一番大きく傾く距離、一番大きな傾き（約11度）
    constexpr float TiltStartDistance = 20.0f;
    constexpr float TiltFullDistance = 180.0f;
    constexpr float MaxTiltRadians = 0.19f;
    // 傾きが目標へ近づく速さ（1秒あたりのラジアン。急には傾かない）
    constexpr float TiltRate = 0.08f;
}

void Stage1TimeStopRoom::Reset()
{
    m_Inside = false;
    m_Broken = false;
    m_FigureShown = false;
    m_TimeScale = 1.0f;
    m_FrozenSeconds = 0.0f;
}

bool Stage1TimeStopRoom::IsInside(const Vector3& position)
{
    return position.x >= MinX && position.x <= MaxX && position.z >= MinZ && position.z <= MaxZ;
}

void Stage1TimeStopRoom::BreakSpell()
{
    m_Broken = true;
    m_TimeScale = 1.0f;
}

void Stage1TimeStopRoom::Update(float deltaTime, const Vector3& playerPosition, int loopCount, bool allowed,
    std::vector<Cue>& cues)
{
    const bool inside = IsInside(playerPosition);
    const bool frozenRoom = inside && loopCount >= 1 && !m_Broken;
    if (frozenRoom && !m_Inside)
    {
        cues.push_back({ CueType::Freeze, playerPosition });
    }
    else if (!frozenRoom && m_Inside && !m_Broken)
    {
        cues.push_back({ CueType::Resume, playerPosition });
    }
    m_Inside = frozenRoom;

    if (frozenRoom)
    {
        m_TimeScale = (std::max)(0.0f, m_TimeScale - deltaTime / FreezeSeconds);
    }
    else
    {
        m_TimeScale = (std::min)(1.0f, m_TimeScale + deltaTime / ResumeSeconds);
    }

    m_FrozenSeconds = frozenRoom && m_TimeScale <= FrozenScale ? m_FrozenSeconds + deltaTime : 0.0f;
    if (!m_FigureShown && allowed && m_FrozenSeconds >= FigureDelaySeconds)
    {
        // プレイヤーから一番遠い隅に出し、止まった部屋の中を、自分と人影だけが動いている
        const std::array<Vector3, 4> corners =
        {
            Vector3(MinX + FigureCornerInset, FigureFootY, MinZ + FigureCornerInset),
            Vector3(MaxX - FigureCornerInset, FigureFootY, MinZ + FigureCornerInset),
            Vector3(MinX + FigureCornerInset, FigureFootY, MaxZ - FigureCornerInset),
            Vector3(MaxX - FigureCornerInset, FigureFootY, MaxZ - FigureCornerInset),
        };
        Vector3 farthest = corners[0];
        float farthestDistance = 0.0f;
        for (const Vector3& corner : corners)
        {
            const float distance = Vector2(corner.x - playerPosition.x, corner.z - playerPosition.z).LengthSquared();
            if (distance > farthestDistance)
            {
                farthestDistance = distance;
                farthest = corner;
            }
        }
        m_FigureShown = true;
        cues.push_back({ CueType::SpawnFigure, farthest });
    }
}

void Stage1TiltRoom::Reset()
{
    m_Roll = 0.0f;
}

void Stage1TiltRoom::Update(float deltaTime, const Vector3& playerPosition, int loopCount, float calm)
{
    float target = 0.0f;
    const bool inside = playerPosition.x >= MinX && playerPosition.x <= MaxX &&
        playerPosition.z >= MinZ && playerPosition.z <= MaxZ;
    if (inside && loopCount >= 1)
    {
        // 入口から遠いほど傾き、暖かい灯りの下ではまっすぐに戻る
        const float distance = Vector2(playerPosition.x - TiltEntrance.x, playerPosition.z - TiltEntrance.y).Length();
        target = MaxTiltRadians * MathUtility::SmoothStep(TiltStartDistance, TiltFullDistance, distance) *
            (1.0f - std::clamp(calm, 0.0f, 1.0f));
    }
    const float step = TiltRate * deltaTime;
    m_Roll = std::clamp(target, m_Roll - step, m_Roll + step);
}
