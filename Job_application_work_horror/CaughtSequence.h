// ============================================================================
// ファイルの役割: 2面で捕まった後の暗転・復帰のタイミングと、捕まった理由を管理している。
// 主な技術: 有限状態機械、タイマー、補間（暗転の進み具合をポストエフェクトへ渡している）
// プレイヤー・敵・扉・演出への命令は Stage2Scene が担当し、このクラスは時間と状態だけを持っている。
// ============================================================================

#pragma once

#include <algorithm>

class CaughtSequence final
{
public:
    // 何に捕まったか（復帰のしかたと、表示する文章が変わる）
    enum class Reason
    {
        // 最後の追跡で影に追いつかれた
        FinalPursuit,
        // 足音を聞きつけて追ってくる影に捕まった
        NoiseStalker
    };

private:
    // 捕まってから復帰を始めるまでの秒数と、画面が暗くなりきるまでの秒数
    static constexpr float RecoveryTime = 1.15f;
    static constexpr float FadeTime = 0.34f;

    // 捕まってからの経過秒（負なら捕まっていない）と、捕まった理由
    float m_Timer = -1.0f;
    Reason m_Reason = Reason::FinalPursuit;

public:
    // 捕まっていない状態に戻している（2面の開始・やり直しで呼んでいる）
    void Reset() noexcept
    {
        m_Timer = -1.0f;
        m_Reason = Reason::FinalPursuit;
    }

    // 捕まった演出を始めている。すでに演出中なら何もせずfalseを返している（二重に捕まらないように）
    bool Start(Reason reason) noexcept
    {
        if (IsActive())
        {
            return false;
        }

        m_Timer = 0.0f;
        m_Reason = reason;
        return true;
    }

    // 一時停止中などに呼ばれても演出が勝手に始まらないよう、演出中だけ時間を進めている。
    void Advance(float deltaTime) noexcept
    {
        if (IsActive())
        {
            m_Timer += deltaTime;
        }
    }
    // 復帰が終わったので、捕まっていない状態に戻している
    void Complete() noexcept { m_Timer = -1.0f; }

    // 演出中か、復帰を始めてよい時間になったか、足音の影に捕まったのかを返している
    bool IsActive() const noexcept { return m_Timer >= 0.0f; }
    bool IsReadyToRecover() const noexcept { return m_Timer >= RecoveryTime; }
    bool WasNoiseStalker() const noexcept
    {
        return m_Reason == Reason::NoiseStalker;
    }

    // 暗転の進み具合（0〜1）を返している。0.34秒で真っ暗になる
    float GetFadeRate() const noexcept
    {
        return (std::clamp)(m_Timer / FadeTime, 0.0f, 1.0f);
    }
};
