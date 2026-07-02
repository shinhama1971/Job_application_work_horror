struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
};

cbuffer TimeBuffer : register(b0)
{
    float time;
    float3 dummy;
};

float rand(float2 co)
{
    return frac(sin(dot(co, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float4 main(in PS_IN input) : SV_Target
{
    float2 uv = input.tex;

    float noise = rand(uv * 900.0f + time * 30.0f);
    noise = smoothstep(0.45f, 1.0f, noise);

    float scan = sin(uv.y * 600.0f + time * 30.0f);
    scan = scan * 0.5f + 0.5f;
    scan = pow(scan, 6.0f);

    float burst = step(0.88f, frac(time * 0.8f));

    float3 col = float3(0.8f, 0.75f, 0.65f) * noise;
    col += scan * 0.15f;
    col += burst * noise * 0.5f;

    return float4(col, 0.35f);
}