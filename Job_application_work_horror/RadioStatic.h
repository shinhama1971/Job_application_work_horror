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

#include <algorithm>
#include <cmath>

class RadioStatic final
{
private:
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
        const float speed = target > m_Level ? 3.2f : 0.7f;
        m_Level += (target - m_Level) * (std::min)(1.0f, speed * deltaTime);

        // 電波が途切れるように、強さを不規則に揺らしている
        const float flutter = 0.78f +
            0.14f * std::sin(m_Time * 7.3f) +
            0.08f * std::sin(m_Time * 17.9f + 1.7f);
        Core::Game* game = Core::Game::GetInstance();
        game->SetAudioCueVolume(SOUND_CUE_RADIO_STATIC, m_Level * flutter);
        // 画面の乱れは控えめにし、進む方向は見失わないようにしている
        game->GetPostProcess()->SetPresenceStatic(m_Level * flutter * 0.45f);
    }

    float GetLevel() const noexcept { return m_Level; }
};
