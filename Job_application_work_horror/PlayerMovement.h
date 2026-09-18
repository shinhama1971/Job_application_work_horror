// ============================================================================
// ファイルの役割: プレイヤーの速度、重力、Sprint、Staminaを更新します。
// 主な技術: DeltaTime、速度補間、Sprint/Stamina状態、Head Bob
// Collisionと座標補正はPlayer側に残し、ステージ形状には依存しません。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <cmath>

class PlayerMovement final
{
private:
    static constexpr float DefaultMoveSpeedPerSecond = 30.0f;
    static constexpr float SprintSpeedMultiplier = 1.65f;
    static constexpr float GravityAcceleration = 36.0f;
    static constexpr float MaxStamina = 100.0f;
    static constexpr float StaminaDrainPerSecond = 13.2f;
    static constexpr float StaminaRecoveryPerSecond = 22.8f;

    static float GetResponse(float responseAt60Fps, float deltaTime)
    {
        return 1.0f - std::pow(
            1.0f - responseAt60Fps, deltaTime * 60.0f);
    }

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

    // 戻り値は、このフレームでStaminaを使い切った場合だけtrueです。
    bool Update(
        DirectX::SimpleMath::Vector3 moveDirection,
        bool wantsToSprint,
        float deltaTime)
    {
        const float moveLengthSquared = moveDirection.LengthSquared();
        const bool isMoving = moveLengthSquared > 0.0001f;

        if (m_SprintExhausted && m_Stamina >= MaxStamina * 0.30f)
        {
            m_SprintExhausted = false;
        }

        m_IsSprinting = isMoving && wantsToSprint &&
            !m_SprintExhausted && m_Stamina > 0.0f;
        bool exhaustedThisFrame = false;
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

        // 急な入力変化をそのまま座標へ反映せず、速度補間で視点の揺れを抑えます。
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
        else
        {
            const float deceleration = std::pow(0.72f, deltaTime * 60.0f);
            m_Velocity.x *= deceleration;
            m_Velocity.z *= deceleration;
            if (std::abs(m_Velocity.x) < 0.001f) m_Velocity.x = 0.0f;
            if (std::abs(m_Velocity.z) < 0.001f) m_Velocity.z = 0.0f;
        }

        m_Velocity.y -= GravityAcceleration * deltaTime;
        return exhaustedThisFrame;
    }

    // 移動速度に対応する一人称カメラの上下・左右揺れを更新します。
    // 位相と減衰を経過時間で進め、停止時だけ自然に中央へ戻します。
    void UpdateHeadBob(float deltaTime)
    {
        const float horizontalSpeed = std::sqrt(
            m_Velocity.x * m_Velocity.x + m_Velocity.z * m_Velocity.z);
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

    void StopControl() { m_IsSprinting = false; }
    void ResetVelocity() { m_Velocity = DirectX::SimpleMath::Vector3::Zero; }
    void StopVerticalVelocity() { m_Velocity.y = 0.0f; }

    void RestoreStamina()
    {
        m_Stamina = MaxStamina;
        m_StaminaRecoveryDelay = 0.0f;
        m_SprintExhausted = false;
    }

    const DirectX::SimpleMath::Vector3& GetVelocity() const
    {
        return m_Velocity;
    }

    bool IsSprinting() const { return m_IsSprinting; }
    float GetStamina() const { return m_Stamina; }
    float GetHeadBobOffset() const { return m_HeadBobOffset; }
    float GetHeadBobSideOffset() const { return m_HeadBobSideOffset; }
    float GetMaximumHorizontalSpeed() const
    {
        return DefaultMoveSpeedPerSecond * SprintSpeedMultiplier;
    }

    bool IsMovingHorizontally() const
    {
        return m_Velocity.x * m_Velocity.x +
            m_Velocity.z * m_Velocity.z > 0.0025f;
    }
};
