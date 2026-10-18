// ============================================================================
// ファイルの役割: 2面の「誰かの濡れた足跡」が、水たまりから出たプレイヤーの後をついて来る動きを管理している。
// 主な技術: 有限状態機械（待つ → 一歩ずつ近づく → 止まる）、プレイヤーへ向かう歩幅と左右の足の交互の配置、
//           懐中電灯で照らすと止まる判定
// 足跡を床に描く（Ground::AddFootprint）・音・照明の演出は Stage2Scene が担当している。
//
// ・2周目以降、プレイヤーが水たまりから出ると、少し遅れて、同じ水たまりから裸足の濡れた足跡が一歩ずつ現れ、
//   プレイヤーの足跡のすぐ横に並んで、プレイヤーの方へ近づいてくる（足跡だけで、姿は見えない）。
// ・一番新しい足跡を懐中電灯で照らすと、そこで止まる。照らさずにすぐ近くまで来させると、照明が揺れて止まる。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <cmath>

class FootprintFollower final
{
public:
    // 最初の一歩までの秒数、一歩ごとの秒数、歩幅、左右の足の幅、止まる距離、横へずらす幅、足跡の最大数
    static constexpr float StartDelaySeconds = 1.6f;
    static constexpr float StepSeconds = 0.62f;
    static constexpr float Stride = 8.5f;
    static constexpr float SideOffset = 2.2f;
    static constexpr float ReachDistance = 16.0f;
    // プレイヤーの足跡と重ならず、すぐ横に並んで見えるよう、歩く線を横へずらす幅
    static constexpr float LaneOffset = 6.5f;
    static constexpr int MaxSteps = 18;

    // Updateの結果（何も起きていない・一歩進んだ・照らされて止まった・すぐ近くまで来て止まった）
    enum class Event
    {
        None,
        Step,
        Lit,
        Reached
    };

    // 一歩進んだときの、足跡の位置・向き・左右
    struct Step
    {
        DirectX::SimpleMath::Vector2 Position;
        float Yaw = 0.0f;
        float Side = 1.0f;
    };

private:
    bool m_Active = false;
    DirectX::SimpleMath::Vector2 m_Position;
    float m_Timer = 0.0f;
    float m_Side = 1.0f;
    int m_Steps = 0;
    bool m_HasLatest = false;
    DirectX::SimpleMath::Vector2 m_Latest;

public:
    void Reset() noexcept
    {
        m_Active = false;
        m_Steps = 0;
        m_HasLatest = false;
    }

    // 足跡が歩いている途中か
    bool IsActive() const noexcept { return m_Active; }

    // 水たまりの出口（origin）から、足跡を出し始めている
    void Start(const DirectX::SimpleMath::Vector2& origin) noexcept
    {
        m_Active = true;
        m_Position = origin;
        m_Timer = -StartDelaySeconds;
        m_Side = 1.0f;
        m_Steps = 0;
        m_HasLatest = false;
    }

    // 1フレーム分進めている。eyeとforwardはカメラ、flashlightOnは懐中電灯が点いているか
    Event Update(
        float deltaTime,
        const DirectX::SimpleMath::Vector3& playerPosition,
        const DirectX::SimpleMath::Vector3& eye,
        const DirectX::SimpleMath::Vector3& forward,
        bool flashlightOn,
        Step& step) noexcept
    {
        if (!m_Active)
        {
            return Event::None;
        }

        // 一番新しい足跡を懐中電灯の中心で照らす（正面から内積0.93超、90未満の距離）と、そこで止まる
        if (m_HasLatest && flashlightOn)
        {
            DirectX::SimpleMath::Vector3 toLatest(m_Latest.x - eye.x, -99.5f - eye.y, m_Latest.y - eye.z);
            const float distance = toLatest.Length();
            if (distance > 0.001f && distance < 90.0f && forward.Dot(toLatest / distance) > 0.93f)
            {
                m_Active = false;
                return Event::Lit;
            }
        }

        m_Timer += deltaTime;
        if (m_Timer < StepSeconds)
        {
            return Event::None;
        }
        m_Timer = 0.0f;

        const DirectX::SimpleMath::Vector2 target(playerPosition.x, playerPosition.z);
        DirectX::SimpleMath::Vector2 toPlayer = target - m_Position;
        const float distance = toPlayer.Length();
        if (distance < ReachDistance || m_Steps >= MaxSteps)
        {
            m_Active = false;
            return Event::Reached;
        }
        toPlayer /= distance;
        // 最初の一歩は、水たまりの出口に。そこからプレイヤーの方へ一歩ずつ進んでいる
        if (m_Steps > 0)
        {
            m_Position += toPlayer * Stride;
        }
        const float yaw = std::atan2(toPlayer.x, toPlayer.y);
        const DirectX::SimpleMath::Vector2 right(std::cos(yaw), -std::sin(yaw));
        step.Position = m_Position + right * (LaneOffset + SideOffset * m_Side);
        step.Yaw = yaw;
        step.Side = m_Side;
        m_Side = -m_Side;
        ++m_Steps;
        m_Latest = step.Position;
        m_HasLatest = true;
        return Event::Step;
    }
};
