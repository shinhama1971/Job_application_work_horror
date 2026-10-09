// ============================================================================
// ファイルの役割: 2面の時計の異変に固有の、針の角度・見たかどうか・通知を出す時間を管理している。
// 主な技術: 有限状態機械、時間による角度の変化、「よく見て気づく」遊び
// 描画するObjectへの反映とゲームの進行は Stage2Scene が担当している。
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <DirectXMath.h>

// 2面の廊下の時計。周回ごとに針の位置と動き方を変え、異変の周回では針を逆回りさせている。
class ClockAnomaly final
{
private:
    // 時針と分針の角度（ラジアン）、気づいたときの通知を出す残り秒数、この周回で異変を見たか
    float m_HourAngle = 0.42f;
    float m_MinuteAngle = -0.78f;
    float m_NoticeTimer = 0.0f;
    bool m_ObservedThisLoop = false;

public:
    // 1周目の状態（少しずつ正しく進む時計）に戻している
    void Reset() noexcept
    {
        m_HourAngle = 0.42f;
        m_MinuteAngle = -0.78f;
        m_NoticeTimer = 0.0f;
        m_ObservedThisLoop = false;
    }

    // anomalyLoopは「この周回で見つけるべき異変が時計か」を表している（Stage2AnomalyPlanが決めている）。
    // 時計の周回では針が逆回りし、それ以外の1〜2周目は時計が止まっている。3周目以降は両方の針が真下を指している。
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

    // 針を1フレーム分動かしている：異変の周回は逆回り、最初の周回だけゆっくり正しく進み、それ以外は止まっている
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

    // 通知を出す残り秒数を減らしている
    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    // 針の実際の角度を返している
    float GetHourAngle() const noexcept { return m_HourAngle; }
    float GetMinuteAngle() const noexcept { return m_MinuteAngle; }

    // 逆回りしている周回だけ、針を一定の刻みで飛ばし、機械が狂ったようなカクカクした動きにしている。
    float GetDisplayedHourAngle(bool anomalyLoop) const noexcept
    {
        return anomalyLoop ? QuantizeAngle(m_HourAngle) : m_HourAngle;
    }

    float GetDisplayedMinuteAngle(bool anomalyLoop) const noexcept
    {
        return anomalyLoop ? QuantizeAngle(m_MinuteAngle) : m_MinuteAngle;
    }

    // この周回で時計の異変を見たかを返している
    bool WasObservedThisLoop() const noexcept { return m_ObservedThisLoop; }

    // 異変を見たことを記録し、2.6秒間の通知を出している
    void MarkObserved() noexcept
    {
        m_ObservedThisLoop = true;
        m_NoticeTimer = 2.6f;
    }

    // 通知の残り秒数を返す・通知を消している
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
    void ClearNotice() noexcept { m_NoticeTimer = 0.0f; }

private:
    // 角度を0.105ラジアン（約6度）刻みに切り捨てている
    static float QuantizeAngle(float angle) noexcept
    {
        constexpr float clockStep = 0.105f;
        return std::floor(angle / clockStep) * clockStep;
    }
};
