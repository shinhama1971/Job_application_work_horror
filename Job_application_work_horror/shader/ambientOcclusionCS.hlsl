// ============================================================================
// シェーダーの役割: 画面空間の環境遮蔽（SSAO）。深度プリパスの深度から、画素のまわりの半球に物がどれだけ詰まっているかを調べ、
// 物と物の隙間・壁の根元・棚の下など、周りの光が届きにくい所ほど暗くする値（1 = 遮る物なし、0 = 完全に囲まれている）を書いている。
// 主な技術: 深度からの位置の復元、深度の差から求める法線、法線の向きの半球に散らした点の判定、4x4の模様で回す標本の向き
//           （回した模様は ambientOcclusionBlurCS で深度を見ながらぼかして消している）
// ============================================================================

// AmbientOcclusion.cpp の Parameters と同じ並び（b0）
cbuffer AmbientOcclusionBuffer : register(b0)
{
    float4x4 Projection;        // 視点の空間 → 画面
    float4x4 InverseProjection; // 画面 → 視点の空間
    float2 ViewportSize;
    float2 ViewportOffset;
    float Radius;               // 調べる半球の半径（ワールドの長さ）
    float Intensity;            // 暗くする強さ
    float Bias;                 // 自分の面で暗くならないための、深さのずれ
    float MaxDistance;          // これより遠い所は計算しない（遠くは霧に沈むため）
    float2 OcclusionSize;       // 環境遮蔽の画像の大きさ（描く範囲の半分。軽くするため、縦横半分の解像度で計算している）
    float2 OcclusionPadding;
};

Texture2D<float> SceneDepth : register(t0);
RWTexture2D<float> OcclusionOutput : register(u0);

// 法線の向きの半球に散らした12個の点（長さ0.15〜1。内側ほど多く置いている）
static const float3 g_Kernel[12] =
{
    float3(0.538f, 0.183f, 0.259f), float3(-0.403f, 0.452f, 0.150f), float3(0.123f, -0.552f, 0.328f),
    float3(-0.287f, -0.210f, 0.121f), float3(0.704f, -0.257f, 0.422f), float3(-0.639f, 0.112f, 0.563f),
    float3(0.231f, 0.701f, 0.497f), float3(-0.092f, -0.833f, 0.252f), float3(0.312f, 0.034f, 0.881f),
    float3(-0.751f, -0.432f, 0.398f), float3(0.081f, 0.142f, 0.173f), float3(0.611f, 0.598f, 0.122f)
};

// 画素の位置（描く範囲の左上が0）から、視点の空間の位置を復元している
float3 ViewPositionAt(int2 pixel)
{
    const float depth = SceneDepth.Load(int3(pixel + int2(ViewportOffset), 0));
    const float2 uv = (float2(pixel) + 0.5f) / ViewportSize;
    const float4 position = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f), InverseProjection);
    return position.xyz / position.w;
}

// 0〜1の乱数（4x4の模様にして、隣り合う画素で標本の向きを変えている）
float InterleavedAngle(int2 pixel)
{
    const int2 cell = pixel & 3;
    return (float(cell.x * 4 + cell.y) + 0.5f) / 16.0f * 6.2831853f;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // 半分の解像度の1画素が、描く範囲の2x2画素の左上を受け持っている
    const int2 outputPixel = int2(id.xy);
    if (any(outputPixel >= int2(OcclusionSize)))
    {
        return;
    }
    const int2 pixel = min(outputPixel * 2, int2(ViewportSize) - 1);
    const float depth = SceneDepth.Load(int3(pixel + int2(ViewportOffset), 0));
    const float3 center = ViewPositionAt(pixel);
    // 何も描いていない所と、遠くて霧に沈む所は、遮る物なしにしている
    if (depth >= 1.0f || center.z > MaxDistance)
    {
        OcclusionOutput[outputPixel] = 1.0f;
        return;
    }

    // 法線：左右・上下の隣の画素のうち、深さの差が小さい方を使い、物の縁で法線が崩れないようにしている
    const float3 right = ViewPositionAt(pixel + int2(1, 0)) - center;
    const float3 left = center - ViewPositionAt(pixel - int2(1, 0));
    const float3 down = ViewPositionAt(pixel + int2(0, 1)) - center;
    const float3 up = center - ViewPositionAt(pixel - int2(0, 1));
    const float3 horizontal = abs(right.z) < abs(left.z) ? right : left;
    const float3 vertical = abs(down.z) < abs(up.z) ? down : up;
    float3 normal = normalize(cross(vertical, horizontal));
    // 法線はカメラの方（視点の空間の-z）を向けている
    if (dot(normal, center) > 0.0f)
    {
        normal = -normal;
    }

    // 法線を軸にした座標の向き（4x4の模様で回して、少ない標本でも偏らないようにしている）
    const float angle = InterleavedAngle(outputPixel);
    const float3 randomVector = float3(cos(angle), sin(angle), 0.0f);
    const float3 tangent = normalize(randomVector - normal * dot(randomVector, normal));
    const float3 bitangent = cross(normal, tangent);

    float occlusion = 0.0f;
    [unroll]
    for (int index = 0; index < 12; ++index)
    {
        const float3 offset = g_Kernel[index];
        const float3 samplePosition = center + (tangent * offset.x + bitangent * offset.y + normal * offset.z) * Radius;
        // 標本の点を画面へ投影し、その画素に描かれている物の深さと比べている
        const float4 clip = mul(float4(samplePosition, 1.0f), Projection);
        const float2 ndc = clip.xy / clip.w;
        const int2 samplePixel = int2(float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * ViewportSize);
        if (any(samplePixel < 0) || any(samplePixel >= int2(ViewportSize)))
        {
            continue;
        }
        const float sceneZ = ViewPositionAt(samplePixel).z;
        // 標本の点より手前に物があれば遮られている。ただし、半径より大きく手前の物（離れた前景）は数えない
        const float rangeCheck = smoothstep(0.0f, 1.0f, Radius / max(abs(center.z - sceneZ), 0.001f));
        occlusion += (sceneZ < samplePosition.z - Bias ? 1.0f : 0.0f) * rangeCheck;
    }

    const float distanceFade = 1.0f - smoothstep(MaxDistance * 0.6f, MaxDistance, center.z);
    OcclusionOutput[outputPixel] = saturate(1.0f - occlusion / 12.0f * Intensity * distanceFade);
}
