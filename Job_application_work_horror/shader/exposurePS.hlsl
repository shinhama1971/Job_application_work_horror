// ============================================================================
// シェーダーの役割: 露出と暗部の持ち上げを行い、暗所でも進行可能な明るさへ調整します。
// 定数バッファのスロットと入出力構造はCPU側の定義と必ず一致させてください。
// ============================================================================

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
    float exposurePadding;
};

Texture2D sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);

float4 main(PS_IN input) : SV_TARGET
{
    const float3 scene =
        sceneTexture.Sample(sceneSampler, saturate(input.uv)).rgb;

    uint sceneWidth;
    uint sceneHeight;
    sceneTexture.GetDimensions(sceneWidth, sceneHeight);
    const float2 texelSize = rcp(float2(sceneWidth, sceneHeight));
    const float2 localOffset = texelSize * 6.0f;
    const float3 localAverage =
        (scene +
         sceneTexture.SampleLevel(
            sceneSampler,
            saturate(input.uv + float2(localOffset.x, 0.0f)),
            0.0f).rgb +
         sceneTexture.SampleLevel(
            sceneSampler,
            saturate(input.uv - float2(localOffset.x, 0.0f)),
            0.0f).rgb +
         sceneTexture.SampleLevel(
            sceneSampler,
            saturate(input.uv + float2(0.0f, localOffset.y)),
            0.0f).rgb +
         sceneTexture.SampleLevel(
            sceneSampler,
            saturate(input.uv - float2(0.0f, localOffset.y)),
            0.0f).rgb) * 0.20f;

    // A power surge briefly blooms the image during event pulses. The clamp
    // prevents eye adaptation from washing out UI drawn after this pass.
    const float eventExposure = horrorPulseStrength * 0.08f;
    const float exposureGain =
        max(exposure + eventExposure - 1.0f, 0.0f);
    const float3 adaptationLight = scene * exposureGain;

    // Lift only a fraction of deep shadows as tension rises. This produces a
    // cold, fog-like silhouette separation without flattening lit surfaces.
    const float luminance = dot(scene, float3(0.2126f, 0.7152f, 0.0722f));
    const float localLuminance = dot(
        localAverage,
        float3(0.2126f, 0.7152f, 0.0722f));
    const float localDarkness =
        1.0f - smoothstep(0.025f, 0.18f, localLuminance);
    const float highlightProtection =
        1.0f - smoothstep(0.14f, 0.58f, luminance);
    const float2 centered =
        (input.uv - float2(0.5f, 0.5f)) * float2(screenAspect, 1.0f);
    const float centerPriority =
        1.0f - smoothstep(0.18f, 0.76f, length(centered));
    const float adaptationRequest = saturate(
        (exposure - 1.025f) / 0.125f);
    const float localLiftStrength =
        adaptationRequest * localDarkness * highlightProtection *
        (0.0045f + centerPriority * 0.0095f);
    const float3 adaptationTint = lerp(
        float3(0.92f, 0.95f, 1.0f),
        float3(0.74f, 0.87f, 1.0f),
        corridorTension * 0.48f);
    const float3 localAdaptation =
        adaptationTint * localLiftStrength;
    const float shadowMask = 1.0f - smoothstep(0.025f, 0.22f, luminance);
    const float3 tensionHaze = float3(0.020f, 0.034f, 0.040f) *
        corridorTension * shadowMask;
    return float4(
        adaptationLight + localAdaptation + tensionHaze,
        1.0f);
}
