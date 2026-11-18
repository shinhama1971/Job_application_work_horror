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
public:
    // 知らせの種類（目的表示の文章を選ぶのに使う。SelectStage2Objective）
    static constexpr int NoFeedback = 0;
    static constexpr int FalseDoorNeedsLight = 1;     // 偽の扉をライトを消したまま見た
    static constexpr int ClockNeedsDark = 2;          // 時計をライトを点けたまま見た
    static constexpr int SignalWrongOrder = 3;        // 信号盤の順番が違う
    static constexpr int SignalNoiseReset = 4;        // 走る音で危険度が最大になり、信号盤がやり直しになった
    static constexpr int SignalStalkerCaught = 5;     // 信号盤の間に来る影に追いつかれた
    static constexpr int SignalStalkerRepelled = 6;   // 信号盤の間に来る影をライトで追い払った
    // 間違えたときの知らせを出す秒数と、数える間違いの回数の上限（この回数で一番強く知らせる）
    static constexpr float MistakeNoticeSeconds = 2.2f;
    static constexpr int MaxMistakeCount = 3;

private:
    // 知らせの残り秒数、知らせの種類（0はなし）、間違えた回数（3回まで数えている）
    float m_Timer = 0.0f;
    int m_Type = NoFeedback;
    int m_MistakeCount = 0;

public:
    // 何も出していない状態に戻している
    void Reset() noexcept
    {
        m_Timer = 0.0f;
        m_Type = NoFeedback;
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
        m_Timer = MistakeNoticeSeconds;
        m_MistakeCount = (std::min)(m_MistakeCount + 1, MaxMistakeCount);
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
        m_Type = NoFeedback;
        m_Timer = 0.0f;
    }

    // 知らせを出しているか、その種類、間違えた回数を返している
    bool IsVisible() const noexcept { return m_Timer > 0.0f; }
    int GetType() const noexcept { return m_Type; }
    int GetMistakeCount() const noexcept { return m_MistakeCount; }
};
