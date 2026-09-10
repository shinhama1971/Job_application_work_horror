// ============================================================================
// ファイルの役割: 2面の騒音危険度と、それに付随する通知時間を保持します。
// 敵の生成、演出、音声などのゲーム進行は Stage2Scene 側が担当します。
// ============================================================================

#pragma once

#include <algorithm>

class NoiseThreatSystem final
{
private:
    float m_Threat = 0.0f;
    float m_EventCooldown = 0.0f;
    float m_WarningTimer = 0.0f;
    float m_WetStepNoticeTimer = 0.0f;
    float m_StalkerCooldown = 0.0f;
    float m_StalkerNoticeTimer = 0.0f;

public:
    void Reset() noexcept
    {
        m_Threat = 0.0f;
        m_EventCooldown = 0.0f;
        m_WarningTimer = 0.0f;
        m_WetStepNoticeTimer = 0.0f;
        m_StalkerCooldown = 0.0f;
        m_StalkerNoticeTimer = 0.0f;
    }

    // Stage2Scene が従来個別に減算していた5つのタイマーを、同じ順序と式で更新します。
    void UpdateTimers(float deltaTime) noexcept
    {
        m_EventCooldown = (std::max)(0.0f, m_EventCooldown - deltaTime);
        m_WarningTimer = (std::max)(0.0f, m_WarningTimer - deltaTime);
        m_WetStepNoticeTimer = (std::max)(0.0f, m_WetStepNoticeTimer - deltaTime);
        m_StalkerCooldown = (std::max)(0.0f, m_StalkerCooldown - deltaTime);
        m_StalkerNoticeTimer = (std::max)(0.0f, m_StalkerNoticeTimer - deltaTime);
    }

    float GetThreat() const noexcept { return m_Threat; }
    void SetThreat(float value) noexcept { m_Threat = value; }

    float GetEventCooldown() const noexcept { return m_EventCooldown; }
    void SetEventCooldown(float value) noexcept { m_EventCooldown = value; }

    float GetWarningTimer() const noexcept { return m_WarningTimer; }
    void SetWarningTimer(float value) noexcept { m_WarningTimer = value; }

    float GetWetStepNoticeTimer() const noexcept { return m_WetStepNoticeTimer; }
    void SetWetStepNoticeTimer(float value) noexcept { m_WetStepNoticeTimer = value; }

    float GetStalkerCooldown() const noexcept { return m_StalkerCooldown; }
    void SetStalkerCooldown(float value) noexcept { m_StalkerCooldown = value; }

    float GetStalkerNoticeTimer() const noexcept { return m_StalkerNoticeTimer; }
    void SetStalkerNoticeTimer(float value) noexcept { m_StalkerNoticeTimer = value; }

};
