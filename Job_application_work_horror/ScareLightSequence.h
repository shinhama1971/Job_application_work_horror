// ============================================================================
// ファイルの役割: 1面の、照明が次々に明滅していく演出について、時間と段階を管理している。
// 主な技術: 有限状態機械、時間による制御、点光源の演出、音とのタイミング合わせ
// 照明・PostProcess・振動への命令は StageScene が担当している。
// ============================================================================

#pragma once

// 演出の始まりから決まった時刻（0.45・0.90・1.38・1.92・2.65秒）に、段階を1つずつ起こしている。
class ScareLightSequence final
{
private:
    // 演出の経過秒（負なら演出していない）、通知の残り秒数、次に起こす段階（-1なら始まっていない）
    float m_Timer = -1.0f;
    float m_NoticeTimer = 0.0f;
    int m_Phase = -1;

public:
    // 演出していない状態に戻している
    void Reset() noexcept
    {
        m_Timer = -1.0f;
        m_NoticeTimer = 0.0f;
        m_Phase = -1;
    }

    // 演出を始め、3.8秒の通知を出している
    void Start() noexcept
    {
        m_Timer = 0.0f;
        m_NoticeTimer = 3.8f;
        m_Phase = 0;
    }

    // 途中で演出をやめている（通知はそのまま）
    void Cancel() noexcept
    {
        m_Timer = -1.0f;
        m_Phase = -1;
    }

    // 通知を消している
    void ClearNotice() noexcept { m_NoticeTimer = 0.0f; }

    // 通知の残り秒数を減らしている
    void UpdateNotice(float deltaTime) noexcept
    {
        if (m_NoticeTimer > 0.0f)
        {
            m_NoticeTimer -= deltaTime;
        }
    }

    // 止めた後に呼ばれても段階-1のまま勝手に再開しないよう、演出中だけ時間を進めている。
    void Advance(float deltaTime) noexcept
    {
        if (IsActive())
        {
            m_Timer += deltaTime;
        }
    }

    // 次の段階の時刻が来ていれば、その段階の番号を返して次へ進めている（まだなら-1）。最後の段階で演出を終えている
    int ConsumePendingBeat() noexcept
    {
        constexpr float BeatTimes[] = { 0.45f, 0.90f, 1.38f, 1.92f, 2.65f };
        constexpr int BeatCount =
            static_cast<int>(sizeof(BeatTimes) / sizeof(BeatTimes[0]));
        if (!IsActive() || m_Phase >= BeatCount ||
            m_Timer < BeatTimes[m_Phase])
        {
            return -1;
        }

        const int beat = m_Phase++;
        if (m_Phase >= BeatCount)
        {
            m_Timer = -1.0f;
        }
        return beat;
    }

    // 演出中か、通知の残り秒数を返している
    bool IsActive() const noexcept { return m_Timer >= 0.0f; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
