// ============================================================================
// ファイルの役割: 1面で、排水かヒューズ3本の回収の後に一度だけ起きる濁流の進行。
// （説明はStage1CorridorFlood.hを参照）
// ============================================================================

#include "Stage1CorridorFlood.h"
#include "MathUtility.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 始める範囲：南北の廊下の中で、南の入口からこの距離より奥まで入ったとき（入ってすぐでは、逃げる距離が短すぎる）
    constexpr float TriggerMinZ = 72.0f;
    constexpr float TriggerMaxZ = 230.0f;
    constexpr float TriggerHalfWidth = 40.0f;
    // 照明が消えてから水が押し寄せ始めるまでの秒数（轟音だけが近づいてくる間）。
    // 来ると分かっていても見えない時間にするため、轟音・水滴・地鳴りがだんだん強まるのを待たせている
    constexpr float WarningSeconds = 3.6f;
    // 前触れの間：轟音を鳴らす間隔と、その大きさ（始めの割合 → 最後に1）
    constexpr float WarningRushInterval = 0.8f;
    constexpr float WarningRushStartStrength = 0.35f;
    // 前触れの間：天井から落ちる水滴の間隔（始め → 最後）、落ちる所のプレイヤーからの距離（最小・幅）
    constexpr float WarningDripStartInterval = 0.75f;
    constexpr float WarningDripEndInterval = 0.16f;
    constexpr float WarningDripMinDistance = 8.0f;
    constexpr float WarningDripDistanceRange = 26.0f;
    // 水滴の場所を決める乱数（落ちた回数から作る）の種
    constexpr float DripAngleSeed = 12.9898f;
    constexpr float DripDistanceSeed = 78.233f;
    constexpr float DripHashScale = 43758.5453f;
    // 地鳴りの強さ（0〜1）：前触れの始めの強さ（ここから最後に1まで強まる）、強まり方の指数、押し寄せている間の強さ
    constexpr float TremorWarningStart = 0.15f;
    constexpr float TremorWarningCurve = 1.6f;
    constexpr float TremorRushing = 0.45f;
    // 水の先頭が進む速さ（プレイヤーが歩く速さ30より速く、走る速さ約50より遅い）
    constexpr float RushSpeed = 40.0f;
    // 直線部から円弧に近付く間に減速し、出口では南北の廊下と同じ速度にする。
    constexpr float InletSpeed = 70.0f;
    constexpr float InletBrakeDistance = 24.0f;
    constexpr float BendStart = Stage1CorridorFlood::InletOriginX - SIGNAL_LOST_FLOOD_BEND_START_X;
    constexpr float BendRadius = SIGNAL_LOST_FLOOD_BEND_RADIUS;
    constexpr float BendEnd = BendStart + BendRadius * SIGNAL_LOST_FLOOD_QUARTER_TURN;
    constexpr float BendEndZ = SIGNAL_LOST_FLOOD_BEND_CENTER_Z - BendRadius;
    // 角に縁のノイズによる乾いた隙間を残さないため、2つの浅い水面を少し重ねる。
    constexpr float JunctionWaterOverlap = 10.0f;
    constexpr float InletSoundZ = SIGNAL_LOST_FLOOD_BEND_CENTER_Z;
    static_assert(InletSoundZ == (Stage1CorridorFlood::InletMinZ + Stage1CorridorFlood::InletMaxZ) * 0.5f);
    // 水が止まる所（南の入口の少し奥。中央ホールへは流れ込まない）
    constexpr float StopZ = Stage1CorridorFlood::CorridorEntranceZ + 2.0f;
    // 追いつかれたとみなす、水の先頭からの距離（先頭より北＝水の中にいる）
    constexpr float CatchMargin = 2.0f;
    // 水が止まってから引き始めるまでの秒数と、引いていく速さ（押し寄せるときより遅い）
    constexpr float HoldSeconds = 4.0f;
    // 出現時は水が廊下の奥から伸びる間に立ち上がり、停止したときは先端が崩れて床へ戻る。
    constexpr float CrestRiseDistance = 18.0f;
    constexpr float CrestCollapseSeconds = 0.85f;
    constexpr float RecedeSpeed = 24.0f;
    // 追いつかれた後の暗転の秒数（この半分の所でプレイヤーを開始地点へ戻している）
    constexpr float CaughtBlackoutSeconds = 2.4f;
    constexpr float RestartPoint = 0.5f;
    // 追いつかれてから、画面の下から水面が上がりきるまでの秒数（この後、真っ暗になっていく）
    constexpr float SubmergeRiseSeconds = 0.8f;
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
    m_InletDistance = 0.0f;
    m_Redness = 0.0f;
    m_RushSoundTimer = 0.0f;
    m_ForceStart = false;
    m_DripTimer = 0.0f;
    m_DripCount = 0;
}

void Stage1CorridorFlood::ForceStart()
{
    m_ForceStart = true;
}

void Stage1CorridorFlood::StartWarning(std::vector<Cue>& cues)
{
    m_State = State::Warning;
    m_StateTimer = 0.0f;
    m_RushSoundTimer = WarningRushInterval;
    m_DripTimer = WarningDripStartInterval;
    cues.push_back({ CueType::LightsOut, Vector3::Zero });
    // 波が見える前に、右の通路の奥から音が届く。
    cues.push_back({ CueType::Rush, Vector3(InletOriginX, SoundY, InletSoundZ), WarningRushStartStrength });
}

void Stage1CorridorFlood::Update(float deltaTime, const Vector3& playerPosition, bool ready, bool allowed,
    std::vector<Cue>& cues)
{
    const bool inCorridor = std::fabs(playerPosition.x) <= TriggerHalfWidth &&
        playerPosition.z >= TriggerMinZ && playerPosition.z <= TriggerMaxZ;
    m_StateTimer += deltaTime;

    switch (m_State)
    {
    case State::Idle:
        if (m_ForceStart || (ready && allowed && inCorridor))
        {
            StartWarning(cues);
        }
        break;
    case State::Warning:
        UpdateWarning(deltaTime, playerPosition, cues);
        if (m_StateTimer >= WarningSeconds)
        {
            m_State = State::Inflow;
            m_StateTimer = 0.0f;
            m_RushSoundTimer = 0.0f;
        }
        break;
    case State::Inflow:
        UpdateInflow(deltaTime, playerPosition, cues);
        break;
    case State::Rushing:
        UpdateRushing(deltaTime, playerPosition, cues);
        break;
    case State::Holding:
        if (m_StateTimer >= HoldSeconds)
        {
            m_State = State::Receding;
            m_StateTimer = 0.0f;
            cues.push_back({ CueType::LightsOn, Vector3::Zero });
        }
        break;
    case State::CaughtBlackout:
        UpdateCaughtBlackout(deltaTime, cues);
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

    UpdateRedness(deltaTime);
}

// 水の音を、決まった間隔でその位置から鳴らす合図を足している
void Stage1CorridorFlood::PlayRushAt(float deltaTime, float x, float z, std::vector<Cue>& cues)
{
    m_RushSoundTimer -= deltaTime;
    if (m_RushSoundTimer <= 0.0f)
    {
        m_RushSoundTimer += RushSoundInterval;
        cues.push_back({ CueType::Rush, Vector3(x, SoundY, z) });
    }
}

// 前触れの間：右の通路の奥の轟音がだんだん大きくなり、天井から落ちる水滴がだんだん増えていく
void Stage1CorridorFlood::UpdateWarning(float deltaTime, const Vector3& playerPosition, std::vector<Cue>& cues)
{
    const float progress = GetWarningProgress();
    m_RushSoundTimer -= deltaTime;
    if (m_RushSoundTimer <= 0.0f)
    {
        m_RushSoundTimer += WarningRushInterval;
        cues.push_back({ CueType::Rush, Vector3(InletOriginX, SoundY, InletSoundZ),
            std::lerp(WarningRushStartStrength, 1.0f, progress) });
    }

    m_DripTimer -= deltaTime;
    if (m_DripTimer <= 0.0f)
    {
        m_DripTimer += std::lerp(WarningDripStartInterval, WarningDripEndInterval, progress);
        // 落ちる所は、プレイヤーのまわりの決まった乱数の向きと距離（毎回同じ順番で落ちる）
        const float seed = static_cast<float>(++m_DripCount);
        const float angleHash = std::sin(seed * DripAngleSeed) * DripHashScale;
        const float distanceHash = std::sin(seed * DripDistanceSeed) * DripHashScale;
        const float angle = (angleHash - std::floor(angleHash)) * MathUtility::TwoPi;
        const float distance = WarningDripMinDistance + (distanceHash - std::floor(distanceHash)) * WarningDripDistanceRange;
        const Vector3 drip(
            std::clamp(playerPosition.x + std::cos(angle) * distance, CorridorMinX, CorridorMaxX),
            playerPosition.y,
            playerPosition.z + std::sin(angle) * distance);
        cues.push_back({ CueType::Drip, drip, progress });
    }
}

float Stage1CorridorFlood::GetWarningProgress() const
{
    return m_State == State::Warning ? std::clamp(m_StateTimer / WarningSeconds, 0.0f, 1.0f) : 0.0f;
}

// 地鳴りの強さ：前触れの間にだんだん強まり、流れ込んでいる間が一番強く、廊下を押し寄せている間は少し弱める
float Stage1CorridorFlood::GetTremor() const
{
    switch (m_State)
    {
    case State::Warning:
        return std::lerp(TremorWarningStart, 1.0f, std::pow(GetWarningProgress(), TremorWarningCurve));
    case State::Inflow:
        return 1.0f;
    case State::Rushing:
        return TremorRushing;
    default:
        return 0.0f;
    }
}

// 追いつかれた：暗転を始める
void Stage1CorridorFlood::CatchPlayer(const Vector3& playerPosition, std::vector<Cue>& cues)
{
    m_State = State::CaughtBlackout;
    m_StateTimer = 0.0f;
    cues.push_back({ CueType::Caught, playerPosition });
}

// 右の通路から流れ込み、角を曲がる間：先頭を進め、通路か角でプレイヤーに届いたら追いつく
void Stage1CorridorFlood::UpdateInflow(float deltaTime, const Vector3& playerPosition, std::vector<Cue>& cues)
{
    const float speed = std::lerp(InletSpeed, RushSpeed,
        MathUtility::SmoothStep01((m_InletDistance - BendStart + InletBrakeDistance) / InletBrakeDistance));
    const float nextDistance = m_InletDistance + speed * deltaTime;
    m_InletDistance = (std::min)(BendEnd, nextDistance);
    const float angle = (std::max)(0.0f, (m_InletDistance - BendStart) / BendRadius);
    const float frontX = m_InletDistance < BendStart ? InletOriginX - m_InletDistance :
        SIGNAL_LOST_FLOOD_BEND_START_X - BendRadius * std::sin(angle);
    const float frontZ = BendEndZ + BendRadius * std::cos(angle);
    PlayRushAt(deltaTime, frontX, frontZ, cues);

    const bool inBranch = playerPosition.z >= InletMinZ && playerPosition.z <= InletMaxZ &&
        playerPosition.x >= frontX - CatchMargin && playerPosition.x <= InletOriginX;
    // 曲がっている間は、波頭の接線に直交する向きで近くのプレイヤーとの前後を測る。
    const float along = (playerPosition.x - frontX) * std::cos(angle) +
        (playerPosition.z - frontZ) * std::sin(angle);
    const bool atJunction = m_InletDistance >= BendStart &&
        playerPosition.x >= CorridorMinX && playerPosition.x <= CorridorMaxX &&
        playerPosition.z >= BendEndZ - CatchMargin && playerPosition.z <= InletMaxZ;
    if (inBranch || (atJunction && along >= -CatchMargin))
    {
        CatchPlayer(playerPosition, cues);
    }
    else if (m_InletDistance >= BendEnd)
    {
        m_State = State::Rushing;
        m_StateTimer = 0.0f;
        // 曲がり切ったフレームの残りの距離も、南へ進んだ量に含める。
        m_FrontZ = BendEndZ - (nextDistance - BendEnd);
    }
}

// 南北の廊下を押し寄せる間：先頭を南へ進め、プレイヤーに届いたら追いつき、入口まで来たら止まる
void Stage1CorridorFlood::UpdateRushing(float deltaTime, const Vector3& playerPosition, std::vector<Cue>& cues)
{
    m_FrontZ = (std::max)(StopZ, m_FrontZ - RushSpeed * deltaTime);
    PlayRushAt(deltaTime, 0.0f, m_FrontZ, cues);
    const bool insideWidth = playerPosition.x >= CorridorMinX && playerPosition.x <= CorridorMaxX;
    if (insideWidth && playerPosition.z >= m_FrontZ - CatchMargin && playerPosition.z <= CorridorEndZ)
    {
        CatchPlayer(playerPosition, cues);
    }
    else if (m_FrontZ <= StopZ)
    {
        // 先に逃げ切っていても、波が入口まで届くのをホールから見えるようにする。
        m_State = State::Holding;
        m_StateTimer = 0.0f;
    }
}

// 追いつかれた後の暗転：真ん中で開始地点へ戻し、終わったら演出を終える
void Stage1CorridorFlood::UpdateCaughtBlackout(float deltaTime, std::vector<Cue>& cues)
{
    if (m_StateTimer >= CaughtBlackoutSeconds * RestartPoint &&
        m_StateTimer - deltaTime < CaughtBlackoutSeconds * RestartPoint)
    {
        // 真っ暗な間に、開始地点へ戻し、水も引かせている（明るくなったときには、廊下に水はない）
        cues.push_back({ CueType::Restart, Vector3::Zero });
        cues.push_back({ CueType::LightsOn, Vector3::Zero });
        m_FrontZ = CorridorEndZ;
        m_InletDistance = 0.0f;
    }
    if (m_StateTimer >= CaughtBlackoutSeconds)
    {
        m_State = State::Finished;
        m_StateTimer = 0.0f;
    }
}

// 水の赤さ：押し寄せている間・止まっている間は濃くし、それ以外は薄くしている
void Stage1CorridorFlood::UpdateRedness(float deltaTime)
{
    const bool redWater = m_State == State::Inflow || m_State == State::Rushing ||
        m_State == State::Holding || m_State == State::CaughtBlackout;
    m_Redness = redWater
        ? (std::min)(1.0f, m_Redness + RednessRiseRate * deltaTime)
        : (std::max)(0.0f, m_Redness - RednessFallRate * deltaTime);
}

float Stage1CorridorFlood::GetCrestStrength() const
{
    if (m_State == State::Holding)
    {
        return 1.0f - (std::min)(1.0f, m_StateTimer / CrestCollapseSeconds);
    }
    if (IsInletWave())
    {
        return MathUtility::SmoothStep01(m_InletDistance / CrestRiseDistance);
    }
    if (HasWater() && (m_State == State::Rushing || m_State == State::CaughtBlackout))
    {
        return 1.0f;
    }
    return 0.0f;
}

Stage1CorridorFlood::WaveFront Stage1CorridorFlood::GetWaveFront() const
{
    const float distance = IsInletWave() ? m_InletDistance : BendEnd + BendEndZ - m_FrontZ;
    return { distance, GetCrestStrength() };
}

float Stage1CorridorFlood::GetShallowFrontZ() const
{
    if (!IsInletWave()) { return m_FrontZ; }
    const float angle = (std::max)(0.0f, (m_InletDistance - BendStart) / BendRadius);
    return std::lerp(CorridorEndZ, BendEndZ, 1.0f - std::cos(angle));
}

bool Stage1CorridorFlood::IsInletWave() const
{
    return m_State == State::Inflow || (m_State == State::CaughtBlackout &&
        m_FrontZ == CorridorEndZ && m_InletDistance > 0.0f);
}

float Stage1CorridorFlood::GetInletWaterMinX() const
{
    if (IsInletWave())
    {
        return (std::max)(CorridorMaxX - JunctionWaterOverlap, InletOriginX - m_InletDistance);
    }
    return HasWater() ? CorridorMaxX - JunctionWaterOverlap : CorridorMaxX;
}

float Stage1CorridorFlood::GetInletWaterMaxX() const
{
    if (IsInletWave() || HasWater())
    {
        // 南北の廊下から水が引くときは、右の通路の水も角へ戻す。
        const float drain = MathUtility::SmoothStep01((m_FrontZ - BendEndZ) / (CorridorEndZ - BendEndZ));
        return IsInletWave() ? InletOriginX : std::lerp(InletOriginX, CorridorMaxX, drain);
    }
    return CorridorMaxX;
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
    // 水面が上がりきってから開始地点へ戻すまでに真っ暗にし、戻した後で明るくしている
    const float half = CaughtBlackoutSeconds * RestartPoint;
    if (m_StateTimer < half)
    {
        return std::clamp((m_StateTimer - SubmergeRiseSeconds) / (half - SubmergeRiseSeconds), 0.0f, 1.0f);
    }
    return 1.0f - (m_StateTimer - half) / (CaughtBlackoutSeconds - half);
}

float Stage1CorridorFlood::GetSubmerge() const
{
    // 追いつかれてから開始地点へ戻すまでの間だけ、画面の下から水面を上げている（戻した後は、もう水の中ではない）
    if (m_State != State::CaughtBlackout || m_StateTimer >= CaughtBlackoutSeconds * RestartPoint)
    {
        return 0.0f;
    }
    return std::clamp(m_StateTimer / SubmergeRiseSeconds, 0.0f, 1.0f);
}
