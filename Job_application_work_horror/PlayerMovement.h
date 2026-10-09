// ============================================================================
// ファイルの役割: プレイヤーの速度、重力、走る操作、スタミナを更新している。
// 主な技術: 経過時間による計算、なめらかな速度の変化、走る・スタミナ切れの状態、歩くときの頭の揺れ
// 当たり判定と座標の押し戻しはPlayer側に残し、ステージの形には依存していない。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <cmath>

// プレイヤーの動き方だけを計算するクラス。Playerが入力から移動の向きを渡し、結果の速度で位置を動かしている。
class PlayerMovement final
{
private:
    // 歩く速さ（1秒あたり30）、走るときの倍率、重力の加速度
    static constexpr float DefaultMoveSpeedPerSecond = 30.0f;
    static constexpr float SprintSpeedMultiplier = 1.65f;
    static constexpr float GravityAcceleration = 36.0f;
    // スタミナの最大値、走っている間に1秒あたり減る量（約7.6秒で空）、休んでいる間に1秒あたり回復する量
    static constexpr float MaxStamina = 100.0f;
    static constexpr float StaminaDrainPerSecond = 13.2f;
    static constexpr float StaminaRecoveryPerSecond = 22.8f;

    // 60fpsで1フレームあたりresponseだけ近づける動きを、フレームレートが違っても同じ速さになるよう補正している
    static float GetResponse(float responseAt60Fps, float deltaTime)
    {
        return 1.0f - std::pow(
            1.0f - responseAt60Fps, deltaTime * 60.0f);
    }

    // 速度、スタミナ、回復を始めるまでの待ち、頭の揺れの位相と上下・左右のずれ、スタミナ切れで走れない状態か、走っているか
    DirectX::SimpleMath::Vector3 m_Velocity =
        DirectX::SimpleMath::Vector3::Zero;
    float m_Stamina = MaxStamina;
    float m_StaminaRecoveryDelay = 0.0f;
    float m_HeadBobTimer = 0.0f;
    float m_HeadBobOffset = 0.0f;
    float m_HeadBobSideOffset = 0.0f;
    bool m_SprintExhausted = false;
    bool m_IsSprinting = false;

public:
    // 止まった・スタミナ満タンの状態に戻している
    void Initialize()
    {
        m_Velocity = DirectX::SimpleMath::Vector3::Zero;
        m_Stamina = MaxStamina;
        m_StaminaRecoveryDelay = 0.0f;
        m_HeadBobTimer = 0.0f;
        m_HeadBobOffset = 0.0f;
        m_HeadBobSideOffset = 0.0f;
        m_SprintExhausted = false;
        m_IsSprinting = false;
    }

    // 1フレーム分、速度とスタミナを更新している。戻り値は、このフレームでスタミナを使い切った場合だけtrue。
    bool Update(
        DirectX::SimpleMath::Vector3 moveDirection,
        bool wantsToSprint,
        float deltaTime)
    {
        const float moveLengthSquared = moveDirection.LengthSquared();
        const bool isMoving = moveLengthSquared > 0.0001f;

        // スタミナ切れの後は、30%まで回復するまで走れないようにしている
        if (m_SprintExhausted && m_Stamina >= MaxStamina * 0.30f)
        {
            m_SprintExhausted = false;
        }

        m_IsSprinting = isMoving && wantsToSprint &&
            !m_SprintExhausted && m_Stamina > 0.0f;
        bool exhaustedThisFrame = false;
        // 走っている間はスタミナを減らし、止めた後は少し待ってから回復させている（使い切ったときは1秒待つ）
        if (m_IsSprinting)
        {
            m_Stamina = (std::max)(
                0.0f, m_Stamina - StaminaDrainPerSecond * deltaTime);
            m_StaminaRecoveryDelay = 0.45f;
            if (m_Stamina <= 0.0f)
            {
                m_IsSprinting = false;
                m_SprintExhausted = true;
                m_StaminaRecoveryDelay = 1.0f;
                exhaustedThisFrame = true;
            }
        }
        else if (m_StaminaRecoveryDelay > 0.0f)
        {
            m_StaminaRecoveryDelay = (std::max)(
                0.0f, m_StaminaRecoveryDelay - deltaTime);
        }
        else
        {
            m_Stamina = (std::min)(
                MaxStamina, m_Stamina + StaminaRecoveryPerSecond * deltaTime);
        }

        // 急な入力の変化をそのまま座標へ反映せず、速度をなめらかに近づけて、視点の揺れを抑えている。
        const float currentMoveSpeed = DefaultMoveSpeedPerSecond *
            (m_IsSprinting ? SprintSpeedMultiplier : 1.0f);
        if (isMoving)
        {
            if (moveLengthSquared > 1.0f)
            {
                moveDirection.Normalize();
            }
            const DirectX::SimpleMath::Vector3 desiredVelocity =
                moveDirection * currentMoveSpeed;
            const float acceleration = GetResponse(
                m_IsSprinting ? 0.24f : 0.30f, deltaTime);
            m_Velocity.x +=
                (desiredVelocity.x - m_Velocity.x) * acceleration;
            m_Velocity.z +=
                (desiredVelocity.z - m_Velocity.z) * acceleration;
        }
        // 入力がなければ、速度を少しずつ0へ近づけて止めている
        else
        {
            const float deceleration = std::pow(0.72f, deltaTime * 60.0f);
            m_Velocity.x *= deceleration;
            m_Velocity.z *= deceleration;
            if (std::abs(m_Velocity.x) < 0.001f) m_Velocity.x = 0.0f;
            if (std::abs(m_Velocity.z) < 0.001f) m_Velocity.z = 0.0f;
        }

        // 重力で下向きの速度を増やしている（床より下に行かないようにするのはPlayerが担当している）
        m_Velocity.y -= GravityAcceleration * deltaTime;
        return exhaustedThisFrame;
    }

    // 移動の速さに合わせて、一人称のカメラの上下・左右の揺れを更新している。
    // 位相と減り方を経過時間で進め、止まったときだけ自然に真ん中へ戻している。
    void UpdateHeadBob(float deltaTime)
    {
        const float horizontalSpeed = std::sqrt(
            m_Velocity.x * m_Velocity.x + m_Velocity.z * m_Velocity.z);
        // 動いている間：速いほど大きく速く揺らし、上下は足を着くたびに沈む形（sinの絶対値）、左右はその半分の速さで揺らしている
        if (horizontalSpeed > 0.025f)
        {
            const float speedRate = (std::min)(
                horizontalSpeed / GetMaximumHorizontalSpeed(), 1.0f);
            m_HeadBobTimer +=
                (m_IsSprinting ? 13.2f : 8.4f) *
                (0.55f + speedRate * 0.45f) * deltaTime;
            const float amplitude =
                (m_IsSprinting ? 0.48f : 0.30f) * speedRate;
            const float verticalTarget =
                std::abs(std::sin(m_HeadBobTimer)) * amplitude -
                amplitude * 0.48f;
            const float sideTarget =
                std::sin(m_HeadBobTimer * 0.5f) * amplitude * 0.34f;
            m_HeadBobOffset +=
                (verticalTarget - m_HeadBobOffset) *
                GetResponse(0.30f, deltaTime);
            m_HeadBobSideOffset +=
                (sideTarget - m_HeadBobSideOffset) *
                GetResponse(0.24f, deltaTime);
        }
        else
        {
            const float returnDecay = std::pow(0.84f, deltaTime * 60.0f);
            m_HeadBobOffset *= returnDecay;
            m_HeadBobSideOffset *= returnDecay;
        }
    }

    // 操作できない間は走っていない扱いにしている
    void StopControl() { m_IsSprinting = false; }
    // 速度を0にする・下向きの速度だけ0にする
    void ResetVelocity() { m_Velocity = DirectX::SimpleMath::Vector3::Zero; }
    void StopVerticalVelocity() { m_Velocity.y = 0.0f; }

    // スタミナを満タンに戻している
    void RestoreStamina()
    {
        m_Stamina = MaxStamina;
        m_StaminaRecoveryDelay = 0.0f;
        m_SprintExhausted = false;
    }

    // 速度・走っているか・スタミナ・頭の揺れを返している
    const DirectX::SimpleMath::Vector3& GetVelocity() const
    {
        return m_Velocity;
    }

    bool IsSprinting() const { return m_IsSprinting; }
    float GetStamina() const { return m_Stamina; }
    float GetHeadBobOffset() const { return m_HeadBobOffset; }
    float GetHeadBobSideOffset() const { return m_HeadBobSideOffset; }
    // 走っているときの最高の水平の速さ
    float GetMaximumHorizontalSpeed() const
    {
        return DefaultMoveSpeedPerSecond * SprintSpeedMultiplier;
    }

    // 実際に水平方向へ動いているか
    bool IsMovingHorizontally() const
    {
        return m_Velocity.x * m_Velocity.x +
            m_Velocity.z * m_Velocity.z > 0.0025f;
    }
};
