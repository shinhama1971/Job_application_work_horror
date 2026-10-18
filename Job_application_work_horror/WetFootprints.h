// ============================================================================
// ファイルの役割: 床に残る濡れた足跡（水たまりを踏んだ後の自分の足跡と、2面の「誰か」の裸足の足跡）を管理している。
// 主な技術: 決まった数の輪のバッファ、時間で乾く濡れ具合、歩いた向きからの足の向き、左右の足の交互の配置
// 足跡の形は濡れた床のシェーダー（wetFloorPS の GetWetFootprints）が描き、ここでは場所と濡れ具合だけを持っている。
//
// ・水たまり（または浸水した範囲）を歩いた後、乾いた床へ出てから9歩の間、濡れた靴の跡が残る（だんだん薄くなる）。
// ・足跡は約40秒かけて乾き、縁から薄くなって消える。
// ・水から出た瞬間を ConsumeWaterExit で知らせ、2面の「誰かの足跡」が、そこから後をついて来るのに使っている。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <array>
#include <cmath>

class WetFootprints final
{
public:
    // シェーダーへ渡す足跡の最大数（wetFloorPS の FOOTPRINT_COUNT と同じ）
    static constexpr int MaxCount = 32;
    // 足跡1つ分（位置・向き・左右・裸足か・濡れ具合）
    struct Print
    {
        DirectX::SimpleMath::Vector2 Position;
        float Yaw = 0.0f;
        float Side = 1.0f;      // 右足は1、左足は-1（足の形を左右反転する）
        bool Bare = false;
        float Wetness = 0.0f;   // 0〜1（0で消える）
    };

private:
    // 水から出た後に足跡が残る歩数、乾くまでの秒数、足の間の横の幅（歩いた線から片側への距離）
    static constexpr int WetStepCount = 9;
    static constexpr float DrySeconds = 40.0f;
    static constexpr float SideOffset = 2.4f;

    std::array<Print, MaxCount> m_Prints{};
    int m_Next = 0;
    // 自分の足：濡れたまま歩ける残りの歩数、前の一歩の位置、次は右足か、水から出た瞬間か（と、その場所）
    int m_WetStepsLeft = 0;
    DirectX::SimpleMath::Vector2 m_LastStep;
    bool m_HasLastStep = false;
    float m_NextSide = 1.0f;
    bool m_WasInWater = false;
    bool m_WaterExitPending = false;
    DirectX::SimpleMath::Vector2 m_WaterExit;

public:
    // 足跡をすべて消している
    void Clear() noexcept
    {
        m_Prints = {};
        m_Next = 0;
        m_WetStepsLeft = 0;
        m_HasLastStep = false;
        m_WasInWater = false;
        m_WaterExitPending = false;
    }

    // 足跡を1つ足している（いっぱいなら一番古いものを上書きする）
    void Add(const DirectX::SimpleMath::Vector2& position, float yaw, float side, bool bare, float wetness) noexcept
    {
        Print& print = m_Prints[static_cast<std::size_t>(m_Next)];
        print.Position = position;
        print.Yaw = yaw;
        print.Side = side;
        print.Bare = bare;
        print.Wetness = wetness;
        m_Next = (m_Next + 1) % MaxCount;
    }

    // プレイヤーの一歩ごとに呼んでいる。inWater は、その一歩が水の中だったか
    void OnPlayerStep(const DirectX::SimpleMath::Vector3& position, bool inWater) noexcept
    {
        const DirectX::SimpleMath::Vector2 step(position.x, position.z);
        if (inWater)
        {
            m_WetStepsLeft = WetStepCount;
        }
        else if (m_WetStepsLeft > 0 && m_HasLastStep)
        {
            if (m_WasInWater)
            {
                // 水から出た最初の一歩：その場所（水たまりの縁のすぐ外）を知らせる（2面の「誰かの足跡」の出どころ）
                m_WaterExitPending = true;
                m_WaterExit = step;
            }
            const DirectX::SimpleMath::Vector2 direction = step - m_LastStep;
            if (direction.LengthSquared() > 0.01f)
            {
                const float yaw = std::atan2(direction.x, direction.y);
                // 歩いた線から、右足は右へ、左足は左へずらしている（右 = 前を時計回りに90度）
                const DirectX::SimpleMath::Vector2 right(std::cos(yaw), -std::sin(yaw));
                const float wetness = 0.35f + 0.65f * static_cast<float>(m_WetStepsLeft) / WetStepCount;
                Add(step + right * (SideOffset * m_NextSide), yaw, m_NextSide, false, wetness);
                m_NextSide = -m_NextSide;
            }
            --m_WetStepsLeft;
        }
        m_WasInWater = inWater;
        m_LastStep = step;
        m_HasLastStep = true;
    }

    // 時間とともに乾かしている
    void Update(float deltaTime) noexcept
    {
        for (Print& print : m_Prints)
        {
            print.Wetness = (std::max)(0.0f, print.Wetness - deltaTime / DrySeconds);
        }
    }

    // 水から出た瞬間なら、その場所を返してtrueにしている（1回だけ）
    bool ConsumeWaterExit(DirectX::SimpleMath::Vector2& exitPoint) noexcept
    {
        if (!m_WaterExitPending)
        {
            return false;
        }
        m_WaterExitPending = false;
        exitPoint = m_WaterExit;
        return true;
    }

    // シェーダーへ渡す形にしている（1つにつき2つ：xy = 位置、z = sin(向き)、w = cos(向き) と、x = 濡れ具合、y = 左右、z = 裸足か）。
    // 乾いた足跡は詰めて、数をinfo.xに入れている
    void Fill(DirectX::SimpleMath::Vector4* out, DirectX::SimpleMath::Vector4& info) const noexcept
    {
        int count = 0;
        for (const Print& print : m_Prints)
        {
            if (print.Wetness <= 0.0f)
            {
                continue;
            }
            out[count * 2] = DirectX::SimpleMath::Vector4(
                print.Position.x, print.Position.y, std::sin(print.Yaw), std::cos(print.Yaw));
            out[count * 2 + 1] = DirectX::SimpleMath::Vector4(
                print.Wetness, print.Side, print.Bare ? 1.0f : 0.0f, 0.0f);
            ++count;
        }
        info = DirectX::SimpleMath::Vector4(static_cast<float>(count), 0.0f, 0.0f, 0.0f);
    }
};
