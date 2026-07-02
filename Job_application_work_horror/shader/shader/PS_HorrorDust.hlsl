struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

cbuffer TimeBuffer : register(b0)
{
    float time;
    float3 dummy;
};

Texture2D tex : register(t0);
SamplerState samp : register(s0);

float rand(float2 co)
{
    return frac(sin(dot(co, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float4 main(PS_IN pin) : SV_TARGET
{
    float2 uv = pin.uv;
    float2 center = float2(0.5f, 0.5f);

    float4 color = tex.Sample(samp, uv);

    float noise = rand(uv * 800.0f + time * 20.0f);
    noise = smoothstep(0.45f, 1.0f, noise);
    color.rgb = lerp(color.rgb, float3(0.8f, 0.75f, 0.65f), noise * 0.45f);

    float scan = sin(uv.y * 500.0f + time * 25.0f);
    scan = scan * 0.5f + 0.5f;
    scan = pow(scan, 6.0f);
    color.rgb += scan * 0.08f;

    // 一定間隔でザザッと強いノイズを出す
    float burst = step(0.86f, frac(time * 0.7f));
    float burstNoise = rand(uv * 1600.0f + time * 80.0f);
    burstNoise = smoothstep(0.25f, 1.0f, burstNoise);

    color.rgb = lerp(color.rgb, float3(burstNoise, burstNoise, burstNoise), burst * 0.65f);

    // ザザッとなる瞬間だけ横線を強くする
    float burstLine = sin(uv.y * 900.0f + time * 80.0f);
    burstLine = burstLine * 0.5f + 0.5f;
    burstLine = pow(burstLine, 3.0f);
    color.rgb += burstLine * burst * 0.25f;

    float dist = distance(uv, center);
    float vignette = 1.0f - smoothstep(0.3f, 0.9f, dist);
    color.rgb *= vignette;

    float flicker = rand(float2(time * 5.0f, time * 13.0f));
    flicker = lerp(0.65f, 1.15f, flicker);
    color.rgb *= flicker;

    color.rgb *= float3(1.15f, 0.8f, 0.65f);
    color.rgb *= 0.75f;

    return color;
}