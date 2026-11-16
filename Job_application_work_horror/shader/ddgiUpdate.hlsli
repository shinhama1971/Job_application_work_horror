// ============================================================================
// 共通の処理: DDGI のプローブの八面体マップ（放射照度か距離）を、このフレームの光線の結果で更新している。
// ddgiIrradianceUpdateCS（DDGI_UPDATE_DISTANCE なし）と ddgiDistanceUpdateCS（あり）が読み込んでいる。
// 主な技術: 光線の向きと画素の向きの内積による重み付き平均、前の値と混ぜるヒステリシス、八面体マップの縁の画素の写し
//
// ・1スレッドグループ = 1プローブの区画、1スレッド = 区画の中の1画素。
// ・DirectX 11 では 16bit の画像を UAV から読めないので、前の値は別の画像（ピンポンのもう片方。t4）から読んでいる。
// ・八面体マップは縁で折り返してつながっているので、線形補間で縁をまたいで読めるよう、まわり1画素に反対側の画素を写している。
//   区画の中の画素は同じグループのスレッドが計算するので、グループ共有メモリに置いてから縁を書いている。
// ============================================================================
#ifndef DDGI_UPDATE_INCLUDED
#define DDGI_UPDATE_INCLUDED

#include "ddgiCommon.hlsli"

#if defined(DDGI_UPDATE_DISTANCE)
#define DDGI_TILE_TEXELS DDGI_DISTANCE_TEXELS
// 距離は、画素の向きに近い光線だけを使う（向きの内積をこの数で累乗した重み。Majercik 2019）
static const float DDGI_DISTANCE_SHARPNESS = 50.0f;
#else
#define DDGI_TILE_TEXELS DDGI_IRRADIANCE_TEXELS
#endif

// このフレームの光線の結果（ddgiTraceCS）と、前の画像（ピンポンのもう片方）、書き込む画像
Texture2D<float4> RayResults : register(t1);
Texture2D<float4> PreviousAtlas : register(t4);
RWTexture2D<float4> OutputAtlas : register(u0);

// 区画の中の画素の新しい値（縁を書くために、グループで共有している）
groupshared float4 g_TileValues[DDGI_TILE_TEXELS * DDGI_TILE_TEXELS];

// 縁の画素（区画の左上が0、中は1〜N）に写す、中の画素の位置（八面体マップの折り返し）
uint2 GetBorderSource(uint2 border)
{
    const uint n = DDGI_TILE_TEXELS;
    const uint last = n + 1;
    // 四隅は、反対の隅の画素
    if ((border.x == 0 || border.x == last) && (border.y == 0 || border.y == last))
    {
        return uint2(border.x == 0 ? n : 1, border.y == 0 ? n : 1);
    }
    // 上下の辺は左右を反転、左右の辺は上下を反転して写す
    if (border.y == 0)
    {
        return uint2(last - border.x, 1);
    }
    if (border.y == last)
    {
        return uint2(last - border.x, n);
    }
    if (border.x == 0)
    {
        return uint2(1, last - border.y);
    }
    return uint2(n, last - border.y);
}

// 縁の画素の番号（0〜4N+3）から、区画の中の位置へ（上の辺、下の辺、左の辺、右の辺の順）
uint2 GetBorderTexel(uint index)
{
    const uint n = DDGI_TILE_TEXELS;
    const uint row = n + 2;
    if (index < row)
    {
        return uint2(index, 0);
    }
    if (index < row * 2)
    {
        return uint2(index - row, n + 1);
    }
    if (index < row * 2 + n)
    {
        return uint2(0, 1 + index - row * 2);
    }
    return uint2(n + 1, 1 + index - row * 2 - n);
}

[numthreads(DDGI_TILE_TEXELS, DDGI_TILE_TEXELS, 1)]
void main(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID)
{
    // グループの位置（x = プローブの x と y の段、y = z）からプローブを決めている
    const uint3 grid = uint3(groupId.x % ProbeCounts.x, groupId.x / ProbeCounts.x, groupId.y);
    const uint probeIndex = GetProbeIndex(grid);
    const uint2 tileOrigin = GetProbeTileOrigin(grid, DDGI_TILE_TEXELS);
    const uint2 local = threadId.xy;
    const uint2 texel = tileOrigin + DDGI_BORDER + local;

    // この画素が表す向き
    const float2 coordinate = (float2(local) + 0.5f) / DDGI_TILE_TEXELS * 2.0f - 1.0f;
    const float3 texelDirection = OctahedralDecode(coordinate);

    float4 sum = 0.0f;
    float weightSum = 0.0f;
    [loop]
    for (uint ray = 0; ray < RaysPerProbe; ++ray)
    {
        const float4 result = RayResults.Load(int3(ray, probeIndex, 0));
        const float alignment = max(dot(texelDirection, GetProbeRayDirection(ray)), 0.0f);
#if defined(DDGI_UPDATE_DISTANCE)
        // 壁の中のプローブ（負の距離）は、距離を0として覚え、まわりの点から見えないものにしている
        const float hitDistance = min(max(result.a, 0.0f), MaxRayDistance);
        const float weight = pow(alignment, DDGI_DISTANCE_SHARPNESS);
        sum.xy += float2(hitDistance, hitDistance * hitDistance) * weight;
#else
        // 壁の裏に当たった光線は使わない
        const float weight = result.a < 0.0f ? 0.0f : alignment;
        sum.rgb += result.rgb * weight;
#endif
        weightSum += weight;
    }

    const float4 previous = PreviousAtlas.Load(int3(texel, 0));
    float4 value = previous;
    if (weightSum > DDGI_EPSILON)
    {
        const float4 current = sum / weightSum;
        value = ResetHistory > 0.5f ? current : lerp(current, previous, Hysteresis);
    }
    else if (ResetHistory > 0.5f)
    {
        value = 0.0f;
    }
    value.a = 1.0f;
    OutputAtlas[texel] = value;
    g_TileValues[local.y * DDGI_TILE_TEXELS + local.x] = value;

    // 区画の中の画素がそろってから、縁の画素を書いている
    GroupMemoryBarrierWithGroupSync();
    const uint threadIndex = local.y * DDGI_TILE_TEXELS + local.x;
    const uint borderCount = DDGI_TILE_TEXELS * 4 + 4;
    if (threadIndex < borderCount)
    {
        const uint2 border = GetBorderTexel(threadIndex);
        const uint2 source = GetBorderSource(border) - 1;
        OutputAtlas[tileOrigin + border] = g_TileValues[source.y * DDGI_TILE_TEXELS + source.x];
    }
}

#endif
