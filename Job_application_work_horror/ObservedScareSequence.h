// ============================================================================
// ファイルの役割: 2面で影をじっと見たときに起きる、照明が次々に明滅する演出の時間の進み方を管理している。
// 主な技術: 見ているかの判定と組み合わせた有限状態機械、決まった時刻に起こす演出、タイマー
// 照明・音・PostProcessへの演出の命令は Stage2Scene が担当している。
// ============================================================================

#pragma once

#include <algorithm>

// 影を見てしまったときの1.35秒の演出。決まった時刻（0.05・0.30・0.58・0.90秒）に段階を1つずつ起こしている。
class ObservedScareSequence final
{
private:
    // 演出の長さ（秒）
    static constexpr float Duration = 1.35f;

    // 演出の経過秒（負なら演出していない）、通知の残り秒数、次に起こす段階
    float m_Timer = -1.0f;
    float m_NoticeTimer = 0.0f;
    int m_Phase = 0;

public:
    // 演出していない状態に戻している
    void Reset() noexcept
    {
        m_Timer = -1.0f;
        m_NoticeTimer = 0.0f;
        m_Phase = 0;
    }

    // 演出を始めている。すでに演出中ならfalseを返している（二重に始めない）
    bool Start() noexcept
    {
        if (IsActive())
        {
            return false;
        }

        m_Timer = 0.0f;
        m_NoticeTimer = 2.8f;
        m_Phase = 0;
        return true;
    }

    // 経過秒を進めている
    void Advance(float deltaTime) noexcept { m_Timer += deltaTime; }

    // 1フレームで複数の時刻を越えた場合も、前と同じく1つずつ順番に返している。
    int ConsumePendingBeat() noexcept
    {
        constexpr float BeatTimes[] = { 0.05f, 0.30f, 0.58f, 0.90f };
        constexpr int BeatCount =
            static_cast<int>(sizeof(BeatTimes) / sizeof(BeatTimes[0]));
        if (!IsActive() || m_Phase >= BeatCount ||
            m_Timer < BeatTimes[m_Phase])
        {
            return -1;
        }

        return m_Phase++;
    }

    // 演出の長さを過ぎたら終えている
    void CompleteIfElapsed() noexcept
    {
        if (m_Timer >= Duration)
        {
            m_Timer = -1.0f;
        }
    }

    // 通知の残り秒数を減らしている
    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    // 演出中か、経過秒、通知の残り秒数を返している
    bool IsActive() const noexcept { return m_Timer >= 0.0f; }
    float GetTimer() const noexcept { return m_Timer; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }

    // 画面の雰囲気の強さ（始まりが1で、終わりに向けて0へ下がる）
    float GetAtmosphereIntensity() const noexcept
    {
        return 1.0f - (std::min)(m_Timer / Duration, 1.0f);
    }
};
