// ============================================================================
// ファイルの役割: 2面で捕獲された後の暗転・復帰タイミングと捕獲理由を管理します。
// プレイヤー、敵、ドア、演出への命令は Stage2Scene が担当します。
// ============================================================================

#pragma once

#include <algorithm>

class CaughtSequence final
{
public:
    enum class Reason
    {
        FinalPursuit,
        NoiseStalker
    };

private:
    static constexpr float RecoveryTime = 1.15f;
    static constexpr float FadeTime = 0.34f;

    float m_Timer = -1.0f;
    Reason m_Reason = Reason::FinalPursuit;

public:
    void Reset() noexcept
    {
        m_Timer = -1.0f;
        m_Reason = Reason::FinalPursuit;
    }

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

    void Advance(float deltaTime) noexcept { m_Timer += deltaTime; }
    void Complete() noexcept { m_Timer = -1.0f; }

    bool IsActive() const noexcept { return m_Timer >= 0.0f; }
    bool IsReadyToRecover() const noexcept { return m_Timer >= RecoveryTime; }
    bool WasNoiseStalker() const noexcept
    {
        return m_Reason == Reason::NoiseStalker;
    }

    float GetFadeRate() const noexcept
    {
        return (std::clamp)(m_Timer / FadeTime, 0.0f, 1.0f);
    }
};
