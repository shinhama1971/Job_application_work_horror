// ============================================================================
// ファイルの役割: 演出で使う「照明の明滅・画面の脈打ち・コントローラーの振動」の強さと長さを、1つの名前付きの組にまとめている。
// 1面・2面の演出は、出来事ごとに少しずつちがう強さと長さを使う。数を呼び出しの中に直接書かず、
// 「入口で後ろの照明が明滅する」のような名前の付いた定数として、各ファイルの先頭に並べるための型と、渡すための関数。
// ============================================================================

#pragma once

#include "CeilingLight.h"
#include "Input.h"
#include "PostProcess.h"

namespace EventFeedback
{
    // 天井の照明の明滅（CeilingLight::TriggerEventFlicker に渡す長さと強さ）
    struct FlickerSpec
    {
        float Seconds;
        float Strength;
    };

    // 画面の脈打ち（PostProcess::TriggerHorrorPulse の強さと長さ、TriggerBloomPulse の一番明るいときの強さと長さ）
    struct PulseSpec
    {
        float Strength;
        float Seconds;
    };

    // コントローラーの振動（Input::SetVibration に渡す、60fpsで数えたフレーム数と強さ）
    struct RumbleSpec
    {
        int Frames;
        float Strength;
    };

    // 鳴らす音の高さ（1で元の高さ）と音量の倍率（Game::PlayAudioCue・PlayAudioCueAt に渡す）
    struct SoundSpec
    {
        float Pitch;
        float Volume;
    };

    inline void Flicker(CeilingLight& light, const FlickerSpec& flicker)
    {
        light.TriggerEventFlicker(flicker.Seconds, flicker.Strength);
    }

    inline void HorrorPulse(Effect::PostProcess& postProcess, const PulseSpec& pulse)
    {
        postProcess.TriggerHorrorPulse(pulse.Strength, pulse.Seconds);
    }

    inline void BloomPulse(Effect::PostProcess& postProcess, const PulseSpec& pulse)
    {
        postProcess.TriggerBloomPulse(pulse.Strength, pulse.Seconds);
    }

    inline void Rumble(const RumbleSpec& rumble)
    {
        Input::SetVibration(rumble.Frames, rumble.Strength);
    }
}
