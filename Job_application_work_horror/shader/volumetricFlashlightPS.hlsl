// ============================================================================
// シェーダーの役割: 懐中電灯の光の筋（空気中の霧やちりで光が見える散乱）を、画面に重ねている。
// ============================================================================

// 頂点シェーダーから受け取る値
struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// 画面効果の値（FullScreenQuad.cppのTimeBufferと同じ並びの前半）
cbuffer PostProcessBuffer : register(b0)
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

// 懐中電灯（common.hlslのLIGHTと同じ並び。b3）
struct LIGHT
{
    bool Enable;
    bool FlashlightEnabled;
    float Intensity;
    float Range;
    float4 Direction;
    float4 Diffuse;
    float4 Ambient;
    float4 SpotParams;
};

cbuffer LightBuffer : register(b3)
{
    LIGHT Light;
};

// シャドウマップの行列とパラメーター（b8。z=near、w=farを使っている）
cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
};

// 懐中電灯から見た深度（シャドウマップ、t5）とサンプラー
Texture2D<float> FlashlightDepth : register(t5);
SamplerState LinearSampler : register(s0);

// 0〜1の疑似乱数と、それをなめらかにつないだノイズ
float Hash(float2 value)
{
    float3 value3 = frac(float3(value.x, value.y, value.x) * 0.1031f);
    value3 += dot(value3, value3.yzx + 33.33f);
    return frac((value3.x + value3.y) * value3.z);
}

float ValueNoise(float2 value)
{
    const float2 cell = floor(value);
    float2 local = frac(value);
    local = local * local * (3.0f - 2.0f * local);

    const float bottom = lerp(
        Hash(cell),
        Hash(cell + float2(1.0f, 0.0f)),
        local.x);
    const float top = lerp(
        Hash(cell + float2(0.0f, 1.0f)),
        Hash(cell + float2(1.0f, 1.0f)),
        local.x);
    return lerp(bottom, top, local.y);
}

// シャドウマップの深度（0〜1）を、実際の距離に戻している
float LinearizeDepth(float depth, float nearPlane, float farPlane)
{
    return nearPlane * farPlane /
        max(farPlane - depth * (farPlane - nearPlane), 0.001f);
}

float4 main(PS_IN input) : SV_Target
{
    // ライトが消えているときや、効果が0のときは何も足さない
    if (!Light.Enable || !Light.FlashlightEnabled || volumeIntensity <= 0.0f)
    {
        return 0.0f;
    }

    // カメラからの光線を求め、正方形のスポットライトの深度マップへ投影している。
    // 深度マップは手元（目から右下へ数単位、FlashlightRig.h）から作っているが、ずれは光の筋のぼやけより小さいため、
    // カメラと同じ位置・向きとみなして、画面の位置からそのまま読んでいる。
    const float2 screenNdc = float2(
        input.uv.x * 2.0f - 1.0f,
        1.0f - input.uv.y * 2.0f);
    const float mainTanHalfFov = tan(radians(30.0f));
    const float shadowTanHalfFov = tan(radians(33.0f));
    const float2 shadowNdc = float2(
        screenNdc.x * screenAspect * mainTanHalfFov / shadowTanHalfFov,
        screenNdc.y * mainTanHalfFov / shadowTanHalfFov);
    const float2 shadowUV = float2(
        shadowNdc.x * 0.5f + 0.5f,
        -shadowNdc.y * 0.5f + 0.5f);

    if (any(shadowUV <= 0.0f) || any(shadowUV >= 1.0f))
    {
        return 0.0f;
    }

    // その方向で、光が何かに当たるまでの距離を求めている
    const float depth = FlashlightDepth.SampleLevel(
        LinearSampler,
        shadowUV,
        0.0f);
    const float occluderDistance = LinearizeDepth(
        depth,
        ShadowParameters.z,
        ShadowParameters.w);

    // 光の円錐の中心ほど濃くしている
    const float radial = saturate(1.0f - length(shadowNdc));
    const float softBeam = radial * radial * (3.0f - 2.0f * radial);
    // 平方根で変化させ、壁の近くの短い光線も見せつつ、長い廊下では散乱した光を多くためている。
    const float visibleLength = sqrt(saturate(
        (occluderDistance - 2.0f) / 120.0f));
    // 画面の下の方（床）では薄くしている
    const float floorFade = 1.0f - smoothstep(0.62f, 0.98f, input.uv.y);

    // 連続した画面の流れを使い、毎フレーム乱数を変えるテレビのノイズのようなちらつきを避けている。
    // 速さの違う2つの層で、光の円錐の中の違う奥行きに漂う粒を表している。
    const float2 dustFlow = float2(time * 0.42f, -time * 0.24f);
    const float nearDustNoise = ValueNoise(
        input.pos.xy * 0.045f + dustFlow);
    // 遠くの粒は補間しないセルのノイズで十分なため、4回の乱数の計算を1回にしている。
    const float farDustNoise = Hash(floor(
        input.pos.xy * 0.019f - dustFlow * 0.57f + 19.7f));
    const float nearDust =
        smoothstep(0.78f, 0.96f, nearDustNoise) * 0.055f;
    const float farDust =
        smoothstep(0.84f, 0.98f, farDustNoise) * 0.028f;
    const float driftingDust = nearDust + farDust;
    const float slowVariation =
        sin(input.uv.y * 38.0f - time * 1.7f) * 0.5f + 0.5f;

    // 光の筋の濃さ：驚かせる演出や緊張が高いほど濃くしている
    float density =
        (0.032f + slowVariation * 0.012f + driftingDust) *
        softBeam * visibleLength * floorFade * volumeIntensity;
    density *= 1.0f + horrorPulseStrength * 0.32f;
    density *= 1.0f + corridorTension * 0.48f;
    // 光の筋の色：普段は暖かい色、緊張が高いと青白くしている
    const float3 beamColor = lerp(
        float3(1.0f, 0.78f, 0.50f),
        float3(0.76f, 0.84f, 0.88f),
        corridorTension * 0.34f) *
        saturate(Light.Intensity / 1.35f);

    // 色と濃さを返している（アルファブレンドではなく加算合成で重ねている）
    return float4(beamColor, saturate(density));
}
