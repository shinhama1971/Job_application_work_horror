// ============================================================================
// ファイルの役割: 2面の時計異変に固有の針角度、観察状態、通知時間を管理します。
// 描画Objectへの反映とゲーム進行は Stage2Scene が引き続き担当します。
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>

class ClockAnomaly final
{
private:
    float m_HourAngle = 0.42f;
    float m_MinuteAngle = -0.78f;
    float m_NoticeTimer = 0.0f;
    bool m_ObservedThisLoop = false;

public:
    void Reset() noexcept
    {
        m_HourAngle = 0.42f;
        m_MinuteAngle = -0.78f;
        m_NoticeTimer = 0.0f;
        m_ObservedThisLoop = false;
    }

    void ConfigureForLoop(int loopCount) noexcept
    {
        m_ObservedThisLoop = false;
        if (loopCount == 1)
        {
            m_HourAngle = 1.18f;
            m_MinuteAngle = -2.34f;
        }
        else if (loopCount == 2)
        {
            m_HourAngle = -0.62f;
            m_MinuteAngle = 2.72f;
        }
        else if (loopCount >= 3)
        {
            m_HourAngle = 3.14159265f;
            m_MinuteAngle = 3.14159265f;
        }
    }

    void Update(int loopCount, float deltaTime) noexcept
    {
        if (loopCount == 0)
        {
            m_MinuteAngle += deltaTime * 0.035f;
            m_HourAngle += deltaTime * 0.0029f;
        }
        else if (loopCount == 2)
        {
            m_MinuteAngle -= deltaTime * 0.82f;
            m_HourAngle -= deltaTime * 0.068f;
        }
    }

    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    float GetHourAngle() const noexcept { return m_HourAngle; }
    float GetMinuteAngle() const noexcept { return m_MinuteAngle; }

    float GetDisplayedHourAngle(int loopCount) const noexcept
    {
        return loopCount == 2 ? QuantizeAngle(m_HourAngle) : m_HourAngle;
    }

    float GetDisplayedMinuteAngle(int loopCount) const noexcept
    {
        return loopCount == 2 ? QuantizeAngle(m_MinuteAngle) : m_MinuteAngle;
    }

    bool WasObservedThisLoop() const noexcept { return m_ObservedThisLoop; }

    void MarkObserved() noexcept
    {
        m_ObservedThisLoop = true;
        m_NoticeTimer = 2.6f;
    }

    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
    void ClearNotice() noexcept { m_NoticeTimer = 0.0f; }

private:
    static float QuantizeAngle(float angle) noexcept
    {
        constexpr float clockStep = 0.105f;
        return std::floor(angle / clockStep) * clockStep;
    }
};
