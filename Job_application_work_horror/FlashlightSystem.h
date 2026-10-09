// ============================================================================
// ファイルの役割: 懐中電灯のON/OFF、電池の消費、電池が少ないときの通知を管理している。
// 主な技術: 経過時間による電池の消費、電池の状態、電圧が下がったときのちらつき、光の強さのなめらかな変化
// 光の色・範囲・描画はPlayer側に残し、このクラスは見た目に依存しないようにしている。
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>

class FlashlightSystem final
{
public:
    // 1フレーム分の懐中電灯の状態（Playerがこれを見て光を作っている）
    struct FrameState
    {
        // 点いているか
        bool visible = false;
        // 光を描くか（消した直後も、光が弱まりきるまでは描いている）
        bool render = false;
        // このフレームで一瞬暗くなり始めたか（音を鳴らすきっかけに使っている）
        bool voltageDropStarted = false;
        // 光の強さの倍率（電池が少ないと揺れる）
        float output = 1.0f;
        // 電池の少なさ（20%で0、0%で1）
        float batteryStress = 0.0f;
        // 点灯の度合い（0〜1、点けたり消したりするときになめらかに変わる）
        float powerBlend = 0.0f;
    };

private:
    // 電池の最大値（%）と、点けている間に1秒あたり減る量（約83秒で空になる）
    static constexpr float MaxBattery = 100.0f;
    static constexpr float ConsumptionPerSecond = 1.2f;

    // 点いているか、電池の残り、電池を拾ったときの通知の残り秒数、点灯の度合い
    bool m_IsOn = true;
    float m_Battery = MaxBattery;
    float m_BatteryNoticeTimer = 0.0f;
    float m_PowerBlend = 1.0f;
    // 電池が少ない警告をどこまで出したか（0=なし、1=20%以下、2=10%以下）、ちらつきの経過時間、前フレームで暗くなっていたか
    int m_LowBatteryWarningLevel = 0;
    float m_FlickerTime = 0.0f;
    bool m_WasVoltageDrop = false;

public:
    // シーンの始めに、通知とちらつきの状態を初期化している（電池の残りはそのまま）
    void InitializeRuntimeNotifications()
    {
        m_BatteryNoticeTimer = 0.0f;
        m_LowBatteryWarningLevel = 0;
        m_PowerBlend = m_IsOn ? 1.0f : 0.0f;
        m_FlickerTime = 0.0f;
        m_WasVoltageDrop = false;
    }

    // 電池を拾ったときの通知の残り秒数を減らしている
    void TickNotice(float deltaTime)
    {
        m_BatteryNoticeTimer = (std::max)(
            0.0f, m_BatteryNoticeTimer - deltaTime);
    }

    // 点灯・消灯を切り替えている。電池が空なら切り替えずにfalseを返している
    bool Toggle()
    {
        if (m_Battery <= 0.0f)
        {
            return false;
        }
        m_IsOn = !m_IsOn;
        return true;
    }

    // 点けている間は電池を減らし、空になったら消している。戻り値は、このフレームで新しく達した警告の段階（0は通知なし）。
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
        // 電池が25%より多くなったら、警告をもう一度出せるようにしている
        if (m_Battery > 25.0f)
        {
            m_LowBatteryWarningLevel = 0;
        }
        return 0;
    }

    // 電池の状態から、このフレームの光の強さと、電圧が下がったときのちらつきを計算している。
    FrameState UpdateFrameState(float deltaTime)
    {
        FrameState state{};
        state.visible = m_IsOn;

        // 点灯の度合いを目標（点灯なら1、消灯なら0）へ近づけている。60fpsを基準にして、フレームレートが違っても同じ速さにしている
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
        // 電池が20%以下：少ないほど光を不安定に揺らし、ときどき一瞬暗くしている
        if (m_IsOn && m_Battery <= 20.0f)
        {
            m_FlickerTime += deltaTime;
            state.batteryStress = (20.0f - m_Battery) / 20.0f;

            // 周期の違う揺れを重ね、規則的すぎない電圧の揺らぎにしている。
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

            // 一瞬暗くなる間隔：電池が少ないほど短く、暗い時間は長くしている
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

    // 電池を回復している（最大100%）。拾ったことを2.2秒通知し、警告の段階を今の残りに合わせている
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

    // 点いているか、電池の残り、通知の残り秒数を返している
    bool IsOn() const { return m_IsOn; }
    float GetBattery() const { return m_Battery; }
    float GetBatteryNoticeTimer() const { return m_BatteryNoticeTimer; }
};
