// ============================================================================
// ファイルの役割: 4桁の暗証番号錠の状態（正解の番号・入力中の番号・選択中の桁・失敗回数）を管理します。
// 主な技術: 入力や描画に依存しない純粋な状態クラス
// キー入力の読み取りと画面表示は Stage1KeypadDoor と Hud が担当します。
// ============================================================================

#pragma once

#include <array>

class KeypadLock final
{
public:
    static constexpr int DigitCount = 4;
    using Digits = std::array<int, DigitCount>;

    enum class SubmitResult
    {
        Wrong,
        Correct
    };

    void SetCode(const Digits& code) noexcept
    {
        m_Code = code;
        m_Solved = false;
        m_Mistakes = 0;
        ResetEntry();
    }

    // 入力をやり直せるよう、入力中の番号を0000・左端の桁に戻します。
    void ResetEntry() noexcept
    {
        m_Entered.fill(0);
        m_Cursor = 0;
    }

    // 選択する桁を左右へ動かします。端では反対側へ回り込みます。
    void MoveCursor(int delta) noexcept
    {
        m_Cursor = Wrap(m_Cursor + delta, DigitCount);
    }

    // 選択中の桁の数字を増減します。9の次は0に戻ります。
    void ChangeDigit(int delta) noexcept
    {
        int& digit = m_Entered[static_cast<std::size_t>(m_Cursor)];
        digit = Wrap(digit + delta, 10);
    }

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

    const Digits& GetCode() const noexcept { return m_Code; }
    const Digits& GetEntered() const noexcept { return m_Entered; }
    int GetCursor() const noexcept { return m_Cursor; }
    int GetMistakes() const noexcept { return m_Mistakes; }
    bool IsSolved() const noexcept { return m_Solved; }

private:
    static int Wrap(int value, int count) noexcept
    {
        return ((value % count) + count) % count;
    }

    Digits m_Code{};
    Digits m_Entered{};
    int m_Cursor = 0;
    int m_Mistakes = 0;
    bool m_Solved = false;
};
