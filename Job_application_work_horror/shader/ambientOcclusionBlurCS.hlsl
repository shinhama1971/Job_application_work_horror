// ============================================================================
// シェーダーの役割: 画面空間の環境遮蔽（ambientOcclusionCS）の粒々を、深さの近い画素どうしだけで平均してなめらかにしている。
// 主な技術: 4x4の範囲の平均（標本の向きを回した4x4の模様をちょうど打ち消す）、深さの差で重みを下げる縁を保つぼかし
// 環境遮蔽は描く範囲の縦横半分の解像度で持っているので、深さは対応する画素（2倍の位置）から読んでいる。
// ============================================================================

cbuffer AmbientOcclusionBuffer : register(b0)
{
    float4x4 Projection;
    float4x4 InverseProjection;
    float2 ViewportSize;
    float2 ViewportOffset;
    float Radius;
    float Intensity;
    float Bias;
    float MaxDistance;
    float2 OcclusionSize;
    float2 OcclusionPadding;
};

Texture2D<float> SceneDepth : register(t0);
Texture2D<float> RawOcclusion : register(t1);
RWTexture2D<float> OcclusionOutput : register(u0);

// 半分の解像度の画素に対応する所の深さ（0〜1）を、カメラからの前向きの距離へ戻している
float LinearDepthAt(int2 occlusionPixel)
{
    const int2 pixel = min(occlusionPixel * 2, int2(ViewportSize) - 1);
    const float depth = SceneDepth.Load(int3(pixel + int2(ViewportOffset), 0));
    const float4 position = mul(float4(0.0f, 0.0f, depth, 1.0f), InverseProjection);
    return position.z / position.w;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const int2 pixel = int2(id.xy);
    if (any(pixel >= int2(OcclusionSize)))
    {
        return;
    }
    const float centerDepth = LinearDepthAt(pixel);
    float total = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int y = -2; y < 2; ++y)
    {
        [unroll]
        for (int x = -2; x < 2; ++x)
        {
            const int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), int2(OcclusionSize) - 1);
            // 深さが離れている画素（物の向こう側）は、ほとんど混ぜない
            const float depthDifference = abs(LinearDepthAt(samplePixel) - centerDepth);
            const float weight = 1.0f / (1.0f + depthDifference * 0.8f);
            total += RawOcclusion.Load(int3(samplePixel, 0)) * weight;
            weightSum += weight;
        }
    }
    OcclusionOutput[pixel] = total / max(weightSum, 0.0001f);
}
