// ============================================================================
// 共通の処理: 床と壁が共有する、懐中電灯の光の形（円錐・レンズの模様）と、上下で色を変えた環境光。
// common.hlslとfastNoise.hlsliの後に読み込んでいる。
// ============================================================================
#ifndef FLASHLIGHT_LIGHTING_INCLUDED
#define FLASHLIGHT_LIGHTING_INCLUDED

// 上を向いた面はわずかに青白く、下を向いた面はわずかに暖かい色にした環境光を返している
float3 GetHemisphereAmbient(float3 worldNormal)
{
    const float skyAmount = saturate(normalize(worldNormal).y * 0.5f + 0.5f);
    const float3 tint = lerp(float3(0.84f, 0.82f, 0.78f),
        float3(0.92f, 0.98f, 1.06f), skyAmount);
    return Light.Ambient.rgb * tint * lerp(0.90f, 1.06f, skyAmount);
}

// 懐中電灯の円錐の中での明るさ（内側の円錐で最大、外側の円錐で0）を返している
float GetFlashlightBeamProfile(float3 pixelDirection)
{
    const float coneDot = dot(pixelDirection, normalize(Light.Direction.xyz));
    const float cone = smoothstep(Light.SpotParams.y, Light.SpotParams.x, coneDot);
    const float shapedCone = pow(saturate(cone), max(Light.SpotParams.z, 0.01f));
    return shapedCone * lerp(0.82f, 1.08f, smoothstep(0.38f, 1.0f, cone));
}

// 懐中電灯の光の色：中心は少し青みのある白、縁へ行くほど黄色く暖かくにじむ（安い懐中電灯の光。サイレントヒルの懐中電灯を参考）
float3 GetFlashlightTint(float3 pixelDirection)
{
    const float coneDot = dot(pixelDirection, normalize(Light.Direction.xyz));
    const float cone = smoothstep(Light.SpotParams.y, Light.SpotParams.x, coneDot);
    const float edge = smoothstep(0.15f, 1.0f, 1.0f - cone);
    return lerp(float3(0.96f, 1.0f, 1.07f), float3(1.10f, 0.88f, 0.58f), edge);
}

// 懐中電灯のレンズの模様：中心を明るくし、レンズの汚れのような細かいむら、反射鏡の輪、指紋のくもりと傷の影を付けている。
// 電池が弱るほど（Light.SpotParams.w）、中心が暗くなり、輪のむらがはっきりする
float GetFlashlightLensPattern(float3 pixelDirection)
{
    const float outerCosine = max(Light.SpotParams.y, 0.05f);
    const float outerTangent =
        sqrt(saturate(1.0f - outerCosine * outerCosine)) / outerCosine;
    // 揺れるライトの向きを基準にし、明るい中心と外周を一致させている。
    const float3 forward = normalize(Light.Direction.xyz);
    const float3 referenceUp = abs(forward.y) < 0.99f
        ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 right = normalize(cross(referenceUp, forward));
    const float3 up = cross(forward, right);
    // 画素の方向を、ライトの円錐の中の2次元の座標（外周が半径1）にしている
    const float2 lensUV = float2(dot(pixelDirection, right), dot(pixelDirection, up)) /
        max(dot(pixelDirection, forward) * outerTangent, 0.001f);
    const float radius = length(lensUV);

    const float centerHotspot =
        1.0f - smoothstep(0.0f, 0.78f, radius);
    const float patternFade =
        1.0f - smoothstep(0.38f, 1.02f, radius);
    const float largeDust = FastValueNoise(lensUV * 6.2f + 13.7f);
    const float fineDust = FastValueNoise(lensUV * 17.0f - 5.2f);
    const float lensDirt =
        ((largeDust - 0.5f) * 0.030f +
         (fineDust - 0.5f) * 0.012f) * patternFade;

    // 電池の弱り具合（0〜1。Player が SpotParams.w で渡している）。弱るほど、光のむらがはっきりする
    const float weakness = saturate(Light.SpotParams.w);
    // 反射鏡の同心円の輪（ほんのりした明暗）と、明るい中心のすぐ外側の暗い輪（安い懐中電灯の光の形）
    const float rings = sin(radius * 31.0f) * 0.5f + 0.5f;
    const float ringPattern =
        (rings - 0.5f) * lerp(0.030f, 0.075f, weakness) * smoothstep(0.15f, 0.5f, radius) * patternFade;
    const float darkRingOffset = (radius - 0.30f) / 0.06f;
    const float darkRing = -exp(-darkRingOffset * darkRingOffset) * lerp(0.045f, 0.10f, weakness);
    // レンズに残った指紋のようなくもり（右上が少し暗い）と、1本の細い傷の影
    const float smudge = -(1.0f - smoothstep(0.0f, 1.0f,
        length((lensUV - float2(0.35f, 0.25f)) / float2(0.35f, 0.22f)))) * 0.06f;
    const float scratch = -(1.0f - smoothstep(0.0f, 0.012f, abs(lensUV.y - lensUV.x * 0.4f + 0.18f))) *
        0.07f * step(abs(lensUV.x), 0.6f);

    // 外周は少し暗く、中心は明るい、本物の懐中電灯に近い光の形にしている。電池が弱ると、中心の明るさが落ちる
    return saturate(
        0.90f + centerHotspot * lerp(0.16f, 0.06f, weakness) +
        lensDirt * (1.0f + weakness) + ringPattern + darkRing + smudge + scratch);
}

#endif
