// ============================================================================
// ファイルの役割: 2面のパズルで間違えた回数と、画面に出す知らせの時間を管理している。
// 主な技術: 出来事をきっかけにした処理、タイマー、ほかの仕組みとの連携
// 照明・画面効果・振動への演出の命令は Stage2Scene が担当している。
// ============================================================================

#pragma once

#include <algorithm>

// 間違えたとき・ヒントを出すときに、画面へ出す知らせの種類と残り時間を持っている。
class PuzzleFeedback final
{
private:
    // 知らせの残り秒数、知らせの種類（0はなし）、間違えた回数（3回まで数えている）
    float m_Timer = 0.0f;
    int m_Type = 0;
    int m_MistakeCount = 0;

public:
    // 何も出していない状態に戻している
    void Reset() noexcept
    {
        m_Timer = 0.0f;
        m_Type = 0;
        m_MistakeCount = 0;
    }

    // 知らせの残り秒数を減らしている
    void Update(float deltaTime) noexcept
    {
        m_Timer = (std::max)(0.0f, m_Timer - deltaTime);
    }

    // 間違いを記録して2.2秒の知らせを出している。知らせを出している間の間違いは数えずfalseを返している（続けて数えないため）
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

    // 知らせの種類と表示する秒数を直接決めている
    void Set(int type, float duration) noexcept
    {
        m_Type = type;
        m_Timer = duration;
    }

    // 知らせの種類だけを変えている
    void SetType(int type) noexcept { m_Type = type; }

    // 知らせを消している
    void Clear() noexcept
    {
        m_Type = 0;
        m_Timer = 0.0f;
    }

    // 知らせを出しているか、その種類、間違えた回数を返している
    bool IsVisible() const noexcept { return m_Timer > 0.0f; }
    int GetType() const noexcept { return m_Type; }
    int GetMistakeCount() const noexcept { return m_MistakeCount; }
};
