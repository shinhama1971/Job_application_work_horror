// ============================================================================
// ファイルの役割: TensionPulse が決めた心拍音・呼吸音を実際に鳴らし、心拍に合わせてコントローラーを震わせている。
// 1面（StageScene）と2面（Stage2Scene）で同じ鳴らし方を使うため、ここにまとめている。
// TensionPulse 自体は入力や音に依存しない状態クラスのままにし、鳴らす処理だけをこちらに分けている。
// ============================================================================

#pragma once

#include "Game.h"
#include "Input.h"
#include "PostProcess.h"
#include "TensionPulse.h"

// 緊張が高いほど、画面の端から視界が狭まる（PostProcess::SetFearVision）。心拍が聞こえ始める緊張度から効き始め、
// 0.85 で一番狭くなる。心拍が鳴ったフレームでは、端の闇が一瞬さらに迫る
inline void ShowFearVision(const TensionPulse& tension, const TensionPulse::Output& pulse)
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr || game->GetPostProcess() == nullptr)
    {
        return;
    }
    const float strength = (std::clamp)(
        (tension.GetLevel() - TensionPulse::HeartAudibleLevel) / (0.85f - TensionPulse::HeartAudibleLevel), 0.0f, 1.0f);
    game->GetPostProcess()->SetFearVision(strength, pulse.beat && strength > 0.0f);
}

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
