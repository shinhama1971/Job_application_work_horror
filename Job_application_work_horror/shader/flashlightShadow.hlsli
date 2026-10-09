// ============================================================================
// 共通の処理: 懐中電灯のシャドウマップ、その定数バッファ、PCSS（遮る物から離れた所ほど影の縁を柔らかくする）フィルター。
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

// 懐中電灯のレンズの大きさ（ワールドの長さ。約10cm）。大きいほど影の縁がぼやける
static const float FlashlightLensSize = 2.0f;

// 遮る物の探索と比較に使う、円の中にばらけた8つの点（半径1）
static const float2 g_ShadowDisk[8] =
{
    float2(-0.613f, 0.617f), float2(0.680f, -0.160f), float2(-0.299f, -0.791f), float2(0.645f, 0.493f),
    float2(-0.651f, -0.133f), float2(0.421f, -0.713f), float2(0.077f, 0.946f), float2(0.912f, -0.215f)
};

// シャドウマップの深度（0〜1）を、ライトからの距離に戻している（near=ShadowParameters.z、far=ShadowParameters.w）
float LinearizeShadowDepth(float depth)
{
    const float nearPlane = ShadowParameters.z;
    const float farPlane = ShadowParameters.w;
    return nearPlane * farPlane / max(farPlane - depth * (farPlane - nearPlane), 0.001f);
}

// 2次元の値から0〜1の疑似乱数を作っている（円の配置を画素ごとに回すのに使う）
float ShadowHash21(float2 value)
{
    float3 value3 = frac(float3(value.xyx) * 0.1031f);
    value3 += dot(value3, value3.yzx + 33.33f);
    return frac((value3.x + value3.y) * value3.z);
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

    // PCSS（影の縁を、遮る物から離れた所ほど柔らかくする）。
    // 1. 周りのシャドウマップの深度を読み、受ける面より手前にある物（遮る物）の平均の距離を求める。
    // 2. 半影（ぼやけた縁）の幅は、光源の大きさ ×（受ける面までの距離 − 遮る物までの距離）÷ 遮る物までの距離。
    //    物の根元の影はくっきり、遠くの壁に落ちた影はぼんやりする。
    // 3. その幅の円の中で、比較を8か所行って平均する。
    const float receiverBias = ShadowParameters.y *
        lerp(1.10f, 0.82f, saturate((projected.z - 0.04f) / 0.86f));
    const float compareDepth = projected.z - receiverBias;
    const float texel = ShadowParameters.x;
    const float receiverDistance = LinearizeShadowDepth(projected.z);

    // 画素ごとに円の配置を回し、少ない回数でも縁が段々にならず、細かいざらつきになるようにしている。
    // 回し方はシャドウマップ上の位置から決め、カメラが止まっていれば模様も止まって見えるようにしている。
    const float angle = ShadowHash21(floor(shadowUV / texel)) * 6.2831853f;
    float sinAngle;
    float cosAngle;
    sincos(angle, sinAngle, cosAngle);
    const float2x2 rotation = float2x2(cosAngle, -sinAngle, sinAngle, cosAngle);

    // 1. 遮る物を探す範囲は、一番広い半影の幅に合わせて固定（約10テクセル）にしている
    const float2 shadowSize = float2(1.0f, 1.0f) / texel;
    float blockerSum = 0.0f;
    float blockerCount = 0.0f;
    [unroll]
    for (int searchIndex = 0; searchIndex < 8; ++searchIndex)
    {
        const float2 offset = mul(g_ShadowDisk[searchIndex], rotation) * (texel * 10.0f);
        const int2 texelPosition = int2(saturate(shadowUV + offset) * (shadowSize - 1.0f));
        const float occluderDepth = g_FlashlightShadowMap.Load(int3(texelPosition, 0));
        if (occluderDepth < compareDepth)
        {
            blockerSum += LinearizeShadowDepth(occluderDepth);
            blockerCount += 1.0f;
        }
    }
    // 周りに遮る物が1つも無ければ、比較をせずに「光が当たる」としている（影の外の大部分の画素はここで終わる）
    if (blockerCount <= 0.0f)
    {
        return 1.0f;
    }

    // 2. 半影の幅（ワールドの長さ）を、受ける面の距離でのシャドウマップ上の長さに直している。
    //    影の画角は66度なので、距離dでの幅は 2 × d × tan(33度)。
    const float blockerDistance = blockerSum / blockerCount;
    const float penumbraWidth = FlashlightLensSize *
        max(receiverDistance - blockerDistance, 0.0f) / max(blockerDistance, 0.001f);
    const float penumbraRadius = 0.5f * penumbraWidth /
        (2.0f * receiverDistance * 0.6494f);
    // 細い物の影が消えないよう最小1テクセル、ぼやけすぎないよう最大12テクセルにしている
    const float filterRadius = clamp(penumbraRadius, texel, texel * 12.0f);

    // 3. 中心と、回した8か所の比較を平均している
    float visibility = g_FlashlightShadowMap.SampleCmpLevelZero(
        g_ShadowSampler, shadowUV, compareDepth) * (1.0f / 9.0f);
    [unroll]
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex)
    {
        const float2 offset = mul(g_ShadowDisk[sampleIndex], rotation) * filterRadius;
        visibility += g_FlashlightShadowMap.SampleCmpLevelZero(
            g_ShadowSampler, shadowUV + offset, compareDepth) * (1.0f / 9.0f);
    }

    return visibility;
}
