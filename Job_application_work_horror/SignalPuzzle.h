// ============================================================================
// ファイルの役割: 2面の信号盤パズルで、入力の順番と、解けたかどうかだけを管理している。
// 主な技術: 有限状態機械、入力の順番の照合、間違えたら最初からやり直す仕組み
// ============================================================================

#pragma once

#include <array>

// 青・黄・赤の3つの信号盤を、決まった順番（0→1→2）で操作するパズル。
class SignalPuzzle final
{
public:
    // 操作した結果（順番が違う・受け付けた・全部そろって解けた）
    enum class AcceptResult
    {
        WrongOrder,
        Accepted,
        Completed
    };

private:
    // 各信号盤を受け付けたか、次に操作すべき番号、解けたか
    std::array<bool, 3> m_Accepted{ false, false, false };
    int m_Step = 0;
    bool m_Complete = false;

public:
    // 最初からやり直している
    void Reset()
    {
        m_Accepted.fill(false);
        m_Step = 0;
        m_Complete = false;
    }

    // デバッグ用などで、解けた状態にしている
    void ForceComplete()
    {
        m_Accepted.fill(true);
        m_Step = 3;
        m_Complete = true;
    }

    // 信号盤を操作している。順番どおりなら受け付け、違えば最初からやり直している
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

    // その信号盤を受け付けたか、次の番号、解けたかを返している
    bool IsAccepted(int signalIndex) const
    {
        return signalIndex >= 0 && signalIndex < 3 &&
            m_Accepted[signalIndex];
    }

    int GetStep() const { return m_Step; }
    bool IsComplete() const { return m_Complete; }
};
