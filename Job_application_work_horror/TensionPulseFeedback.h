// ============================================================================
// ファイルの役割: TensionPulse が決めた心拍音・呼吸音を実際に鳴らし、心拍に合わせてコントローラーを震わせている。
// 1面（StageScene）と2面（Stage2Scene）で同じ鳴らし方を使うため、ここにまとめている。
// TensionPulse 自体は入力や音に依存しない状態クラスのままにし、鳴らす処理だけをこちらに分けている。
// ============================================================================

#pragma once

#include "Game.h"
#include "Input.h"
#include "TensionPulse.h"

// 心拍の音を鳴らし（強い緊張なら振動も）、呼吸の音を鳴らしている
inline void PlayTensionPulse(const TensionPulse::Output& pulse)
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }
    if (pulse.beat)
    {
        game->PlayAudioCue(SOUND_CUE_HEARTBEAT, pulse.beatPitch, pulse.beatVolume);
        if (pulse.vibration > 0.0f)
        {
            Input::SetVibration(5, pulse.vibration);
        }
    }
    if (pulse.breath)
    {
        game->PlayAudioCue(SOUND_CUE_BREATH, pulse.breathPitch, pulse.breathVolume);
    }
}
