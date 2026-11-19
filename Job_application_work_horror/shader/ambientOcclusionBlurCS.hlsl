// ============================================================================
// シェーダーの役割: 画面空間の環境遮蔽と接触影（ambientOcclusionCS）の粒々を、深さの近い画素どうしだけで平均してなめらかにしている。
// 主な技術: 4x4の範囲の平均（標本の向きを回した4x4の模様をちょうど打ち消す）、深さの差で重みを下げる縁を保つぼかし
// 環境遮蔽は描く範囲の縦横半分の解像度で持っているので、深さは対応する画素（2倍の位置）から読んでいる。
// ============================================================================

#include "renderingConfig.hlsli"
#include "computeDispatchConfig.hlsli"

cbuffer AmbientOcclusionBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_AO_PARAMS_SLOT))
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

Texture2D<float> SceneDepth : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_AO_DEPTH_SLOT));
// x = 環境遮蔽、y = 接触影（ambientOcclusionCS）。どちらも同じぼかしで粒を消している
Texture2D<float2> RawOcclusion : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_AO_SECOND_INPUT_SLOT));
RWTexture2D<float2> OcclusionOutput : register(SIGNAL_LOST_UAV_REGISTER(SIGNAL_LOST_AO_OUTPUT_SLOT));

// 半分の解像度の画素に対応する所の深さ（0〜1）を、カメラからの前向きの距離へ戻している
float LinearDepthAt(int2 occlusionPixel)
{
    const int2 pixel = min(occlusionPixel * SIGNAL_LOST_OCCLUSION_DOWNSAMPLE_FACTOR, int2(ViewportSize) - 1);
    const float depth = SceneDepth.Load(int3(pixel + int2(ViewportOffset), 0));
    const float4 position = mul(float4(0.0f, 0.0f, depth, 1.0f), InverseProjection);
    return position.z / position.w;
}

// 奥行きが異なる面を混ぜず、重みの合計で割る際の下限を保つ。
static const float OCCLUSION_BLUR_DEPTH_DISCONTINUITY_WEIGHT = 0.8f;
static const float OCCLUSION_BLUR_MIN_WEIGHT_SUM = 0.0001f;

// 中心の前後を読む4×4の窓。開始と終了を縦横で揃える。
static const int OCCLUSION_BLUR_WINDOW_BEGIN = -2;
static const int OCCLUSION_BLUR_WINDOW_END = 2;

[numthreads(SIGNAL_LOST_OCCLUSION_GROUP_SIZE, SIGNAL_LOST_OCCLUSION_GROUP_SIZE, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const int2 pixel = int2(id.xy);
    if (any(pixel >= int2(OcclusionSize)))
    {
        return;
    }
    const float centerDepth = LinearDepthAt(pixel);
    float2 total = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int y = OCCLUSION_BLUR_WINDOW_BEGIN; y < OCCLUSION_BLUR_WINDOW_END; ++y)
    {
        [unroll]
        for (int x = OCCLUSION_BLUR_WINDOW_BEGIN; x < OCCLUSION_BLUR_WINDOW_END; ++x)
        {
            const int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), int2(OcclusionSize) - 1);
            // 深さが離れている画素（物の向こう側）は、ほとんど混ぜない
            const float depthDifference = abs(LinearDepthAt(samplePixel) - centerDepth);
            const float weight = 1.0f / (1.0f + depthDifference * OCCLUSION_BLUR_DEPTH_DISCONTINUITY_WEIGHT);
            total += RawOcclusion.Load(int3(samplePixel, 0)) * weight;
            weightSum += weight;
        }
    }
    OcclusionOutput[pixel] = total / max(weightSum, OCCLUSION_BLUR_MIN_WEIGHT_SUM);
}
