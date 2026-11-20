// ============================================================================
// ファイルの役割: 1面で、西棟の水を抜いた後に一度だけ起きる「抜いた水が、ループ廊下の奥から赤い濁流になって押し寄せる」演出の進行。
// （説明はStage1CorridorFlood.hを参照）
// ============================================================================

#include "Stage1CorridorFlood.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 始める範囲：南北の廊下の中で、南の入口からこの距離より奥まで入ったとき（入ってすぐでは、逃げる距離が短すぎる）
    constexpr float TriggerMinZ = 110.0f;
    constexpr float TriggerMaxZ = 230.0f;
    constexpr float TriggerHalfWidth = 40.0f;
    // 照明が消えてから水が押し寄せ始めるまでの秒数（轟音だけが近づいてくる間）
    constexpr float WarningSeconds = 1.6f;
    // 水の先頭が進む速さ（プレイヤーが歩く速さ30より速く、走る速さ約50より遅い）
    constexpr float RushSpeed = 40.0f;
    // 水が止まる所（南の入口の少し奥。中央ホールへは流れ込まない）
    constexpr float StopZ = Stage1CorridorFlood::CorridorEntranceZ + 2.0f;
    // 追いつかれたとみなす、水の先頭からの距離（先頭より北＝水の中にいる）
    constexpr float CatchMargin = 2.0f;
    // 逃げ切ったとみなす所（南の入口を抜けて、中央ホールへ出た）
    constexpr float EscapeZ = Stage1CorridorFlood::CorridorEntranceZ - 4.0f;
    // 水が止まってから引き始めるまでの秒数と、引いていく速さ（押し寄せるときより遅い）
    constexpr float HoldSeconds = 4.0f;
    constexpr float RecedeSpeed = 24.0f;
    // 追いつかれた後の暗転の秒数（この半分の所でプレイヤーを開始地点へ戻している）
    constexpr float CaughtBlackoutSeconds = 2.4f;
    constexpr float RestartPoint = 0.5f;
    // 水の赤さが濃くなる・薄くなる速さ（1秒あたり）
    constexpr float RednessRiseRate = 1.2f;
    constexpr float RednessFallRate = 0.4f;
    // 水の音を鳴らす間隔（水の先頭の所から、続けて鳴らす）
    constexpr float RushSoundInterval = 1.1f;
    // 音を鳴らす高さ（床の上の水の高さあたり）
    constexpr float SoundY = -96.0f;
}

void Stage1CorridorFlood::Reset()
{
    m_State = State::Idle;
    m_StateTimer = 0.0f;
    m_FrontZ = CorridorEndZ;
    m_Redness = 0.0f;
    m_RushSoundTimer = 0.0f;
    m_ForceStart = false;
}

void Stage1CorridorFlood::ForceStart()
{
    m_ForceStart = true;
}

void Stage1CorridorFlood::StartWarning(std::vector<Cue>& cues)
{
    m_State = State::Warning;
    m_StateTimer = 0.0f;
    cues.push_back({ CueType::LightsOut, Vector3::Zero });
    // 北の角から、轟音が近づいてくる
    cues.push_back({ CueType::Rush, Vector3(0.0f, SoundY, CorridorEndZ) });
}

void Stage1CorridorFlood::Update(float deltaTime, const Vector3& playerPosition, bool drained, bool allowed,
    std::vector<Cue>& cues)
{
    const bool inCorridor = std::fabs(playerPosition.x) <= TriggerHalfWidth &&
        playerPosition.z >= TriggerMinZ && playerPosition.z <= TriggerMaxZ;
    m_StateTimer += deltaTime;

    switch (m_State)
    {
    case State::Idle:
        if (m_ForceStart || (drained && allowed && inCorridor))
        {
            StartWarning(cues);
        }
        break;
    case State::Warning:
        if (m_StateTimer >= WarningSeconds)
        {
            m_State = State::Rushing;
            m_StateTimer = 0.0f;
            m_FrontZ = CorridorEndZ;
            m_RushSoundTimer = 0.0f;
        }
        break;
    case State::Rushing:
    {
        m_FrontZ = (std::max)(StopZ, m_FrontZ - RushSpeed * deltaTime);
        m_RushSoundTimer -= deltaTime;
        if (m_RushSoundTimer <= 0.0f)
        {
            m_RushSoundTimer += RushSoundInterval;
            cues.push_back({ CueType::Rush, Vector3(0.0f, SoundY, m_FrontZ) });
        }
        const bool insideWidth = playerPosition.x >= CorridorMinX && playerPosition.x <= CorridorMaxX;
        if (insideWidth && playerPosition.z >= m_FrontZ - CatchMargin && playerPosition.z <= CorridorEndZ)
        {
            m_State = State::CaughtBlackout;
            m_StateTimer = 0.0f;
            cues.push_back({ CueType::Caught, Vector3(playerPosition.x, SoundY, playerPosition.z) });
        }
        else if (playerPosition.z < EscapeZ || m_FrontZ <= StopZ)
        {
            // 逃げ切ったか、水が入口まで来た（廊下の外にいる）ので、入口で止めている
            m_State = State::Holding;
            m_StateTimer = 0.0f;
        }
        break;
    }
    case State::Holding:
        if (m_StateTimer >= HoldSeconds)
        {
            m_State = State::Receding;
            m_StateTimer = 0.0f;
            cues.push_back({ CueType::LightsOn, Vector3::Zero });
        }
        break;
    case State::CaughtBlackout:
        if (m_StateTimer >= CaughtBlackoutSeconds * RestartPoint &&
            m_StateTimer - deltaTime < CaughtBlackoutSeconds * RestartPoint)
        {
            // 真っ暗な間に、開始地点へ戻し、水も引かせている（明るくなったときには、廊下に水はない）
            cues.push_back({ CueType::Restart, Vector3::Zero });
            cues.push_back({ CueType::LightsOn, Vector3::Zero });
            m_FrontZ = CorridorEndZ;
        }
        if (m_StateTimer >= CaughtBlackoutSeconds)
        {
            m_State = State::Finished;
            m_StateTimer = 0.0f;
        }
        break;
    case State::Receding:
        m_FrontZ = (std::min)(CorridorEndZ, m_FrontZ + RecedeSpeed * deltaTime);
        if (m_FrontZ >= CorridorEndZ)
        {
            m_State = State::Finished;
        }
        break;
    case State::Finished:
        break;
    }

    // 水の赤さ：押し寄せている間・止まっている間は濃くし、それ以外は薄くしている
    const bool redWater = m_State == State::Rushing || m_State == State::Holding || m_State == State::CaughtBlackout;
    m_Redness = redWater
        ? (std::min)(1.0f, m_Redness + RednessRiseRate * deltaTime)
        : (std::max)(0.0f, m_Redness - RednessFallRate * deltaTime);
}

bool Stage1CorridorFlood::HasWater() const
{
    return m_FrontZ < CorridorEndZ;
}

float Stage1CorridorFlood::GetBlackout() const
{
    if (m_State != State::CaughtBlackout)
    {
        return 0.0f;
    }
    // 前半で真っ暗にし、後半で明るく戻している
    const float half = CaughtBlackoutSeconds * RestartPoint;
    return m_StateTimer < half
        ? m_StateTimer / half
        : 1.0f - (m_StateTimer - half) / (CaughtBlackoutSeconds - half);
}
