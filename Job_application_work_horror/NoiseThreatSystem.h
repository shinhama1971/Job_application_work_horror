// ============================================================================
// ファイルの役割: 2面の足音による危険度と、それに関係する通知・待ち時間を持っている。
// 主な技術: 物音の積み重ね、時間による減少、しきい値の判定、敵が反応するための刺激の値
// 敵の生成・演出・音などのゲームの進行は Stage2Scene 側が担当している。
// ============================================================================

#pragma once

#include <algorithm>

// 2面で、プレイヤーの足音（特に水たまりを踏んだ音）がどれだけ影を引き寄せているかを表す値と、各種のタイマー。
class NoiseThreatSystem final
{
private:
    // 危険度（0〜1）、次の物音のイベントまでの待ち、警告を出す残り秒数、水音の足音の通知の残り秒数、
    // 足音を追う影を次に出すまでの待ち、影が来たことの通知の残り秒数
    float m_Threat = 0.0f;
    float m_EventCooldown = 0.0f;
    float m_WarningTimer = 0.0f;
    float m_WetStepNoticeTimer = 0.0f;
    float m_StalkerCooldown = 0.0f;
    float m_StalkerNoticeTimer = 0.0f;

public:
    // 何も起きていない状態に戻している
    void Reset() noexcept
    {
        m_Threat = 0.0f;
        m_EventCooldown = 0.0f;
        m_WarningTimer = 0.0f;
        m_WetStepNoticeTimer = 0.0f;
        m_StalkerCooldown = 0.0f;
        m_StalkerNoticeTimer = 0.0f;
    }

    // Stage2Sceneが前はばらばらに減らしていた5つのタイマーを、同じ順番と式で減らしている。
    void UpdateTimers(float deltaTime) noexcept
    {
        m_EventCooldown = (std::max)(0.0f, m_EventCooldown - deltaTime);
        m_WarningTimer = (std::max)(0.0f, m_WarningTimer - deltaTime);
        m_WetStepNoticeTimer = (std::max)(0.0f, m_WetStepNoticeTimer - deltaTime);
        m_StalkerCooldown = (std::max)(0.0f, m_StalkerCooldown - deltaTime);
        m_StalkerNoticeTimer = (std::max)(0.0f, m_StalkerNoticeTimer - deltaTime);
    }

    // 危険度と各タイマーの値を読み書きしている（増やし方・減らし方の計算はStage2Sceneが行っている）
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
