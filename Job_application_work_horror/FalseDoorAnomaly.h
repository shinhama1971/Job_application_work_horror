// ============================================================================
// ファイルの役割: 2面の偽ドア異変に固有の進行状態と表示状態を管理します。
// 視線判定、Object配置、演出の実行は Stage2Scene が担当します。
// ============================================================================

#pragma once

#include <algorithm>

class FalseDoorAnomaly final
{
private:
    float m_NoticeTimer = 0.0f;
    bool m_Observed = false;
    bool m_Moved = false;
    bool m_Visible = false;
    bool m_RightSide = false;

public:
    void Reset() noexcept
    {
        m_NoticeTimer = 0.0f;
        m_Observed = false;
        m_Moved = false;
        m_Visible = false;
        m_RightSide = false;
    }

    // 周回切り替え時は、従来どおり通知タイマーを止めずに進行だけ戻します。
    void ResetProgressForLoop() noexcept
    {
        m_Observed = false;
        m_Moved = false;
    }

    void SetVisualState(bool visible, bool rightSide) noexcept
    {
        m_Visible = visible;
        m_RightSide = rightSide;
    }

    void MarkObserved() noexcept { m_Observed = true; }

    void MarkMoved() noexcept
    {
        m_Moved = true;
        m_NoticeTimer = 2.8f;
        SetVisualState(true, true);
    }

    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    bool WasObserved() const noexcept { return m_Observed; }
    bool HasMoved() const noexcept { return m_Moved; }
    bool IsVisible() const noexcept { return m_Visible; }
    bool IsOnRightSide() const noexcept { return m_RightSide; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
    void ClearNotice() noexcept { m_NoticeTimer = 0.0f; }
};
