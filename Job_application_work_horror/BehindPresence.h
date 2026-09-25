// ============================================================================
// ファイルの役割: 2面の「背後の気配」の出現間隔と、見られた・追いつかれたの判定を管理します。
// 主な技術: 有限状態機械、描画・入力に依存しない進行ロジック
// どこに出すか、プレイヤーが見ているか、距離はいくつかはSceneが計算して渡します。
// ============================================================================

#pragma once

#include <algorithm>

class BehindPresence final
{
public:
    enum class State
    {
        Waiting,    // 次の出現まで待っている
        Following   // プレイヤーの背後から近づいている
    };

    enum class Event
    {
        None,
        Spawn,      // このフレームで背後に出現させる
        Seen,       // 振り向いて見られたので消す
        Reached,    // 気づかれないまま背後まで近づいた
        Vanished    // 一定時間たっても気づかれなかった、または出現できない状況になった
    };

    static constexpr float MinInterval = 18.0f;
    static constexpr float MaxInterval = 34.0f;
    static constexpr float MaxFollowSeconds = 24.0f;
    static constexpr float ReachDistance = 16.0f;
    // 出現した瞬間に視界の端で見つかっても、すぐ消えないようにする猶予です。
    static constexpr float SeenGraceSeconds = 0.35f;

private:
    State m_State = State::Waiting;
    float m_WaitTimer = MaxInterval;
    float m_FollowTime = 0.0f;

    Event ReturnToWaiting(Event event, float nextIntervalRoll) noexcept
    {
        m_State = State::Waiting;
        m_FollowTime = 0.0f;
        const float roll = (std::clamp)(nextIntervalRoll, 0.0f, 1.0f);
        m_WaitTimer = MinInterval + (MaxInterval - MinInterval) * roll;
        return event;
    }

public:
    void Reset(float firstDelay) noexcept
    {
        m_State = State::Waiting;
        m_WaitTimer = (std::max)(firstDelay, 0.0f);
        m_FollowTime = 0.0f;
    }

    // 出現位置が背後に取れなかったときなど、今回の出現を取りやめて少し後に再挑戦します。
    void Postpone(float seconds) noexcept
    {
        m_State = State::Waiting;
        m_FollowTime = 0.0f;
        m_WaitTimer = (std::max)(seconds, 0.0f);
    }

    // nextIntervalRollは0〜1の乱数で、次の出現までの間隔を決めるために使います。
    Event Update(
        float deltaTime,
        bool spawnAllowed,
        bool playerLooking,
        float distanceToPlayer,
        float nextIntervalRoll) noexcept
    {
        if (m_State == State::Waiting)
        {
            m_WaitTimer = (std::max)(0.0f, m_WaitTimer - deltaTime);
            if (m_WaitTimer > 0.0f || !spawnAllowed)
            {
                return Event::None;
            }
            m_State = State::Following;
            m_FollowTime = 0.0f;
            return Event::Spawn;
        }

        if (!spawnAllowed)
        {
            return ReturnToWaiting(Event::Vanished, nextIntervalRoll);
        }

        m_FollowTime += deltaTime;
        if (playerLooking && m_FollowTime >= SeenGraceSeconds)
        {
            return ReturnToWaiting(Event::Seen, nextIntervalRoll);
        }
        if (distanceToPlayer <= ReachDistance)
        {
            return ReturnToWaiting(Event::Reached, nextIntervalRoll);
        }
        if (m_FollowTime >= MaxFollowSeconds)
        {
            return ReturnToWaiting(Event::Vanished, nextIntervalRoll);
        }
        return Event::None;
    }

    State GetState() const noexcept { return m_State; }
    bool IsFollowing() const noexcept { return m_State == State::Following; }
};
