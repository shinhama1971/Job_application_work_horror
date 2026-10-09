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

float4 main(in PS_IN input) : SV_Target
{
    const float4 source = g_Texture.Sample(g_SamplerState, input.tex);

    // 黒を灰色へ持ち上げるだけでは輪郭がぼやけるため、露出とガンマを順に掛けている。
    float3 color = 1.0f - exp(-max(source.rgb, 0.0f) * 2.65f);
    color = pow(saturate(color + 0.018f), 0.76f);

    // 色を少し残した暗視カメラのような表現にし、赤い非常灯や扉の材質の違いも見分けられるようにしている。
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luminance.xxx, color, 0.56f);
    color *= float3(0.88f, 1.06f, 0.92f);

    // 粒状のノイズと走査線で、監視カメラの映像らしさを出している
    const float grain = Hash(floor(input.pos.xy)) - 0.5f;
    color += grain * 0.018f;
    const float scanline = sin(input.pos.y * 3.14159265f) * 0.012f;
    color *= 1.0f + scanline;

    return float4(saturate(color), source.a);
}
