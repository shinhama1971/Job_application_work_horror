// ============================================================================
// ファイルの役割: 2面で影を注視した際の照明連鎖演出の時間進行を管理します。
// 照明、音、PostProcessへの演出命令は Stage2Scene が担当します。
// ============================================================================

#pragma once

#include <algorithm>

class ObservedScareSequence final
{
private:
    static constexpr float Duration = 1.35f;

    float m_Timer = -1.0f;
    float m_NoticeTimer = 0.0f;
    int m_Phase = 0;

public:
    void Reset() noexcept
    {
        m_Timer = -1.0f;
        m_NoticeTimer = 0.0f;
        m_Phase = 0;
    }

    bool Start() noexcept
    {
        if (IsActive())
        {
            return false;
        }

        m_Timer = 0.0f;
        m_NoticeTimer = 2.8f;
        m_Phase = 0;
        return true;
    }

    void Advance(float deltaTime) noexcept { m_Timer += deltaTime; }

    // 1フレームで複数時刻を越えた場合も、従来どおり順番に返します。
    int ConsumePendingBeat() noexcept
    {
        constexpr float BeatTimes[] = { 0.05f, 0.30f, 0.58f, 0.90f };
        constexpr int BeatCount =
            static_cast<int>(sizeof(BeatTimes) / sizeof(BeatTimes[0]));
        if (!IsActive() || m_Phase >= BeatCount ||
            m_Timer < BeatTimes[m_Phase])
        {
            return -1;
        }

        return m_Phase++;
    }

    void CompleteIfElapsed() noexcept
    {
        if (m_Timer >= Duration)
        {
            m_Timer = -1.0f;
        }
    }

    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    bool IsActive() const noexcept { return m_Timer >= 0.0f; }
    float GetTimer() const noexcept { return m_Timer; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }

    float GetAtmosphereIntensity() const noexcept
    {
        return 1.0f - (std::min)(m_Timer / Duration, 1.0f);
    }
};
