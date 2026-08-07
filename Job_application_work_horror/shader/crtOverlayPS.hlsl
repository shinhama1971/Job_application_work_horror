struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

cbuffer TimeBuffer : register(b0)
{
    float time;
    float bloomIntensity;
    float noiseAmount;
    float vignetteStrength;
};

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float4 main(PS_IN input) : SV_TARGET
{
    const float2 center = float2(0.5f, 0.5f);
    float2 centered = input.uv - center;

    // Soft edge darkening that does not hide gameplay information.
    float edgeDistance = length(centered * float2(1.15f, 1.0f));
    float vignette = smoothstep(0.30f, 0.72f, edgeDistance) *
        0.30f * vignetteStrength;

    // Alternating CRT rows. SV_POSITION is used so the line width remains
    // stable at different window resolutions.
    float scanWave = sin(input.pos.y * 3.14159265f) * 0.5f + 0.5f;
    float scanline = (1.0f - scanWave) * 0.030f * noiseAmount;

    float frame = floor(time * 30.0f);
    float noise = Hash(floor(input.pos.xy) + frame * float2(17.0f, 31.0f));
    float darkGrain = smoothstep(0.68f, 1.0f, noise) *
        0.020f * noiseAmount;

    // A very faint rolling band gives the image analogue motion.
    float rolling = sin(input.uv.y * 10.0f - time * 1.8f) * 0.5f + 0.5f;
    rolling = pow(rolling, 12.0f) * 0.018f * noiseAmount;

    // Rare dust pixels are bright but use very low opacity.
    float dust = step(0.9985f, noise);

    float darkAlpha = saturate(vignette + scanline + darkGrain + rolling);
    float dustAlpha = dust * 0.045f * noiseAmount;
    float alpha = saturate(darkAlpha + dustAlpha);

    float3 overlayColor = lerp(
        float3(0.0f, 0.0f, 0.0f),
        float3(0.70f, 0.74f, 0.70f),
        dust
    );

    return float4(overlayColor, alpha);
}
