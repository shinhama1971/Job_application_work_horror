// ============================================================================
// シェーダーの役割: 人影（ShadowMan）の輪郭をノイズで崩しながら消している（崩れる縁は赤く光る）。
// ============================================================================

#include "common.hlsl"

// ShadowMan.cppのDissolveBufferと同じ並び（b7を一時的に借りている）。経過時間、見えている度合い、縁の幅
cbuffer DissolveBuffer : register(b7)
{
    float DissolveTime;
    float Visibility;
    float EdgeWidth;
    float DissolvePadding;
};

// 3次元の値から0〜1の疑似乱数を作っている
float Hash31(float3 value)
{
    value = frac(value * 0.1031f);
    value += dot(value, value.yzx + 33.33f);
    return frac((value.x + value.y) * value.z);
}

float4 main(in PS_IN input) : SV_Target
{
    // ワールド空間のマス目で模様を人影に固定し、プレイヤーの方へ回っても模様がずれないようにしている。
    // 動く2つ目の模様を重ね、止まったノイズに見えるのを防いでいる。
    const float3 cell = floor(input.worldPos * 0.42f);
    const float coarseNoise = Hash31(cell);
    const float movingNoise = Hash31(
        cell * 1.73f + floor(DissolveTime * 14.0f));
    const float dissolveNoise =
        coarseNoise * 0.72f + movingNoise * 0.28f;

    // 見えている度合いより乱数が大きい画素は描かない（度合いが下がるほど穴が増える）
    const float signedEdge = Visibility - dissolveNoise;
    clip(signedEdge);

    // 消える境目の近くを、赤く光る縁にしている
    const float edge = 1.0f - smoothstep(
        0.0f,
        max(EdgeWidth, 0.001f),
        signedEdge);

    // 懐中電灯の円錐の中だけ、わずかに明るくしている
    const float distanceFromCamera = length(input.viewPos);
    float flashlight = 0.0f;
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > 0.001f)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float cone = smoothstep(
            Light.SpotParams.y,
            Light.SpotParams.x,
            dot(pixelDirection, normalize(Light.Direction.xyz)));
        const float rangeFade = saturate(
            1.0f - distanceFromCamera / max(Light.Range, 0.001f));
        flashlight = cone * rangeFade * rangeFade;
    }

    // ほとんど黒い人影の色に、脈打つ赤い縁を足している
    const float pulse = sin(DissolveTime * 23.0f) * 0.5f + 0.5f;
    const float3 silhouette = float3(0.006f, 0.009f, 0.008f) +
        flashlight * float3(0.025f, 0.032f, 0.027f);
    const float3 burningEdge =
        float3(1.15f, 0.055f, 0.018f) * edge * (0.72f + pulse * 0.28f);

    return float4(silhouette + burningEdge, 1.0f);
}
