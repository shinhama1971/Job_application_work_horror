// ============================================================================
// ファイルの役割: 1面の西棟（浸水した機械室）の進み具合と、姿の見えない物音の演出を管理している。
// 主な技術: 有限状態機械、乱数による物音の予定、立体音響（説明はStage1WestWing.hを参照）
// ============================================================================

#include "Stage1WestWing.h"

#include "Door.h"
#include "Game.h"
#include "Input.h"
#include "Item.h"
#include "KeyItem.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 水の滴る音の間隔（秒）。西棟の中にいる間だけ鳴らしている。
    constexpr float MinDripInterval = 1.4f;
    constexpr float MaxDripInterval = 3.8f;
    // 背後で水の中を歩く音を鳴らすまでの、西棟の中にいた秒数（1回目・2回目）。
    constexpr float FirstWadingSeconds = 9.0f;
    constexpr float SecondWadingSeconds = 26.0f;
    // 西棟の入口の扉の位置（鍵で開く扉。ヒューズを取ると勝手に閉まる）。
    const Vector3 EntranceDoorPosition(-220.0f, -74.0f, -20.0f);
}

// 受け取ったObjectを覚え、鍵を隠し、扉に鍵をかけた状態から始めている
void Stage1WestWing::Init(const Parts& parts)
{
    m_Parts = parts;
    m_Step = Step::Waiting;
    m_Scheduled.clear();
    m_DripTimer = 0.0f;
    m_InsideSeconds = 0.0f;
    m_WadingEventsPlayed = 0;
    m_EnteredOnce = false;
    if (m_Parts.key != nullptr)
    {
        m_Parts.key->SetActive(false);
    }
    if (m_Parts.door != nullptr)
    {
        m_Parts.door->SetLocked(true);
    }
}

// 鍵を探す段階に進め、右の倉庫に鍵を出している（2回目以降は何もしない）
void Stage1WestWing::Activate()
{
    if (m_Step != Step::Waiting)
    {
        return;
    }
    m_Step = Step::FindKey;
    if (m_Parts.key != nullptr)
    {
        m_Parts.key->SetActive(true);
    }
}

// その位置が西棟の範囲の中かを返している
bool Stage1WestWing::IsInside(const Vector3& position)
{
    return position.x > MinX && position.x < MaxX &&
        position.z > MinZ && position.z < MaxZ;
}

// 進み具合に合わせて、目的地の矢印が指す場所（鍵・入口の扉・ヒューズ）を返している
Vector3 Stage1WestWing::GetGuideTarget() const
{
    switch (m_Step)
    {
    case Step::FindKey:
        return m_Parts.key != nullptr ? m_Parts.key->GetPosition() : Vector3::Zero;
    case Step::OpenDoor:
        return EntranceDoorPosition;
    case Step::FindFuse:
        return m_Parts.fuse != nullptr ? m_Parts.fuse->GetPosition() : Vector3::Zero;
    default:
        return Vector3::Zero;
    }
}

// minimum〜maximumの一様な乱数を返している
float Stage1WestWing::RandomRange(float minimum, float maximum)
{
    std::uniform_real_distribution<float> distribution(minimum, maximum);
    return distribution(m_Random);
}

void Stage1WestWing::Update(const Player& player, float deltaTime,
    const Vector3& listenerForward, std::vector<AmbientSoundCue>& cues)
{
    if (m_Parts.door == nullptr || m_Parts.key == nullptr || m_Parts.fuse == nullptr)
    {
        return;
    }

    // --- 進行：鍵を拾ったら扉の鍵を外し、扉が開いたら奥へ、ヒューズを取ったら入口の扉を閉めている
    switch (m_Step)
    {
    case Step::FindKey:
        if (m_Parts.key->IsCollected())
        {
            m_Step = Step::OpenDoor;
            m_Parts.door->SetLocked(false);
        }
        break;
    case Step::OpenDoor:
        if (m_Parts.door->IsOpen())
        {
            m_Step = Step::FindFuse;
        }
        break;
    case Step::FindFuse:
        if (m_Parts.fuse->IsCollected())
        {
            m_Step = Step::Done;
            SlamEntranceDoor();
        }
        break;
    default:
        break;
    }

    // --- 予定した物音を、時間が来たものから鳴らしている
    for (ScheduledCue& scheduled : m_Scheduled)
    {
        scheduled.Delay -= deltaTime;
        if (scheduled.Delay <= 0.0f)
        {
            cues.push_back(scheduled.Cue);
        }
    }
    m_Scheduled.erase(
        std::remove_if(m_Scheduled.begin(), m_Scheduled.end(),
            [](const ScheduledCue& scheduled) { return scheduled.Delay <= 0.0f; }),
        m_Scheduled.end());

    // --- 西棟の中にいる間の物音
    const Vector3 position = player.GetPosition();
    if (!IsInside(position))
    {
        return;
    }
    m_InsideSeconds += deltaTime;

    // 初めて入ったとき、奥の方で配管が一度だけ鳴っている（奥に何かがあると感じさせる）。
    if (!m_EnteredOnce)
    {
        m_EnteredOnce = true;
        AmbientSoundCue knock;
        knock.Label = SOUND_CUE_PIPE_KNOCK;
        knock.Position = Vector3(-330.0f, -60.0f, 220.0f);
        knock.Pitch = 0.82f;
        knock.Volume = 0.75f;
        cues.push_back(knock);
        m_DripTimer = RandomRange(MinDripInterval, MaxDripInterval);
    }

    // 天井から水が滴る音。近くのどこかで、高く小さな水音を鳴らしている。
    m_DripTimer -= deltaTime;
    if (m_DripTimer <= 0.0f)
    {
        m_DripTimer = RandomRange(MinDripInterval, MaxDripInterval);
        const float angle = RandomRange(0.0f, 6.2831853f);
        const float distance = RandomRange(10.0f, 38.0f);
        AmbientSoundCue drip;
        drip.Label = SOUND_CUE_WATER_STEP;
        drip.Position = Vector3(
            (std::clamp)(position.x + std::cos(angle) * distance, MinX, MaxX),
            -60.0f,
            (std::clamp)(position.z + std::sin(angle) * distance, MinZ, MaxZ));
        drip.Pitch = RandomRange(1.25f, 1.35f);
        drip.Volume = RandomRange(0.18f, 0.32f);
        cues.push_back(drip);
    }

    // 背後で何かが水の中を歩く音（2回まで）。振り返っても何もいない。
    const float wadingSeconds = m_WadingEventsPlayed == 0 ? FirstWadingSeconds : SecondWadingSeconds;
    if (m_WadingEventsPlayed < 2 && m_InsideSeconds >= wadingSeconds && m_Step != Step::Done)
    {
        ++m_WadingEventsPlayed;
        ScheduleWading(position, listenerForward);
    }
}

void Stage1WestWing::ScheduleWading(const Vector3& listenerPosition, const Vector3& listenerForward)
{
    // 聞き手の真後ろ、少し離れた所（24）から、ゆっくり数歩（4歩）近づいて止まっている。
    Vector3 backward = -listenerForward;
    backward.y = 0.0f;
    if (backward.LengthSquared() < 0.001f)
    {
        backward = Vector3(0.0f, 0.0f, -1.0f);
    }
    backward.Normalize();
    const Vector3 side(backward.z, 0.0f, -backward.x);

    constexpr int StepCount = 4;
    for (int index = 0; index < StepCount; ++index)
    {
        const float distance = 24.0f - static_cast<float>(index) * 3.0f;
        // 左右に少しずつ揺らし、歩いているように聞こえるようにしている
        const float sway = (index % 2 == 0 ? 1.0f : -1.0f) * 1.5f;
        Vector3 stepPosition = listenerPosition + backward * distance + side * sway;
        stepPosition.x = (std::clamp)(stepPosition.x, MinX, MaxX);
        stepPosition.z = (std::clamp)(stepPosition.z, MinZ, MaxZ);
        stepPosition.y = -99.0f;

        ScheduledCue scheduled;
        scheduled.Delay = 0.35f + static_cast<float>(index) * 0.62f;
        scheduled.Cue.Label = SOUND_CUE_WATER_STEP;
        scheduled.Cue.Position = stepPosition;
        scheduled.Cue.Pitch = 0.86f;
        scheduled.Cue.Volume = 0.55f + static_cast<float>(index) * 0.08f;
        m_Scheduled.push_back(scheduled);
    }
}

void Stage1WestWing::SlamEntranceDoor()
{
    // ヒューズを取った瞬間、入口の扉が勝手に閉まる（鍵はかからず、内側から開けて戻れる）。
    Core::Game* game = Core::Game::GetInstance();
    m_Parts.door->ResetClosed(0);
    game->PlayAudioCueAt(SOUND_CUE_DOOR, EntranceDoorPosition, 0.78f, 1.0f);
    game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.45f);
    Input::SetVibration(10, 0.22f);
}
