// ============================================================================
// ファイルの役割: 1面の「音を立てたときだけ輪郭が浮かぶ真っ暗な部屋」（開始地点の部屋）の進行。
// （説明はStage1EchoRoom.hを参照）
// ============================================================================

#include "Stage1EchoRoom.h"

#include "shader/renderingConfig.hlsli"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

static_assert(Stage1EchoRoom::MaxPulses == SIGNAL_LOST_MAX_ECHO_PULSES);

namespace
{
    // ループ廊下をこの回数だけ回った後に、開始地点の部屋へ戻ると起きる
    constexpr int DarkLoopCount = 2;
    // 戻った後、部屋の中へ入るのを待つ秒数（戻った直後は、扉の厚みの分だけ部屋の外にいることがある）
    constexpr float ArmWindowSeconds = 3.0f;
    // 足音の輪：一歩の長さ（歩く・走る）、広がる一番大きな半径（歩く・走る）、強さ（歩く・走る）
    constexpr float WalkStride = 12.0f;
    constexpr float RunStride = 16.0f;
    constexpr float WalkPulseRadius = 110.0f;
    constexpr float RunPulseRadius = 180.0f;
    constexpr float WalkPulseStrength = 0.75f;
    constexpr float RunPulseStrength = 1.0f;
    // 輪が広がる速さ（1秒あたりの半径）
    constexpr float PulseSpeed = 75.0f;
    // 人影：出す所（部屋の奥の隅、プレイヤーから遠い方）、歩く速さ（聞いた音へ）、
    // 足音の間隔・輪の大きさ・強さ、触れられたとみなす距離、聞いた所に着いたとみなす距離
    constexpr float FigureCornerInset = 14.0f;
    constexpr float FigureSpeed = 15.0f;
    constexpr float FigureRunHeardSpeed = 24.0f;
    constexpr float FigureStepInterval = 0.9f;
    constexpr float FigurePulseRadius = 34.0f;
    constexpr float FigurePulseStrength = 0.5f;
    constexpr float CatchDistance = 10.0f;
    constexpr float ArriveDistance = 2.0f;
    // 人影の足もとの高さ（床 y=-100 のわずか上）と、音の輪を出す高さ（足もと）
    constexpr float FootY = -99.0f;
}

void Stage1EchoRoom::Reset()
{
    m_State = State::Waiting;
    m_Pulses = {};
    m_PulseShapes = {};
    m_PulseStrengths = {};
    m_StrideTravel = 0.0f;
    m_FigureStepTimer = 0.0f;
}

bool Stage1EchoRoom::IsInside(const Vector3& position)
{
    return position.x >= MinX && position.x <= MaxX && position.z >= MinZ && position.z <= MaxZ;
}

void Stage1EchoRoom::AddPulse(const Vector3& origin, float maxRadius, float strength)
{
    Pulse* slot = &m_Pulses[0];
    for (Pulse& pulse : m_Pulses)
    {
        if (pulse.Strength < slot->Strength)
        {
            slot = &pulse;
        }
    }
    *slot = { origin, 0.0f, maxRadius, strength };
}

void Stage1EchoRoom::Finish(std::vector<Cue>& cues)
{
    m_State = State::Finished;
    m_Pulses = {};
    cues.push_back({ CueType::Restore, Vector3::Zero });
}

void Stage1EchoRoom::ArmOnLoopArrival(int loopCount, bool force)
{
    if (m_State == State::Waiting && (force || loopCount >= DarkLoopCount))
    {
        m_State = State::Armed;
        m_ArmTimer = 0.0f;
    }
}

void Stage1EchoRoom::Update(float deltaTime, const Vector3& playerPosition, bool running, std::vector<Cue>& cues)
{
    switch (m_State)
    {
    case State::Waiting:
        break;
    case State::Armed:
        UpdateArmed(deltaTime, playerPosition, cues);
        break;
    case State::Dark:
        UpdateDark(deltaTime, playerPosition, running, cues);
        break;
    case State::Finished:
        break;
    }
    AdvancePulses(deltaTime);
}

// 戻った直後に部屋の中へ入ったら暗くしている（しばらく入らなければ、次に戻るときまで待つ）
void Stage1EchoRoom::UpdateArmed(float deltaTime, const Vector3& playerPosition, std::vector<Cue>& cues)
{
    if (!IsInside(playerPosition))
    {
        m_ArmTimer += deltaTime;
        if (m_ArmTimer >= ArmWindowSeconds)
        {
            m_State = State::Waiting;
        }
    }
    else
    {
        m_State = State::Dark;
        m_LastPlayerPosition = playerPosition;
        m_StrideTravel = 0.0f;
        // 人影は、部屋の奥の2つの隅（北側）のうち、プレイヤーから遠い方に出している
        const Vector3 left(MinX + FigureCornerInset, FootY, MaxZ - FigureCornerInset);
        const Vector3 right(MaxX - FigureCornerInset, FootY, MaxZ - FigureCornerInset);
        m_FigurePosition = Vector3::DistanceSquared(left, playerPosition) > Vector3::DistanceSquared(right, playerPosition)
            ? left : right;
        m_FigureTarget = m_FigurePosition;
        m_FigureStepTimer = FigureStepInterval;
        cues.push_back({ CueType::Darken, playerPosition });
    }
}

// 暗い部屋：歩いた距離が一歩分になるたびに足音の輪を出し、人影は最後に聞いた音の所へ歩いてくる
void Stage1EchoRoom::UpdateDark(float deltaTime, const Vector3& playerPosition, bool running, std::vector<Cue>& cues)
{
    if (!IsInside(playerPosition))
    {
        Finish(cues);
        return;
    }
    // 歩いた距離が一歩分になるたびに、足元から音の輪を出し、人影にその所を聞かせている
    m_StrideTravel += Vector2(playerPosition.x - m_LastPlayerPosition.x, playerPosition.z - m_LastPlayerPosition.z).Length();
    m_LastPlayerPosition = playerPosition;
    const float stride = running ? RunStride : WalkStride;
    float figureSpeed = FigureSpeed;
    if (m_StrideTravel >= stride)
    {
        m_StrideTravel = 0.0f;
        AddPulse(Vector3(playerPosition.x, FootY, playerPosition.z),
            running ? RunPulseRadius : WalkPulseRadius, running ? RunPulseStrength : WalkPulseStrength);
        m_FigureTarget = Vector3(playerPosition.x, FootY, playerPosition.z);
    }
    if (running)
    {
        figureSpeed = FigureRunHeardSpeed;
    }

    // 人影は、最後に聞いた音の所へ歩いていく（着いたら、次の音を待って立ち止まる）
    Vector3 toTarget = m_FigureTarget - m_FigurePosition;
    toTarget.y = 0.0f;
    const float distanceToTarget = toTarget.Length();
    if (distanceToTarget > ArriveDistance)
    {
        m_FigurePosition += toTarget / distanceToTarget * (std::min)(distanceToTarget, figureSpeed * deltaTime);
        // 歩いている間だけ、人影の足音も小さな輪になる
        m_FigureStepTimer -= deltaTime;
        if (m_FigureStepTimer <= 0.0f)
        {
            m_FigureStepTimer += FigureStepInterval;
            AddPulse(m_FigurePosition, FigurePulseRadius, FigurePulseStrength);
            cues.push_back({ CueType::FigureStep, m_FigurePosition });
        }
    }

    const Vector3 toPlayer = playerPosition - m_FigurePosition;
    if (Vector2(toPlayer.x, toPlayer.z).Length() <= CatchDistance)
    {
        cues.push_back({ CueType::Caught, m_FigurePosition });
        Finish(cues);
    }
}

// 輪を広げ、広がるほど弱めている（一番大きな半径で消える）
void Stage1EchoRoom::AdvancePulses(float deltaTime)
{
    for (std::size_t index = 0; index < MaxPulses; ++index)
    {
        Pulse& pulse = m_Pulses[index];
        if (pulse.Strength > 0.0f)
        {
            pulse.Radius += PulseSpeed * deltaTime;
            if (pulse.Radius >= pulse.MaxRadius)
            {
                pulse = {};
            }
        }
        // 遠くの壁に届くまで明るさが残るよう、広がりの終わりの方でまとめて弱めている
        const float spread = pulse.MaxRadius > 0.0f ? pulse.Radius / pulse.MaxRadius : 1.0f;
        const float fade = 1.0f - spread * spread;
        m_PulseShapes[index] = Vector4(pulse.Origin.x, pulse.Origin.y, pulse.Origin.z, pulse.Radius);
        m_PulseStrengths[index] = pulse.Strength * fade;
    }
}
