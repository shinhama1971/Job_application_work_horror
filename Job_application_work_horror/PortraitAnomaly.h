// ============================================================================
// ファイルの役割: 2面の肖像画の異変に固有の、見つめた時間・変化したか・通知の状態を管理している。
// 主な技術: 見ているかの条件、見つめ続けた時間の計測、有限状態機械
// 見ているかの判定と、描画するObjectへの演出の反映は Stage2Scene が担当している。
//
// 肖像画は、懐中電灯で照らしたまま見つめ続けると目が開く（偽の扉の「照らして目を離す」、
// 時計の「ライトを消して見る」と違う操作にし、3つの異変で解き方が重ならないようにしている）。
// ============================================================================

#pragma once

#include <algorithm>

class PortraitAnomaly final
{
public:
    // 目が開くまでに見つめ続ける秒数。
    static constexpr float RequiredStareSeconds = 2.4f;

private:
    // 通知の残り秒数、見つめ続けている秒数、この周回で目が開いたか
    float m_NoticeTimer = 0.0f;
    float m_StareSeconds = 0.0f;
    bool m_ChangedThisLoop = false;

public:
    // 何も起きていない状態に戻している
    void Reset() noexcept
    {
        m_NoticeTimer = 0.0f;
        m_StareSeconds = 0.0f;
        m_ChangedThisLoop = false;
    }

    // 周回の始まりは、前と同じく通知の時間はそのままにして、進み具合だけを戻している。
    void ResetProgressForLoop() noexcept
    {
        m_StareSeconds = 0.0f;
        m_ChangedThisLoop = false;
    }

    // 見つめている間は時間をため、目を離すと最初からやり直しにしている。
    // 必要な時間に届いたフレームだけtrueを返している（目が開いたら2.4秒の通知を出している）。
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

    // 通知の残り秒数を減らしている
    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    // 見つめている途中か、見つめた時間の割合（0〜1）、この周回で目が開いたか、通知の残り秒数を返している
    bool IsStaring() const noexcept { return m_StareSeconds > 0.0f; }
    float GetStareRate() const noexcept
    {
        return (std::min)(m_StareSeconds / RequiredStareSeconds, 1.0f);
    }
    bool HasChangedThisLoop() const noexcept { return m_ChangedThisLoop; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
