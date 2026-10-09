// ============================================================================
// ファイルの役割: 2面の「背後の気配」がいつ出るかと、見られた・追いつかれたの判定を管理している。
// 主な技術: 有限状態機械、描画・入力に依存しない進行ロジック（Sceneから値を受け取るだけ）
// どこに出すか、プレイヤーが見ているか、距離はいくつかはSceneが計算して渡している。
// ============================================================================

#pragma once

#include <algorithm>

// プレイヤーの背後に気配（影）を出し、振り向けば消え、気づかずに近づかれると捕まる、という流れを管理している。
class BehindPresence final
{
public:
    enum class State
    {
        Waiting,    // 次の出現まで待っている
        Following   // プレイヤーの背後から近づいている
    };

    // Updateが返す、そのフレームで起きた出来事（Sceneはこれを見て影を出したり消したりしている）
    enum class Event
    {
        None,       // 何も起きていない
        Spawn,      // このフレームで背後に出現させる
        Seen,       // 振り向いて見られたので消す
        Reached,    // 気づかれないまま背後まで近づいた
        Vanished    // 一定時間たっても気づかれなかった、または出現できない状況になった
    };

    // 出現の間隔は18〜34秒の間でランダムに決めている
    static constexpr float MinInterval = 18.0f;
    static constexpr float MaxInterval = 34.0f;
    // 背後についてから、この秒数たっても気づかれなければ消している
    static constexpr float MaxFollowSeconds = 24.0f;
    // この距離まで近づいたら「追いつかれた」としている
    static constexpr float ReachDistance = 16.0f;
    // 出現した瞬間に視界の端で見つかっても、すぐ消えないようにする猶予の秒数。
    static constexpr float SeenGraceSeconds = 0.35f;

private:
    // 今の状態、次の出現までの残り秒数、背後についてからの秒数
    State m_State = State::Waiting;
    float m_WaitTimer = MaxInterval;
    float m_FollowTime = 0.0f;

    // 待ち状態へ戻し、次の出現までの間隔を乱数で決め直している。起きた出来事はそのまま返している。
    Event ReturnToWaiting(Event event, float nextIntervalRoll) noexcept
    {
        m_State = State::Waiting;
        m_FollowTime = 0.0f;
        const float roll = (std::clamp)(nextIntervalRoll, 0.0f, 1.0f);
        m_WaitTimer = MinInterval + (MaxInterval - MinInterval) * roll;
        return event;
    }

public:
    // 最初の出現までの秒数を決めて、待ち状態から始め直している（2面の開始・やり直しで呼んでいる）
    void Reset(float firstDelay) noexcept
    {
        m_State = State::Waiting;
        m_WaitTimer = (std::max)(firstDelay, 0.0f);
        m_FollowTime = 0.0f;
    }

    // 出現位置が背後に取れなかったときなど、今回の出現を取りやめて少し後に再挑戦している。
    void Postpone(float seconds) noexcept
    {
        m_State = State::Waiting;
        m_FollowTime = 0.0f;
        m_WaitTimer = (std::max)(seconds, 0.0f);
    }

    // 1フレーム分進めている。nextIntervalRollは0〜1の乱数で、次の出現までの間隔を決めるのに使っている。
    // spawnAllowedは今出してよいか（演出中や隠れている間はfalse）、playerLookingはプレイヤーが気配の方を見ているか。
    Event Update(
        float deltaTime,
        bool spawnAllowed,
        bool playerLooking,
        float distanceToPlayer,
        float nextIntervalRoll) noexcept
    {
        // 待ち状態：時間が来て、出してよい状況なら出現させている
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

        // 出してはいけない状況になったら、黙って消している
        if (!spawnAllowed)
        {
            return ReturnToWaiting(Event::Vanished, nextIntervalRoll);
        }

        // 見られたら消え、近づききったら捕まえ、長く気づかれなければ消えている
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

    // 今の状態と、背後についているかどうかを返している
    State GetState() const noexcept { return m_State; }
    bool IsFollowing() const noexcept { return m_State == State::Following; }
};
