// ============================================================================
// ファイルの役割: 2面の肖像画異変に固有の観察・変化・通知状態を管理します。
// 視線判定と描画Objectへの演出反映は Stage2Scene が担当します。
// ============================================================================

#pragma once

#include <algorithm>

class PortraitAnomaly final
{
private:
    float m_NoticeTimer = 0.0f;
    bool m_Observed = false;
    bool m_ChangedThisLoop = false;

public:
    void Reset() noexcept
    {
        m_NoticeTimer = 0.0f;
        m_Observed = false;
        m_ChangedThisLoop = false;
    }

    // 周回開始時は、従来どおり通知時間を維持して進行状態だけを戻します。
    void ResetProgressForLoop() noexcept
    {
        m_Observed = false;
        m_ChangedThisLoop = false;
    }

    void MarkObserved() noexcept { m_Observed = true; }

    void MarkChanged() noexcept
    {
        m_ChangedThisLoop = true;
        m_NoticeTimer = 2.4f;
    }

    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    bool WasObserved() const noexcept { return m_Observed; }
    bool HasChangedThisLoop() const noexcept { return m_ChangedThisLoop; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
