// ============================================================================
// ファイルの役割: 2面の信号パズルの入力順序と完了状態だけを管理します。
// ============================================================================

#pragma once

#include <array>

class SignalPuzzle final
{
public:
    enum class AcceptResult
    {
        WrongOrder,
        Accepted,
        Completed
    };

private:
    std::array<bool, 3> m_Accepted{ false, false, false };
    int m_Step = 0;
    bool m_Complete = false;

public:
    void Reset()
    {
        m_Accepted.fill(false);
        m_Step = 0;
        m_Complete = false;
    }

    void ForceComplete()
    {
        m_Accepted.fill(true);
        m_Step = 3;
        m_Complete = true;
    }

    AcceptResult Accept(int signalIndex)
    {
        if (signalIndex < 0 || signalIndex >= 3 || signalIndex != m_Step)
        {
            Reset();
            return AcceptResult::WrongOrder;
        }

        m_Accepted[signalIndex] = true;
        ++m_Step;
        if (m_Step >= 3)
        {
            m_Complete = true;
            return AcceptResult::Completed;
        }
        return AcceptResult::Accepted;
    }

    bool IsAccepted(int signalIndex) const
    {
        return signalIndex >= 0 && signalIndex < 3 &&
            m_Accepted[signalIndex];
    }

    int GetStep() const { return m_Step; }
    bool IsComplete() const { return m_Complete; }
};
