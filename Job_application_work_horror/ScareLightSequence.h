// ============================================================================
// ファイルの役割: 1面の照明連鎖演出に固有の時間・フェーズ状態を管理します。
// 照明、PostProcess、振動への命令は StageScene が担当します。
// ============================================================================

#pragma once

class ScareLightSequence final
{
private:
    float m_Timer = -1.0f;
    float m_NoticeTimer = 0.0f;
    int m_Phase = -1;

public:
    void Reset() noexcept
    {
        m_Timer = -1.0f;
        m_NoticeTimer = 0.0f;
        m_Phase = -1;
    }

    void Start() noexcept
    {
        m_Timer = 0.0f;
        m_NoticeTimer = 3.8f;
        m_Phase = 0;
    }

    void Cancel() noexcept
    {
        m_Timer = -1.0f;
        m_Phase = -1;
    }

    void ClearNotice() noexcept { m_NoticeTimer = 0.0f; }

    void UpdateNotice(float deltaTime) noexcept
    {
        if (m_NoticeTimer > 0.0f)
        {
            m_NoticeTimer -= deltaTime;
        }
    }

    void Advance(float deltaTime) noexcept { m_Timer += deltaTime; }

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

    bool IsActive() const noexcept { return m_Timer >= 0.0f; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
