// ============================================================================
// ファイルの役割: 2面の肖像画異変に固有の観察・変化・通知状態を管理します。
// 主な技術: 視線条件、見つめ続けた時間の計測、有限状態機械
// 視線判定と描画Objectへの演出反映は Stage2Scene が担当します。
//
// 肖像画は、懐中電灯で照らしたまま見つめ続けると目が開きます（偽ドアの「照らして目を離す」、
// 時計の「ライトを消して見る」と違う操作にし、3つの異変で解き方が重ならないようにしています）。
// ============================================================================

#pragma once

#include <algorithm>

class PortraitAnomaly final
{
public:
    // 目が開くまでに見つめ続ける秒数です。
    static constexpr float RequiredStareSeconds = 2.4f;

private:
    float m_NoticeTimer = 0.0f;
    float m_StareSeconds = 0.0f;
    bool m_ChangedThisLoop = false;

public:
    void Reset() noexcept
    {
        m_NoticeTimer = 0.0f;
        m_StareSeconds = 0.0f;
        m_ChangedThisLoop = false;
    }

    // 周回開始時は、従来どおり通知時間を維持して進行状態だけを戻します。
    void ResetProgressForLoop() noexcept
    {
        m_StareSeconds = 0.0f;
        m_ChangedThisLoop = false;
    }

    // 見つめている間は時間をため、目を離すと最初からやり直しです。
    // 必要な時間に届いたフレームだけtrueを返します。
    bool UpdateStare(bool staring, float deltaTime) noexcept
    {
        if (m_ChangedThisLoop)
        {
            return false;
        }
        m_StareSeconds = staring ? m_StareSeconds + deltaTime : 0.0f;
        if (m_StareSeconds < RequiredStareSeconds)
        {
            return false;
        }
        m_ChangedThisLoop = true;
        m_NoticeTimer = 2.4f;
        return true;
    }

    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    bool IsStaring() const noexcept { return m_StareSeconds > 0.0f; }
    float GetStareRate() const noexcept
    {
        return (std::min)(m_StareSeconds / RequiredStareSeconds, 1.0f);
    }
    bool HasChangedThisLoop() const noexcept { return m_ChangedThisLoop; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
