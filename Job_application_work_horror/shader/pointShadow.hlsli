// ============================================================================
// 共通の処理: 揺れる裸電球（2面）の影。電球の位置から6方向を撮った「電球からの距離」のキューブマップ（PointShadowMap）と比べ、
// 電球との間に物があれば、その電球の光を届けないようにしている。litTexturePS と wetFloorPS で使っている。
// flashlightShadow.hlsli（b8の PointShadowLight・PointShadowOptions）と、g_SamplerState（s0）の後に読み込んでいる。
// ============================================================================
#ifndef POINT_SHADOW_INCLUDED
#define POINT_SHADOW_INCLUDED

// 電球からの距離（光の届く範囲で割った0〜1）を6方向に書いたキューブマップ
TextureCube<float> g_PointShadowMap : register(t2);

// 少しずらして読む4つの向き（影の縁を少し柔らかくする）
static const float2 g_PointShadowTaps[4] =
{
    float2(-1.0f, -0.4f), float2(0.4f, -1.0f), float2(1.0f, 0.4f), float2(-0.4f, 1.0f)
};

// この点光源から、この位置へ光が届く割合（0〜1）を返している。
// 影を作っている電球の光（位置が PointShadowLight と同じ）でなければ、いつも1を返す。
float GetPointLightShadow(float3 worldPosition, float3 worldNormal, float3 lightPosition)
{
    float visibility = 1.0f;
    [branch]
    if (PointShadowOptions.x > 0.5f && distance(lightPosition, PointShadowLight.xyz) < 0.5f)
    {
        const float range = max(PointShadowLight.w, 0.001f);
        // 面から少し浮かせた位置で比べ、自分自身の面で影ができる（にきびのような模様）のを防いでいる
        const float3 toPixel = worldPosition + normalize(worldNormal) * 0.8f - PointShadowLight.xyz;
        const float pixelDistance = length(toPixel);
        const float3 direction = toPixel / max(pixelDistance, 0.001f);
        const float3 helper = abs(direction.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        const float3 tangent = normalize(cross(direction, helper));
        const float3 bitangent = cross(direction, tangent);

        float lit = 0.0f;
        [unroll]
        for (int tap = 0; tap < 4; ++tap)
        {
            const float3 sampleDirection = direction +
                (tangent * g_PointShadowTaps[tap].x + bitangent * g_PointShadowTaps[tap].y) * 0.010f;
            // キューブマップの面は左手系、このゲームの座標は右手系なので、z を反転して読んでいる（ReflectionProbe.cpp と同じ）
            const float storedDistance = g_PointShadowMap.SampleLevel(g_SamplerState,
                float3(sampleDirection.x, sampleDirection.y, -sampleDirection.z), 0.0f) * range;
            lit += pixelDistance - 1.2f <= storedDistance ? 1.0f : 0.0f;
        }
        visibility = lit * 0.25f;
    }
    return visibility;
}

#endif
