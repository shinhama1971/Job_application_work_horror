// 床と壁が共有する懐中電灯の配光・半球環境光。common.hlslとfastNoise.hlsliの後に読み込みます。
#ifndef FLASHLIGHT_LIGHTING_INCLUDED
#define FLASHLIGHT_LIGHTING_INCLUDED

float3 GetHemisphereAmbient(float3 worldNormal)
{
    const float skyAmount = saturate(normalize(worldNormal).y * 0.5f + 0.5f);
    const float3 tint = lerp(float3(0.84f, 0.82f, 0.78f),
        float3(0.92f, 0.98f, 1.06f), skyAmount);
    return Light.Ambient.rgb * tint * lerp(0.90f, 1.06f, skyAmount);
}

float GetFlashlightBeamProfile(float3 pixelDirection)
{
    const float coneDot = dot(pixelDirection, normalize(Light.Direction.xyz));
    const float cone = smoothstep(Light.SpotParams.y, Light.SpotParams.x, coneDot);
    const float shapedCone = pow(saturate(cone), max(Light.SpotParams.z, 0.01f));
    return shapedCone * lerp(0.82f, 1.08f, smoothstep(0.38f, 1.0f, cone));
}

float GetFlashlightLensPattern(float3 pixelDirection)
{
    const float outerCosine = max(Light.SpotParams.y, 0.05f);
    const float outerTangent =
        sqrt(saturate(1.0f - outerCosine * outerCosine)) / outerCosine;
    // 揺れるライトの方向を基準にし、ホットスポットと外周を一致させます。
    const float3 forward = normalize(Light.Direction.xyz);
    const float3 referenceUp = abs(forward.y) < 0.99f
        ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 right = normalize(cross(referenceUp, forward));
    const float3 up = cross(forward, right);
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

    // 外周は少し暗く、中心は明るい実物の懐中電灯に近い配光です。
    return saturate(
        0.90f + centerHotspot * 0.16f + lensDirt);
}

#endif
