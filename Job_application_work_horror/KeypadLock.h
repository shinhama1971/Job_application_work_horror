// ============================================================================
// ファイルの役割: 4桁の暗証番号の錠の状態（正解の番号・入力中の番号・選んでいる桁・間違えた回数）を管理している。
// 主な技術: 入力や描画に依存しない、状態だけを持つクラス
// キー入力の読み取りと画面の表示は Stage1KeypadDoor と Hud が担当している。
// ============================================================================

#pragma once

#include <array>

// 暗証番号の錠。番号を合わせて決定すると、正解か不正解かを返している。
class KeypadLock final
{
public:
    // 桁の数と、4桁の番号の型
    static constexpr int DigitCount = 4;
    using Digits = std::array<int, DigitCount>;

    // 決定した結果（不正解・正解）
    enum class SubmitResult
    {
        Wrong,
        Correct
    };

    // 正解の番号を決め、解いていない・間違い0回の状態に戻している（番号はプレイごとにランダムに決まる）
    void SetCode(const Digits& code) noexcept
    {
        m_Code = code;
        m_Solved = false;
        m_Mistakes = 0;
        ResetEntry();
    }

    // 入力をやり直せるよう、入力中の番号を0000・左端の桁に戻している。
    void ResetEntry() noexcept
    {
        m_Entered.fill(0);
        m_Cursor = 0;
    }

    // 選ぶ桁を左右へ動かしている。端では反対側へ回り込む。
    void MoveCursor(int delta) noexcept
    {
        m_Cursor = Wrap(m_Cursor + delta, DigitCount);
    }

    // 選んでいる桁の数字を増減している。9の次は0に戻る。
    void ChangeDigit(int delta) noexcept
    {
        int& digit = m_Entered[static_cast<std::size_t>(m_Cursor)];
        digit = Wrap(digit + delta, 10);
    }

    // 入力中の番号を決定している。正解なら解けた状態にし、不正解なら間違えた回数を増やしている
    SubmitResult Submit() noexcept
    {
        if (m_Entered == m_Code)
        {
            m_Solved = true;
            return SubmitResult::Correct;
        }
        ++m_Mistakes;
        return SubmitResult::Wrong;
    }

    // 正解の番号・入力中の番号・選んでいる桁・間違えた回数・解けたかを返している
    const Digits& GetCode() const noexcept { return m_Code; }
    const Digits& GetEntered() const noexcept { return m_Entered; }
    int GetCursor() const noexcept { return m_Cursor; }
    int GetMistakes() const noexcept { return m_Mistakes; }
    bool IsSolved() const noexcept { return m_Solved; }

private:
    // 0〜count-1の範囲に回り込ませている（負の値も正しく回り込む）
    static int Wrap(int value, int count) noexcept
    {
        return ((value % count) + count) % count;
    }

    // 正解の番号、入力中の番号、選んでいる桁、間違えた回数、解けたか
    Digits m_Code{};
    Digits m_Entered{};
    int m_Cursor = 0;
    int m_Mistakes = 0;
    bool m_Solved = false;
};
