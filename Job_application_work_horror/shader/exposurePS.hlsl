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
    float screenAspect;
    float volumeIntensity;
    float lensDistortionStrength;
    float horrorPulseStrength;
    float exposure;
    float3 exposurePadding;
};

Texture2D sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);

float4 main(PS_IN input) : SV_TARGET
{
    const float3 scene =
        sceneTexture.Sample(sceneSampler, saturate(input.uv)).rgb;

    // A power surge briefly blooms the image during event pulses. The clamp
    // prevents eye adaptation from washing out UI drawn after this pass.
    const float eventExposure = horrorPulseStrength * 0.08f;
    const float exposureGain =
        max(exposure + eventExposure - 1.0f, 0.0f);
    const float3 adaptationLight = scene * exposureGain;
    return float4(adaptationLight, 1.0f);
}
