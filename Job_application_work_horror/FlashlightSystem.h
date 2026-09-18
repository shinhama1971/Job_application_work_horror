// ============================================================================
// ファイルの役割: 懐中電灯のON/OFF、電池消費、低残量通知を管理します。
// 主な技術: DeltaTime、電池状態、低電圧フリッカー、光量補間
// 光の色・範囲・描画はPlayer側に残し、見た目への依存を持ちません。
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>

class FlashlightSystem final
{
public:
    struct FrameState
    {
        bool visible = false;
        bool render = false;
        bool voltageDropStarted = false;
        float output = 1.0f;
        float batteryStress = 0.0f;
        float powerBlend = 0.0f;
    };

private:
    static constexpr float MaxBattery = 100.0f;
    static constexpr float ConsumptionPerSecond = 1.2f;

    bool m_IsOn = true;
    float m_Battery = MaxBattery;
    float m_BatteryNoticeTimer = 0.0f;
    float m_PowerBlend = 1.0f;
    int m_LowBatteryWarningLevel = 0;
    float m_FlickerTime = 0.0f;
    bool m_WasVoltageDrop = false;

public:
    void InitializeRuntimeNotifications()
    {
        m_BatteryNoticeTimer = 0.0f;
        m_LowBatteryWarningLevel = 0;
        m_PowerBlend = m_IsOn ? 1.0f : 0.0f;
        m_FlickerTime = 0.0f;
        m_WasVoltageDrop = false;
    }

    void TickNotice(float deltaTime)
    {
        m_BatteryNoticeTimer = (std::max)(
            0.0f, m_BatteryNoticeTimer - deltaTime);
    }

    bool Toggle()
    {
        if (m_Battery <= 0.0f)
        {
            return false;
        }
        m_IsOn = !m_IsOn;
        return true;
    }

    // 戻り値は、このフレームで新しく到達した警告段階です。0は通知なしです。
    int UpdateBattery(float deltaTime)
    {
        if (m_IsOn)
        {
            m_Battery -= ConsumptionPerSecond * deltaTime;
            if (m_Battery <= 0.0f)
            {
                m_Battery = 0.0f;
                m_IsOn = false;
            }
        }

        const int warningLevel = m_Battery <= 10.0f
            ? 2
            : (m_Battery <= 20.0f ? 1 : 0);
        if (warningLevel > m_LowBatteryWarningLevel)
        {
            m_LowBatteryWarningLevel = warningLevel;
            return warningLevel;
        }
        if (m_Battery > 25.0f)
        {
            m_LowBatteryWarningLevel = 0;
        }
        return 0;
    }

    // 電池状態から、このフレームの光量と低電圧フリッカーを算出します。
    FrameState UpdateFrameState(float deltaTime)
    {
        FrameState state{};
        state.visible = m_IsOn;

        const float blendTarget = state.visible ? 1.0f : 0.0f;
        const float responseAt60Fps = state.visible ? 0.22f : 0.34f;
        const float blendResponse = 1.0f - std::pow(
            1.0f - responseAt60Fps, deltaTime * 60.0f);
        m_PowerBlend += (blendTarget - m_PowerBlend) * blendResponse;
        if (m_PowerBlend < 0.002f)
        {
            m_PowerBlend = 0.0f;
        }
        state.powerBlend = m_PowerBlend;
        state.render = state.visible || m_PowerBlend > 0.002f;

        bool voltageDrop = false;
        if (m_IsOn && m_Battery <= 20.0f)
        {
            m_FlickerTime += deltaTime;
            state.batteryStress = (20.0f - m_Battery) / 20.0f;

            // 異なる周期を重ね、規則的すぎない電圧揺らぎにします。
            const float flickerTime = m_FlickerTime;
            const float slowVoltage = std::sin(
                flickerTime * 7.1f + std::sin(flickerTime * 1.7f) * 1.8f);
            const float ballastNoise =
                std::sin(flickerTime * 13.7f) *
                std::sin(flickerTime * 4.3f);
            const float unstableOutput =
                0.93f + slowVoltage * 0.025f + ballastNoise * 0.015f;
            state.output = 1.0f +
                (unstableOutput - 1.0f) * state.batteryStress;

            const float dropCycle = 3.7f - state.batteryStress * 1.45f;
            const float dropPhase = std::fmod(flickerTime, dropCycle);
            const float dropDuration =
                0.025f + state.batteryStress * 0.060f;
            if (dropPhase < dropDuration)
            {
                voltageDrop = true;
                state.output *= 0.80f - state.batteryStress * 0.10f;
            }
        }
        else
        {
            m_FlickerTime = 0.0f;
        }

        state.voltageDropStarted = voltageDrop && !m_WasVoltageDrop;
        m_WasVoltageDrop = voltageDrop;
        return state;
    }

    void AddBattery(float value)
    {
        m_Battery += value;
        if (m_Battery > MaxBattery)
        {
            m_Battery = MaxBattery;
        }
        m_BatteryNoticeTimer = 2.2f;
        m_LowBatteryWarningLevel = m_Battery <= 20.0f ? 1 : 0;
    }

    bool IsOn() const { return m_IsOn; }
    float GetBattery() const { return m_Battery; }
    float GetBatteryNoticeTimer() const { return m_BatteryNoticeTimer; }
};
