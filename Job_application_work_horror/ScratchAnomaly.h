// ============================================================================
// ファイルの役割: 2面の引っかき傷異変に固有の更新間隔と発生状態を管理します。
// 傷Objectの表示・発光と恐怖演出は Stage2Scene が担当します。
// ============================================================================

#pragma once

#include <algorithm>

class ScratchAnomaly final
{
private:
    static constexpr float UpdateInterval = 0.05f;

    float m_NoticeTimer = 0.0f;
    float m_UpdateAccumulator = 0.0f;
    bool m_ScareTriggered = false;

public:
    void Reset() noexcept
    {
        m_NoticeTimer = 0.0f;
        m_UpdateAccumulator = 0.0f;
        m_ScareTriggered = false;
    }

    // 従来の周回処理と同じく、発生済みフラグだけを戻します。
    void ResetProgressForLoop() noexcept { m_ScareTriggered = false; }

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

    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    bool WasScareTriggered() const noexcept { return m_ScareTriggered; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
