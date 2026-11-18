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
    // スタミナの最大値、走っている間に1秒あたり減る量（約10秒で空）、1秒あたり回復する量（立ち止まっている間・歩いている間）。
    // 1面が広くなったため、走れる時間を延ばした。歩きながらでは戻りにくくし、息を整えるために立ち止まる間を作っている
    // （立ち止まれば約3.8秒、歩きながらなら約6.7秒で満タン）。
    static constexpr float MaxStamina = 100.0f;
    static constexpr float StaminaDrainPerSecond = 10.0f;
    static constexpr float StaminaRecoveryPerSecond = 26.0f;
    static constexpr float StaminaRecoveryWhileWalkingPerSecond = 15.0f;
    // スタミナを使い切った後、この割合まで戻るまでは走れない
    static constexpr float ExhaustedSprintResumeRate = 0.25f;
    // 走るのをやめてから回復を始めるまでの待ち（普段と、使い切ったとき）
    static constexpr float StaminaRecoveryDelaySeconds = 0.45f;
    static constexpr float ExhaustedRecoveryDelaySeconds = 1.0f;

    // 速度の追いつき方（60fpsで1フレームあたりに近づける割合。走るときは少し重く）と、入力がないときの止まり方
    static constexpr float WalkAcceleration = 0.30f;
    static constexpr float SprintAcceleration = 0.24f;
    static constexpr float StopDecelerationPerFrame = 0.72f;
    // 1フレームあたりの割合の基準にするフレームレート
    static constexpr float ResponseReferenceFps = 60.0f;
    // これより遅い速さは止まったとみなして0にする
    static constexpr float StopSpeedEpsilon = 0.001f;
    // 頭を揺らし始める速さと、動いているとみなす速さの2乗
    static constexpr float HeadBobMinSpeed = 0.025f;
    static constexpr float MovingSpeedSquared = 0.0025f;

    // 歩くときの頭の揺れ：位相の進む速さ（走る・歩く）、遅いときに速さを落とす割合、上下の揺れの大きさ（走る・歩く）、
    // 上下の揺れの中心を下げる割合（足を着いたときに沈む形にする）、左右の揺れの大きさと速さ（上下の半分）、
    // 揺れを目標へ近づける割合（上下・左右）、止まったときに真ん中へ戻す割合（60fpsで1フレームあたり）
    static constexpr float SprintBobFrequency = 13.2f;
    static constexpr float WalkBobFrequency = 8.4f;
    static constexpr float BobSlowSpeedFrequencyRate = 0.55f;
    static constexpr float SprintBobAmplitude = 0.48f;
    static constexpr float WalkBobAmplitude = 0.30f;
    static constexpr float BobVerticalCenterRate = 0.48f;
    static constexpr float BobSideAmplitudeRate = 0.34f;
    static constexpr float BobSideFrequencyRate = 0.5f;
    static constexpr float BobVerticalResponse = 0.30f;
    static constexpr float BobSideResponse = 0.24f;
    static constexpr float BobReturnPerFrame = 0.84f;

    // 60fpsで1フレームあたりresponseだけ近づける動きを、フレームレートが違っても同じ速さになるよう補正している
    static float GetResponse(float responseAt60Fps, float deltaTime)
    {
        return 1.0f - std::pow(
            1.0f - responseAt60Fps, deltaTime * ResponseReferenceFps);
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

        // スタミナ切れの後は、25%まで回復するまで走れないようにしている
        if (m_SprintExhausted && m_Stamina >= MaxStamina * ExhaustedSprintResumeRate)
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
            m_StaminaRecoveryDelay = StaminaRecoveryDelaySeconds;
            if (m_Stamina <= 0.0f)
            {
                m_IsSprinting = false;
                m_SprintExhausted = true;
                m_StaminaRecoveryDelay = ExhaustedRecoveryDelaySeconds;
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
            const float recovery = isMoving ? StaminaRecoveryWhileWalkingPerSecond : StaminaRecoveryPerSecond;
            m_Stamina = (std::min)(MaxStamina, m_Stamina + recovery * deltaTime);
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
                m_IsSprinting ? SprintAcceleration : WalkAcceleration, deltaTime);
            m_Velocity.x +=
                (desiredVelocity.x - m_Velocity.x) * acceleration;
            m_Velocity.z +=
                (desiredVelocity.z - m_Velocity.z) * acceleration;
        }
        // 入力がなければ、速度を少しずつ0へ近づけて止めている
        else
        {
            const float deceleration = std::pow(StopDecelerationPerFrame, deltaTime * ResponseReferenceFps);
            m_Velocity.x *= deceleration;
            m_Velocity.z *= deceleration;
            if (std::abs(m_Velocity.x) < StopSpeedEpsilon) m_Velocity.x = 0.0f;
            if (std::abs(m_Velocity.z) < StopSpeedEpsilon) m_Velocity.z = 0.0f;
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
        if (horizontalSpeed > HeadBobMinSpeed)
        {
            const float speedRate = (std::min)(
                horizontalSpeed / GetMaximumHorizontalSpeed(), 1.0f);
            m_HeadBobTimer +=
                (m_IsSprinting ? SprintBobFrequency : WalkBobFrequency) *
                (BobSlowSpeedFrequencyRate + speedRate * (1.0f - BobSlowSpeedFrequencyRate)) * deltaTime;
            const float amplitude =
                (m_IsSprinting ? SprintBobAmplitude : WalkBobAmplitude) * speedRate;
            const float verticalTarget =
                std::abs(std::sin(m_HeadBobTimer)) * amplitude -
                amplitude * BobVerticalCenterRate;
            const float sideTarget =
                std::sin(m_HeadBobTimer * BobSideFrequencyRate) * amplitude * BobSideAmplitudeRate;
            m_HeadBobOffset +=
                (verticalTarget - m_HeadBobOffset) *
                GetResponse(BobVerticalResponse, deltaTime);
            m_HeadBobSideOffset +=
                (sideTarget - m_HeadBobSideOffset) *
                GetResponse(BobSideResponse, deltaTime);
        }
        else
        {
            const float returnDecay = std::pow(BobReturnPerFrame, deltaTime * 60.0f);
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
            m_Velocity.z * m_Velocity.z > MovingSpeedSquared;
    }
};
