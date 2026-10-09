// ============================================================================
// シェーダーの役割: ブルームの画像を、横方向にガウシアンぼかしで、にじませている。
// ============================================================================

// 入力の画像（t0）と、書き込む先（u0）
Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

// 1グループの画素の数（128）、ぼかしの半径（左右4画素）、共有メモリに置く画素の数（両端の分も含む）
static const int GroupSize = 128;
static const int Radius = 4;
static const int SharedSize = GroupSize + Radius * 2 + 1;

// 色ごとに配列を分け、隣どうしのスレッドの読み書きが、別々の共有メモリのバンクへ散らばるようにしている。
groupshared float SharedRed[SharedSize];
groupshared float SharedGreen[SharedSize];
groupshared float SharedBlue[SharedSize];

// 共有メモリに色を書く・読む
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

// 1スレッドが1画素を担当し、グループの128画素と両端4画素ずつを共有メモリへ読み込んでから、ぼかしている
[numthreads(GroupSize, 1, 1)]
void main(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID)
{
    uint width;
    uint height;
    InputTexture.GetDimensions(width, height);

    const int groupStart = int(groupId.x) * GroupSize;
    const int localIndex = int(groupThreadId.x) + Radius;
    const int y = min(int(dispatchThreadId.y), int(height) - 1);
    const int centerX = clamp(groupStart + int(groupThreadId.x), 0, int(width) - 1);
    StoreShared(localIndex, InputTexture.Load(int3(centerX, y, 0)).rgb);

    // グループの先頭の4スレッドが、両端からはみ出す分の画素も読み込んでいる
    if (int(groupThreadId.x) < Radius)
    {
        const int leftX = clamp(groupStart + int(groupThreadId.x) - Radius, 0, int(width) - 1);
        const int rightX = clamp(groupStart + GroupSize + int(groupThreadId.x), 0, int(width) - 1);
        StoreShared(int(groupThreadId.x), InputTexture.Load(int3(leftX, y, 0)).rgb);
        StoreShared(GroupSize + Radius + int(groupThreadId.x), InputTexture.Load(int3(rightX, y, 0)).rgb);
    }

    // 全スレッドが読み込み終わるのを待っている
    GroupMemoryBarrierWithGroupSync();

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    // 左右4画素ずつ、ガウス分布の重み（合計1）で足し合わせている
    float3 result = LoadShared(localIndex) * 0.22702703f;
    result += (LoadShared(localIndex - 1) + LoadShared(localIndex + 1)) * 0.19459459f;
    result += (LoadShared(localIndex - 2) + LoadShared(localIndex + 2)) * 0.12162162f;
    result += (LoadShared(localIndex - 3) + LoadShared(localIndex + 3)) * 0.05405405f;
    result += (LoadShared(localIndex - 4) + LoadShared(localIndex + 4)) * 0.01621622f;
    OutputTexture[dispatchThreadId.xy] = float4(result, 1.0f);
}
