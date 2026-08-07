#include "common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
Texture2D<float> g_FlashlightShadowMap : register(t5);
Texture2D g_PlanarReflection : register(t6);
SamplerComparisonState g_ShadowSampler : register(s1);

cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

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

    // A rotated Poisson disk avoids the square pattern of a 3x3 kernel.
    // The radius grows with receiver depth, imitating a small flashlight bulb.
    static const float2 poissonDisk[12] =
    {
        float2(-0.326f, -0.406f), float2(-0.840f, -0.074f),
        float2(-0.696f,  0.457f), float2(-0.203f,  0.621f),
        float2( 0.962f, -0.195f), float2( 0.473f, -0.480f),
        float2( 0.519f,  0.767f), float2( 0.185f, -0.893f),
        float2( 0.507f,  0.064f), float2( 0.896f,  0.412f),
        float2(-0.322f, -0.933f), float2(-0.792f, -0.598f)
    };

    const float rotationNoise = frac(sin(dot(
        floor(shadowUV * 512.0f),
        float2(12.9898f, 78.233f))) * 43758.5453f);
    const float angle = rotationNoise * 6.2831853f;
    const float cosine = cos(angle);
    const float sine = sin(angle);
    const float receiverDepth = saturate(
        (projected.z - 0.04f) / 0.86f);
    const float filterRadius = ShadowParameters.x *
        lerp(1.20f, 3.35f, receiverDepth);
    const float receiverBias = ShadowParameters.y *
        lerp(1.10f, 0.82f, receiverDepth);

    float visibility = 0.0f;
    [unroll]
    for (int sampleIndex = 0; sampleIndex < 12; ++sampleIndex)
    {
        const float2 sampleOffset = poissonDisk[sampleIndex];
        const float2 rotatedOffset = float2(
            sampleOffset.x * cosine - sampleOffset.y * sine,
            sampleOffset.x * sine + sampleOffset.y * cosine);
        visibility += g_FlashlightShadowMap.SampleCmpLevelZero(
            g_ShadowSampler,
            shadowUV + rotatedOffset * filterRadius,
            projected.z - receiverBias);
    }

    return visibility / 12.0f;
}

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

float FractalNoise(float2 value)
{
    float result = 0.0f;
    float amplitude = 0.55f;
    [unroll]
    for (int octave = 0; octave < 4; ++octave)
    {
        result += ValueNoise(value) * amplitude;
        value = value * 2.03f + 7.17f;
        amplitude *= 0.48f;
    }
    return result;
}

float GetFlashlightLensPattern(float3 pixelDirection)
{
    const float outerCosine = max(Light.SpotParams.y, 0.05f);
    const float outerTangent =
        sqrt(saturate(1.0f - outerCosine * outerCosine)) / outerCosine;
    const float2 lensUV = pixelDirection.xy /
        max(pixelDirection.z * outerTangent, 0.001f);
    const float radius = length(lensUV);

    const float centerHotspot = 1.0f - smoothstep(0.0f, 0.72f, radius);
    const float softRing = exp(-pow((radius - 0.54f) * 7.0f, 2.0f));
    const float largeDust = ValueNoise(lensUV * 4.8f + 13.7f);
    const float fineDust = ValueNoise(lensUV * 13.0f - 5.2f);
    const float lensDirt = (largeDust - 0.5f) * 0.11f +
        (fineDust - 0.5f) * 0.035f;

    return saturate(
        0.91f + centerHotspot * 0.13f + softRing * 0.035f + lensDirt);
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
    // Each large world-space cell may contain one irregular, rotated pool.
    // Keeping the radius below the cell boundary makes separate puddles
    // instead of turning the entire floor into one uniformly wet surface.
    const float cellSize = 82.0f;
    const float2 gridPosition = worldPosition / cellSize;
    const float2 cell = floor(gridPosition);
    float2 localPosition = frac(gridPosition) - 0.5f;

    const float2 randomValue = Hash22(cell);
    const float hasPuddle = step(
        0.62f,
        Hash21(cell + float2(53.4f, 27.9f)));
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
        (FractalNoise(worldPosition * 0.052f + cell * 1.73f) - 0.5f) * 0.22f +
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
        const float broadVariation = saturate(FractalNoise(
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

    const float dripRing = GetDripRing(input.worldPos.xz) * puddle;
    ripplePattern += dripRing * 1.35f;

    // Slowly scrolling derivatives distort the real planar reflection.
    const float2 animatedNoiseOffset = float2(
        WetTime * 0.018f,
        -WetTime * 0.013f);
    const float ripple = FractalNoise(
        input.worldPos.xz * 0.095f + animatedNoiseOffset);
    const float rippleX = FractalNoise(
        input.worldPos.xz * 0.095f + animatedNoiseOffset +
        float2(0.035f, 0.0f));
    const float rippleZ = FractalNoise(
        input.worldPos.xz * 0.095f + animatedNoiseOffset +
        float2(0.0f, 0.035f));
    const float3 detailWorldNormal = normalize(
        input.worldNormal +
        float3(ripple - rippleX, 0.0f, ripple - rippleZ) *
        (0.72f + abs(ripplePattern) * 0.55f) * puddle * RippleStrength);

    const float baseLuminance = dot(
        color.rgb,
        float3(0.2126f, 0.7152f, 0.0722f));
    const float3 wetColor = lerp(
        color.rgb * 0.58f,
        float3(0.22f, 0.27f, 0.285f) + baseLuminance.xxx * 0.10f,
        0.72f);
    color.rgb = lerp(color.rgb, wetColor, puddle * 0.92f);
    color.rgb *= 1.0f - shore * 0.10f;

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
        const float distanceToLight = length(offsetToLight);
        const float lightRange = max(EnvironmentLights[i].PositionRange.w, 0.001f);
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
            * puddle * 0.48f;

        // A soft footprint makes ceiling fixtures visibly reflect in water.
        const float horizontalDistance = length(offsetToLight.xz);
        const float reflectedFixture = pow(saturate(
            1.0f - horizontalDistance / max(lightRange * 0.42f, 0.001f)),
            5.0f);
        specularLighting += EnvironmentLights[i].ColorIntensity.rgb
            * EnvironmentLights[i].ColorIntensity.a
            * reflectedFixture * puddle * 0.22f;
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
    specularLighting += float3(0.16f, 0.22f, 0.24f)
        * puddle * (0.10f + fresnel * 0.58f);
    const float rippleHighlight = pow(
        saturate(ripplePattern * 0.5f + 0.5f),
        10.0f) * puddle;
    specularLighting += float3(0.12f, 0.16f, 0.17f) * rippleHighlight * 0.16f;
    specularLighting += float3(0.28f, 0.34f, 0.35f)
        * dripRing * 0.20f * RippleStrength;
    specularLighting += float3(0.10f, 0.13f, 0.125f) * shore;
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
        (0.010f + abs(ripplePattern) * 0.0045f) *
        puddle * RippleStrength;
    reflectionUV = saturate(reflectionUV + waterDistortion);

    const float3 reflectedScene =
        g_PlanarReflection.Sample(g_SamplerState, reflectionUV).rgb;
    const float reflectionStrength = puddle * reflectionInside *
        lerp(0.62f, 0.90f, fresnel) * ReflectionStrength;
    color.rgb = lerp(
        color.rgb,
        reflectedScene * 0.94f + float3(0.012f, 0.018f, 0.022f),
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
        saturate(FractalNoise(input.worldPos.xz * 0.018f + 21.0f)) * 0.22f;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * 0.22f;
    const float fogFactor = saturate(distanceFog + heightFog);
    const float3 fogColor = max(
        Light.Ambient.rgb * 0.38f,
        float3(0.070f, 0.078f, 0.084f));
    color.rgb = lerp(color.rgb, fogColor, fogFactor);

    return color;
}

