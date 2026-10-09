// ============================================================================
// ファイルの役割: 2面の最後の演出（停電）と、その後の追跡について、時間と段階を管理している。
// 主な技術: 有限状態機械、決まった時刻に起こす演出、カメラ・照明・音のタイミング合わせ
// 敵・照明・扉・PostProcessへの命令は Stage2Scene が担当している。
// ============================================================================

#pragma once

#include <algorithm>

// 最後の停電の演出（決まった時刻に照明や音を順に変える）と、その後に影から逃げる追跡の時間を管理している。
class FinalSequence final
{
private:
    // 演出の経過秒（負なら始まっていない）、追跡の残り秒数、次に追跡の脈動を出すまでの秒数
    float m_SequenceTimer = -1.0f;
    float m_PursuitTimer = 0.0f;
    float m_PursuitPulseTimer = 0.0f;
    // 影を見てしまったときの罰（照明の明滅など）を、続けて出さないための残り秒数
    float m_GazePenaltyTimer = 0.0f;
    // 次に起こす演出の段階
    int m_Phase = 0;

public:
    // 始まる前の状態に戻している
    void Reset() noexcept
    {
        m_SequenceTimer = -1.0f;
        m_PursuitTimer = 0.0f;
        m_PursuitPulseTimer = 0.0f;
        m_GazePenaltyTimer = 0.0f;
        m_Phase = 0;
    }

    // 停電の演出を始め、7秒間の追跡を始めている
    void Start() noexcept
    {
        m_SequenceTimer = 0.0f;
        m_PursuitTimer = 7.0f;
        m_PursuitPulseTimer = 0.12f;
        m_GazePenaltyTimer = 0.0f;
        m_Phase = 0;
    }

    // 追跡と、見てしまったときの罰の残り秒数を減らしている
    void UpdateCountdowns(float deltaTime) noexcept
    {
        m_PursuitTimer = (std::max)(0.0f, m_PursuitTimer - deltaTime);
        m_GazePenaltyTimer =
            (std::max)(0.0f, m_GazePenaltyTimer - deltaTime);
    }

    // 演出の経過秒を進めている
    void AdvanceSequence(float deltaTime) noexcept
    {
        m_SequenceTimer += deltaTime;
    }

    // 次の段階の時刻（0.05・0.30・0.58・0.90秒）が来ていれば、その段階の番号を返して次へ進めている（まだなら-1）
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

    // 追跡中の脈動（画面と振動）を出す時間になったかを返している
    bool AdvancePursuitPulse(float deltaTime) noexcept
    {
        m_PursuitPulseTimer -= deltaTime;
        return m_PursuitPulseTimer <= 0.0f;
    }

    // 次の脈動までの秒数を決めている。影が近いほど間隔を短くしている（0.72秒〜0.32秒）
    void SchedulePursuitPulse(float proximity) noexcept
    {
        m_PursuitPulseTimer = 0.72f - proximity * 0.40f;
    }

    // 追跡中に影を見てしまったとき、罰を出してよいかを返している（1.45秒に1回まで）
    bool TryTriggerGazePenalty() noexcept
    {
        if (!IsPursuitActive() || m_GazePenaltyTimer > 0.0f)
        {
            return false;
        }
        m_GazePenaltyTimer = 1.45f;
        return true;
    }

    // 追跡を終えている
    void StopPursuit() noexcept { m_PursuitTimer = 0.0f; }

    // 捕まったので、追跡と罰を止めている
    void StopForCaught() noexcept
    {
        m_PursuitTimer = 0.0f;
        m_PursuitPulseTimer = 0.0f;
        m_GazePenaltyTimer = 0.0f;
    }

    // やり直すときに、演出をもう一度最初から再生できるようにしている
    void ResetSequenceForRetry() noexcept
    {
        m_SequenceTimer = -1.0f;
        m_Phase = 0;
    }

    // 演出中か、追跡中か、罰を出している最中か、演出の経過秒を返している
    bool IsSequenceActive() const noexcept { return m_SequenceTimer >= 0.0f; }
    bool IsPursuitActive() const noexcept { return m_PursuitTimer > 0.0f; }
    bool HasGazePenalty() const noexcept { return m_GazePenaltyTimer > 0.0f; }
    float GetSequenceTimer() const noexcept { return m_SequenceTimer; }

    // 罰の残り具合（0〜1）を返している
    float GetGazePenaltyRate() const noexcept
    {
        return (std::clamp)(m_GazePenaltyTimer / 1.45f, 0.0f, 1.0f);
    }
};
