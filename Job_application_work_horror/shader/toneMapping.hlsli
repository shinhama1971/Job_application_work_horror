// ============================================================================
// 共通の処理: トーンマッピングの曲線（ACES）と、その逆の計算。
// 主な技術: ACES のフィルムの曲線（Narkowicz 2015「ACES Filmic Tone Mapping Curve」の近似式）、色合いを保つ輝度への適用、
//           曲線の逆関数（2次方程式の解）
//
// ・画面効果の最後（toneMapPS.hlsl）が、HDRで描いた画面に1回だけ曲線を掛けている。
// ・電球や非常灯などの自ら光る色（Emission）は、0〜1の「画面に出したい明るさ」で決めてある。
//   そのまま足すと曲線で暗く丸められるので、曲線の逆で HDR の明るさに直してから足している（InverseToneMapEmission）。
//   曲線を通った後に元の明るさで見え、ブルームには1を超える強い光として入る。
// ============================================================================
#ifndef TONE_MAPPING_INCLUDED
#define TONE_MAPPING_INCLUDED

// ACES の近似式 f(x) = x(ax + b) / (x(cx + d) + e) の係数（Narkowicz 2015）
static const float ACES_A = 2.51f;
static const float ACES_B = 0.03f;
static const float ACES_C = 2.43f;
static const float ACES_D = 0.59f;
static const float ACES_E = 0.14f;

// 曲線に入れる前に掛ける明るさ。1面・2面の自動撮影の51枚で、画面の平均の明るさがHDRにする前とほぼ同じになる値にしている
// （1.0では約16%明るく、0.71では約13%暗くなった）
static const float ACES_EXPOSURE = 0.85f;

// 逆の計算で扱う、画面に出したい明るさの上限（曲線の上限 a / c ≒ 1.03 に近づくほど、逆の値が無限に大きくなるため）
static const float ACES_INVERSE_MAX_OUTPUT = 0.97f;

// 曲線を赤・緑・青に別々に掛けた分と、明るさ（輝度）だけに掛けて色の比率を保った分を混ぜる割合（1で全部、輝度に掛ける）。
// 別々に掛けると明るい色ほど彩度が落ち、懐中電灯の暖かい光や霧の色が灰色に寄る。輝度だけに掛けると色は残るが、
// 電球の芯が白くならない。ほとんどを輝度に掛け、強い光だけが少し白へ寄るようにしている
static const float ACES_HUE_PRESERVATION = 0.75f;
// 輝度の重み（Rec.709）と、0で割らないための小さな値
static const float3 TONE_MAP_LUMINANCE_WEIGHTS = float3(0.2126f, 0.7152f, 0.0722f);
static const float TONE_MAP_MIN_LUMINANCE = 1e-5f;

float3 AcesFilmic(float3 color)
{
    return saturate((color * (ACES_A * color + ACES_B)) / (color * (ACES_C * color + ACES_D) + ACES_E));
}

float AcesFilmicScalar(float value)
{
    return saturate((value * (ACES_A * value + ACES_B)) / (value * (ACES_C * value + ACES_D) + ACES_E));
}

// 露出を掛けてから曲線を通している（画面効果の最後のトーンマッピング）
float3 ToneMapAces(float3 color)
{
    const float3 exposed = color * ACES_EXPOSURE;
    const float luminance = max(dot(exposed, TONE_MAP_LUMINANCE_WEIGHTS), TONE_MAP_MIN_LUMINANCE);
    const float3 huePreserved = exposed * (AcesFilmicScalar(luminance) / luminance);
    return saturate(lerp(AcesFilmic(exposed), huePreserved, ACES_HUE_PRESERVATION));
}

// 曲線の逆：f(x) = y を x について解いている。(a - cy)x² + (b - dy)x - ey = 0 の正の解
float InverseAcesFilmicScalar(float displayValue)
{
    const float y = clamp(displayValue, 0.0f, ACES_INVERSE_MAX_OUTPUT);
    const float quadraticTerm = ACES_A - ACES_C * y;
    const float linearTerm = ACES_B - ACES_D * y;
    const float constantTerm = -ACES_E * y;
    return (-linearTerm + sqrt(linearTerm * linearTerm - 4.0f * quadraticTerm * constantTerm)) / (2.0f * quadraticTerm);
}

// 自ら光る色（0〜1の画面に出したい明るさ）を、トーンマッピングの後にその明るさで見える HDR の値へ直している。
// 色の比率を変えないよう、輝度だけで逆を計算して、色全体に同じ倍率を掛けている
float3 InverseToneMapEmission(float3 displayEmission)
{
    const float3 emission = max(displayEmission, 0.0f);
    const float luminance = dot(emission, TONE_MAP_LUMINANCE_WEIGHTS);
    if (luminance <= TONE_MAP_MIN_LUMINANCE)
    {
        return emission;
    }
    return emission * (InverseAcesFilmicScalar(luminance) / (luminance * ACES_EXPOSURE));
}

#endif
