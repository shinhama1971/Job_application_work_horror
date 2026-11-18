// ============================================================================
// ファイルの役割: 携帯ラジオの雑音（サイレントヒルのラジオを参考）。影が近づくと雑音が鳴り出し、近いほど大きくなり、
//                 画面にも同じ強さで信号の乱れ（横の帯のずれ・雑音の線）を重ねている。
// 主な技術: 危険度から雑音の強さへの変換、上がるのは速く下がるのはゆっくりの追従、不規則に強弱する揺らぎ
// 危険度（影の近さ）は各Sceneが求め、ここでは強さの計算と、音・画面への反映だけを行っている。
//
// ・姿が見えなくても、雑音の強さで「何かが近くにいる」ことが分かる。ロッカーに隠れているときや、
//   懐中電灯を消して進むときの判断材料になる。
// ============================================================================

#pragma once

#include "Game.h"
#include "PostProcess.h"

#include <algorithm>
#include <cmath>

class RadioStatic final
{
private:
    // 鳴り始める速さ（約0.3秒）と、静まる速さ（約1.5秒）
    static constexpr float RiseSpeed = 3.2f;
    static constexpr float FallSpeed = 0.7f;
    // 電波が途切れるような揺れ：中心、1つ目の揺れ（幅と速さ）、2つ目の揺れ（幅・速さ・ずらし）
    static constexpr float FlutterCenter = 0.78f;
    static constexpr float FlutterAmountA = 0.14f;
    static constexpr float FlutterRateA = 7.3f;
    static constexpr float FlutterAmountB = 0.08f;
    static constexpr float FlutterRateB = 17.9f;
    static constexpr float FlutterPhaseB = 1.7f;
    // 画面の乱れは控えめにし、進む方向は見失わないようにする（音の強さに掛ける割合）
    static constexpr float ScreenStaticScale = 0.45f;
    // 雑音の強さ（0〜1）と、揺らぎを動かす時間
    float m_Level = 0.0f;
    float m_Time = 0.0f;

public:
    void Reset() noexcept
    {
        m_Level = 0.0f;
        m_Time = 0.0f;
    }

    // 1フレーム分進め、音と画面へ反映している。threat は影の近さ（0〜1、各Sceneの危険度）、allowed は鳴らしてよいか
    void Update(float deltaTime, float threat, bool allowed)
    {
        m_Time += deltaTime;
        // 少しでも近づくと鳴り始め、すぐ近くで最大になる
        const float target = allowed ? std::sqrt((std::clamp)(threat, 0.0f, 1.0f)) : 0.0f;
        // 鳴り始めは速く（約0.3秒）、静まるのはゆっくり（約1.5秒）
        const float speed = target > m_Level ? RiseSpeed : FallSpeed;
        m_Level += (target - m_Level) * (std::min)(1.0f, speed * deltaTime);

        // 電波が途切れるように、強さを不規則に揺らしている
        const float flutter = FlutterCenter +
            FlutterAmountA * std::sin(m_Time * FlutterRateA) +
            FlutterAmountB * std::sin(m_Time * FlutterRateB + FlutterPhaseB);
        Core::Game* game = Core::Game::GetInstance();
        game->SetAudioCueVolume(SOUND_CUE_RADIO_STATIC, m_Level * flutter);
        // 画面の乱れは控えめにし、進む方向は見失わないようにしている
        game->GetPostProcess()->SetPresenceStatic(m_Level * flutter * ScreenStaticScale);
    }

    float GetLevel() const noexcept { return m_Level; }
};
