// ============================================================================
// ファイルの役割: 1面で、姿の見えない物音（天井裏の足音・配管を叩く音・遠くの扉）を
//                 ランダムな間隔と場所で起こしている。
// 主な技術: 立体音響と組み合わせた環境の演出、乱数による出現の制御、時間をずらして鳴らす音の予定
// ============================================================================

#include "Stage1AmbientSounds.h"

#include "StageSurveillanceCameras.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

using namespace DirectX::SimpleMath;

namespace
{
    // 1面の施設の広さ（壁の内側。西棟は含めていない）。この外で鳴る音は選ばない。
    constexpr float StageMinX = -205.0f;
    constexpr float StageMaxX = 205.0f;
    constexpr float StageMinZ = -170.0f;
    constexpr float StageMaxZ = 330.0f;
    constexpr float CeilingVoidY = -44.0f;  // 天井板（y=-47付近）のすぐ上
    constexpr float Pi = 3.14159265f;

    // 見ていない方向（背後・横）から鳴らすと、振り向いても何もいない怖さが出る。
    // 視線との内積がこの値以下の場所だけを選んでいる（0で真横、負で背後寄り）。
    constexpr float MaxFacingDot = 0.35f;

    // 壁際の配管。Stage1Layoutの配管（PropPipe...）に沿った線分。
    struct PipeSegment
    {
        Vector3 Start;
        Vector3 End;
    };
    const std::array<PipeSegment, 6> PipeSegments =
    {{
        { Vector3(-205.0f, -55.0f, -165.0f), Vector3(-205.0f, -55.0f, 285.0f) },
        { Vector3(205.0f, -55.0f, -165.0f), Vector3(205.0f, -55.0f, 285.0f) },
        { Vector3(-202.0f, -52.5f, 37.0f), Vector3(-28.0f, -52.5f, 37.0f) },
        { Vector3(28.0f, -52.5f, 37.0f), Vector3(202.0f, -52.5f, 37.0f) },
        { Vector3(-205.0f, -52.5f, 177.0f), Vector3(-45.0f, -52.5f, 177.0f) },
        { Vector3(45.0f, -52.5f, 177.0f), Vector3(205.0f, -52.5f, 177.0f) }
    }};

    // 聞き手から見て、距離が範囲内で、しかも視線の外にある場所かどうかを返している。
    bool IsUnseenSpot(
        const Vector3& spot,
        const Vector3& listenerPosition,
        const Vector3& listenerForward,
        float minimumDistance,
        float maximumDistance)
    {
        Vector3 toSpot = spot - listenerPosition;
        toSpot.y = 0.0f;
        const float distance = toSpot.Length();
        if (distance < minimumDistance || distance > maximumDistance)
        {
            return false;
        }
        Vector3 forward(listenerForward.x, 0.0f, listenerForward.z);
        if (forward.LengthSquared() < 0.0001f)
        {
            return true;
        }
        forward.Normalize();
        return forward.Dot(toSpot / distance) <= MaxFacingDot;
    }

    // 施設の広さの中にあるか
    bool IsInsideStage(const Vector3& position)
    {
        return position.x >= StageMinX && position.x <= StageMaxX &&
            position.z >= StageMinZ && position.z <= StageMaxZ;
    }
}

// 予定をすべて消し、最初の物音までの時間に戻している
void Stage1AmbientSounds::Reset()
{
    m_Scheduled.clear();
    m_WaitTimer = FirstDelay;
    m_LastKind = Kind::DistantDoor;
}

void Stage1AmbientSounds::Update(
    float deltaTime,
    bool allowed,
    bool powerRestored,
    const Vector3& listenerPosition,
    const Vector3& listenerForward,
    std::vector<AmbientSoundCue>& cues)
{
    if (!allowed)
    {
        // 台本の演出と重ならないよう、鳴っている途中の物音も打ち切り、待ち時間も止めている。
        m_Scheduled.clear();
        return;
    }

    if (m_Scheduled.empty())
    {
        m_WaitTimer -= deltaTime;
        if (m_WaitTimer <= 0.0f)
        {
            // 条件に合う場所が見つからないときは、少し待ってから（4秒後に）選び直している。
            m_WaitTimer = TryStartEvent(powerRestored, listenerPosition, listenerForward)
                ? NextInterval(powerRestored)
                : 4.0f;
        }
    }

    // 予定の時間が来た音を、このフレームに鳴らす音として渡している
    for (ScheduledCue& scheduled : m_Scheduled)
    {
        scheduled.Delay -= deltaTime;
        if (scheduled.Delay <= 0.0f)
        {
            cues.push_back(scheduled.Cue);
        }
    }
    std::erase_if(m_Scheduled, [](const ScheduledCue& scheduled)
    {
        return scheduled.Delay <= 0.0f;
    });
}

// 台本の演出から、天井裏の足音をすぐに始めている
bool Stage1AmbientSounds::StartCeilingStepsNow(
    bool powerRestored,
    const Vector3& listenerPosition,
    const Vector3& listenerForward)
{
    if (!ScheduleCeilingSteps(listenerPosition, listenerForward))
    {
        return false;
    }
    m_LastKind = Kind::CeilingSteps;
    m_WaitTimer = NextInterval(powerRestored);
    return true;
}

bool Stage1AmbientSounds::TryStartEvent(
    bool powerRestored,
    const Vector3& listenerPosition,
    const Vector3& listenerForward)
{
    // 同じ種類が続かないよう、前回と違う種類から順に試している。
    // 扉の音は目立つので、足音・配管より出にくくしている。
    std::array<Kind, 3> order = { Kind::CeilingSteps, Kind::PipeKnocks, Kind::DistantDoor };
    std::shuffle(order.begin(), order.end(), m_Random);
    if (RandomRange(0.0f, 1.0f) < 0.6f && order[0] == Kind::DistantDoor)
    {
        std::swap(order[0], order[2]);
    }
    std::stable_partition(order.begin(), order.end(), [this](Kind kind)
    {
        return kind != m_LastKind;
    });

    for (Kind kind : order)
    {
        bool scheduled = false;
        switch (kind)
        {
        case Kind::CeilingSteps:
            scheduled = ScheduleCeilingSteps(listenerPosition, listenerForward);
            break;
        case Kind::PipeKnocks:
            scheduled = SchedulePipeKnocks(listenerPosition, listenerForward);
            break;
        case Kind::DistantDoor:
            // 電力が戻る前の施設は静まり返っているため、扉が動くのは電力が戻った後だけにしている。
            scheduled = powerRestored &&
                ScheduleDistantDoor(listenerPosition, listenerForward);
            break;
        }
        if (scheduled)
        {
            m_LastKind = kind;
            return true;
        }
    }
    return false;
}

// 背後の少し離れた天井裏から、プレイヤーの近くを通り過ぎるように足音が歩いていく。
bool Stage1AmbientSounds::ScheduleCeilingSteps(
    const Vector3& listenerPosition,
    const Vector3& listenerForward)
{
    Vector3 forward(listenerForward.x, 0.0f, listenerForward.z);
    if (forward.LengthSquared() < 0.0001f)
    {
        forward = Vector3(0.0f, 0.0f, 1.0f);
    }
    forward.Normalize();
    const Vector3 right(forward.z, 0.0f, -forward.x);

    for (int attempt = 0; attempt < 10; ++attempt)
    {
        // 真後ろから左右100度の範囲で、少し離れた場所（85〜125）から歩き始めている。
        const float angle = RandomRange(-100.0f, 100.0f) * Pi / 180.0f;
        const float distance = RandomRange(85.0f, 125.0f);
        Vector3 start = listenerPosition +
            (-forward * std::cos(angle) + right * std::sin(angle)) * distance;
        start.y = CeilingVoidY;
        if (!IsInsideStage(start))
        {
            continue;
        }

        // プレイヤーの真上を少し外れた場所へ向かって歩いている。
        Vector3 target = listenerPosition + right * RandomRange(-28.0f, 28.0f);
        target.y = CeilingVoidY;
        Vector3 direction = target - start;
        if (direction.LengthSquared() < 1.0f)
        {
            continue;
        }
        direction.Normalize();

        // 歩幅18で5〜7歩、0.54〜0.66秒おきに鳴らしている（施設の外に出たらそこで止める）
        constexpr float Stride = 18.0f;
        const int stepCount = 5 + static_cast<int>(RandomRange(0.0f, 2.99f));
        const float stepInterval = RandomRange(0.54f, 0.66f);
        m_Scheduled.clear();
        for (int step = 0; step < stepCount; ++step)
        {
            const Vector3 position = start + direction * (Stride * static_cast<float>(step));
            if (!IsInsideStage(position))
            {
                break;
            }
            ScheduledCue scheduled;
            scheduled.Delay = stepInterval * static_cast<float>(step);
            scheduled.Cue.Label = SOUND_CUE_FOOTSTEP;
            scheduled.Cue.Position = position;
            // 人より重く低い足音にし、左右の足でわずかに音の高さを変えている。
            scheduled.Cue.Pitch = (step % 2 == 0) ? 0.60f : 0.66f;
            scheduled.Cue.Volume = 1.9f;
            scheduled.Cue.MinimumOcclusion = 0.72f;
            m_Scheduled.push_back(scheduled);
        }
        // 3歩以上鳴らせる道筋が見つかったら決定している
        if (m_Scheduled.size() >= 3)
        {
            return true;
        }
        m_Scheduled.clear();
    }
    return false;
}

// 壁際の配管を、見えない何かが2〜4回叩いている。
bool Stage1AmbientSounds::SchedulePipeKnocks(
    const Vector3& listenerPosition,
    const Vector3& listenerForward)
{
    std::uniform_int_distribution<int> segmentDistribution(
        0, static_cast<int>(PipeSegments.size()) - 1);
    // 配管の線分の上から、視線の外で60〜200離れた場所を探している
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const PipeSegment& segment = PipeSegments[
            static_cast<std::size_t>(segmentDistribution(m_Random))];
        const Vector3 spot = Vector3::Lerp(
            segment.Start, segment.End, RandomRange(0.0f, 1.0f));
        if (!IsUnseenSpot(spot, listenerPosition, listenerForward, 60.0f, 200.0f))
        {
            continue;
        }

        const int knockCount = 2 + static_cast<int>(RandomRange(0.0f, 2.99f));
        float delay = 0.0f;
        m_Scheduled.clear();
        for (int knock = 0; knock < knockCount; ++knock)
        {
            ScheduledCue scheduled;
            scheduled.Delay = delay;
            scheduled.Cue.Label = SOUND_CUE_PIPE_KNOCK;
            scheduled.Cue.Position = spot;
            scheduled.Cue.Pitch = RandomRange(0.88f, 1.08f);
            scheduled.Cue.Volume = 1.2f;
            m_Scheduled.push_back(scheduled);
            // 一定の間隔にせず、ためらうような間を空けている。
            delay += RandomRange(0.42f, 0.90f);
        }
        return true;
    }
    return false;
}

// 離れた扉が、誰もいないのにきしんでいる。
bool Stage1AmbientSounds::ScheduleDistantDoor(
    const Vector3& listenerPosition,
    const Vector3& listenerForward)
{
    std::vector<Vector3> doors =
    {
        Vector3(0.0f, -74.0f, 40.0f),       // 中央の廊下の扉
        Vector3(202.0f, -74.0f, 307.5f)     // 出口の扉
    };
    // 監視カメラで見られる閉ざされた扉も候補に加えている
    for (const StageSealedDoor& sealed : StageSealedDoors)
    {
        doors.emplace_back(sealed.Position[0], sealed.Position[1], sealed.Position[2]);
    }
    std::shuffle(doors.begin(), doors.end(), m_Random);

    // 視線の外で90〜260離れた扉を1つ選んでいる
    for (const Vector3& door : doors)
    {
        if (!IsUnseenSpot(door, listenerPosition, listenerForward, 90.0f, 260.0f))
        {
            continue;
        }
        ScheduledCue scheduled;
        scheduled.Cue.Label = SOUND_CUE_DOOR;
        scheduled.Cue.Position = door;
        scheduled.Cue.Pitch = RandomRange(0.72f, 0.82f);
        scheduled.Cue.Volume = 0.95f;
        m_Scheduled.clear();
        m_Scheduled.push_back(scheduled);
        return true;
    }
    return false;
}

// minimum〜maximumの一様な乱数を返している
float Stage1AmbientSounds::RandomRange(float minimum, float maximum)
{
    std::uniform_real_distribution<float> distribution(minimum, maximum);
    return distribution(m_Random);
}

// 次の物音までの間隔を決めている（電力が戻っていれば0.72倍）
float Stage1AmbientSounds::NextInterval(bool powerRestored)
{
    const float interval = RandomRange(MinInterval, MaxInterval);
    return powerRestored ? interval * PoweredIntervalScale : interval;
}
