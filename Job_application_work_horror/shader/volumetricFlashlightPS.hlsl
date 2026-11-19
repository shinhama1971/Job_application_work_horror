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
#include "postProcessBuffer.hlsli"

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


#define WATER_CAUSTICS_COMPACT_BUFFER
#include "waterCausticsLayout.hlsli"
#undef WATER_CAUSTICS_COMPACT_BUFFER

// 懐中電灯から見た深度（シャドウマップ、t5）とサンプラー
Texture2D<float> FlashlightDepth : register(t5);
SamplerState LinearSampler : register(s0);

// 疑似乱数の係数（Dave Hoskins の hash12。決まった値）
#define VOLUME_HASH_SCALE 0.1031f
#define VOLUME_HASH_OFFSET 33.33f

// 0〜1の疑似乱数と、それをなめらかにつないだノイズ
float Hash(float2 value)
{
    float3 value3 = frac(float3(value.x, value.y, value.x) * VOLUME_HASH_SCALE);
    value3 += dot(value3, value3.yzx + VOLUME_HASH_OFFSET);
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

// 見えない影の人影が、この画素の光の帯をどこで遮るか。遮る度合い（0〜1）を返し、遮る所までの距離を distance に返している。
// 懐中電灯は目のすぐそばにあるので、目から見た人影の形が、そのまま光の帯の中の影の形になる（人影の先の霧には光が届かない）。
// 人影は胴から頭までのカプセル1本で表し、ビュー空間で、画素の視線とカプセルの芯が一番近づく距離から求めている
// 人影：見え方のしきい値、カプセルの下端・上端の高さ、線分の長さの下限、
// 目のすぐそばでは遮らない距離、輪郭のぼかし（外・内の半径）
#define BEAM_FIGURE_MIN_VISIBILITY 0.001f
#define BEAM_FIGURE_BOTTOM 4.0f
#define BEAM_FIGURE_TOP 36.0f
#define BEAM_FIGURE_MIN_LENGTH_SQ 1e-4f
#define BEAM_FIGURE_MIN_DISTANCE 1.0f
#define BEAM_FIGURE_OUTER_RADIUS 6.6f
#define BEAM_FIGURE_INNER_RADIUS 4.2f

float GetUnseenFigureBeamOcclusion(float3 viewDirection, out float distance)
{
    distance = 0.0f;
    float occlusion = 0.0f;
    [branch]
    if (UnseenFigure.w > BEAM_FIGURE_MIN_VISIBILITY)
    {
        const float3 bottom = mul(float4(UnseenFigure.xyz + float3(0.0f, BEAM_FIGURE_BOTTOM, 0.0f), 1.0f), CausticView).xyz;
        const float3 top = mul(float4(UnseenFigure.xyz + float3(0.0f, BEAM_FIGURE_TOP, 0.0f), 1.0f), CausticView).xyz;
        // 視線（目からviewDirectionの向き）と線分 bottom-top の一番近い点を求めている
        const float3 segment = top - bottom;
        const float segmentLength2 = max(dot(segment, segment), BEAM_FIGURE_MIN_LENGTH_SQ);
        const float along = dot(viewDirection, segment);
        const float rayOffset = dot(viewDirection, -bottom);
        const float segmentOffset = dot(segment, -bottom);
        const float segmentT = saturate((segmentOffset - along * rayOffset) / max(segmentLength2 - along * along, BEAM_FIGURE_MIN_LENGTH_SQ));
        distance = max(along * segmentT - rayOffset, 0.0f);
        const float miss = length(viewDirection * distance - (bottom + segment * segmentT));
        occlusion = distance > BEAM_FIGURE_MIN_DISTANCE ? smoothstep(BEAM_FIGURE_OUTER_RADIUS, BEAM_FIGURE_INNER_RADIUS, miss) * saturate(UnseenFigure.w) : 0.0f;
    }
    return occlusion;
}

// 0で割らないための下限
#define VOLUME_MIN_DIVISOR 0.001f

// シャドウマップの深度（0〜1）を、実際の距離に戻している
float LinearizeDepth(float depth, float nearPlane, float farPlane)
{
    return nearPlane * farPlane /
        max(farPlane - depth * (farPlane - nearPlane), VOLUME_MIN_DIVISOR);
}

// 画面の半画角（度）と、懐中電灯の深度マップの半画角（度。影の画角66度の半分）
#define VOLUME_MAIN_HALF_FOV_DEGREES 30.0f
#define VOLUME_SHADOW_HALF_FOV_DEGREES 33.0f
// ゆっくりした縦のむら（縞の数・流れる速さ）
#define VOLUME_SLOW_VARIATION_FREQUENCY 38.0f
#define VOLUME_SLOW_VARIATION_SPEED 1.7f

// 画面下端で光を薄め、床の輪郭を隠さないための開始位置と終了位置。
static const float VOLUME_FLOOR_FADE_START = 0.62f;
static const float VOLUME_FLOOR_FADE_END = 0.98f;

// 静かな場面の光の濃さと、恐怖・緊張の演出で加える濃さ。
static const float VOLUME_BASE_DENSITY = 0.026f;
static const float VOLUME_DENSITY_VARIATION = 0.010f;
static const float VOLUME_SMOKE_DENSITY = 0.040f;
static const float VOLUME_HORROR_DENSITY_GAIN = 0.32f;
static const float VOLUME_TENSION_DENSITY_GAIN = 0.48f;

// 通常時の暖色から緊張時の青白い色へ寄せる。基準の光量で強さを正規化する。
static const float3 VOLUME_BEAM_WARM_TINT = float3(1.0f, 0.78f, 0.50f);
static const float3 VOLUME_BEAM_COLD_TINT = float3(0.76f, 0.84f, 0.88f);
static const float VOLUME_BEAM_TENSION_BLEND = 0.34f;
static const float VOLUME_BEAM_REFERENCE_INTENSITY = 1.35f;

// 遮蔽物までの距離と、人影までの距離を同じ範囲で光の濃さへ変換する。
static const float VOLUME_VISIBLE_LENGTH_START = 2.0f;
static const float VOLUME_VISIBLE_LENGTH_RANGE = 120.0f;
static const float VOLUME_FIGURE_SCATTER_ATTENUATION = 0.55f;

// 光の円錐に粒を2層で漂わせ、近い粒と遠い粒の分布を変える。
static const float VOLUME_DUST_FLOW_X = 0.42f;
static const float VOLUME_DUST_FLOW_Y = 0.24f;
static const float VOLUME_NEAR_DUST_NOISE_SCALE = 0.045f;
static const float VOLUME_FAR_DUST_NOISE_SCALE = 0.019f;
static const float VOLUME_FAR_DUST_FLOW_SCALE = 0.57f;
static const float VOLUME_FAR_DUST_SEED = 19.7f;
static const float VOLUME_NEAR_DUST_START = 0.78f;
static const float VOLUME_NEAR_DUST_FULL = 0.96f;
static const float VOLUME_NEAR_DUST_WEIGHT = 0.055f;
static const float VOLUME_FAR_DUST_START = 0.84f;
static const float VOLUME_FAR_DUST_FULL = 0.98f;
static const float VOLUME_FAR_DUST_WEIGHT = 0.028f;

// 大きさと向きが違う煙を重ね、光の筋の濃さを場所によって変える。
static const float VOLUME_SMOKE_LARGE_SCALE_X = 4.5f;
static const float VOLUME_SMOKE_LARGE_SCALE_Y = 3.2f;
static const float VOLUME_SMOKE_LARGE_FLOW_X = 0.055f;
static const float VOLUME_SMOKE_LARGE_FLOW_Y = 0.030f;
static const float VOLUME_SMOKE_SMALL_SCALE_X = 10.0f;
static const float VOLUME_SMOKE_SMALL_SCALE_Y = 7.0f;
static const float VOLUME_SMOKE_SMALL_FLOW_X = 0.085f;
static const float VOLUME_SMOKE_SMALL_FLOW_Y = 0.045f;
static const float VOLUME_SMOKE_SMALL_SEED = 5.0f;
static const float VOLUME_SMOKE_START = 0.30f;
static const float VOLUME_SMOKE_FULL = 0.85f;
static const float VOLUME_SMOKE_LARGE_WEIGHT = 0.65f;
static const float VOLUME_SMOKE_SMALL_WEIGHT = 0.35f;

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
    const float mainTanHalfFov = tan(radians(VOLUME_MAIN_HALF_FOV_DEGREES));
    const float shadowTanHalfFov = tan(radians(VOLUME_SHADOW_HALF_FOV_DEGREES));
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
    float visibleLength = sqrt(saturate(
        (occluderDistance - VOLUME_VISIBLE_LENGTH_START) / VOLUME_VISIBLE_LENGTH_RANGE));
    // 見えない影の人影が光の帯の中に立つと、その先の霧には光が届かず、帯の中に人の形の暗い切れ目ができる
    // （目には見えないが、照らすと形が浮かぶ。照らされた影は気づいて寄ってくる。UnseenPresenceAnomaly.h）
    float figureDistance;
    const float3 viewDirection = normalize(float3(
        screenNdc.x * screenAspect * mainTanHalfFov, screenNdc.y * mainTanHalfFov, 1.0f));
    const float figureOcclusion = GetUnseenFigureBeamOcclusion(viewDirection, figureDistance);
    [branch]
    if (figureOcclusion > 0.0f && figureDistance < occluderDistance)
    {
        // 人影までの短い区間の霧だけが光り、その先は暗い（人影の手前の霧も少し薄くして、輪郭をはっきりさせている）
        const float lengthToFigure = sqrt(saturate((figureDistance - VOLUME_VISIBLE_LENGTH_START) / VOLUME_VISIBLE_LENGTH_RANGE)) * VOLUME_FIGURE_SCATTER_ATTENUATION;
        visibleLength = lerp(visibleLength, lengthToFigure, figureOcclusion);
    }
    // 画面の下の方（床）では薄くしている
    const float floorFade = 1.0f - smoothstep(VOLUME_FLOOR_FADE_START, VOLUME_FLOOR_FADE_END, input.uv.y);

    // 連続した画面の流れを使い、毎フレーム乱数を変えるテレビのノイズのようなちらつきを避けている。
    // 速さの違う2つの層で、光の円錐の中の違う奥行きに漂う粒を表している。
    const float2 dustFlow = float2(time * VOLUME_DUST_FLOW_X, -time * VOLUME_DUST_FLOW_Y);
    const float nearDustNoise = ValueNoise(
        input.pos.xy * VOLUME_NEAR_DUST_NOISE_SCALE + dustFlow);
    // 遠くの粒は補間しないセルのノイズで十分なため、4回の乱数の計算を1回にしている。
    const float farDustNoise = Hash(floor(
        input.pos.xy * VOLUME_FAR_DUST_NOISE_SCALE - dustFlow * VOLUME_FAR_DUST_FLOW_SCALE + VOLUME_FAR_DUST_SEED));
    const float nearDust =
        smoothstep(VOLUME_NEAR_DUST_START, VOLUME_NEAR_DUST_FULL, nearDustNoise) * VOLUME_NEAR_DUST_WEIGHT;
    const float farDust =
        smoothstep(VOLUME_FAR_DUST_START, VOLUME_FAR_DUST_FULL, farDustNoise) * VOLUME_FAR_DUST_WEIGHT;
    const float driftingDust = nearDust + farDust;
    const float slowVariation =
        sin(input.uv.y * VOLUME_SLOW_VARIATION_FREQUENCY - time * VOLUME_SLOW_VARIATION_SPEED) * 0.5f + 0.5f;
    // 光の中に漂う煙：大きさの違う2つのノイズを逆向きにゆっくり流し、光の筋に濃い所と薄い所のむらを作っている
    const float smokeLarge = ValueNoise(
        input.uv * float2(VOLUME_SMOKE_LARGE_SCALE_X * screenAspect, VOLUME_SMOKE_LARGE_SCALE_Y) + float2(time * VOLUME_SMOKE_LARGE_FLOW_X, -time * VOLUME_SMOKE_LARGE_FLOW_Y));
    const float smokeSmall = ValueNoise(
        input.uv * float2(VOLUME_SMOKE_SMALL_SCALE_X * screenAspect, VOLUME_SMOKE_SMALL_SCALE_Y) + float2(-time * VOLUME_SMOKE_SMALL_FLOW_X, time * VOLUME_SMOKE_SMALL_FLOW_Y) + VOLUME_SMOKE_SMALL_SEED);
    const float smoke = smoothstep(VOLUME_SMOKE_START, VOLUME_SMOKE_FULL, smokeLarge * VOLUME_SMOKE_LARGE_WEIGHT + smokeSmall * VOLUME_SMOKE_SMALL_WEIGHT);

    // 光の筋の濃さ：驚かせる演出や緊張が高いほど濃くしている
    float density =
        (VOLUME_BASE_DENSITY + slowVariation * VOLUME_DENSITY_VARIATION + driftingDust + smoke * VOLUME_SMOKE_DENSITY) *
        softBeam * visibleLength * floorFade * volumeIntensity;
    density *= 1.0f + horrorPulseStrength * VOLUME_HORROR_DENSITY_GAIN;
    density *= 1.0f + corridorTension * VOLUME_TENSION_DENSITY_GAIN;
    // 光の筋の色：普段は暖かい色、緊張が高いと青白くしている
    const float3 beamColor = lerp(
        VOLUME_BEAM_WARM_TINT,
        VOLUME_BEAM_COLD_TINT,
        corridorTension * VOLUME_BEAM_TENSION_BLEND) *
        saturate(Light.Intensity / VOLUME_BEAM_REFERENCE_INTENSITY);

    // 色と濃さを返している（アルファブレンドではなく加算合成で重ねている）
    return float4(beamColor, saturate(density));
}
