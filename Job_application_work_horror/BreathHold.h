// ============================================================================
// ファイルの役割: 2面のロッカーに隠れている間の「息を止める」操作の状態（残りの息・止めているか・聞かれそうな時間）を管理している。
// 主な技術: 減っていく値と戻る値（ゲージ）、猶予の時間を持つ判定、入力や描画に依存しない状態クラス
// 入力の読み取り・影の動き・音と捕まる演出は Stage2Scene が担当している。
//
// ・影がロッカーのすぐ近く（DangerDistance 以内）にいる間は、息を止めていないと呼吸を聞かれ、少しの猶予の後に見つかる。
// ・息は止めている間だけ減り（約6秒）、なくなると大きく息を吐いてしまう（影が近くにいれば見つかる）。
// ・止めていない間は、息がゆっくり戻る。
// ============================================================================

#pragma once

#include <algorithm>

class BreathHold final
{
public:
    // 影がこれより近いと、息を止めていないと聞かれる距離
    static constexpr float DangerDistance = 26.0f;
    // 息を止めていられる秒数、息が戻る速さ（1秒あたり）、止めずにいても見つからない猶予の秒数
    static constexpr float HoldSeconds = 6.0f;
    static constexpr float RecoverPerSecond = 0.35f;
    static constexpr float ExposedGraceSeconds = 0.7f;

    // Updateの結果（何も起きていない・息を止め始めた・息が続かず吐いてしまった・呼吸を聞かれた）
    enum class Event
    {
        None,
        StartedHolding,
        Gasped,
        Heard
    };

private:
    // 残りの息（0〜1）、止めているか、影が近いのに止めていない秒数、吐いた後に息を整えている秒数
    float m_Breath = 1.0f;
    bool m_Holding = false;
    float m_ExposedSeconds = 0.0f;
    float m_RecoverLock = 0.0f;

public:
    void Reset() noexcept
    {
        m_Breath = 1.0f;
        m_Holding = false;
        m_ExposedSeconds = 0.0f;
        m_RecoverLock = 0.0f;
    }

    // 1フレーム分進めている。holdInputは息を止めるボタンを押しているか、dangerは影が近いか
    Event Update(float deltaTime, bool holdInput, bool danger) noexcept
    {
        Event result = Event::None;
        m_RecoverLock = (std::max)(0.0f, m_RecoverLock - deltaTime);
        // 吐いてしまった直後は、少しの間止められない
        const bool wantsHold = holdInput && m_RecoverLock <= 0.0f && m_Breath > 0.0f;
        if (wantsHold && !m_Holding)
        {
            result = Event::StartedHolding;
        }
        m_Holding = wantsHold;

        if (m_Holding)
        {
            m_Breath -= deltaTime / HoldSeconds;
            if (m_Breath <= 0.0f)
            {
                m_Breath = 0.0f;
                m_Holding = false;
                m_RecoverLock = 1.2f;
                m_ExposedSeconds = 0.0f;
                return Event::Gasped;
            }
        }
        else
        {
            m_Breath = (std::min)(1.0f, m_Breath + RecoverPerSecond * deltaTime);
        }

        // 影が近いのに息を止めていないと、猶予の後に呼吸を聞かれる
        m_ExposedSeconds = (danger && !m_Holding) ? m_ExposedSeconds + deltaTime : 0.0f;
        if (m_ExposedSeconds >= ExposedGraceSeconds)
        {
            m_ExposedSeconds = 0.0f;
            return Event::Heard;
        }
        return result;
    }

    // 残りの息（0〜1）、止めているか、聞かれそうな度合い（0〜1）
    float GetBreath() const noexcept { return m_Breath; }
    bool IsHolding() const noexcept { return m_Holding; }
    float GetExposure() const noexcept { return (std::min)(m_ExposedSeconds / ExposedGraceSeconds, 1.0f); }
};
