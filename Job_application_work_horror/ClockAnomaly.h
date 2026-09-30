// ============================================================================
// ファイルの役割: 2面の時計異変に固有の針角度、観察状態、通知時間を管理します。
// 主な技術: 有限状態機械、時間補間、観察型ゲームプレイ
// 描画Objectへの反映とゲーム進行は Stage2Scene が引き続き担当します。
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <DirectXMath.h>

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

    // anomalyLoopは「この周回で見つけるべき異変が時計か」です（Stage2AnomalyPlanが決めます）。
    // 時計の周回では針が逆回りし、それ以外の1〜2周目は時計が止まっています。
    void ConfigureForLoop(int loopCount, bool anomalyLoop) noexcept
    {
        m_ObservedThisLoop = false;
        if (loopCount >= 3)
        {
            m_HourAngle = DirectX::XM_PI;
            m_MinuteAngle = DirectX::XM_PI;
        }
        else if (anomalyLoop)
        {
            m_HourAngle = -0.62f;
            m_MinuteAngle = 2.72f;
        }
        else if (loopCount >= 1)
        {
            m_HourAngle = 1.18f;
            m_MinuteAngle = -2.34f;
        }
    }

    void Update(int loopCount, bool anomalyLoop, float deltaTime) noexcept
    {
        if (anomalyLoop)
        {
            m_MinuteAngle -= deltaTime * 0.82f;
            m_HourAngle -= deltaTime * 0.068f;
        }
        else if (loopCount == 0)
        {
            m_MinuteAngle += deltaTime * 0.035f;
            m_HourAngle += deltaTime * 0.0029f;
        }
    }

    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    float GetHourAngle() const noexcept { return m_HourAngle; }
    float GetMinuteAngle() const noexcept { return m_MinuteAngle; }

    // 逆回りしている周回だけ、針を一定の刻みで飛ばして機械が狂った動きにします。
    float GetDisplayedHourAngle(bool anomalyLoop) const noexcept
    {
        return anomalyLoop ? QuantizeAngle(m_HourAngle) : m_HourAngle;
    }

    float GetDisplayedMinuteAngle(bool anomalyLoop) const noexcept
    {
        return anomalyLoop ? QuantizeAngle(m_MinuteAngle) : m_MinuteAngle;
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
