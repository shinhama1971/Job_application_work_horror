// ============================================================================
// ファイルの役割: 2面の「偽の扉」の異変について、進み具合と見た目の状態を管理している。
// 主な技術: 有限状態機械、物の位置を変える演出、よく見て気づく遊び
// 見ているかの判定、Objectの配置、演出の実行は Stage2Scene が担当している。
// ============================================================================

#pragma once

#include <algorithm>

// 廊下に偽の扉が現れ、ライトで照らして見た後、目を離した隙に反対側（右側）の壁へ移る異変。その状態だけを持っている。
class FalseDoorAnomaly final
{
private:
    // 気づいたときの通知を出す残り秒数、偽の扉を見たか、反対側へ移ったか、表示しているか、右側の壁にあるか
    float m_NoticeTimer = 0.0f;
    bool m_Observed = false;
    bool m_Moved = false;
    bool m_Visible = false;
    bool m_RightSide = false;

public:
    // 何も起きていない状態に戻している
    void Reset() noexcept
    {
        m_NoticeTimer = 0.0f;
        m_Observed = false;
        m_Moved = false;
        m_Visible = false;
        m_RightSide = false;
    }

    // 周回が切り替わるときは、通知の時間は止めずに、進み具合だけを戻している。
    void ResetProgressForLoop() noexcept
    {
        m_Observed = false;
        m_Moved = false;
    }

    // 表示するか、どちらの壁に出すかを決めている
    void SetVisualState(bool visible, bool rightSide) noexcept
    {
        m_Visible = visible;
        m_RightSide = rightSide;
    }

    // 偽の扉を見たことを記録している
    void MarkObserved() noexcept { m_Observed = true; }

    // 偽の扉が右側の壁へ移ったことを記録し、2.8秒間の通知を出している
    void MarkMoved() noexcept
    {
        m_Moved = true;
        m_NoticeTimer = 2.8f;
        SetVisualState(true, true);
    }

    // 通知を出す残り秒数を減らしている
    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    // 見たか、移ったか、表示しているか、右側にあるか、通知の残り秒数を返している
    bool WasObserved() const noexcept { return m_Observed; }
    bool HasMoved() const noexcept { return m_Moved; }
    bool IsVisible() const noexcept { return m_Visible; }
    bool IsOnRightSide() const noexcept { return m_RightSide; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
    // 通知を消している
    void ClearNotice() noexcept { m_NoticeTimer = 0.0f; }
};
