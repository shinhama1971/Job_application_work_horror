// ============================================================================
// シェーダーの役割: 色収差・歪み・色調をまとめて掛ける、前の版の最後の画面効果（今はどこからも読み込んでいない）。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させる必要がある。
// ============================================================================

// 頂点シェーダーから受け取る値
struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// 経過時間、ブルームの強さ、ノイズの量
cbuffer TimeBuffer : register(b0)
{
    float time;
    float bloomIntensity;
    float noiseAmount;
    float padding;
};

// 描いた画面（t0）、ブルームの画像（t1）、サンプラー（s0）
Texture2D sceneTexture : register(t0);
Texture2D bloomTexture : register(t1);
SamplerState sceneSampler : register(s0);

// 2次元の値から0〜1の疑似乱数を作っている
float Hash(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float4 main(PS_IN input) : SV_TARGET
{
    const float2 center = float2(0.5f, 0.5f);
    float2 centered = input.uv - center;

    // 普段のプレイに合う、控えめな樽型の歪み。
    float2 uv = center + centered * (1.0f + dot(centered, centered) * 0.055f);
    float insideScreen =
        step(0.0f, uv.x) * step(uv.x, 1.0f) *
        step(0.0f, uv.y) * step(uv.y, 1.0f);
    uv = saturate(uv);

    // 放射状の色ずれ（画面の角ほど強い）。
    float radialAmount = dot(centered, centered);
    float2 colorOffset = centered * (0.0022f + radialAmount * 0.0035f);
    float red = sceneTexture.Sample(sceneSampler, saturate(uv + colorOffset)).r;
    float green = sceneTexture.Sample(sceneSampler, uv).g;
    float blue = sceneTexture.Sample(sceneSampler, saturate(uv - colorOffset)).b;
    float3 color = float3(red, green, blue);

    // 縮めた解像度で作ったブルームを、元の画面に重ねている。
    color += bloomTexture.Sample(sceneSampler, uv).rgb * bloomIntensity;

    // 白く光らない程度の、細かいアナログの粒状のノイズ。
    float grain = Hash(floor(input.pos.xy) + floor(time * 60.0f) * 17.0f) - 0.5f;
    color += grain * 0.028f * noiseAmount;

    // 走査線と、ゆっくり流れる明るい帯。
    float scanline = sin(input.pos.y * 3.14159265f);
    color *= 1.0f + (-0.025f + scanline * 0.018f) * noiseAmount;

    float rollingBand = sin(input.uv.y * 8.0f - time * 1.7f) * 0.5f + 0.5f;
    rollingBand = pow(rollingBand, 10.0f);
    color += rollingBand * 0.018f * noiseAmount;

    // まれに出る、細い横のトラッキングの乱れ。
    float eventPhase = frac(time * 0.19f);
    float eventStrength = smoothstep(0.965f, 0.985f, eventPhase) *
        (1.0f - smoothstep(0.985f, 1.0f, eventPhase));
    float trackingLine = 1.0f - smoothstep(
        0.0f,
        0.012f,
        abs(input.uv.y - frac(time * 0.37f))
    );
    color += trackingLine * eventStrength * 0.10f * noiseAmount;

    // 画面の端を暗くし、照準の周りに注意を集めている。
    float vignette = smoothstep(0.82f, 0.28f, length(centered));
    color *= lerp(0.70f, 1.0f, vignette);

    // 少し彩度を落とした、冷たい緑がかったホラーの色調。
    float luminance = dot(color, float3(0.299f, 0.587f, 0.114f));
    color = lerp(float3(luminance, luminance, luminance), color, 0.84f);
    color *= float3(0.96f, 1.0f, 0.97f);
    // 歪みで画面の外になった部分は黒くしている
    color *= insideScreen;

    return float4(saturate(color), 1.0f);
}
