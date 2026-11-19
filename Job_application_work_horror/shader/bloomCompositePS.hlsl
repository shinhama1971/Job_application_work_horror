// ============================================================================
// シェーダーの役割: 元の画面の色へ、ブルームの画像（と横に伸びる光の筋・レンズの汚れ）を加算合成している。
// 主な技術: 大きさの違う4段のブルームを重みを付けて重ねる（マルチレベルのブルーム）
// ============================================================================

// 頂点シェーダー（unlitTextureVS）から受け取る値
struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// 画面効果の値（FullScreenQuad.cppのTimeBufferと同じ並び。ここではブルームの強さ・縦横比・レンズの汚れを使っている）
#include "postProcessBuffer.hlsli"

// レンズの汚れを作る三つの波の向き・細かさと、重ねたときの濃さ。
static const float2 LENS_DIRT_FINE_FREQUENCY = float2(23.17f, 17.83f);
static const float2 LENS_DIRT_CROSS_FREQUENCY = float2(-37.11f, 13.57f);
static const float2 LENS_DIRT_BROAD_FREQUENCY = float2(7.31f, -9.17f);
static const float LENS_DIRT_FINE_PHASE = 1.73f;
static const float LENS_DIRT_CROSS_MODULATION = 2.35f;
static const float LENS_DIRT_BROAD_MODULATION = 1.15f;
static const float LENS_DIRT_FINE_WEIGHT = 0.24f;
// 3つの波を足す前の、汚れの濃さの基準（波が0のときの濃さ）
#define LENS_DIRT_BASE 0.50f
static const float LENS_DIRT_CROSS_WEIGHT = 0.31f;
static const float LENS_DIRT_BROAD_WEIGHT = 0.45f;

// 細い段から左右対称に光を拾い、近い試料を遠い試料より強く混ぜる。
static const float STREAK_NEAR_OFFSET_TEXELS = 7.0f;
static const float STREAK_FAR_OFFSET_TEXELS = 20.0f;
static const float STREAK_NEAR_WEIGHT = 0.34f;
static const float STREAK_FAR_WEIGHT = 0.16f;

// ブルームと光の筋の明るさの比較には、同じリニア RGB の輝度係数を使う。
static const float3 BLOOM_LUMINANCE_WEIGHTS = float3(0.2126f, 0.7152f, 0.0722f);

// 広がった光を抑え、明るくまとまった光源やイベント時に横の筋を出す。
static const float STREAK_DIFFUSE_SUPPRESSION = 0.72f;
static const float STREAK_COMPACT_GAIN = 4.2f;
static const float STREAK_COMPACT_BIAS = 0.20f;
static const float STREAK_BRIGHTNESS_START = 0.045f;
static const float STREAK_BRIGHTNESS_END = 0.32f;
static const float STREAK_EVENT_BLOOM_THRESHOLD = 0.48f;
static const float STREAK_EVENT_RESPONSE = 0.85f;
static const float STREAK_BASE_STRENGTH = 0.055f;
static const float STREAK_EVENT_STRENGTH = 0.075f;
static const float3 STREAK_TINT = float3(0.84f, 0.91f, 1.0f);

// レンズの汚れのむらを、sinを重ねた模様で作っている（0〜1）
float LensDirtWave(float2 uv)
{
    const float first = sin(dot(uv, LENS_DIRT_FINE_FREQUENCY) + LENS_DIRT_FINE_PHASE);
    const float second = sin(
        dot(uv, LENS_DIRT_CROSS_FREQUENCY) + first * LENS_DIRT_CROSS_MODULATION);
    const float broad = sin(
        dot(uv, LENS_DIRT_BROAD_FREQUENCY) + second * LENS_DIRT_BROAD_MODULATION);
    return saturate(first * LENS_DIRT_FINE_WEIGHT + second * LENS_DIRT_CROSS_WEIGHT + broad * LENS_DIRT_BROAD_WEIGHT + LENS_DIRT_BASE);
}

// ぼかしたブルームの画像の段（t0 = 縦横1/4、t1 = 1/8、t2 = 1/16、t3 = 1/32）とサンプラー（s0）
Texture2D bloomTexture : register(t0);
Texture2D bloomWide1Texture : register(t1);
Texture2D bloomWide2Texture : register(t2);
Texture2D bloomWide3Texture : register(t3);
SamplerState bloomSampler : register(s0);

// 段ごとの重み。1/4の段は光の近くの強いにじみ、小さい段ほど部屋に広がる淡い光の暈（かさ）になる。
// 光の近くのにじみは以前の1段だけのブルームに近い強さで残し、その上に広い段を足している
// （合計を1にすると、広い段は1画素あたりの明るさが小さいので、暈がほとんど見えなかった）
static const float BLOOM_LEVEL0_WEIGHT = 0.75f;
static const float BLOOM_LEVEL1_WEIGHT = 0.50f;
static const float BLOOM_LEVEL2_WEIGHT = 0.40f;
static const float BLOOM_LEVEL3_WEIGHT = 0.35f;

// 段を線形補間で読んでいる。サンプラー（s0）は繰り返しの設定なので、画面の端で反対側の端の光が混ざらないよう、
// 端の画素の中心より外は読まないようにしている（小さい段ほど1画素が大きく、混ざると目立つ）
static const float BLOOM_EDGE_TEXEL_MARGIN = 0.5f;

float3 SampleBloomLevel(Texture2D levelTexture, float2 uv)
{
    uint width;
    uint height;
    levelTexture.GetDimensions(width, height);
    const float2 margin = BLOOM_EDGE_TEXEL_MARGIN / float2(width, height);
    return levelTexture.SampleLevel(bloomSampler, clamp(uv, margin, 1.0f - margin), 0.0f).rgb;
}

// 画面の外側ほど汚れを浮かせ、模様は一定の濃さを超えた領域だけ現れる。
static const float LENS_DIRT_EDGE_START = 0.08f;
static const float LENS_DIRT_EDGE_FULL = 0.72f;
static const float LENS_DIRT_MASK_START = 0.54f;
static const float LENS_DIRT_MASK_FULL = 0.84f;
static const float LENS_DIRT_EDGE_MASK_GAIN = 0.12f;

// 明るい光源の周囲でだけ暖色の汚れを出し、暗い画面を覆わないようにする。
static const float LENS_DIRT_MIN_LUMINANCE = 0.025f;
static const float LENS_DIRT_FULL_LUMINANCE = 0.30f;
static const float3 LENS_DIRT_GLOW_TINT = float3(1.00f, 0.88f, 0.70f);
static const float LENS_DIRT_GLOW_SCALE = 0.12f;

float4 main(PS_IN input) : SV_TARGET
{
    uint bloomWidth;
    uint bloomHeight;
    bloomTexture.GetDimensions(bloomWidth, bloomHeight);
    const float2 texelSize = rcp(float2(bloomWidth, bloomHeight));
    const float2 uv = saturate(input.uv);
    // 光の筋の形を決める、一番細かい段
    const float3 sharpBloom = SampleBloomLevel(bloomTexture, uv);
    // 段を重みを付けて重ねている（小さい段は線形補間で読むので、引き伸ばしても四角く見えない）
    const float3 bloom =
        sharpBloom * BLOOM_LEVEL0_WEIGHT +
        SampleBloomLevel(bloomWide1Texture, uv) * BLOOM_LEVEL1_WEIGHT +
        SampleBloomLevel(bloomWide2Texture, uv) * BLOOM_LEVEL2_WEIGHT +
        SampleBloomLevel(bloomWide3Texture, uv) * BLOOM_LEVEL3_WEIGHT;

    // ぼかしたブルームを使い回して、控えめな横方向の光の筋を作っている。
    // 広い間隔で読み取り、テクスチャを足さずに、蛍光灯の横長の形を強調している。
    float3 streak = 0.0f;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv + float2(texelSize.x * STREAK_NEAR_OFFSET_TEXELS, 0.0f)), 0.0f).rgb * STREAK_NEAR_WEIGHT;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv - float2(texelSize.x * STREAK_NEAR_OFFSET_TEXELS, 0.0f)), 0.0f).rgb * STREAK_NEAR_WEIGHT;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv + float2(texelSize.x * STREAK_FAR_OFFSET_TEXELS, 0.0f)), 0.0f).rgb * STREAK_FAR_WEIGHT;
    streak += bloomTexture.SampleLevel(
        bloomSampler, saturate(uv - float2(texelSize.x * STREAK_FAR_OFFSET_TEXELS, 0.0f)), 0.0f).rgb * STREAK_FAR_WEIGHT;

    // 筋は、明るくまとまった光（ブルームより筋が強い所）だけに出している
    const float streakLuminance = dot(
        streak,
        BLOOM_LUMINANCE_WEIGHTS);
    const float bloomLuminanceForShape = dot(
        sharpBloom,
        BLOOM_LUMINANCE_WEIGHTS);
    const float compactHighlight = saturate(
        (streakLuminance - bloomLuminanceForShape * STREAK_DIFFUSE_SUPPRESSION) * STREAK_COMPACT_GAIN + STREAK_COMPACT_BIAS);
    const float brightStreak = smoothstep(
        STREAK_BRIGHTNESS_START,
        STREAK_BRIGHTNESS_END,
        streakLuminance);
    // イベントでブルームを強めたときは、筋も強くしている
    const float eventBoost = saturate(
        (bloomIntensity - STREAK_EVENT_BLOOM_THRESHOLD) * STREAK_EVENT_RESPONSE);
    const float streakStrength =
        (STREAK_BASE_STRENGTH + eventBoost * STREAK_EVENT_STRENGTH) * brightStreak * compactHighlight;
    const float3 streakColor =
        streak * STREAK_TINT * streakStrength;

    // レンズの汚れは、ブルームがある場所だけに表示している。
    // いつも張り付いた汚れを避けながら、明るい照明にレンズらしい反応を加えている。
    const float2 centered = (uv - 0.5f) * float2(screenAspect, 1.0f);
    const float edgeWeight = smoothstep(LENS_DIRT_EDGE_START, LENS_DIRT_EDGE_FULL, length(centered));
    const float dirtWave = LensDirtWave(uv);
    const float dirtMask = smoothstep(
        LENS_DIRT_MASK_START,
        LENS_DIRT_MASK_FULL,
        dirtWave + edgeWeight * LENS_DIRT_EDGE_MASK_GAIN);
    const float bloomLuminance = dot(
        bloom,
        BLOOM_LUMINANCE_WEIGHTS);
    const float visibleDirt = dirtMask * smoothstep(LENS_DIRT_MIN_LUMINANCE, LENS_DIRT_FULL_LUMINANCE, bloomLuminance)
        * saturate(lensDirtStrength);
    const float3 dirtGlow = bloom * LENS_DIRT_GLOW_TINT
        * visibleDirt * LENS_DIRT_GLOW_SCALE;

    // ブルーム＋光の筋＋レンズの汚れの光を返している（加算合成で画面に足される）
    return float4(
        bloom * bloomIntensity + streakColor + dirtGlow,
        1.0f);
}
