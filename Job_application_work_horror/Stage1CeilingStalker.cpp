// ============================================================================
// ファイルの役割: 1面の「天井裏の足音が、照明を消しながら迫ってくる」演出の進行。
// （説明はStage1CeilingStalker.hを参照）
// ============================================================================

#include "Stage1CeilingStalker.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 始める範囲：照明の線から横にこの幅の中にいて、線の一番手前の照明よりこの距離までの所にいる
    constexpr float TriggerHalfWidth = 25.0f;
    constexpr float TriggerBeforeFirstLight = 40.0f;
    // 足音の線を、端の照明からさらに延ばす長さ（端の照明の向こうから歩いてきて、向こうへ歩き去る）
    constexpr float ChainMargin = 60.0f;
    // 足音が歩き始める所（プレイヤーの背後のこの距離）と、背後にこれだけの長さが取れなければ始めない
    constexpr float StartBehind = 80.0f;
    constexpr float MinimumStartGap = 50.0f;
    // 足音の速さ（プレイヤーが歩く速さ30より少し速く、走る速さ約50より遅い）と、1歩の間の秒数、左右の足の音の高さ
    constexpr float StalkerSpeed = 36.0f;
    constexpr float StepInterval = 0.42f;
    constexpr float StepPitchLeft = 0.56f;
    constexpr float StepPitchRight = 0.61f;
    // 追いつかれたとみなす、上から見た距離
    constexpr float CatchDistance = 10.0f;
    // 追いつかれた後に暗闇が続く秒数、足音が端まで行った後や暗闇の後に照明が戻り始めるまでの秒数、1つずつ点き直す間隔
    constexpr float BlackoutSeconds = 2.6f;
    constexpr float RestoreDelaySeconds = 2.0f;
    constexpr float RestoreInterval = 0.7f;
    // 追いつかれた後の脅威の度合い（暗闇の間）
    constexpr float BlackoutThreat = 1.0f;
}

void Stage1CeilingStalker::Reset()
{
    m_State = State::Idle;
    m_Fixtures.clear();
    m_StepTimer = 0.0f;
    m_StepCount = 0;
    m_StateTimer = 0.0f;
    m_PlayerDistance = 0.0f;
}

bool Stage1CeilingStalker::TryStart(const Vector3& playerPosition, const Vector3& forward,
    const std::vector<Fixture>& fixtures)
{
    if (m_State != State::Idle || fixtures.empty())
    {
        return false;
    }

    // 照明の線（南北にまっすぐ並んでいる）の x と、両端の z
    float axisX = 0.0f;
    float minZ = fixtures.front().Position.z;
    float maxZ = minZ;
    for (const Fixture& fixture : fixtures)
    {
        axisX += fixture.Position.x;
        minZ = (std::min)(minZ, fixture.Position.z);
        maxZ = (std::max)(maxZ, fixture.Position.z);
    }
    axisX /= static_cast<float>(fixtures.size());

    if (std::fabs(playerPosition.x - axisX) > TriggerHalfWidth ||
        playerPosition.z < minZ - TriggerBeforeFirstLight || playerPosition.z > maxZ)
    {
        return false;
    }

    // 向いている方へ迫ってくるよう、背後から歩かせている
    const float direction = forward.z >= 0.0f ? 1.0f : -1.0f;
    const float chainMinZ = minZ - ChainMargin;
    const float chainMaxZ = maxZ + ChainMargin;
    const float startZ = std::clamp(playerPosition.z - direction * StartBehind, chainMinZ, chainMaxZ);
    if (std::fabs(playerPosition.z - startZ) < MinimumStartGap)
    {
        return false;
    }

    // 足音が歩き始める所より後ろの照明は、足音が真下を通らないので扱わない（追いつかれたときも消さない）
    m_Fixtures.clear();
    for (const Fixture& fixture : fixtures)
    {
        if ((fixture.Position.z - startZ) * direction > 0.0f)
        {
            m_Fixtures.push_back({ fixture, false });
        }
    }
    m_AxisX = axisX;
    m_ChainMinZ = chainMinZ;
    m_ChainMaxZ = chainMaxZ;
    m_StepZ = startZ;
    m_Direction = direction;
    m_StepTimer = 0.0f;
    m_StepCount = 0;
    m_StateTimer = 0.0f;
    m_PlayerDistance = std::fabs(playerPosition.z - startZ);
    m_State = State::Approaching;
    return true;
}

void Stage1CeilingStalker::Update(float deltaTime, const Vector3& playerPosition, std::vector<Cue>& cues)
{
    switch (m_State)
    {
    case State::Approaching:
    {
        m_StepZ += m_Direction * StalkerSpeed * deltaTime;
        // 重い足音を一定の間隔で鳴らしている（左右の足で少し高さを変える）
        m_StepTimer -= deltaTime;
        if (m_StepTimer <= 0.0f)
        {
            m_StepTimer += StepInterval;
            Cue step;
            step.Type = CueType::Step;
            step.Position = Vector3(m_AxisX, CeilingVoidY, m_StepZ);
            step.Pitch = (m_StepCount % 2 == 0) ? StepPitchLeft : StepPitchRight;
            cues.push_back(step);
            ++m_StepCount;
        }
        // 足音が真下を通り過ぎた照明を消している
        for (TrackedFixture& tracked : m_Fixtures)
        {
            const float passed = (m_StepZ - tracked.Light.Position.z) * m_Direction;
            if (!tracked.Off && passed >= 0.0f)
            {
                tracked.Off = true;
                cues.push_back({ CueType::LightOff, tracked.Light.Number, tracked.Light.Position, 1.0f });
            }
        }
        // 上から見た距離が近づいたら、追いつかれている
        const Vector2 toPlayer(playerPosition.x - m_AxisX, playerPosition.z - m_StepZ);
        m_PlayerDistance = toPlayer.Length();
        if (m_PlayerDistance < CatchDistance)
        {
            cues.push_back({ CueType::Caught, 0, Vector3(playerPosition.x, CeilingVoidY, playerPosition.z), 1.0f });
            TurnOffAll(cues);
            m_State = State::Blackout;
            m_StateTimer = BlackoutSeconds;
            break;
        }
        // 線の端まで歩き去ったら、少し待って照明を戻す
        if (m_StepZ > m_ChainMaxZ || m_StepZ < m_ChainMinZ)
        {
            m_State = State::Waiting;
            m_StateTimer = RestoreDelaySeconds;
        }
        break;
    }
    case State::Blackout:
        m_StateTimer -= deltaTime;
        if (m_StateTimer <= 0.0f)
        {
            m_State = State::Waiting;
            m_StateTimer = RestoreDelaySeconds;
        }
        break;
    case State::Waiting:
        m_StateTimer -= deltaTime;
        if (m_StateTimer <= 0.0f)
        {
            m_State = State::Restoring;
            m_StateTimer = 0.0f;
        }
        break;
    case State::Restoring:
    {
        m_StateTimer -= deltaTime;
        if (m_StateTimer > 0.0f)
        {
            break;
        }
        m_StateTimer = RestoreInterval;
        // 消えた照明を、消えた順に1つずつ点け直している
        bool restored = false;
        for (TrackedFixture& tracked : m_Fixtures)
        {
            if (tracked.Off)
            {
                tracked.Off = false;
                cues.push_back({ CueType::LightOn, tracked.Light.Number, tracked.Light.Position, 1.0f });
                restored = true;
                break;
            }
        }
        if (!restored)
        {
            m_State = State::Finished;
        }
        break;
    }
    case State::Idle:
    case State::Finished:
        break;
    }
}

float Stage1CeilingStalker::GetThreat() const
{
    if (m_State == State::Approaching)
    {
        return std::clamp(1.0f - m_PlayerDistance / StartBehind, 0.0f, 1.0f);
    }
    return m_State == State::Blackout ? BlackoutThreat : 0.0f;
}

void Stage1CeilingStalker::TurnOffAll(std::vector<Cue>& cues)
{
    for (TrackedFixture& tracked : m_Fixtures)
    {
        if (!tracked.Off)
        {
            tracked.Off = true;
            cues.push_back({ CueType::LightOff, tracked.Light.Number, tracked.Light.Position, 1.0f });
        }
    }
}
