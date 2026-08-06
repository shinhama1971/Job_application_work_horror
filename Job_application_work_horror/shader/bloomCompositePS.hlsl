struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

cbuffer BloomCompositeBuffer : register(b0)
{
    float time;
    float bloomIntensity;
    float noiseAmount;
    float padding;
};

Texture2D bloomTexture : register(t0);
SamplerState bloomSampler : register(s0);

float4 main(PS_IN input) : SV_TARGET
{
    const float3 bloom = bloomTexture.Sample(bloomSampler, saturate(input.uv)).rgb;
    return float4(bloom * bloomIntensity, 1.0f);
}
