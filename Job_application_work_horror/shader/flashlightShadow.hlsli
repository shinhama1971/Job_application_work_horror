// ============================================================================
// 共通処理: 懐中電灯用シャドウマップ、定数バッファ、軽量PCFフィルター。
// litTexturePS と wetFloorPS の影品質を同じ場所で調整できます。
// ============================================================================

Texture2D<float> g_FlashlightShadowMap : register(t5);
SamplerComparisonState g_ShadowSampler : register(s1);

cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

float GetFlashlightShadow(float4 shadowPosition)
{
    if (shadowPosition.w <= 0.0f)
    {
        return 1.0f;
    }

    const float3 projected = shadowPosition.xyz / shadowPosition.w;
    const float2 shadowUV = float2(
        projected.x * 0.5f + 0.5f,
        -projected.y * 0.5f + 0.5f);

    if (shadowUV.x <= 0.0f || shadowUV.x >= 1.0f ||
        shadowUV.y <= 0.0f || shadowUV.y >= 1.0f ||
        projected.z <= 0.0f || projected.z >= 1.0f)
    {
        return 1.0f;
    }

    // 5 taps retain a soft flashlight edge while avoiding the old 12 texture
    // comparisons and per-pixel sin/cos rotation. The centre tap stabilises
    // thin geometry and the asymmetric disk prevents a square-looking edge.
    static const float2 poissonDisk[4] =
    {
        float2(-0.72f, -0.31f), float2(0.39f, -0.78f),
        float2(0.76f, 0.28f), float2(-0.28f, 0.73f)
    };
    const float receiverDepth = saturate(
        (projected.z - 0.04f) / 0.86f);
    // 遠距離でも影を広げすぎず、物体の輪郭を読み取れる柔らかさに留めます。
    const float filterRadius = ShadowParameters.x *
        lerp(0.85f, 2.25f, receiverDepth);
    const float receiverBias = ShadowParameters.y *
        lerp(1.10f, 0.82f, receiverDepth);

    float visibility = g_FlashlightShadowMap.SampleCmpLevelZero(
        g_ShadowSampler, shadowUV, projected.z - receiverBias) * 0.28f;
    [unroll]
    for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
    {
        visibility += g_FlashlightShadowMap.SampleCmpLevelZero(
            g_ShadowSampler,
            shadowUV + poissonDisk[sampleIndex] * filterRadius,
            projected.z - receiverBias) * 0.18f;
    }

    return visibility;
}
