// ============================================================================
// ファイルの役割: 1面の電力復旧と出口通電に固有の時間・フェーズ状態を管理します。
// ============================================================================

#pragma once

class StagePowerSequence final
{
private:
    bool m_WasPowerRestored = false;
    float m_RestoreTimer = -1.0f;
    int m_RestorePhase = -1;
    float m_ExitTimer = -1.0f;
    int m_ExitPhase = -1;
    bool m_ExitComplete = false;

public:
    void Reset() noexcept
    {
        m_WasPowerRestored = false;
        m_RestoreTimer = -1.0f;
        m_RestorePhase = -1;
        m_ExitTimer = -1.0f;
        m_ExitPhase = -1;
        m_ExitComplete = false;
    }

    bool ObservePowerState(bool restored) noexcept
    {
        const bool started = restored && !m_WasPowerRestored;
        if (started)
        {
            m_RestoreTimer = 0.0f;
            m_RestorePhase = 0;
        }
        m_WasPowerRestored = restored;
        return started;
    }

    void AdvanceRestore(float deltaTime) noexcept { m_RestoreTimer += deltaTime; }

    int ConsumeRestoreBeat() noexcept
    {
        constexpr float BeatTimes[] = { 0.38f, 1.15f, 2.25f };
        constexpr int BeatCount =
            static_cast<int>(sizeof(BeatTimes) / sizeof(BeatTimes[0]));
        if (m_RestoreTimer < 0.0f || m_RestorePhase < 0 ||
            m_RestorePhase >= BeatCount ||
            m_RestoreTimer < BeatTimes[m_RestorePhase])
        {
            return -1;
        }
        return m_RestorePhase++;
    }

    bool BeginExitIfNeeded() noexcept
    {
        if (m_ExitTimer >= 0.0f)
        {
            return false;
        }
        m_ExitTimer = 0.0f;
        m_ExitPhase = 0;
        return true;
    }

    void AdvanceExit(float deltaTime) noexcept { m_ExitTimer += deltaTime; }

    int ConsumeExitBeat() noexcept
    {
        constexpr float BeatTimes[] = { 0.28f, 0.78f, 1.30f };
        constexpr int BeatCount =
            static_cast<int>(sizeof(BeatTimes) / sizeof(BeatTimes[0]));
        if (m_ExitComplete || m_ExitTimer < 0.0f || m_ExitPhase < 0 ||
            m_ExitPhase >= BeatCount || m_ExitTimer < BeatTimes[m_ExitPhase])
        {
            return -1;
        }
        const int beat = m_ExitPhase++;
        if (m_ExitPhase >= BeatCount)
        {
            m_ExitComplete = true;
        }
        return beat;
    }

    bool IsRestoreActive() const noexcept { return m_RestoreTimer >= 0.0f; }
    float GetRestoreTimer() const noexcept { return m_RestoreTimer; }
    bool IsExitComplete() const noexcept { return m_ExitComplete; }
};
