// ============================================================================
// シェーダーの役割: 監視カメラの生映像だけを明るく補正し、暗所の形状と異常を見分けやすくします。
// 本編の露出やホラーらしい暗さには影響を与えません。
// ============================================================================

#include "common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float4 main(in PS_IN input) : SV_Target
{
    const float4 source = g_Texture.Sample(g_SamplerState, input.tex);

    // 黒を灰色へ持ち上げるだけでは輪郭が眠るため、露出とガンマを順に適用します。
    float3 color = 1.0f - exp(-max(source.rgb, 0.0f) * 2.65f);
    color = pow(saturate(color + 0.018f), 0.76f);

    // 色味を少し残した暗視表現にし、赤い非常灯や扉の材質差も判断できるようにします。
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luminance.xxx, color, 0.56f);
    color *= float3(0.88f, 1.06f, 0.92f);

    const float grain = Hash(floor(input.pos.xy)) - 0.5f;
    color += grain * 0.018f;
    const float scanline = sin(input.pos.y * 3.14159265f) * 0.012f;
    color *= 1.0f + scanline;

    return float4(saturate(color), source.a);
}
