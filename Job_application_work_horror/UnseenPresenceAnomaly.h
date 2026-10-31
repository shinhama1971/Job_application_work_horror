// ============================================================================
// ファイルの役割: 2面の「見えない影」の異変に固有の状態（姿の見えない影の位置と歩く向き、すれ違ったか、捕まえたか、通知）を管理している。
// 主な技術: 往復して歩く移動、プレイヤーの動きに引き寄せられる横の動き、すれ違いの判定（相手のどちら側にいたかの入れ替わり）、
//           入力や描画に依存しない状態クラス
// 携帯ラジオの雑音・心拍・照明の演出と、捕まったときの処理は Stage2Scene が担当している。
//
// ・この異変の周回だけ、廊下の奥から、姿の見えない影がゆっくり歩いてくる。目では見えず、近いほど携帯ラジオの雑音
//   （RadioStatic）が強くなることだけが手がかりになる。
// ・すぐそばまで近づかれると捕まる（周回はそのままで入口へ戻る）。雑音が強くなったら、壁際に寄って動かずに待つか、
//   ロッカーに隠れてやり過ごす。捕まらずにすれ違えば、見つけたことになる。
// ・影は廊下の真ん中を歩くが、プレイヤーが動いていると、足音に引き寄せられるように横へ寄ってくる。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <cmath>

class UnseenPresenceAnomaly final
{
public:
    // これより近づかれると捕まる距離、すれ違ったとみなす近さ（水平の距離）
    static constexpr float CatchDistance = 13.0f;
    static constexpr float PassingDistance = 26.0f;
    // 歩く速さ（プレイヤーの歩きより遅い）。廊下の奥と入口の折り返す位置
    static constexpr float WalkSpeed = 13.0f;
    static constexpr float FarZ = 125.0f;
    static constexpr float NearZ = -135.0f;
    // 周回が始まってから歩き出すまでの秒数
    static constexpr float StartDelay = 4.0f;
    // 1フレームの結果（何も起きない・捕まえた・すれ違った）
    enum class Result
    {
        None,
        Caught,
        Passed
    };

private:
    // 影の位置（水平）と歩く向き（-1で入口へ、+1で奥へ）、歩き出すまでの残り秒数、揺れの時間
    float m_X = 0.0f;
    float m_Z = FarZ;
    float m_Direction = -1.0f;
    float m_StartTimer = StartDelay;
    float m_Time = 0.0f;
    // 今動いているか、すれ違いが始まったか（そのときプレイヤーが影の前後どちらにいたか）、見つけたか、通知の残り秒数
    bool m_Active = false;
    bool m_CameClose = false;
    float m_SideWhenClose = 0.0f;
    bool m_Found = false;
    float m_NoticeTimer = 0.0f;

    // 廊下の奥から歩き直させている
    void Restart() noexcept
    {
        m_X = 0.0f;
        m_Z = FarZ;
        m_Direction = -1.0f;
        m_StartTimer = StartDelay;
        m_CameClose = false;
        m_SideWhenClose = 0.0f;
    }

public:
    // 何も起きていない状態に戻している
    void Reset() noexcept
    {
        Restart();
        m_Time = 0.0f;
        m_Active = false;
        m_Found = false;
        m_NoticeTimer = 0.0f;
    }

    // 周回の始まりに、影を廊下の奥へ戻している
    void ResetProgressForLoop() noexcept
    {
        Restart();
        m_Active = false;
        m_Found = false;
    }

    // 1フレーム進めている。enabled はこの周回の異変がこれかどうか。
    // 捕まえた・すれ違ったフレームだけ、その結果を返している。
    Result Update(
        float deltaTime,
        const DirectX::SimpleMath::Vector3& playerPosition,
        bool playerMoving,
        bool playerHiding,
        bool enabled) noexcept
    {
        if (!enabled || m_Found)
        {
            m_Active = false;
            return Result::None;
        }
        if (m_StartTimer > 0.0f)
        {
            m_StartTimer -= deltaTime;
            m_Active = false;
            return Result::None;
        }
        m_Active = true;
        m_Time += deltaTime;

        // 廊下を往復して歩いている（端まで来たら折り返す）
        m_Z += m_Direction * WalkSpeed * deltaTime;
        if (m_Z < NearZ)
        {
            m_Z = NearZ;
            m_Direction = 1.0f;
        }
        else if (m_Z > FarZ)
        {
            m_Z = FarZ;
            m_Direction = -1.0f;
        }

        // 横の動き：ゆっくりふらつき、近くでプレイヤーが動いていると、足音の方へ寄ってくる（止まっていれば、ほとんど寄らない）
        const float wander = std::sin(m_Time * 0.55f) * 6.0f;
        const float alongDistance = std::abs(playerPosition.z - m_Z);
        const float pullRate = alongDistance < 60.0f ? (playerMoving ? 7.0f : 1.2f) : 0.0f;
        const float targetX = alongDistance < 60.0f ? playerPosition.x : wander;
        const float maximumStep = (pullRate > 0.0f ? pullRate : 3.0f) * deltaTime;
        m_X += (std::clamp)(targetX - m_X, -maximumStep, maximumStep);
        m_X = (std::clamp)(m_X, -30.0f, 30.0f);

        const float distance = GetHorizontalDistance(playerPosition);
        if (!playerHiding && distance < CatchDistance)
        {
            // 捕まえた：影は廊下の奥から歩き直す（周回はそのまま）
            Restart();
            return Result::Caught;
        }

        // すれ違いの判定：一度近くまで来た後、プレイヤーが影の前後の反対側へ移り、離れたらすれ違ったことにしている
        const float side = playerPosition.z - m_Z >= 0.0f ? 1.0f : -1.0f;
        if (distance < PassingDistance && !m_CameClose)
        {
            m_CameClose = true;
            m_SideWhenClose = side;
        }
        if (m_CameClose && side != m_SideWhenClose && distance > PassingDistance)
        {
            m_Found = true;
            m_Active = false;
            m_NoticeTimer = 2.8f;
            return Result::Passed;
        }
        // 近くまで来ずに離れた（折り返した）場合は、すれ違いを数え直している
        if (m_CameClose && distance > PassingDistance * 2.0f)
        {
            m_CameClose = false;
        }
        return Result::None;
    }

    // 通知の残り秒数を減らしている
    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(m_NoticeTimer - deltaTime, 0.0f);
    }

    // 影の位置（床の高さ）
    DirectX::SimpleMath::Vector3 GetPosition() const noexcept
    {
        return DirectX::SimpleMath::Vector3(m_X, -99.0f, m_Z);
    }
    // プレイヤーとの水平の距離
    float GetHorizontalDistance(const DirectX::SimpleMath::Vector3& playerPosition) const noexcept
    {
        const float dx = playerPosition.x - m_X;
        const float dz = playerPosition.z - m_Z;
        return std::sqrt(dx * dx + dz * dz);
    }
    // 近さ（0〜1）。携帯ラジオの雑音と心拍に使っている（約110より遠いと0、14より近いと1）
    float GetProximity(const DirectX::SimpleMath::Vector3& playerPosition) const noexcept
    {
        if (!m_Active)
        {
            return 0.0f;
        }
        return 1.0f - (std::clamp)((GetHorizontalDistance(playerPosition) - 14.0f) / 96.0f, 0.0f, 1.0f);
    }
    // すぐそばまで来ているか（目的表示で「動かずにやり過ごす」と出すため）
    bool IsNear(const DirectX::SimpleMath::Vector3& playerPosition) const noexcept
    {
        return m_Active && GetHorizontalDistance(playerPosition) < PassingDistance * 1.6f;
    }
    bool IsActive() const noexcept { return m_Active; }
    bool WasFound() const noexcept { return m_Found; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
