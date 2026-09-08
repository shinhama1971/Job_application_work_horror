// ============================================================================
// シェーダーの役割: 濡れた床の反射、フレネル、波紋、粗さを計算します。
// ============================================================================

#include "common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"

Texture2D g_PlanarReflection : register(t6);

cbuffer WetFloorBuffer : register(b10)
{
    float WetTime;
    float RippleStrength;
    float ReflectionStrength;
    float WetPadding;
}

struct LIT_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
    float4 shadowPos : TEXCOORD6;
    float4 reflectionPos : TEXCOORD7;
};


float Hash21(float2 value)
{
    return frac(sin(dot(value, float2(127.1f, 311.7f))) * 43758.5453f);
}

float ValueNoise(float2 value)
{
    const float2 cell = floor(value);
    float2 blend = frac(value);
    blend = blend * blend * (3.0f - 2.0f * blend);

    const float a = Hash21(cell);
    const float b = Hash21(cell + float2(1.0f, 0.0f));
    const float c = Hash21(cell + float2(0.0f, 1.0f));
    const float d = Hash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);
}

#include "fastNoise.hlsli"


float GetFlashlightLensPattern(float3 pixelDirection)
{
    const float outerCosine = max(Light.SpotParams.y, 0.05f);
    const float outerTangent =
        sqrt(saturate(1.0f - outerCosine * outerCosine)) / outerCosine;
    const float2 lensUV = pixelDirection.xy /
        max(pixelDirection.z * outerTangent, 0.001f);
    const float radius = length(lensUV);

    const float centerHotspot =
        1.0f - smoothstep(0.0f, 0.78f, radius);
    const float patternFade =
        1.0f - smoothstep(0.38f, 1.02f, radius);
    const float largeDust = ValueNoise(lensUV * 6.2f + 13.7f);
    const float fineDust = ValueNoise(lensUV * 17.0f - 5.2f);
    const float lensDirt =
        ((largeDust - 0.5f) * 0.030f +
         (fineDust - 0.5f) * 0.012f) * patternFade;

    return saturate(
        0.965f + centerHotspot * 0.035f + lensDirt);
}

float3 ApplyFilmicHorrorGrade(float3 color)
{
    color = max(color, 0.0f);
    const float3 acesColor = saturate(
        (color * (2.51f * color + 0.03f)) /
        (color * (2.43f * color + 0.59f) + 0.14f));
    color = lerp(color, acesColor, 0.28f);

    const float luminance = dot(
        color,
        float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luminance.xxx, color, 0.86f);
    const float shadowWeight =
        1.0f - smoothstep(0.08f, 0.42f, luminance);
    const float highlightWeight =
        smoothstep(0.38f, 0.90f, luminance);
    color *= lerp(
        1.0f.xxx,
        float3(0.91f, 0.96f, 1.035f),
        shadowWeight * 0.48f);
    color *= lerp(
        1.0f.xxx,
        float3(1.025f, 0.995f, 0.955f),
        highlightWeight * 0.36f);
    return saturate(color);
}

float2 Hash22(float2 value)
{
    const float first = Hash21(value + float2(17.3f, 41.7f));
    const float second = Hash21(value + float2(93.1f, 11.8f));
    return float2(first, second);
}

float GetDripRing(float2 worldPosition)
{
    // Sparse drops make the puddles feel alive without looking like rain.
    const float cellSize = 15.0f;
    const float2 cell = floor(worldPosition / cellSize);
    const float2 localPosition = frac(worldPosition / cellSize);
    const float2 dropPosition = 0.18f + Hash22(cell) * 0.64f;
    const float dropSeed = Hash21(cell + float2(31.7f, 19.3f));
    const float activeDrop = step(0.76f, dropSeed);
    const float phase = frac(WetTime * 0.18f + dropSeed);
    const float radius = phase * 0.48f;
    const float distanceToDrop = length(localPosition - dropPosition);
    const float ring = 1.0f - smoothstep(
        0.012f,
        0.035f,
        abs(distanceToDrop - radius));
    return ring * activeDrop * (1.0f - phase);
}

float GetPuddleMask(
    float2 worldPosition,
    out float shore,
    out float ripplePattern)
{
    shore = 0.0f;
    ripplePattern = 0.0f;
    // Each large world-space cell may contain one irregular, rotated pool.
    // Keeping the radius below the cell boundary makes separate puddles
    // instead of turning the entire floor into one uniformly wet surface.
    const float cellSize = 82.0f;
    const float2 gridPosition = worldPosition / cellSize;
    const float2 cell = floor(gridPosition);
    float2 localPosition = frac(gridPosition) - 0.5f;

    const float hasPuddle = step(
        0.62f,
        Hash21(cell + float2(53.4f, 27.9f)));
    // Most floor cells are dry. Avoid all rotation, edge-noise and wave work
    // in those cells; the branch is coherent over a large world-space tile.
    if (hasPuddle < 0.5f)
    {
        shore = 0.0f;
        ripplePattern = 0.0f;
        return 0.0f;
    }

    const float2 randomValue = Hash22(cell);
    localPosition -= (randomValue - 0.5f) * 0.22f;

    const float angle = randomValue.x * 6.2831853f;
    const float cosine = cos(angle);
    const float sine = sin(angle);
    const float2 rotated = float2(
        localPosition.x * cosine - localPosition.y * sine,
        localPosition.x * sine + localPosition.y * cosine);
    const float2 axes = float2(
        0.72f + randomValue.x * 0.25f,
        0.43f + randomValue.y * 0.20f);

    const float radialDistance = length(rotated / axes);
    const float polarAngle = atan2(rotated.y, rotated.x);
    const float organicLobes =
        sin(polarAngle * 5.0f + randomValue.x * 9.0f) * 0.045f +
        sin(polarAngle * 9.0f + randomValue.y * 13.0f) * 0.022f;
    const float edgeWarp =
        (FastFractalNoise3(worldPosition * 0.052f + cell * 1.73f) - 0.5f) * 0.22f +
        organicLobes;
    const float irregularDistance = radialDistance + edgeWarp;

    const float puddle =
        (1.0f - smoothstep(0.34f, 0.40f, irregularDistance)) * hasPuddle;
    shore = (1.0f - smoothstep(
        0.012f,
        0.070f,
        abs(irregularDistance - 0.37f))) * hasPuddle;
    const float slowWave = sin(
        radialDistance * 48.0f + edgeWarp * 18.0f - WetTime * 1.65f);
    const float crossingWave = sin(
        rotated.x * 76.0f - rotated.y * 41.0f + WetTime * 1.10f);
    ripplePattern =
        (slowWave * 0.72f + crossingWave * 0.28f) * puddle;
    return puddle;
}

float4 main(in LIT_PS_IN input) : SV_Target
{
    float4 color = input.col;

    if (Material.TextureEnable)
    {
        // The source image is visually close to grass. A positive mip bias
        // removes its harsh high-frequency grain, then a restrained palette
        // turns it into damp, dirty concrete without requiring a new asset.
        const float3 sampledFloor = g_Texture.SampleBias(
            g_SamplerState,
            input.tex,
            1.15f).rgb;
        const float floorLuminance = dot(
            sampledFloor,
            float3(0.2126f, 0.7152f, 0.0722f));
        float3 concreteFloor = lerp(
            floorLuminance.xxx,
            sampledFloor,
            0.10f);
        const float broadVariation = saturate(FastFractalNoise3(
            input.worldPos.xz * 0.020f + 6.4f));
        concreteFloor *= float3(0.72f, 0.75f, 0.73f) *
            lerp(0.88f, 1.04f, broadVariation);
        color.rgb *= concreteFloor;
    }
    else
    {
        color *= Material.Diffuse;
    }

    float shore = 0.0f;
    float ripplePattern = 0.0f;
    float puddle = GetPuddleMask(
        input.worldPos.xz,
        shore,
        ripplePattern);
    puddle *= smoothstep(0.55f, 0.92f, saturate(input.worldNormal.y));
    shore *= smoothstep(0.55f, 0.92f, saturate(input.worldNormal.y));

    float dripRing = 0.0f;
    float3 detailWorldNormal = normalize(input.worldNormal);
    // Animated normal reconstruction is useful only on the puddle and its
    // narrow shoreline. Dry concrete now avoids three fractal-noise calls.
    [branch]
    if (puddle > 0.001f || shore > 0.001f)
    {
        dripRing = GetDripRing(input.worldPos.xz) * puddle;
        ripplePattern += dripRing * 1.35f;
        const float2 animatedNoiseOffset = float2(
            WetTime * 0.018f,
            -WetTime * 0.013f);
        const float ripple = FastFractalNoise3(
            input.worldPos.xz * 0.095f + animatedNoiseOffset);
        const float rippleX = FastFractalNoise3(
            input.worldPos.xz * 0.095f + animatedNoiseOffset +
            float2(0.035f, 0.0f));
        const float rippleZ = FastFractalNoise3(
            input.worldPos.xz * 0.095f + animatedNoiseOffset +
            float2(0.0f, 0.035f));
        detailWorldNormal = normalize(
            input.worldNormal +
            float3(ripple - rippleX, 0.0f, ripple - rippleZ) *
            (0.72f + abs(ripplePattern) * 0.55f) * puddle * RippleStrength);
    }

    const float baseLuminance = dot(
        color.rgb,
        float3(0.2126f, 0.7152f, 0.0722f));
    // Shallow indoor water should reveal the floor when viewed from above.
    // Darkening it too much makes the puddle read as a painted black decal.
    const float3 wetColor = lerp(
        color.rgb * 0.76f,
        color.rgb * 0.58f +
            float3(0.018f, 0.030f, 0.034f) + baseLuminance.xxx * 0.035f,
        0.38f);
    color.rgb = lerp(color.rgb, wetColor, puddle * 0.84f);
    color.rgb *= 1.0f - shore * 0.035f;

    float3 lighting = Light.Ambient.rgb;
    float3 specularLighting = 0.0f;
    const float distanceFromCamera = length(input.viewPos);
    const float3 viewDirection = distanceFromCamera > 0.001f
        ? -input.viewPos / distanceFromCamera
        : float3(0.0f, 1.0f, 0.0f);
    const float fresnel = pow(
        1.0f - saturate(dot(normalize(input.viewNormal), viewDirection)),
        4.0f);

    // Ceiling point lights illuminate nearby floors and walls, not only the panels.
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        if (i >= EnvironmentLightCount)
        {
            break;
        }

        const float3 offsetToLight =
            EnvironmentLights[i].PositionRange.xyz - input.worldPos;
        const float lightRange = max(EnvironmentLights[i].PositionRange.w, 0.001f);
        const float distanceSquaredToLight = dot(offsetToLight, offsetToLight);
        [branch]
        if (distanceSquaredToLight >= lightRange * lightRange)
        {
            continue;
        }
        const float distanceToLight = sqrt(distanceSquaredToLight);
        const float3 directionToPointLight =
            offsetToLight / max(distanceToLight, 0.001f);
        float pointAttenuation = saturate(1.0f - distanceToLight / lightRange);
        pointAttenuation *= pointAttenuation;

        const float pointLambert = saturate(dot(
            detailWorldNormal, directionToPointLight));
        const float softPointLambert = 0.20f + pointLambert * 0.80f;
        // Ceiling panels are broad downward emitters, not bare point bulbs.
        // Keep a little sideways spill while concentrating energy on the floor.
        const float downwardAmount = saturate(directionToPointLight.y);
        const float fixtureDistribution = lerp(
            0.22f,
            1.0f,
            smoothstep(0.04f, 0.72f, downwardAmount));

        lighting += EnvironmentLights[i].ColorIntensity.rgb
            * EnvironmentLights[i].ColorIntensity.a
            * pointAttenuation
            * softPointLambert
            * fixtureDistribution;

        specularLighting += EnvironmentLights[i].ColorIntensity.rgb
            * EnvironmentLights[i].ColorIntensity.a
            * pointAttenuation
            * pow(pointLambert, 12.0f)
            * puddle * 0.58f;

        // A soft footprint makes ceiling fixtures visibly reflect in water.
        const float horizontalDistance = length(offsetToLight.xz);
        const float reflectedFixture = pow(saturate(
            1.0f - horizontalDistance / max(lightRange * 0.46f, 0.001f)),
            4.5f);
        specularLighting += EnvironmentLights[i].ColorIntensity.rgb
            * EnvironmentLights[i].ColorIntensity.a
            * reflectedFixture * puddle * 0.38f;
    }

    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > 0.001f)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float3 flashlightDirection = normalize(Light.Direction.xyz);
        const float coneDot = dot(pixelDirection, flashlightDirection);
        const float cone = smoothstep(Light.SpotParams.y, Light.SpotParams.x, coneDot);
        const float shapedCone = pow(saturate(cone), max(Light.SpotParams.z, 0.01f));

        const float normalizedDistance = saturate(
            distanceFromCamera / max(Light.Range, 0.001f)
        );
        const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
        const float attenuation = rangeFade * rangeFade;
        const float lensPattern = GetFlashlightLensPattern(pixelDirection);
        const float physicalFalloff = rcp(
            1.0f + distanceFromCamera * distanceFromCamera * 0.000018f);
        const float naturalAttenuation = attenuation *
            lerp(1.0f, physicalFalloff, 0.32f);

        const float3 normal = normalize(input.viewNormal);
        const float3 directionToLight = -pixelDirection;
        const float lambert = saturate(dot(normal, directionToLight));
        const float softenedLambert = 0.25f + lambert * 0.75f;

        const float shadow = GetFlashlightShadow(input.shadowPos);
        const float flashlightAmount = Light.Intensity
            * shapedCone * naturalAttenuation * lensPattern * shadow;

        lighting += Light.Diffuse.rgb
            * flashlightAmount
            * softenedLambert;

        const float3 halfVector = normalize(directionToLight + viewDirection);
        const float wetSpecular = pow(
            saturate(dot(normal, halfVector)),
            lerp(18.0f, 92.0f, puddle));
        specularLighting += Light.Diffuse.rgb
            * flashlightAmount
            * wetSpecular
            * lerp(0.025f, 1.15f, puddle);

        // Even when the exact mirror angle misses the camera, shallow water
        // returns a broad, weak flashlight reflection instead of becoming black.
        specularLighting += Light.Diffuse.rgb
            * flashlightAmount
            * puddle
            * (0.045f + fresnel * 0.12f);
    }

    color.rgb *= lighting;
    specularLighting += float3(0.19f, 0.25f, 0.28f)
        * puddle * (0.15f + fresnel * 0.76f);
    const float rippleHighlight = pow(
        saturate(ripplePattern * 0.5f + 0.5f),
        10.0f) * puddle;
    specularLighting += float3(0.12f, 0.16f, 0.17f) * rippleHighlight * 0.22f;
    specularLighting += float3(0.28f, 0.34f, 0.35f)
        * dripRing * 0.20f * RippleStrength;
    specularLighting += float3(0.10f, 0.13f, 0.125f)
        * shore * (0.24f + fresnel * 0.46f);
    color.rgb += specularLighting;
    color.rgb += Material.Emission.rgb;

    // Project the world position into the mirrored camera image. This is a
    // real scene reflection; the normal only adds a small water distortion.
    const float reflectionW = max(input.reflectionPos.w, 0.0001f);
    const float3 reflectionNdc =
        input.reflectionPos.xyz / reflectionW;
    float2 reflectionUV = float2(
        reflectionNdc.x * 0.5f + 0.5f,
        -reflectionNdc.y * 0.5f + 0.5f);
    const float reflectionInside =
        step(0.0f, reflectionUV.x) *
        step(reflectionUV.x, 1.0f) *
        step(0.0f, reflectionUV.y) *
        step(reflectionUV.y, 1.0f) *
        step(0.0f, reflectionNdc.z) *
        step(reflectionNdc.z, 1.0f) *
        step(0.0001f, input.reflectionPos.w);

    const float2 waterDistortion =
        detailWorldNormal.xz *
        (0.0045f + abs(ripplePattern) * 0.0022f) *
        puddle * RippleStrength;
    reflectionUV = saturate(reflectionUV + waterDistortion);

    // Fresnel behavior: looking down mostly shows the floor beneath the
    // water; grazing angles strongly show the mirrored room and fixtures.
    // The square-root remap makes mid-angle reflections readable while the
    // low minimum prevents the puddle from returning to a black decal.
    const float viewAngleReflection = sqrt(saturate(fresnel));
    const float reflectionStrength = saturate(
        puddle * reflectionInside *
        lerp(0.24f, 0.98f, viewAngleReflection) * ReflectionStrength);
    float3 reflectedScene = 0.0f;
    [branch]
    if (reflectionStrength > 0.001f)
    {
        reflectedScene =
            g_PlanarReflection.Sample(g_SamplerState, reflectionUV).rgb;
    }
    const float reflectionGain = 1.10f + rippleHighlight * 0.08f;
    color.rgb = lerp(
        color.rgb,
        reflectedScene * reflectionGain + float3(0.023f, 0.032f, 0.037f),
        reflectionStrength);

    // Layered height fog keeps nearby navigation readable while separating
    // distant silhouettes. It gathers near the floor instead of uniformly
    // washing out the entire room.
    const float distanceFog =
        smoothstep(110.0f, 390.0f, distanceFromCamera) * 0.72f;
    const float heightFromFloor = max(input.worldPos.y + 100.0f, 0.0f);
    const float heightDensity = exp(-heightFromFloor * 0.055f);
    const float nearSuppression =
        smoothstep(35.0f, 150.0f, distanceFromCamera);
    const float fogVariation = 0.78f +
        saturate(FastFractalNoise3(input.worldPos.xz * 0.018f + 21.0f)) * 0.22f;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * 0.22f;
    const float fogFactor = saturate(distanceFog + heightFog);
    const float3 fogColor = max(
        Light.Ambient.rgb * 0.38f,
        float3(0.070f, 0.078f, 0.084f));
    color.rgb = lerp(color.rgb, fogColor, fogFactor);
    color.rgb = ApplyFilmicHorrorGrade(color.rgb);

    if (DebugViewMode == 1)
    {
        return float4(detailWorldNormal * 0.5f + 0.5f, 1.0f);
    }
    if (DebugViewMode == 2)
    {
        const float shadowVisibility =
            GetFlashlightShadow(input.shadowPos);
        return float4(shadowVisibility.xxx, 1.0f);
    }
    if (DebugViewMode == 3)
    {
        return float4(saturate(lighting * 0.5f), 1.0f);
    }
    if (DebugViewMode == 4)
    {
        return float4(puddle.xxx, 1.0f);
    }
    if (DebugViewMode == 5)
    {
        return float4(reflectedScene, 1.0f);
    }
    if (DebugViewMode >= 6)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return color;
}

