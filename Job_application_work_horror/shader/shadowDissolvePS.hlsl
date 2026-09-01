// ============================================================================
// シェーダーの役割: 人影(シャドウマン)の輪郭をノイズで崩しながら消失させます。
// ============================================================================

#include "common.hlsl"

cbuffer DissolveBuffer : register(b7)
{
    float DissolveTime;
    float Visibility;
    float EdgeWidth;
    float DissolvePadding;
};

float Hash31(float3 value)
{
    value = frac(value * 0.1031f);
    value += dot(value, value.yzx + 33.33f);
    return frac((value.x + value.y) * value.z);
}

float4 main(in PS_IN input) : SV_Target
{
    // World-space cells keep the pattern attached to the apparition while it
    // turns toward the player. A second moving sample prevents static noise.
    const float3 cell = floor(input.worldPos * 0.42f);
    const float coarseNoise = Hash31(cell);
    const float movingNoise = Hash31(
        cell * 1.73f + floor(DissolveTime * 14.0f));
    const float dissolveNoise =
        coarseNoise * 0.72f + movingNoise * 0.28f;

    const float signedEdge = Visibility - dissolveNoise;
    clip(signedEdge);

    const float edge = 1.0f - smoothstep(
        0.0f,
        max(EdgeWidth, 0.001f),
        signedEdge);

    const float distanceFromCamera = length(input.viewPos);
    float flashlight = 0.0f;
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > 0.001f)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float cone = smoothstep(
            Light.SpotParams.y,
            Light.SpotParams.x,
            dot(pixelDirection, normalize(Light.Direction.xyz)));
        const float rangeFade = saturate(
            1.0f - distanceFromCamera / max(Light.Range, 0.001f));
        flashlight = cone * rangeFade * rangeFade;
    }

    const float pulse = sin(DissolveTime * 23.0f) * 0.5f + 0.5f;
    const float3 silhouette = float3(0.006f, 0.009f, 0.008f) +
        flashlight * float3(0.025f, 0.032f, 0.027f);
    const float3 burningEdge =
        float3(1.15f, 0.055f, 0.018f) * edge * (0.72f + pulse * 0.28f);

    return float4(silhouette + burningEdge, 1.0f);
}
