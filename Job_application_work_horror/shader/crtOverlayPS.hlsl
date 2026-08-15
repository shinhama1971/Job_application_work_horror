struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

cbuffer TimeBuffer : register(b0)
{
    float time;
    float bloomIntensity;
    float noiseAmount;
    float vignetteStrength;
    float screenAspect;
    float volumeIntensity;
    float lensDistortionStrength;
    float horrorPulseStrength;
    float exposure;
    float lensMoisture;
    float corridorTension;
    float filmGradeStrength;
};

Texture2D SceneTexture : register(t0);
SamplerState LinearSampler : register(s0);

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float3 GetLensDrop(
    float2 uv,
    float2 center,
    float radius,
    float aspect)
{
    const float2 offset = (uv - center) * float2(aspect, 1.0f);
    const float distanceToCenter = length(offset);
    const float rim = 1.0f - smoothstep(
        0.004f,
        0.014f,
        abs(distanceToCenter - radius));
    const float body = 1.0f - smoothstep(
        radius * 0.58f,
        radius * 0.96f,
        distanceToCenter);
    const float highlight = (1.0f - smoothstep(
        radius * 0.16f,
        radius * 0.52f,
        length(offset - float2(-radius * 0.30f, -radius * 0.28f)))) *
        body;
    return float3(rim, body, highlight);
}

float2 GetLensDropRefraction(
    float2 uv,
    float2 center,
    float radius,
    float aspect)
{
    const float2 aspectOffset =
        (uv - center) * float2(aspect, 1.0f);
    const float distanceToCenter = length(aspectOffset);
    const float body = 1.0f - smoothstep(
        radius * 0.42f,
        radius * 0.96f,
        distanceToCenter);
    const float curvedEdge = smoothstep(
        radius * 0.10f,
        radius * 0.78f,
        distanceToCenter) * body;
    const float2 direction = aspectOffset /
        max(distanceToCenter, 0.001f);
    return direction * curvedEdge * radius * 0.24f *
        float2(1.0f / aspect, 1.0f);
}

float4 main(PS_IN input) : SV_TARGET
{
    const float2 center = float2(0.5f, 0.5f);
    float2 centered = input.uv - center;
    const float eventNoise = saturate(
        noiseAmount + horrorPulseStrength * 1.20f +
        corridorTension * 0.16f);
    const float eventVignette =
        vignetteStrength + horrorPulseStrength * 0.52f +
        corridorTension * 0.18f;

    // Soft edge darkening that does not hide gameplay information.
    float edgeDistance = length(centered * float2(1.15f, 1.0f));
    const float2 radialDirection = centered /
        max(length(centered), 0.001f);
    const float eventAberration = saturate(
        horrorPulseStrength +
        corridorTension * (0.16f + lensDistortionStrength * 0.24f));
    const float edgeAberration = smoothstep(0.20f, 0.72f, edgeDistance);
    const float2 aspectCentered =
        centered * float2(screenAspect, 1.0f);
    const float radialSquared = dot(aspectCentered, aspectCentered);
    const float radialWarp = lensDistortionStrength *
        (0.0020f + corridorTension * 0.0075f);
    const float2 distortedUv = saturate(
        input.uv + centered * radialSquared * radialWarp);
    const float chromaOffset =
        (0.00035f + horrorPulseStrength * 0.0038f +
         corridorTension * lensDistortionStrength * 0.0018f) *
        edgeAberration;
    const float3 sceneCenter = SceneTexture.SampleLevel(
        LinearSampler,
        input.uv,
        0.0f).rgb;
    const float3 distortedScene = SceneTexture.SampleLevel(
        LinearSampler,
        distortedUv,
        0.0f).rgb;
    const float3 chromaticScene = float3(
        SceneTexture.SampleLevel(
            LinearSampler,
            saturate(distortedUv + radialDirection * chromaOffset),
            0.0f).r,
        distortedScene.g,
        SceneTexture.SampleLevel(
            LinearSampler,
            saturate(distortedUv - radialDirection * chromaOffset),
            0.0f).b);
    const float distortionDifference = length(
        abs(distortedScene - sceneCenter));
    const float distortionAlpha = saturate(
        edgeAberration * lensDistortionStrength *
        (0.010f + corridorTension * 0.018f) +
        distortionDifference * corridorTension * 0.10f);
    const float chromaDifference = length(
        abs(chromaticScene - sceneCenter));
    const float chromaAlpha = saturate(
        eventAberration * edgeAberration * 0.022f +
        chromaDifference * horrorPulseStrength * 0.75f);
    float vignette = smoothstep(0.30f, 0.72f, edgeDistance) *
        0.25f * eventVignette;

    // Alternating CRT rows. SV_POSITION is used so the line width remains
    // stable at different window resolutions.
    float scanWave = sin(input.pos.y * 3.14159265f) * 0.5f + 0.5f;
    float scanline = (1.0f - scanWave) * 0.018f * eventNoise;

    float frame = floor(time * 30.0f);
    float noise = Hash(floor(input.pos.xy) + frame * float2(17.0f, 31.0f));
    float darkGrain = smoothstep(0.68f, 1.0f, noise) *
        0.012f * eventNoise;

    // A very faint rolling band gives the image analogue motion.
    float rolling = sin(input.uv.y * 10.0f - time * 1.8f) * 0.5f + 0.5f;
    rolling = pow(rolling, 12.0f) * 0.012f * eventNoise;

    // Rare dust pixels are bright but use very low opacity.
    float dust = step(0.9985f, noise);

    float darkAlpha = saturate(vignette + scanline + darkGrain + rolling);
    float dustAlpha = dust * 0.030f * eventNoise;

    // Moisture appears only after the player disturbs a puddle. Each drop
    // slides at a different speed, keeping the pattern from looking stamped.
    const float slide0 = frac(0.12f + time * 0.010f);
    const float slide1 = frac(0.48f + time * 0.006f);
    const float slide2 = frac(0.76f + time * 0.008f);
    float3 lensDrop = 0.0f;
    lensDrop = max(lensDrop, GetLensDrop(
        input.uv, float2(0.18f, slide0 * 1.18f - 0.09f),
        0.025f, screenAspect));
    lensDrop = max(lensDrop, GetLensDrop(
        input.uv, float2(0.34f, slide2 * 1.14f - 0.07f),
        0.014f, screenAspect));
    lensDrop = max(lensDrop, GetLensDrop(
        input.uv, float2(0.62f, slide1 * 1.20f - 0.10f),
        0.031f, screenAspect));
    lensDrop = max(lensDrop, GetLensDrop(
        input.uv, float2(0.79f, slide0 * 1.12f - 0.06f),
        0.018f, screenAspect));
    lensDrop = max(lensDrop, GetLensDrop(
        input.uv, float2(0.91f, slide2 * 1.17f - 0.08f),
        0.011f, screenAspect));
    float2 lensRefraction = 0.0f;
    lensRefraction += GetLensDropRefraction(
        input.uv, float2(0.18f, slide0 * 1.18f - 0.09f),
        0.025f, screenAspect);
    lensRefraction += GetLensDropRefraction(
        input.uv, float2(0.34f, slide2 * 1.14f - 0.07f),
        0.014f, screenAspect);
    lensRefraction += GetLensDropRefraction(
        input.uv, float2(0.62f, slide1 * 1.20f - 0.10f),
        0.031f, screenAspect);
    lensRefraction += GetLensDropRefraction(
        input.uv, float2(0.79f, slide0 * 1.12f - 0.06f),
        0.018f, screenAspect);
    lensRefraction += GetLensDropRefraction(
        input.uv, float2(0.91f, slide2 * 1.17f - 0.08f),
        0.011f, screenAspect);
    const float refractionMagnitude = length(lensRefraction);
    if (refractionMagnitude > 0.008f)
    {
        lensRefraction *= 0.008f / refractionMagnitude;
    }
    const float moisture = saturate(lensMoisture);
    const float3 refractedScene = SceneTexture.SampleLevel(
        LinearSampler,
        saturate(input.uv - lensRefraction * moisture),
        0.0f).rgb;
    const float dropRefractionAlpha = saturate(
        (lensDrop.y * 0.085f + lensDrop.x * 0.025f) * moisture);
    const float dropAlpha = saturate(
        (lensDrop.x * 0.13f + lensDrop.y * 0.025f +
         lensDrop.z * 0.09f) * moisture);

    // Scare events add transparent synchronization tears. This pass never
    // replaces the scene, so a missing texture can no longer make it black.
    float bandId = floor(input.uv.y * 38.0f);
    float bandNoise = Hash(float2(bandId, floor(time * 24.0f)));
    float tear = step(0.86f, bandNoise) * horrorPulseStrength;
    float thinLine = 1.0f - smoothstep(
        0.02f,
        0.12f,
        abs(frac(input.uv.y * 38.0f) - 0.5f));
    const float tearOffset =
        (bandNoise - 0.5f) * 0.030f * horrorPulseStrength * thinLine;
    const float3 shiftedScene = SceneTexture.SampleLevel(
        LinearSampler,
        saturate(input.uv + float2(tearOffset, 0.0f)),
        0.0f).rgb;
    float tearAlpha = tear * thinLine * 0.16f;
    float alpha = saturate(
        darkAlpha + dustAlpha + tearAlpha + dropAlpha +
        dropRefractionAlpha + chromaAlpha + distortionAlpha);

    float3 overlayColor = lerp(
        float3(0.0f, 0.0f, 0.0f),
        float3(0.70f, 0.74f, 0.70f),
        dust
    );
    float channelChoice = Hash(float2(bandId + 13.0f, floor(time * 24.0f)));
    float3 tearColor = channelChoice < 0.5f
        ? float3(0.12f, 0.72f, 0.80f)
        : float3(0.82f, 0.12f, 0.20f);
    tearColor = lerp(shiftedScene, tearColor, 0.24f);
    overlayColor = lerp(overlayColor, tearColor, saturate(tear));
    overlayColor = lerp(
        overlayColor,
        distortedScene,
        saturate(distortionAlpha * 18.0f));
    overlayColor = lerp(
        overlayColor,
        chromaticScene,
        saturate(chromaAlpha * 16.0f));
    const float3 waterColor = lerp(
        float3(0.025f, 0.045f, 0.050f),
        float3(0.68f, 0.78f, 0.80f),
        saturate(lensDrop.x * 0.55f + lensDrop.z));
    overlayColor = lerp(
        overlayColor,
        waterColor,
        saturate(dropAlpha * 6.0f));
    overlayColor = lerp(
        overlayColor,
        refractedScene,
        saturate(dropRefractionAlpha * 9.0f));

    // The far corridor slowly cools and desaturates. This remains subtle and
    // transparent so navigation information is never hidden.
    const float lowerScreen = smoothstep(0.22f, 0.92f, input.uv.y);
    const float tensionVeil = corridorTension *
        (0.014f + lowerScreen * 0.010f);
    const float3 tensionColor = float3(0.035f, 0.060f, 0.070f);
    overlayColor = lerp(
        overlayColor,
        tensionColor,
        saturate(tensionVeil * 12.0f));
    alpha = saturate(alpha + tensionVeil);

    // Cinematic split toning keeps shadow detail cold and lets practical
    // lights retain a restrained warm halo. It is composed as another
    // transparent layer so the captured scene can never be replaced by black.
    const float sceneLuminance = dot(
        sceneCenter,
        float3(0.2126f, 0.7152f, 0.0722f));
    const float shadowWeight = 1.0f - smoothstep(
        0.08f, 0.48f, sceneLuminance);
    const float highlightWeight = smoothstep(
        0.42f, 1.10f, sceneLuminance);
    const float filmAlpha = saturate(filmGradeStrength) *
        (shadowWeight * 0.050f + highlightWeight * 0.022f);
    const float3 coldShadow = float3(0.020f, 0.040f, 0.052f);
    const float3 warmLight = float3(0.160f, 0.095f, 0.040f);
    const float3 filmColor = lerp(
        coldShadow,
        warmLight,
        saturate(highlightWeight * 1.35f));
    const float combinedAlpha = alpha + filmAlpha * (1.0f - alpha);
    overlayColor = (
        overlayColor * alpha +
        filmColor * filmAlpha * (1.0f - alpha)) /
        max(combinedAlpha, 0.0001f);
    alpha = combinedAlpha;

    return float4(overlayColor, alpha);
}
