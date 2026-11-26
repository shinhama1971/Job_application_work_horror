// ============================================================================
// シェーダーの役割: 露出（目の慣れ）と暗い部分の持ち上げを計算し、暗い場所でも進める明るさにしている。
// 露出は、シーンが決めた値に、自動露出（autoExposureCS.hlsl）が画面の明るさから求めた倍率を掛けて使っている。
// 暗さに目が慣れるほど、照らされていない暗い所から色が抜けて青白い白黒になり（暗順応・プルキンエ現象）、
// 暗さに慣れた目で急に明るい所を見ると、一瞬まぶしく白く飛ぶ（まぶしさ）。どちらも自動露出のバッファから読んでいる。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "renderingConfig.hlsli"
#include "computeDispatchConfig.hlsli"
// 頂点シェーダー（unlitTextureVS）から受け取る値
struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// 画面効果の値（FullScreenQuad.cppのTimeBufferと同じ並びの前半だけを使っている）
#include "postProcessBuffer.hlsli"

// 描いた画面（t0）とサンプラー（s0）
Texture2D sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);

// 自動露出の結果（autoExposureCS.hlslが書いている）。[0]は慣れた明るさ、[1]は露出の倍率
StructuredBuffer<float> AutoExposureState : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_AUTO_EXPOSURE_TEXTURE_SLOT));
#include "eyeAdaptation.hlsli"

// 画面内の暗がりは近傍の標本を使って判定する。
#define EXPOSURE_LOCAL_SAMPLE_OFFSET_TEXELS 5.0f

// 周りが暗い所だけを明るくし、光が当たる面は変化を抑える。
#define EXPOSURE_LOCAL_DARK_START 0.025f
#define EXPOSURE_LOCAL_DARK_END 0.18f
#define EXPOSURE_HIGHLIGHT_PROTECT_START 0.14f
#define EXPOSURE_HIGHLIGHT_PROTECT_END 0.58f

// 視点の近くを優先し、露出を上げたい場面でだけ暗部を持ち上げる。
#define EXPOSURE_LIFT_CENTER_START 0.18f
#define EXPOSURE_LIFT_CENTER_END 0.76f
#define EXPOSURE_LIFT_GAIN_START 1.025f
#define EXPOSURE_LIFT_GAIN_RANGE 0.125f
#define EXPOSURE_LIFT_BASE_STRENGTH 0.0045f
#define EXPOSURE_LIFT_CENTER_STRENGTH 0.0095f

// 緊張が強いほど暗部に青白さを加える。
#define EXPOSURE_LIFT_TINT float3(0.92f, 0.95f, 1.0f)
#define EXPOSURE_TENSION_LIFT_TINT float3(0.74f, 0.87f, 1.0f)
#define EXPOSURE_LIFT_TENSION_TINT_WEIGHT 0.48f
#define EXPOSURE_TENSION_HAZE_START 0.025f
#define EXPOSURE_TENSION_HAZE_END 0.22f
#define EXPOSURE_TENSION_HAZE_TINT float3(0.020f, 0.034f, 0.040f)

// 元の画面に「足す分の明るさ」だけを返している（加算合成で画面に足される）
// まわりの明るさを3点の平均で求める重みと、輝度の重み
#define EXPOSURE_LOCAL_SAMPLE_WEIGHT (1.0f / 3.0f)
#define EXPOSURE_LUMINANCE_WEIGHTS float3(0.2126f, 0.7152f, 0.0722f)

float4 main(PS_IN input) : SV_TARGET
{
    const float3 scene =
        sceneTexture.Sample(sceneSampler, saturate(input.uv)).rgb;

    uint sceneWidth;
    uint sceneHeight;
    sceneTexture.GetDimensions(sceneWidth, sceneHeight);
    const float2 texelSize = rcp(float2(sceneWidth, sceneHeight));
    const float2 localOffset = texelSize * EXPOSURE_LOCAL_SAMPLE_OFFSET_TEXELS;
    // 中心と斜めの2点で、周りの明るさを取っている。十字の5点との違いは、暗い所の判定では
    // 小さく、全画面の描画のテクスチャの読み取りを40%減らせる。
    const float3 localAverage =
        (scene +
         sceneTexture.SampleLevel(
            sceneSampler,
            saturate(input.uv + localOffset),
            0.0f).rgb +
         sceneTexture.SampleLevel(
            sceneSampler,
            saturate(input.uv - localOffset),
            0.0f).rgb) * EXPOSURE_LOCAL_SAMPLE_WEIGHT;

    // シーンが決めた露出（演出の意図）に、実際の画面の明るさから求めた目の慣れの倍率を掛け、暗順応とまぶしさを加えている（eyeAdaptation.hlsli）。
    // HDRターゲットへの加算で C + (C' - C) = C' とする。暗くする所では加算値が負になる。
    const float adaptedExposure = GetAdaptedExposure();
    const float3 adaptationLight = ApplyEyeAdaptation(scene) - scene;

    // 緊張が高まるほど、深い影の一部だけを持ち上げている。これで冷たい霧のように
    // 輪郭が浮かび、光が当たっている面は平らにならない。
    const float luminance = dot(scene, EXPOSURE_LUMINANCE_WEIGHTS);
    const float localLuminance = dot(
        localAverage,
        EXPOSURE_LUMINANCE_WEIGHTS);
    // 周りが暗く、明るい部分ではない所を、画面の中心ほど強く持ち上げている（露出を上げる設定のときだけ）
    const float localDarkness =
        1.0f - smoothstep(EXPOSURE_LOCAL_DARK_START, EXPOSURE_LOCAL_DARK_END, localLuminance);
    const float highlightProtection =
        1.0f - smoothstep(EXPOSURE_HIGHLIGHT_PROTECT_START, EXPOSURE_HIGHLIGHT_PROTECT_END, luminance);
    const float2 centered =
        (input.uv - float2(0.5f, 0.5f)) * float2(screenAspect, 1.0f);
    const float centerPriority =
        1.0f - smoothstep(EXPOSURE_LIFT_CENTER_START, EXPOSURE_LIFT_CENTER_END, length(centered));
    const float adaptationRequest = saturate(
        (adaptedExposure - EXPOSURE_LIFT_GAIN_START) / EXPOSURE_LIFT_GAIN_RANGE);
    const float localLiftStrength =
        adaptationRequest * localDarkness * highlightProtection *
        (EXPOSURE_LIFT_BASE_STRENGTH + centerPriority * EXPOSURE_LIFT_CENTER_STRENGTH);
    // 持ち上げる光の色：緊張が高いほど青白くしている
    const float3 adaptationTint = lerp(
        EXPOSURE_LIFT_TINT,
        EXPOSURE_TENSION_LIFT_TINT,
        corridorTension * EXPOSURE_LIFT_TENSION_TINT_WEIGHT);
    const float3 localAdaptation =
        adaptationTint * localLiftStrength;
    // 緊張が高いときは、暗い所に青みがかった霧を薄く足している
    const float shadowMask = 1.0f - smoothstep(EXPOSURE_TENSION_HAZE_START, EXPOSURE_TENSION_HAZE_END, luminance);
    const float3 tensionHaze = EXPOSURE_TENSION_HAZE_TINT *
        corridorTension * shadowMask;
    return float4(
        adaptationLight + localAdaptation + tensionHaze,
        1.0f);
}
