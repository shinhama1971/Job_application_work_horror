// ============================================================================
// シェーダーの役割: 画面の各画素に届く DDGI の間接光（壁や床で跳ね返った光）を、縦横半分の解像度で求めている。
// 主な技術: 深度からの位置と法線の復元（SSAO と同じ）、まわりの8つのプローブの重み付きの読み取り（ddgiCommon.hlsli）
//
// ・壁と床のシェーダーで画素ごとに8つのプローブを読むと、大きなシェーダーが重くなるので、ここでまとめて求め、
//   壁と床のシェーダーは結果の画像（t18。screenAmbientOcclusion.hlsli の GetScreenIndirectLight）を1回読むだけにしている。
// ・間接光はなめらかに変わるので、縦横半分の解像度でも粗さは目立たない。
// ・プローブの格子の外（建物の外）は、横の端へ行くほど弱めて0にしている。
// ============================================================================

#include "ddgiCommon.hlsli"
#include "computeDispatchConfig.hlsli"

Texture2D<float> SceneDepth : register(t5);
RWTexture2D<float4> IndirectOutput : register(u0);

// 格子の端から内側へ、間接光を弱める幅（プローブの間隔に対する割合）
static const float DDGI_VOLUME_EDGE_FADE = 0.5f;

// 画素の位置（描く範囲の左上が0）から、視点の空間の位置を復元している
float3 ViewPositionAt(int2 pixel)
{
    const int2 clamped = clamp(pixel, int2(0, 0), int2(ViewportSize) - 1);
    const float depth = SceneDepth.Load(int3(clamped + int2(ViewportOffset), 0));
    const float2 uv = (float2(clamped) + 0.5f) / ViewportSize;
    const float4 position = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f), InverseProjection);
    return position.xyz / position.w;
}

[numthreads(SIGNAL_LOST_DDGI_APPLY_GROUP_SIZE, SIGNAL_LOST_DDGI_APPLY_GROUP_SIZE, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const int2 outputPixel = int2(id.xy);
    if (any(outputPixel >= int2(OutputSize)))
    {
        return;
    }
    const int2 pixel = min(outputPixel * SIGNAL_LOST_DDGI_APPLY_DOWNSAMPLE_FACTOR, int2(ViewportSize) - 1);
    const float depth = SceneDepth.Load(int3(pixel + int2(ViewportOffset), 0));
    if (depth >= 1.0f)
    {
        IndirectOutput[outputPixel] = 0.0f;
        return;
    }

    // 法線：左右・上下の隣の画素のうち、深さの差が小さい方を使い、物の縁で法線が崩れないようにしている（SSAO と同じ）
    const float3 center = ViewPositionAt(pixel);
    const float3 right = ViewPositionAt(pixel + int2(1, 0)) - center;
    const float3 left = center - ViewPositionAt(pixel - int2(1, 0));
    const float3 down = ViewPositionAt(pixel + int2(0, 1)) - center;
    const float3 up = center - ViewPositionAt(pixel - int2(0, 1));
    const float3 horizontal = abs(right.z) < abs(left.z) ? right : left;
    const float3 vertical = abs(down.z) < abs(up.z) ? down : up;
    float3 viewNormal = normalize(cross(vertical, horizontal));
    if (dot(viewNormal, center) > 0.0f)
    {
        viewNormal = -viewNormal;
    }

    // ワールドの位置・法線・カメラへの向きへ直している
    const float3 worldPosition = mul(float4(center, 1.0f), InverseView).xyz;
    const float3 worldNormal = normalize(mul(float4(viewNormal, 0.0f), InverseView).xyz);
    const float3 cameraPosition = InverseView[3].xyz;
    const float3 toCamera = normalize(cameraPosition - worldPosition);

    // 格子の外は弱めている
    const float3 volumeMaximum = ProbeOrigin + float3(ProbeCounts - 1) * ProbeSpacing;
    const float3 margin = ProbeSpacing * DDGI_VOLUME_EDGE_FADE;
    const float3 inside = saturate(min(worldPosition - (ProbeOrigin - margin), (volumeMaximum + margin) - worldPosition) / margin);
    // 高さは床から天井までプローブがあるので、横（x・z）だけで弱めている
    const float volumeFade = inside.x * inside.z;

    IndirectOutput[outputPixel] = float4(GetDDGIIrradiance(worldPosition, worldNormal, toCamera, true) * volumeFade, 1.0f);
}
