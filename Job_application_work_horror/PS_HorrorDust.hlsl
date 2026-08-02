//ブラウン管テレビっぽいノイズ　
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
    float2 originalUV = pin.uv;
    float2 uv = pin.uv;
    float2 center = float2(0.5f, 0.5f);

    // CRTっぽく画面を少し丸く歪ませる
    float2 p = uv - center;
    uv += p * dot(p, p) * 0.3f;
    uv = saturate(uv);

    // モデルのRGBを左右にずらす
    float rgbOffset = 0.004f;

    float r = tex.Sample(samp, saturate(uv + float2(rgbOffset, 0.0f))).r;
    float g = tex.Sample(samp, uv).g;
    float b = tex.Sample(samp, saturate(uv - float2(rgbOffset, 0.0f))).b;

    float3 color = float3(r, g, b);

    // セピア寄りにする
    float gray = dot(color, float3(0.299f, 0.587f, 0.114f));
    float3 sepia = float3(gray * 1.15f, gray * 0.88f, gray * 0.62f);
    color = lerp(color, sepia, 0.45f);

    // 細かいノイズ
    float noise = rand(uv * 800.0f + time * 20.0f);
    noise = smoothstep(0.45f, 1.0f, noise);
    color = lerp(color, float3(0.8f, 0.75f, 0.65f), noise * 0.35f);

    // CRT走査線
    float scan = sin(uv.y * 700.0f + time * 25.0f);
    scan = scan * 0.5f + 0.5f;
    scan = pow(scan, 6.0f);
    color += scan * 0.09f;

    // 横方向の少しだけ揺れ
    float glitch = sin(uv.y * 80.0f + time * 8.0f) * 0.002f;
    uv.x += glitch;

    // 一定間隔でザザッと強いノイズ
    float burst = step(0.86f, frac(time * 0.7f));
    float burstNoise = rand(uv * 1600.0f + time * 80.0f);
    burstNoise = smoothstep(0.25f, 1.0f, burstNoise);

    color = lerp(color, float3(burstNoise, burstNoise, burstNoise), burst * 0.55f);

    // ザザッとなる瞬間だけ横線を強くする
    float burstLine = sin(uv.y * 900.0f + time * 80.0f);
    burstLine = burstLine * 0.5f + 0.5f;
    burstLine = pow(burstLine, 3.0f);
    color += burstLine * burst * 0.25f;

    // 画面端を暗くする
    float dist = distance(originalUV, center);
    float vignette = 1.0f - smoothstep(0.30f, 0.90f, dist);
    color *= vignette + 0.25f;

    // 明滅
    float flicker = rand(float2(time * 5.0f, time * 13.0f));
    flicker = lerp(0.72f, 1.12f, flicker);
    color *= flicker;

    // 全体を少し暖色・暗めに
    color *= float3(1.10f, 0.88f, 0.72f);
    color *= 0.82f;

    return float4(saturate(color), 1.0f);
}