// ============================================================================
// シェーダーの役割: 水平ぼかし済み画像を垂直方向へぼかし、光のにじみを完成させます。
// ============================================================================

Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

static const int GroupSize = 128;
static const int Radius = 4;
static const int SharedSize = GroupSize + Radius * 2 + 1;

groupshared float SharedRed[SharedSize];
groupshared float SharedGreen[SharedSize];
groupshared float SharedBlue[SharedSize];

void StoreShared(int index, float3 color)
{
    SharedRed[index] = color.r;
    SharedGreen[index] = color.g;
    SharedBlue[index] = color.b;
}

float3 LoadShared(int index)
{
    return float3(SharedRed[index], SharedGreen[index], SharedBlue[index]);
}

[numthreads(1, GroupSize, 1)]
void main(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID)
{
    uint width;
    uint height;
    InputTexture.GetDimensions(width, height);

    const int groupStart = int(groupId.y) * GroupSize;
    const int localIndex = int(groupThreadId.y) + Radius;
    const int x = min(int(dispatchThreadId.x), int(width) - 1);
    const int centerY = clamp(groupStart + int(groupThreadId.y), 0, int(height) - 1);
    StoreShared(localIndex, InputTexture.Load(int3(x, centerY, 0)).rgb);

    if (int(groupThreadId.y) < Radius)
    {
        const int topY = clamp(groupStart + int(groupThreadId.y) - Radius, 0, int(height) - 1);
        const int bottomY = clamp(groupStart + GroupSize + int(groupThreadId.y), 0, int(height) - 1);
        StoreShared(int(groupThreadId.y), InputTexture.Load(int3(x, topY, 0)).rgb);
        StoreShared(GroupSize + Radius + int(groupThreadId.y), InputTexture.Load(int3(x, bottomY, 0)).rgb);
    }

    GroupMemoryBarrierWithGroupSync();

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    float3 result = LoadShared(localIndex) * 0.22702703f;
    result += (LoadShared(localIndex - 1) + LoadShared(localIndex + 1)) * 0.19459459f;
    result += (LoadShared(localIndex - 2) + LoadShared(localIndex + 2)) * 0.12162162f;
    result += (LoadShared(localIndex - 3) + LoadShared(localIndex + 3)) * 0.05405405f;
    result += (LoadShared(localIndex - 4) + LoadShared(localIndex + 4)) * 0.01621622f;
    OutputTexture[dispatchThreadId.xy] = float4(result, 1.0f);
}
