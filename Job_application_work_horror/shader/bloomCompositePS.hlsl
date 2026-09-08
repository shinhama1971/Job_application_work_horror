// ============================================================================
// シェーダーの役割: 元のシーン色へブルーム画像を加算合成します。
// ============================================================================

struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

cbuffer BloomCompositeBuffer : register(b0)
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
    float lensDirtStrength;
    float postProcessPadding0;
    float postProcessPadding1;
    float postProcessPadding2;
};

float LensDirtWave(float2 uv)
{
    const float first = sin(dot(uv, float2(23.17f, 17.83f)) + 1.73f);
    const float second = sin(
        dot(uv, float2(-37.11f, 13.57f)) + first * 2.35f);
    const float broad = sin(
        dot(uv, float2(7.31f, -9.17f)) + second * 1.15f);
    return saturate(first * 0.24f + second * 0.31f + broad * 0.45f + 0.50f);
}

Texture2D bloomTexture : register(t0);
SamplerState bloomSampler : register(s0);

float4 main(PS_IN input) : SV_TARGET
{
    uint bloomWidth;
    uint bloomHeight;
    bloomTexture.GetDimensions(bloomWidth, bloomHeight);
    const float2 texelSize = rcp(float2(bloomWidth, bloomHeight));
    const float2 uv = saturate(input.uv);
    const float3 bloom = bloomTexture.SampleLevel(
        bloomSampler,
        uv,
        0.0f).rgb;

    // Reuse the blurred bloom buffer for a restrained horizontal streak.
    // Wide taps favor fluorescent fixtures without requiring another texture.
    float3 streak = 0.0f;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv + float2(texelSize.x * 5.0f, 0.0f)), 0.0f).rgb * 0.26f;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv - float2(texelSize.x * 5.0f, 0.0f)), 0.0f).rgb * 0.26f;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv + float2(texelSize.x * 12.0f, 0.0f)), 0.0f).rgb * 0.15f;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv - float2(texelSize.x * 12.0f, 0.0f)), 0.0f).rgb * 0.15f;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv + float2(texelSize.x * 22.0f, 0.0f)), 0.0f).rgb * 0.09f;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv - float2(texelSize.x * 22.0f, 0.0f)), 0.0f).rgb * 0.09f;

    const float streakLuminance = dot(
        streak,
        float3(0.2126f, 0.7152f, 0.0722f));
    const float3 verticalNeighbors =
        (bloomTexture.SampleLevel(
            bloomSampler,
            saturate(uv + float2(0.0f, texelSize.y * 4.0f)),
            0.0f).rgb +
         bloomTexture.SampleLevel(
            bloomSampler,
            saturate(uv - float2(0.0f, texelSize.y * 4.0f)),
            0.0f).rgb) * 0.5f;
    const float verticalLuminance = dot(
        verticalNeighbors,
        float3(0.2126f, 0.7152f, 0.0722f));
    const float compactHighlight = saturate(
        (streakLuminance - verticalLuminance) * 4.5f + 0.20f);
    const float brightStreak = smoothstep(
        0.045f,
        0.32f,
        streakLuminance);
    const float eventBoost = saturate(
        (bloomIntensity - 0.48f) * 0.85f);
    const float streakStrength =
        (0.055f + eventBoost * 0.075f) * brightStreak * compactHighlight;
    const float3 streakColor =
        streak * float3(0.84f, 0.91f, 1.0f) * streakStrength;

    // Dirt is visible only where bloom already exists. This avoids a static
    // dirty-screen overlay while giving bright fixtures an optical response.
    const float2 centered = (uv - 0.5f) * float2(screenAspect, 1.0f);
    const float edgeWeight = smoothstep(0.08f, 0.72f, length(centered));
    const float dirtWave = LensDirtWave(uv);
    const float dirtMask = smoothstep(
        0.54f,
        0.84f,
        dirtWave + edgeWeight * 0.12f);
    const float bloomLuminance = dot(
        bloom,
        float3(0.2126f, 0.7152f, 0.0722f));
    const float visibleDirt = dirtMask * smoothstep(0.025f, 0.30f, bloomLuminance)
        * saturate(lensDirtStrength);
    const float3 dirtGlow = bloom * float3(1.00f, 0.88f, 0.70f)
        * visibleDirt * 0.12f;

    return float4(
        bloom * bloomIntensity + streakColor + dirtGlow,
        1.0f);
}
