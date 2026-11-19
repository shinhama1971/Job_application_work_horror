// ============================================================================
// シェーダーの役割: 人影（ShadowMan）の輪郭をノイズで崩しながら消している（崩れる縁は赤く光る）。
// ============================================================================

#include "common.hlsl"

// ShadowMan.cppのDissolveBufferと同じ並び（b7を一時的に借りている）。経過時間、見えている度合い、縁の幅
cbuffer DissolveBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_DEBUG_VIEW_BUFFER_SLOT))
{
    float DissolveTime;
    float Visibility;
    float EdgeWidth;
    float DissolvePadding;
};

// 疑似乱数の係数（Dave Hoskins の hash13。決まった値）
#define DISSOLVE_HASH_SCALE 0.1031f
#define DISSOLVE_HASH_OFFSET 33.33f
// 模様のマスの細かさ、動く模様（マスのずらし・切り替わる速さ）、2つの模様の重み
#define DISSOLVE_CELL_SCALE 0.42f
#define DISSOLVE_MOVING_CELL_SCALE 1.73f
#define DISSOLVE_MOVING_RATE 14.0f
#define DISSOLVE_COARSE_WEIGHT 0.72f
#define DISSOLVE_MOVING_WEIGHT 0.28f
// 0で割らないための下限
#define DISSOLVE_MIN_DIVISOR 0.001f
// 縁の脈の速さ、人影の色、懐中電灯で明るくする色、赤く光る縁の色、縁の明るさ（最低＋脈の幅）
#define DISSOLVE_PULSE_SPEED 23.0f
#define DISSOLVE_SILHOUETTE_COLOR float3(0.006f, 0.009f, 0.008f)
#define DISSOLVE_FLASHLIGHT_COLOR float3(0.025f, 0.032f, 0.027f)
#define DISSOLVE_EDGE_COLOR float3(1.15f, 0.055f, 0.018f)
#define DISSOLVE_EDGE_BASE 0.72f
#define DISSOLVE_EDGE_PULSE 0.28f

// 3次元の値から0〜1の疑似乱数を作っている
float Hash31(float3 value)
{
    value = frac(value * DISSOLVE_HASH_SCALE);
    value += dot(value, value.yzx + DISSOLVE_HASH_OFFSET);
    return frac((value.x + value.y) * value.z);
}

float4 main(in PS_IN input) : SV_Target
{
    // ワールド空間のマス目で模様を人影に固定し、プレイヤーの方へ回っても模様がずれないようにしている。
    // 動く2つ目の模様を重ね、止まったノイズに見えるのを防いでいる。
    const float3 cell = floor(input.worldPos * DISSOLVE_CELL_SCALE);
    const float coarseNoise = Hash31(cell);
    const float movingNoise = Hash31(
        cell * DISSOLVE_MOVING_CELL_SCALE + floor(DissolveTime * DISSOLVE_MOVING_RATE));
    const float dissolveNoise =
        coarseNoise * DISSOLVE_COARSE_WEIGHT + movingNoise * DISSOLVE_MOVING_WEIGHT;

    // 見えている度合いより乱数が大きい画素は描かない（度合いが下がるほど穴が増える）
    const float signedEdge = Visibility - dissolveNoise;
    clip(signedEdge);

    // 消える境目の近くを、赤く光る縁にしている
    const float edge = 1.0f - smoothstep(
        0.0f,
        max(EdgeWidth, DISSOLVE_MIN_DIVISOR),
        signedEdge);

    // 懐中電灯の円錐の中だけ、わずかに明るくしている
    const float distanceFromCamera = length(input.viewPos);
    float flashlight = 0.0f;
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > DISSOLVE_MIN_DIVISOR)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float cone = smoothstep(
            Light.SpotParams.y,
            Light.SpotParams.x,
            dot(pixelDirection, normalize(Light.Direction.xyz)));
        const float rangeFade = saturate(
            1.0f - distanceFromCamera / max(Light.Range, DISSOLVE_MIN_DIVISOR));
        flashlight = cone * rangeFade * rangeFade;
    }

    // ほとんど黒い人影の色に、脈打つ赤い縁を足している
    const float pulse = sin(DissolveTime * DISSOLVE_PULSE_SPEED) * 0.5f + 0.5f;
    const float3 silhouette = DISSOLVE_SILHOUETTE_COLOR +
        flashlight * DISSOLVE_FLASHLIGHT_COLOR;
    const float3 burningEdge =
        DISSOLVE_EDGE_COLOR * edge * (DISSOLVE_EDGE_BASE + pulse * DISSOLVE_EDGE_PULSE);

    return float4(silhouette + burningEdge, 1.0f);
}
