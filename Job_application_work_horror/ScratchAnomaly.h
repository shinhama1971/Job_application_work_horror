// ============================================================================
// ファイルの役割: 2面の壁の引っかき傷の異変について、表示を更新する間隔と、驚かせる演出が起きたかを管理している。
// 主な技術: 見ているかの条件、周回ごとに増える表現、状態機械
// 傷のObjectの表示・発光と、怖い演出は Stage2Scene が担当している。
// ============================================================================

#pragma once

#include <algorithm>

// 周回が進むほど壁の傷が増え、2周目以降にライトで近くから正面に照らすと、一度だけ驚かせる演出が起きる。
class ScratchAnomaly final
{
private:
    // 傷の表示を更新する間隔（0.05秒）
    static constexpr float UpdateInterval = 0.05f;

    // 通知の残り秒数、更新の間隔を数える値、驚かせる演出がこの周回で起きたか
    float m_NoticeTimer = 0.0f;
    float m_UpdateAccumulator = 0.0f;
    bool m_ScareTriggered = false;

public:
    // 何も起きていない状態に戻している
    void Reset() noexcept
    {
        m_NoticeTimer = 0.0f;
        m_UpdateAccumulator = 0.0f;
        m_ScareTriggered = false;
    }

    // 前の周回の処理と同じく、演出が起きたかどうかだけを戻している。
    void ResetProgressForLoop() noexcept { m_ScareTriggered = false; }

    // 前の更新から0.05秒たったときだけtrueを返している（毎フレームは更新しない）
    bool ConsumeUpdateInterval(float deltaTime) noexcept
    {
        m_UpdateAccumulator += deltaTime;
        if (m_UpdateAccumulator < UpdateInterval)
        {
            return false;
        }

        m_UpdateAccumulator = 0.0f;
        return true;
    }

    // 見せる傷の数：1周目は3本、2周目は9本、それ以降は全部
    int GetVisiblePieceCount(int loopCount, int totalCount) const noexcept
    {
        if (loopCount == 1)
        {
            return 3;
        }
        if (loopCount == 2)
        {
            return 9;
        }
        return totalCount;
    }

    // 2周目以降で、ライトを点けて92より近くから正面（内積0.9超）に傷を照らしたら、一度だけ演出を起こしてtrueを返している
    bool TryTriggerScare(
        int loopCount,
        bool flashlightOn,
        float distance,
        float facing) noexcept
    {
        if (m_ScareTriggered || loopCount < 2 || !flashlightOn ||
            distance >= 92.0f || facing <= 0.90f)
        {
            return false;
        }

        m_ScareTriggered = true;
        m_NoticeTimer = 2.2f;
        return true;
    }

    // 通知の残り秒数を減らしている
    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    // この周回で演出が起きたか、通知の残り秒数を返している
    bool WasScareTriggered() const noexcept { return m_ScareTriggered; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
