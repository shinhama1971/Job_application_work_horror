// ============================================================================
// シェーダーの役割: ブルームの画像を縦横半分に縮めている（何段かの大きさのブルームを作るため。PostProcess::RunBloom）。
// 主な技術: Compute Shader、テント形の重み（1・3・3・1）で4x4画素を平均する縮小
//
// ・縮めた段を横と縦にぼかすと、同じ9画素のぼかしでも、元の大きさでは2倍・4倍・8倍の広さににじむ。
// ・2x2画素の単純な平均で縮めると、光が画素の境目をまたいで動くときに明るさがちらつくため、
//   まわりの画素まで含めた4x4画素を、中央を重くしたテント形の重みで平均している。
// ============================================================================

#include "renderingConfig.hlsli"
#include "computeDispatchConfig.hlsli"

// 入力の画像（t0。1つ上の段のぼかした画像）と、書き込む先（u0。縦横半分の大きさ）
Texture2D<float4> InputTexture : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_BLOOM_COMPUTE_INPUT_SLOT));
RWTexture2D<float4> OutputTexture : register(SIGNAL_LOST_UAV_REGISTER(SIGNAL_LOST_BLOOM_COMPUTE_OUTPUT_SLOT));

// テント形の重み（1・3・3・1 を合計1にした物）。縦と横の重みを掛けて使う
static const float TENT_WEIGHTS[4] = { 0.125f, 0.375f, 0.375f, 0.125f };
// 4x4画素の左上の、縮めた画素の位置を2倍した所からのずれ
static const int TENT_OFFSET = -1;
static const int TENT_SIZE = 4;

// 1スレッドが、縮めた画像の1画素を担当している
[numthreads(SIGNAL_LOST_BLOOM_TILE_GROUP_SIZE, SIGNAL_LOST_BLOOM_TILE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint outputWidth;
    uint outputHeight;
    OutputTexture.GetDimensions(outputWidth, outputHeight);
    if (dispatchThreadId.x >= outputWidth || dispatchThreadId.y >= outputHeight)
    {
        return;
    }

    uint inputWidth;
    uint inputHeight;
    InputTexture.GetDimensions(inputWidth, inputHeight);
    const int2 maximumSource = int2(inputWidth, inputHeight) - 1;
    const int2 origin = int2(dispatchThreadId.xy * SIGNAL_LOST_BLOOM_WIDE_DOWNSAMPLE_FACTOR) + TENT_OFFSET;

    float3 color = 0.0f;
    [unroll]
    for (int y = 0; y < TENT_SIZE; ++y)
    {
        [unroll]
        for (int x = 0; x < TENT_SIZE; ++x)
        {
            const int2 source = clamp(origin + int2(x, y), 0, maximumSource);
            color += InputTexture.Load(int3(source, 0)).rgb * (TENT_WEIGHTS[x] * TENT_WEIGHTS[y]);
        }
    }
    OutputTexture[dispatchThreadId.xy] = float4(color, 1.0f);
}
