// ============================================================================
// シェーダーの役割: 画面を16x16画素のタイルに分け、タイルごとに影響する点光源の
//                   番号のリストを作っている（タイルベースライティングのライトカリング）。
// 主な技術: 1タイル = 1スレッドグループ、グループ共有メモリ、InterlockedAdd、
//           タイルの視錐台（4つの平面）と、光源の影響の球の交差判定
// ・光源をスレッドで分担して判定し、1スレッドが全部の光源を順に調べる処理を避けている。
// ・投影した後の2Dの判定は画面の端で歪むため、ワールド空間の視錐台と球で判定している。
// ・リストは順不同になるが、光は足し算なので並べ替えはしていない。
// ============================================================================

// タイルの大きさ、1タイルに入れる光源の上限、1タイルのスレッド数（TiledLighting.hと同じ値）
#define TILE_SIZE 16
#define MAX_LIGHTS_PER_TILE 64
#define THREADS_PER_TILE 64

struct POINT_LIGHT
{
    float4 PositionRange;   // xyz = 位置、w = 影響の半径
    // rgb = 色、a = 強さ
    float4 ColorIntensity;
};

// TiledLighting.cppのCullingParamsと同じ並び（b0）
cbuffer TileCullingBuffer : register(b0)
{
    float4x4 InverseViewProjection;
    float4 CameraPosition;
    uint LightCount;
    uint TilesX;
    uint TilesY;
    uint CullingPadding;
    float2 ViewportSize;
    float2 ViewportOffset;
};

StructuredBuffer<POINT_LIGHT> PointLights : register(t0);   // 読み取り専用なのでSRV
RWStructuredBuffer<uint> TileLightIndices : register(u0);   // タイルの数 x MAX_LIGHTS_PER_TILE
RWStructuredBuffer<uint> TileLightCounts : register(u1);    // タイルごとの光源の数

// グループ内で共有する、タイルの光源の数・光源の番号・視錐台の4つの平面
groupshared uint g_TileLightCount;
groupshared uint g_TileLightIds[MAX_LIGHTS_PER_TILE];
groupshared float4 g_TilePlanes[4];

// NDC座標の遠い平面の上の点を、ワールド座標へ戻している。
float3 UnprojectFar(float2 ndc)
{
    const float4 world = mul(float4(ndc, 1.0f, 1.0f), InverseViewProjection);
    return world.xyz / world.w;
}

[numthreads(THREADS_PER_TILE, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID)
{
    const uint tileIndex = groupId.y * TilesX + groupId.x;

    // 代表のスレッドがカウンタを0にし、タイルの視錐台（左右上下の4つの平面）を作っている。
    if (threadId.x == 0)
    {
        g_TileLightCount = 0;

        const float2 minPixel = float2(groupId.xy * TILE_SIZE);
        const float2 maxPixel = min(minPixel + TILE_SIZE, ViewportSize);
        // 画素の座標はyが下向き、NDCはyが上向き。
        const float2 ndcMin = float2(
            minPixel.x / ViewportSize.x * 2.0f - 1.0f,
            1.0f - maxPixel.y / ViewportSize.y * 2.0f);
        const float2 ndcMax = float2(
            maxPixel.x / ViewportSize.x * 2.0f - 1.0f,
            1.0f - minPixel.y / ViewportSize.y * 2.0f);

        const float3 eye = CameraPosition.xyz;
        float3 corners[4];
        corners[0] = UnprojectFar(float2(ndcMin.x, ndcMin.y));
        corners[1] = UnprojectFar(float2(ndcMax.x, ndcMin.y));
        corners[2] = UnprojectFar(float2(ndcMax.x, ndcMax.y));
        corners[3] = UnprojectFar(float2(ndcMin.x, ndcMax.y));
        const float3 center = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;

        [unroll]
        for (uint side = 0; side < 4; ++side)
        {
            // 視点と、遠い平面の隣り合う2隅を通る平面。法線はタイルの内側へ向けている。
            float3 normal = normalize(cross(
                corners[side] - eye,
                corners[(side + 1) % 4] - eye));
            if (dot(normal, center - eye) < 0.0f)
            {
                normal = -normal;
            }
            g_TilePlanes[side] = float4(normal, -dot(normal, eye));
        }
    }
    // 平面ができるまで、グループの全スレッドを待たせている
    GroupMemoryBarrierWithGroupSync();

    // 64スレッドで光源を分担して判定している（光源が64個を超える場合は、何回かに分けている）。
    for (uint lightIndex = threadId.x; lightIndex < LightCount;
        lightIndex += THREADS_PER_TILE)
    {
        const float4 positionRange = PointLights[lightIndex].PositionRange;
        bool overlaps = true;
        [unroll]
        for (uint plane = 0; plane < 4; ++plane)
        {
            // 光源の球が、4つの平面のどれかの外側に完全に出ていたら、このタイルには影響しない
            const float signedDistance =
                dot(g_TilePlanes[plane].xyz, positionRange.xyz) + g_TilePlanes[plane].w;
            if (signedDistance < -positionRange.w)
            {
                overlaps = false;
            }
        }

        if (overlaps)
        {
            // 影響する光源を、共有メモリのリストへ足している（上限を超えた分は捨てている）
            uint slot;
            InterlockedAdd(g_TileLightCount, 1, slot);
            if (slot < MAX_LIGHTS_PER_TILE)
            {
                g_TileLightIds[slot] = lightIndex;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // 書き出しも各スレッドへ分散している。上限を超えた光源は、安全のため捨てている。
    const uint tileLightCount = min(g_TileLightCount, MAX_LIGHTS_PER_TILE);
    if (threadId.x == 0)
    {
        TileLightCounts[tileIndex] = tileLightCount;
    }
    for (uint i = threadId.x; i < tileLightCount; i += THREADS_PER_TILE)
    {
        TileLightIndices[tileIndex * MAX_LIGHTS_PER_TILE + i] = g_TileLightIds[i];
    }
}
