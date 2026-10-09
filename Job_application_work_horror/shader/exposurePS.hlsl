// ============================================================================
// シェーダーの役割: 露出（目の慣れ）と暗い部分の持ち上げを計算し、暗い場所でも進める明るさにしている。
// 露出は、シーンが決めた値に、自動露出（autoExposureCS.hlsl）が画面の明るさから求めた倍率を掛けて使っている。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

// 頂点シェーダー（unlitTextureVS）から受け取る値
struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// 画面効果の値（FullScreenQuad.cppのTimeBufferと同じ並びの前半だけを使っている）
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
    float lensMoisture;
    float corridorTension;
    float exposurePadding;
};

// 描いた画面（t0）とサンプラー（s0）
Texture2D sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);

// 自動露出の結果（autoExposureCS.hlslが書いている）。[0]は慣れた明るさ、[1]は露出の倍率
StructuredBuffer<float> AutoExposureState : register(t3);

// 元の画面に「足す分の明るさ」だけを返している（加算合成で画面に足される）
float4 main(PS_IN input) : SV_TARGET
{
    const float3 scene =
        sceneTexture.Sample(sceneSampler, saturate(input.uv)).rgb;

    uint sceneWidth;
    uint sceneHeight;
    sceneTexture.GetDimensions(sceneWidth, sceneHeight);
    const float2 texelSize = rcp(float2(sceneWidth, sceneHeight));
    const float2 localOffset = texelSize * 5.0f;
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
            0.0f).rgb) * (1.0f / 3.0f);

    // シーンが決めた露出（演出の意図）に、実際の画面の明るさから求めた目の慣れの倍率を掛けている。
    const float adaptedExposure = exposure * AutoExposureState[1];

    // 驚かせる演出の間は、画面を一瞬明るくしている。上限を設けて、
    // 目の慣れでこの後に描くUIが白く飛ばないようにしている。
    const float eventExposure = horrorPulseStrength * 0.08f;
    const float exposureGain =
        max(adaptedExposure + eventExposure - 1.0f, 0.0f);
    const float3 adaptationLight = scene * exposureGain;

    // 緊張が高まるほど、深い影の一部だけを持ち上げている。これで冷たい霧のように
    // 輪郭が浮かび、光が当たっている面は平らにならない。
    const float luminance = dot(scene, float3(0.2126f, 0.7152f, 0.0722f));
    const float localLuminance = dot(
        localAverage,
        float3(0.2126f, 0.7152f, 0.0722f));
    // 周りが暗く、明るい部分ではない所を、画面の中心ほど強く持ち上げている（露出を上げる設定のときだけ）
    const float localDarkness =
        1.0f - smoothstep(0.025f, 0.18f, localLuminance);
    const float highlightProtection =
        1.0f - smoothstep(0.14f, 0.58f, luminance);
    const float2 centered =
        (input.uv - float2(0.5f, 0.5f)) * float2(screenAspect, 1.0f);
    const float centerPriority =
        1.0f - smoothstep(0.18f, 0.76f, length(centered));
    const float adaptationRequest = saturate(
        (adaptedExposure - 1.025f) / 0.125f);
    const float localLiftStrength =
        adaptationRequest * localDarkness * highlightProtection *
        (0.0045f + centerPriority * 0.0095f);
    // 持ち上げる光の色：緊張が高いほど青白くしている
    const float3 adaptationTint = lerp(
        float3(0.92f, 0.95f, 1.0f),
        float3(0.74f, 0.87f, 1.0f),
        corridorTension * 0.48f);
    const float3 localAdaptation =
        adaptationTint * localLiftStrength;
    // 緊張が高いときは、暗い所に青みがかった霧を薄く足している
    const float shadowMask = 1.0f - smoothstep(0.025f, 0.22f, luminance);
    const float3 tensionHaze = float3(0.020f, 0.034f, 0.040f) *
        corridorTension * shadowMask;
    return float4(
        adaptationLight + localAdaptation + tensionHaze,
        1.0f);
}
