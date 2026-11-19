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
// サンプル数はテーブルとループで揃え、同じ重みで平均する。
#define POINT_SHADOW_TAP_COUNT 4
#define POINT_SHADOW_TAP_WEIGHT 0.25f
static const float2 g_PointShadowTaps[POINT_SHADOW_TAP_COUNT] =
{
    float2(-1.0f, -0.4f), float2(0.4f, -1.0f), float2(1.0f, 0.4f), float2(-0.4f, 1.0f)
};

// 影を作る電球を位置で照合するときの許容距離。
#define POINT_SHADOW_LIGHT_MATCH_RADIUS 0.5f

// 面を少し浮かせ、接触部の自己遮蔽を緩める。
#define POINT_SHADOW_SURFACE_NORMAL_BIAS 0.8f

// サンプルの散らばりと比較時の距離バイアス。
#define POINT_SHADOW_SAMPLE_SPREAD 0.010f
#define POINT_SHADOW_DEPTH_BIAS 1.2f

// この点光源から、この位置へ光が届く割合（0〜1）を返している。
// 影を作っている電球の光（位置が PointShadowLight と同じ）でなければ、いつも1を返す。
// 裸電球の影を使うかのしきい値、0で割らないための下限、補助の上向きを横向きに替える縦の向きの上限
#define POINT_SHADOW_ENABLE_THRESHOLD 0.5f
#define POINT_SHADOW_MIN_DIVISOR 0.001f
#define POINT_SHADOW_UP_FALLBACK_LIMIT 0.99f

float GetPointLightShadow(float3 worldPosition, float3 worldNormal, float3 lightPosition)
{
    float visibility = 1.0f;
    [branch]
    if (PointShadowOptions.x > POINT_SHADOW_ENABLE_THRESHOLD && distance(lightPosition, PointShadowLight.xyz) < POINT_SHADOW_LIGHT_MATCH_RADIUS)
    {
        const float range = max(PointShadowLight.w, POINT_SHADOW_MIN_DIVISOR);
        // 面から少し浮かせた位置で比べ、自分自身の面で影ができる（にきびのような模様）のを防いでいる
        const float3 toPixel = worldPosition + normalize(worldNormal) * POINT_SHADOW_SURFACE_NORMAL_BIAS - PointShadowLight.xyz;
        const float pixelDistance = length(toPixel);
        const float3 direction = toPixel / max(pixelDistance, POINT_SHADOW_MIN_DIVISOR);
        const float3 helper = abs(direction.y) < POINT_SHADOW_UP_FALLBACK_LIMIT ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        const float3 tangent = normalize(cross(direction, helper));
        const float3 bitangent = cross(direction, tangent);

        float lit = 0.0f;
        [unroll]
        for (int tap = 0; tap < POINT_SHADOW_TAP_COUNT; ++tap)
        {
            const float3 sampleDirection = direction +
                (tangent * g_PointShadowTaps[tap].x + bitangent * g_PointShadowTaps[tap].y) * POINT_SHADOW_SAMPLE_SPREAD;
            // キューブマップの面は左手系、このゲームの座標は右手系なので、z を反転して読んでいる（ReflectionProbe.cpp と同じ）
            const float storedDistance = g_PointShadowMap.SampleLevel(g_SamplerState,
                float3(sampleDirection.x, sampleDirection.y, -sampleDirection.z), 0.0f) * range;
            lit += pixelDistance - POINT_SHADOW_DEPTH_BIAS <= storedDistance ? 1.0f : 0.0f;
        }
        visibility = lit * POINT_SHADOW_TAP_WEIGHT;
    }
    return visibility;
}

#endif
