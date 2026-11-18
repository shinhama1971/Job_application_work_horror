// ============================================================================
// ファイルの役割: いろいろなファイルで同じ式を書いていた、小さな数学の値と関数を1か所にまとめている。
// 主な技術: 円周率（DirectXMath の値）、なめらかな補間（smoothstep）、距離から「近さ」への変換、角度を -π〜π に直す処理
// シェーダーの smoothstep と同じ式なので、C++ と HLSL で同じ変わり方になる。
// ============================================================================

#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>

namespace MathUtility
{
    // 円周率とその2倍・半分（DirectXMath の値をそのまま使う）
    inline constexpr float Pi = DirectX::XM_PI;
    inline constexpr float TwoPi = DirectX::XM_2PI;
    inline constexpr float HalfPi = DirectX::XM_PIDIV2;

    // 0〜1の t を、両端でゆっくり、真ん中で速く変わる曲線に通している（t*t*(3-2t)）。
    // t はすでに0〜1に収めてあるものとして、ここでは収めない
    constexpr float SmoothCurve(float t) noexcept
    {
        return t * t * (3.0f - 2.0f * t);
    }

    // t を0〜1に収めてから、なめらかな曲線に通している
    inline float SmoothStep01(float t) noexcept
    {
        return SmoothCurve((std::clamp)(t, 0.0f, 1.0f));
    }

    // value が edge0 から edge1 へ進むにつれて、0から1へなめらかに変わる値（HLSL の smoothstep と同じ）
    inline float SmoothStep(float edge0, float edge1, float value) noexcept
    {
        return SmoothStep01((value - edge0) / (edge1 - edge0));
    }

    // 距離から「近さ」（0〜1）を求めている。nearDistance より近ければ1、そこから range だけ離れると0になり、間はまっすぐ変わる
    inline float Proximity(float distance, float nearDistance, float range) noexcept
    {
        return 1.0f - (std::clamp)((distance - nearDistance) / range, 0.0f, 1.0f);
    }

    // 角度（ラジアン）を -π〜π の範囲に直している（2つの向きの差を、近い回り方で表すため）
    inline float WrapAngle(float angle) noexcept
    {
        angle = std::fmod(angle + Pi, TwoPi);
        if (angle < 0.0f)
        {
            angle += TwoPi;
        }
        return angle - Pi;
    }
}
