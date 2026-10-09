// ============================================================================
// 共通の処理: 懐中電灯のシャドウマップ、その定数バッファ、軽いPCF（周りの数か所を比べてぼかす）フィルター。
// litTexturePS と wetFloorPS の影の品質を、同じ場所で調整できるようにしている。
// ============================================================================

// シャドウマップ（t5）と、深度を比べて読む比較サンプラー（s1）
Texture2D<float> g_FlashlightShadowMap : register(t5);
SamplerComparisonState g_ShadowSampler : register(s1);

// ライトのビュー×射影の行列と、x=1テクセルの大きさ、y=比較のずれ、z=near、w=far（ShadowMap.cppと同じ並び）
cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

// 影の明るさ（0=影、1=光が当たる）を返している
float GetFlashlightShadow(float4 shadowPosition)
{
    // ライトの後ろや、シャドウマップの範囲の外は、影ではないことにしている
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

    // 5か所を比べることで、懐中電灯の影の縁の柔らかさを保ちつつ、前の12回の比較と
    // 画素ごとのsin/cosの回転をなくしている。中心の1か所で細い物の影を安定させ、
    // 形をずらした円の配置で、影の縁が四角く見えないようにしている。
    static const float2 poissonDisk[4] =
    {
        float2(-0.72f, -0.31f), float2(0.39f, -0.78f),
        float2(0.76f, 0.28f), float2(-0.28f, 0.73f)
    };
    const float receiverDepth = saturate(
        (projected.z - 0.04f) / 0.86f);
    // 遠くでも影を広げすぎず、物の輪郭を読み取れる柔らかさにとどめている。
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
