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
    float padding;
};

Texture2D sceneTexture : register(t0);
Texture2D bloomTexture : register(t1);
SamplerState sceneSampler : register(s0);

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float4 main(PS_IN input) : SV_TARGET
{
    const float2 center = float2(0.5f, 0.5f);
    float2 centered = input.uv - center;

    // Subtle barrel distortion suitable for normal gameplay.
    float2 uv = center + centered * (1.0f + dot(centered, centered) * 0.055f);
    float insideScreen =
        step(0.0f, uv.x) * step(uv.x, 1.0f) *
        step(0.0f, uv.y) * step(uv.y, 1.0f);
    uv = saturate(uv);

    // Radial chromatic aberration, stronger near the corners.
    float radialAmount = dot(centered, centered);
    float2 colorOffset = centered * (0.0022f + radialAmount * 0.0035f);
    float red = sceneTexture.Sample(sceneSampler, saturate(uv + colorOffset)).r;
    float green = sceneTexture.Sample(sceneSampler, uv).g;
    float blue = sceneTexture.Sample(sceneSampler, saturate(uv - colorOffset)).b;
    float3 color = float3(red, green, blue);

    // Half-resolution compute bloom is sampled back over the original scene.
    color += bloomTexture.Sample(sceneSampler, uv).rgb * bloomIntensity;

    // Fine analogue grain without disruptive white flashes.
    float grain = Hash(floor(input.pos.xy) + floor(time * 60.0f) * 17.0f) - 0.5f;
    color += grain * 0.028f * noiseAmount;

    // Scanlines and a slow rolling brightness band.
    float scanline = sin(input.pos.y * 3.14159265f);
    color *= 1.0f + (-0.025f + scanline * 0.018f) * noiseAmount;

    float rollingBand = sin(input.uv.y * 8.0f - time * 1.7f) * 0.5f + 0.5f;
    rollingBand = pow(rollingBand, 10.0f);
    color += rollingBand * 0.018f * noiseAmount;

    // Rare, narrow horizontal tracking disturbance.
    float eventPhase = frac(time * 0.19f);
    float eventStrength = smoothstep(0.965f, 0.985f, eventPhase) *
        (1.0f - smoothstep(0.985f, 1.0f, eventPhase));
    float trackingLine = 1.0f - smoothstep(
        0.0f,
        0.012f,
        abs(input.uv.y - frac(time * 0.37f))
    );
    color += trackingLine * eventStrength * 0.10f * noiseAmount;

    // Dark edges preserve attention around the reticle.
    float vignette = smoothstep(0.82f, 0.28f, length(centered));
    color *= lerp(0.70f, 1.0f, vignette);

    // Slightly desaturated, cold-green horror grade.
    float luminance = dot(color, float3(0.299f, 0.587f, 0.114f));
    color = lerp(float3(luminance, luminance, luminance), color, 0.84f);
    color *= float3(0.96f, 1.0f, 0.97f);
    color *= insideScreen;

    return float4(saturate(color), 1.0f);
}
