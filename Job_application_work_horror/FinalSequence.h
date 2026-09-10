// ============================================================================
// ファイルの役割: 2面の最終演出と追跡に固有の時間・フェーズ状態を管理します。
// 敵、照明、ドア、PostProcessへの命令は Stage2Scene が担当します。
// ============================================================================

#pragma once

#include <algorithm>

class FinalSequence final
{
private:
    float m_SequenceTimer = -1.0f;
    float m_PursuitTimer = 0.0f;
    float m_PursuitPulseTimer = 0.0f;
    float m_GazePenaltyTimer = 0.0f;
    int m_Phase = 0;

public:
    void Reset() noexcept
    {
        m_SequenceTimer = -1.0f;
        m_PursuitTimer = 0.0f;
        m_PursuitPulseTimer = 0.0f;
        m_GazePenaltyTimer = 0.0f;
        m_Phase = 0;
    }

    void Start() noexcept
    {
        m_SequenceTimer = 0.0f;
        m_PursuitTimer = 7.0f;
        m_PursuitPulseTimer = 0.12f;
        m_GazePenaltyTimer = 0.0f;
        m_Phase = 0;
    }

    void UpdateCountdowns(float deltaTime) noexcept
    {
        m_PursuitTimer = (std::max)(0.0f, m_PursuitTimer - deltaTime);
        m_GazePenaltyTimer =
            (std::max)(0.0f, m_GazePenaltyTimer - deltaTime);
    }

    void AdvanceSequence(float deltaTime) noexcept
    {
        m_SequenceTimer += deltaTime;
    }

    int ConsumePendingBeat() noexcept
    {
        constexpr float BeatTimes[] = { 0.05f, 0.30f, 0.58f, 0.90f };
        constexpr int BeatCount =
            static_cast<int>(sizeof(BeatTimes) / sizeof(BeatTimes[0]));
        if (!IsSequenceActive() || m_Phase >= BeatCount ||
            m_SequenceTimer < BeatTimes[m_Phase])
        {
            return -1;
        }
        return m_Phase++;
    }

    bool AdvancePursuitPulse(float deltaTime) noexcept
    {
        m_PursuitPulseTimer -= deltaTime;
        return m_PursuitPulseTimer <= 0.0f;
    }

    void SchedulePursuitPulse(float proximity) noexcept
    {
        m_PursuitPulseTimer = 0.72f - proximity * 0.40f;
    }

    bool TryTriggerGazePenalty() noexcept
    {
        if (!IsPursuitActive() || m_GazePenaltyTimer > 0.0f)
        {
            return false;
        }
        m_GazePenaltyTimer = 1.45f;
        return true;
    }

    void StopPursuit() noexcept { m_PursuitTimer = 0.0f; }

    void StopForCaught() noexcept
    {
        m_PursuitTimer = 0.0f;
        m_PursuitPulseTimer = 0.0f;
        m_GazePenaltyTimer = 0.0f;
    }

    void ResetSequenceForRetry() noexcept
    {
        m_SequenceTimer = -1.0f;
        m_Phase = 0;
    }

    bool IsSequenceActive() const noexcept { return m_SequenceTimer >= 0.0f; }
    bool IsPursuitActive() const noexcept { return m_PursuitTimer > 0.0f; }
    bool HasGazePenalty() const noexcept { return m_GazePenaltyTimer > 0.0f; }
    float GetSequenceTimer() const noexcept { return m_SequenceTimer; }

    float GetGazePenaltyRate() const noexcept
    {
        return (std::clamp)(m_GazePenaltyTimer / 1.45f, 0.0f, 1.0f);
    }
};
