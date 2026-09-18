// ============================================================================
// ファイルの役割: 1面出口前の前兆演出に固有の時間・フェーズ状態を管理します。
// 主な技術: 有限状態機械、タイマー、照明・音・画面効果の同期
// 読み方: 公開関数は外部から使う操作、メンバー変数は保持する状態を表します。
// ============================================================================

#pragma once

#include <algorithm>

class ExitOmenSequence final
{
private:
    bool m_Triggered = false;
    float m_Timer = 0.0f;
    int m_Phase = -1;

public:
    void Reset() noexcept
    {
        m_Triggered = false;
        m_Timer = 0.0f;
        m_Phase = -1;
    }

    void Start() noexcept
    {
        m_Triggered = true;
        m_Timer = 3.2f;
        m_Phase = 0;
    }

    void Update(float deltaTime) noexcept
    {
        m_Timer = (std::max)(0.0f, m_Timer - deltaTime);
    }

    int ConsumePendingBeat() noexcept
    {
        constexpr float RemainingTimes[] = { 2.45f, 1.35f };
        constexpr int BeatCount =
            static_cast<int>(sizeof(RemainingTimes) / sizeof(RemainingTimes[0]));
        if (!m_Triggered || m_Phase < 0 || m_Phase >= BeatCount ||
            m_Timer > RemainingTimes[m_Phase])
        {
            return -1;
        }
        return m_Phase++;
    }

    bool IsTriggered() const noexcept { return m_Triggered; }
    float GetTimer() const noexcept { return m_Timer; }
};
