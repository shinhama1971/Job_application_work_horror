// ============================================================================
// ファイルの役割: 1面で電力が戻るときと、出口へ送電するときの演出について、時間と段階を管理している。
// 主な技術: 有限状態機械、時間をずらして起こす出来事、照明の制御、音とのタイミング合わせ
// ============================================================================

#pragma once

// 電力が戻った瞬間からの演出（3段階）と、出口の送電盤を操作した後の演出（3段階）を、決まった時刻に1つずつ起こしている。
class StagePowerSequence final
{
private:
    // 前フレームで電力が戻っていたか、電力の演出の経過秒と次の段階、出口の演出の経過秒と次の段階、出口の演出が終わったか
    bool m_WasPowerRestored = false;
    float m_RestoreTimer = -1.0f;
    int m_RestorePhase = -1;
    float m_ExitTimer = -1.0f;
    int m_ExitPhase = -1;
    bool m_ExitComplete = false;

public:
    // 演出していない状態に戻している
    void Reset() noexcept
    {
        m_WasPowerRestored = false;
        m_RestoreTimer = -1.0f;
        m_RestorePhase = -1;
        m_ExitTimer = -1.0f;
        m_ExitPhase = -1;
        m_ExitComplete = false;
    }

    // 電力の状態を毎フレーム受け取り、戻った瞬間に演出を始めてtrueを返している
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

    // 電力の演出の時間を進めている
    void AdvanceRestore(float deltaTime) noexcept { m_RestoreTimer += deltaTime; }

    // 電力の演出の次の段階（0.38・1.15・2.25秒）が来ていれば、その番号を返している（まだなら-1）
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

    // 出口の演出をまだ始めていなければ始め、trueを返している
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

    // 出口の演出の時間を進めている
    void AdvanceExit(float deltaTime) noexcept { m_ExitTimer += deltaTime; }

    // 出口の演出の次の段階（0.28・0.78・1.30秒）が来ていれば番号を返し、最後の段階で終えている
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

    // 電力の演出中か、その経過秒、出口の演出が終わったかを返している
    bool IsRestoreActive() const noexcept { return m_RestoreTimer >= 0.0f; }
    float GetRestoreTimer() const noexcept { return m_RestoreTimer; }
    bool IsExitComplete() const noexcept { return m_ExitComplete; }
};
