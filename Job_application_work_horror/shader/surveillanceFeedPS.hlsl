// ============================================================================
// シェーダーの役割: 監視カメラの生の映像だけを明るく補正し、暗い場所の形や異常を見分けやすくしている。
// 本編の露出や、ホラーらしい暗さには影響を与えない。
// ============================================================================

#include "common.hlsl"

// 監視カメラの映像（t0）とサンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

// 2次元の値から0〜1の疑似乱数を作っている
float Hash(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

// 暗い部分に残っている輪郭を見やすくする監視映像用の露出とガンマ。
#define SURVEILLANCE_EXPOSURE 2.65f
#define SURVEILLANCE_BLACK_LIFT 0.018f
#define SURVEILLANCE_GAMMA_EXPONENT 0.76f

// 赤い非常灯が見分けられる程度に色を残し、暗視風の色を重ねる。
#define SURVEILLANCE_SATURATION 0.56f
#define SURVEILLANCE_CAMERA_TINT float3(0.88f, 1.06f, 0.92f)

// 映像の粒の強さと、1画素おきに濃淡をつける走査線。
#define SURVEILLANCE_GRAIN_AMOUNT 0.018f
#define SURVEILLANCE_SCANLINE_PHASE_PER_PIXEL 3.14159265f
#define SURVEILLANCE_SCANLINE_AMOUNT 0.012f

float4 main(in PS_IN input) : SV_Target
{
    const float4 source = g_Texture.Sample(g_SamplerState, input.tex);

    // 黒を灰色へ持ち上げるだけでは輪郭がぼやけるため、露出とガンマを順に掛けている。
    float3 color = 1.0f - exp(-max(source.rgb, 0.0f) * SURVEILLANCE_EXPOSURE);
    color = pow(saturate(color + SURVEILLANCE_BLACK_LIFT), SURVEILLANCE_GAMMA_EXPONENT);

    // 色を少し残した暗視カメラのような表現にし、赤い非常灯や扉の材質の違いも見分けられるようにしている。
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luminance.xxx, color, SURVEILLANCE_SATURATION);
    color *= SURVEILLANCE_CAMERA_TINT;

    // 粒状のノイズと走査線で、監視カメラの映像らしさを出している
    const float grain = Hash(floor(input.pos.xy)) - 0.5f;
    color += grain * SURVEILLANCE_GRAIN_AMOUNT;
    const float scanline = sin(input.pos.y * SURVEILLANCE_SCANLINE_PHASE_PER_PIXEL) * SURVEILLANCE_SCANLINE_AMOUNT;
    color *= 1.0f + scanline;

    return float4(saturate(color), source.a);
}
