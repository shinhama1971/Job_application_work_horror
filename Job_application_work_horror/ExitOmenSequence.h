// ============================================================================
// ファイルの役割: 1面の出口の手前で起きる前兆の演出について、時間と段階を管理している。
// 主な技術: 有限状態機械、タイマー、照明・音・画面効果のタイミング合わせ
// ============================================================================

#pragma once

#include <algorithm>

// 出口の近くまで来たときの前兆（背後の照明が消え、出口の照明が故障して明滅する）を、決まった時刻に1つずつ起こしている。
// 実際に照明を消したり画面を揺らしたりするのはStageSceneが担当している。
class ExitOmenSequence final
{
private:
    // 演出を始めたか、残り秒数、次に起こす段階（-1なら始まっていない）
    bool m_Triggered = false;
    float m_Timer = 0.0f;
    int m_Phase = -1;

public:
    // 始まる前の状態に戻している
    void Reset() noexcept
    {
        m_Triggered = false;
        m_Timer = 0.0f;
        m_Phase = -1;
    }

    // 演出を始めている（3.2秒の演出）
    void Start() noexcept
    {
        m_Triggered = true;
        m_Timer = 3.2f;
        m_Phase = 0;
    }

    // 残り秒数を減らしている
    void Update(float deltaTime) noexcept
    {
        m_Timer = (std::max)(0.0f, m_Timer - deltaTime);
    }

    // 次の段階の時刻が来ていれば、その段階の番号を返して次へ進めている（まだなら-1）。
    // 残り2.45秒で段階0（背後の照明が消える）、残り1.35秒で段階1（出口の照明が故障する）
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

    // 演出を始めたか、残り秒数を返している
    bool IsTriggered() const noexcept { return m_Triggered; }
    float GetTimer() const noexcept { return m_Timer; }
};
