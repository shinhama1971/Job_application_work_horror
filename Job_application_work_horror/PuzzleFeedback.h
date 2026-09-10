// ============================================================================
// ファイルの役割: 2面のパズル失敗回数と画面フィードバック時間を管理します。
// 照明・画面効果・振動への演出命令は Stage2Scene が担当します。
// ============================================================================

#pragma once

#include <algorithm>

class PuzzleFeedback final
{
private:
    float m_Timer = 0.0f;
    int m_Type = 0;
    int m_MistakeCount = 0;

public:
    void Reset() noexcept
    {
        m_Timer = 0.0f;
        m_Type = 0;
        m_MistakeCount = 0;
    }

    void Update(float deltaTime) noexcept
    {
        m_Timer = (std::max)(0.0f, m_Timer - deltaTime);
    }

    bool TryRegisterMistake(int type) noexcept
    {
        if (m_Timer > 0.0f)
        {
            return false;
        }

        m_Type = type;
        m_Timer = 2.2f;
        m_MistakeCount = (std::min)(m_MistakeCount + 1, 3);
        return true;
    }

    void Set(int type, float duration) noexcept
    {
        m_Type = type;
        m_Timer = duration;
    }

    void SetType(int type) noexcept { m_Type = type; }

    void Clear() noexcept
    {
        m_Type = 0;
        m_Timer = 0.0f;
    }

    bool IsVisible() const noexcept { return m_Timer > 0.0f; }
    int GetType() const noexcept { return m_Type; }
    int GetMistakeCount() const noexcept { return m_MistakeCount; }
};
