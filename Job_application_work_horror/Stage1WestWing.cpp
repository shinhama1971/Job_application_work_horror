// ============================================================================
// ファイルの役割: 1面の西棟（浸水した機械室）の進み具合と、排水バルブの遊び、姿の見えない物音の演出を管理している。
// 主な技術: 有限状態機械、長押しの操作、水が引いていく範囲の更新、乱数による物音の予定、立体音響
// （説明はStage1WestWing.hを参照）
// ============================================================================

#include "Stage1WestWing.h"

#include "Door.h"
#include "DrainValve.h"
#include "Game.h"
#include "Ground.h"
#include "Input.h"
#include "Item.h"
#include "KeyItem.h"
#include "Player.h"
#include "Wall.h"

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
    // 奥のポンプ室の排水口の位置。水はここへ向かって引いていく。
    const Vector3 DrainPosition(-290.0f, -99.5f, 228.0f);
    // 回したバルブの数ごとの、水の手前の端（z）。3つ回すと奥の端（MaxZ）まで引き、水がなくなる。
    constexpr std::array<float, Stage1WestWing::ValveCount + 1> WaterEdgeTargets =
    {
        Stage1WestWing::MinZ, 20.0f, 140.0f, Stage1WestWing::MaxZ
    };
    // 水の端が引いていく速さ（1秒あたり）
    constexpr float WaterDrainSpeed = 10.0f;
    // バルブのランプの色（回す前は赤、回したら緑）
    const Color ValveLampClosed(0.90f, 0.08f, 0.05f, 1.0f);
    const Color ValveLampOpened(0.10f, 0.85f, 0.25f, 1.0f);

    // ランプの色を変えている（本体の色は光る色を暗くしたもの。照らす光の色も自動で変わる）
    void SetLampColor(Wall* lamp, const Color& color)
    {
        if (lamp != nullptr)
        {
            lamp->SetAppearance(
                Color(color.R() * 0.6f, color.G() * 0.6f, color.B() * 0.6f, 1.0f), color, 24.0f);
        }
    }
}

// 受け取ったObjectを覚え、鍵とヒューズを隠し、扉に鍵をかけ、西棟を水で満たした状態から始めている
void Stage1WestWing::Init(const Parts& parts)
{
    m_Parts = parts;
    m_Step = Step::Waiting;
    m_Scheduled.clear();
    m_DripTimer = 0.0f;
    m_InsideSeconds = 0.0f;
    m_WadingEventsPlayed = 0;
    m_EnteredOnce = false;
    m_WaterEdgeZ = MinZ;
    m_GurgleTimer = 0.0f;
    m_TurnSoundTimer = 0.0f;
    m_TurningTimer = 0.0f;
    m_TurningProgress = 0.0f;
    m_TurningText.clear();
    m_NoticeText = {};
    m_NoticeTimer = 0.0f;
    if (m_Parts.key != nullptr)
    {
        m_Parts.key->SetActive(false);
    }
    if (m_Parts.fuse != nullptr)
    {
        m_Parts.fuse->SetActive(false);
    }
    if (m_Parts.door != nullptr)
    {
        m_Parts.door->SetLocked(true);
    }
    for (DrainValve* valve : m_Parts.valves)
    {
        if (valve != nullptr)
        {
            valve->SetInteractionAllowed(false);
        }
    }
    for (Wall* lamp : m_Parts.valveLamps)
    {
        SetLampColor(lamp, ValveLampClosed);
    }
    ApplyFloodRegion();
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

int Stage1WestWing::GetOpenedValveCount() const
{
    return static_cast<int>(std::count_if(m_Parts.valves.begin(), m_Parts.valves.end(),
        [](const DrainValve* valve) { return valve != nullptr && valve->IsOpened(); }));
}

// 進み具合に合わせて、目的地の矢印が指す場所（鍵・入口の扉・一番近いバルブ・ヒューズ）を返している
Vector3 Stage1WestWing::GetGuideTarget() const
{
    switch (m_Step)
    {
    case Step::FindKey:
        return m_Parts.key != nullptr ? m_Parts.key->GetPosition() : Vector3::Zero;
    case Step::OpenDoor:
        return EntranceDoorPosition;
    case Step::DrainWater:
    {
        // まだ回していないバルブのうち、一番近いものを指している（全部回した後は、水が引いていく先の排水口）。
        Vector3 target = DrainPosition;
        float bestDistance = -1.0f;
        for (const DrainValve* valve : m_Parts.valves)
        {
            if (valve == nullptr || valve->IsOpened())
            {
                continue;
            }
            const float distance = Vector3::DistanceSquared(valve->GetInteractionPosition(), m_LastPlayerPosition);
            if (bestDistance < 0.0f || distance < bestDistance)
            {
                bestDistance = distance;
                target = valve->GetInteractionPosition();
            }
        }
        return target;
    }
    case Step::FindFuse:
        return m_Parts.fuse != nullptr ? m_Parts.fuse->GetPosition() : Vector3::Zero;
    default:
        return Vector3::Zero;
    }
}

// 回している間は進み具合を、そうでなければ短い知らせを返している
std::string_view Stage1WestWing::GetObjectiveText() const
{
    if (m_TurningTimer > 0.0f)
    {
        return m_TurningText;
    }
    if (m_NoticeTimer > 0.0f)
    {
        return m_NoticeText;
    }
    return {};
}

// minimum〜maximumの一様な乱数を返している
float Stage1WestWing::RandomRange(float minimum, float maximum)
{
    std::uniform_real_distribution<float> distribution(minimum, maximum);
    return distribution(m_Random);
}

void Stage1WestWing::ShowNotice(std::string_view text, float seconds)
{
    m_NoticeText = text;
    m_NoticeTimer = seconds;
}

void Stage1WestWing::Update(const Player& player, float deltaTime,
    const Vector3& listenerForward, const Interactable* focused,
    std::vector<AmbientSoundCue>& cues)
{
    if (m_Parts.door == nullptr || m_Parts.key == nullptr || m_Parts.fuse == nullptr)
    {
        return;
    }
    const Vector3 position = player.GetPosition();
    m_LastPlayerPosition = position;
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    m_TurningTimer = (std::max)(0.0f, m_TurningTimer - deltaTime);

    // --- 進行：鍵を拾ったら扉の鍵を外し、扉が開いたらバルブを回せるようにし、
    //     水が全部引いたらヒューズを出し、ヒューズを取ったら入口の扉を閉めている
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
            m_Step = Step::DrainWater;
            for (DrainValve* valve : m_Parts.valves)
            {
                if (valve != nullptr)
                {
                    valve->SetInteractionAllowed(true);
                }
            }
        }
        break;
    case Step::DrainWater:
        // 初めて中に入ったとき、何をすればよいかを短く伝えている
        if (IsInside(position) && !m_EnteredOnce)
        {
            ShowNotice("床が水に沈んでいる 赤いランプのバルブで水を抜く", 3.6f);
        }
        UpdateValves(player, deltaTime, listenerForward, focused, cues);
        if (UpdateWaterLevel(deltaTime, cues))
        {
            m_Step = Step::FindFuse;
            m_Parts.fuse->SetActive(true);
            ShowNotice("水が引いた 奥のポンプ室でヒューズを探す", 3.2f);
            // 水が引ききった瞬間、奥の排水口で配管が一度大きく鳴っている
            AmbientSoundCue knock;
            knock.Label = SOUND_CUE_PIPE_KNOCK;
            knock.Position = DrainPosition;
            knock.Pitch = 0.62f;
            knock.Volume = 0.95f;
            cues.push_back(knock);
            Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.40f);
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

// 調べようとしているバルブでEキー（A）を押し続けている間だけ回している。
// 回している間はきしむ音を鳴らし、回し終えたらランプを緑にし、背後で何かが水の中を歩く。
void Stage1WestWing::UpdateValves(const Player& player, float deltaTime,
    const Vector3& listenerForward, const Interactable* focused,
    std::vector<AmbientSoundCue>& cues)
{
    const bool holding = Input::GetKeyPress(VK_E) || Input::GetButtonPress(XINPUT_A);
    if (!holding)
    {
        m_TurnSoundTimer = 0.0f;
        return;
    }
    for (size_t index = 0; index < m_Parts.valves.size(); ++index)
    {
        DrainValve* valve = m_Parts.valves[index];
        if (valve == nullptr || valve->IsOpened() ||
            focused != static_cast<const Interactable*>(valve))
        {
            continue;
        }

        const bool opened = valve->Turn(deltaTime);
        m_TurningProgress = valve->GetProgress();
        m_TurningText = "バルブを回している " +
            std::to_string(static_cast<int>(m_TurningProgress * 100.0f)) + "%";
        m_TurningTimer = 0.6f;

        // 回し始めと、回している間は0.55秒ごとに、錆びた金属がきしむ音を鳴らしている
        m_TurnSoundTimer -= deltaTime;
        if (m_TurnSoundTimer <= 0.0f)
        {
            m_TurnSoundTimer = 0.55f;
            AmbientSoundCue creak;
            creak.Label = SOUND_CUE_PIPE_KNOCK;
            creak.Position = valve->GetInteractionPosition();
            creak.Pitch = RandomRange(1.45f, 1.70f);
            creak.Volume = 0.32f;
            cues.push_back(creak);
        }

        if (opened)
        {
            m_TurningTimer = 0.0f;
            SetLampColor(m_Parts.valveLamps[index], ValveLampOpened);
            const int openedCount = GetOpenedValveCount();
            ShowNotice(openedCount < ValveCount
                ? "水が引き始めた 残りのバルブを探す"
                : "全部のバルブを開けた 水が奥へ引いていく", 3.0f);

            // 配管の奥で、低い音がゆっくり遠ざかっていく（水が流れ出した合図）
            AmbientSoundCue knock;
            knock.Label = SOUND_CUE_PIPE_KNOCK;
            knock.Position = valve->GetInteractionPosition() + Vector3(0.0f, 20.0f, 0.0f);
            knock.Pitch = 0.70f;
            knock.Volume = 0.85f;
            cues.push_back(knock);
            Input::SetVibration(8, 0.18f);
            // 手が離せなかった間に、背後へ何かが近づいていた
            ScheduleWading(player.GetPosition(), listenerForward);
        }
        break;
    }
}

// 水の端を、回したバルブの数に合った目標まで少しずつ動かしている。全部引いたらtrueを返している
bool Stage1WestWing::UpdateWaterLevel(float deltaTime, std::vector<AmbientSoundCue>& cues)
{
    const float target = WaterEdgeTargets[static_cast<size_t>(GetOpenedValveCount())];
    if (m_WaterEdgeZ < target)
    {
        m_WaterEdgeZ = (std::min)(target, m_WaterEdgeZ + WaterDrainSpeed * deltaTime);
        ApplyFloodRegion();

        // 引いていく水の端のどこかで、ごぼごぼと低い水音を鳴らしている
        m_GurgleTimer -= deltaTime;
        if (m_GurgleTimer <= 0.0f)
        {
            m_GurgleTimer = RandomRange(0.55f, 0.95f);
            AmbientSoundCue gurgle;
            gurgle.Label = SOUND_CUE_WATER_STEP;
            gurgle.Position = Vector3(RandomRange(MinX + 10.0f, MaxX - 10.0f), -99.5f, m_WaterEdgeZ);
            gurgle.Pitch = RandomRange(0.55f, 0.72f);
            gurgle.Volume = RandomRange(0.30f, 0.45f);
            cues.push_back(gurgle);
        }
    }
    return m_WaterEdgeZ >= MaxZ;
}

// 床の水に浸かった範囲を、水の端から奥の壁までにしている（端が奥の壁に着くと、範囲の広さが0になり水がなくなる）
void Stage1WestWing::ApplyFloodRegion()
{
    if (m_Parts.ground != nullptr)
    {
        m_Parts.ground->SetFloodRegion(Vector2(MinX, m_WaterEdgeZ), Vector2(MaxX, MaxZ));
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
