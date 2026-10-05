// ============================================================================
// 共通処理: 部屋の角の暗がり（簡易アンビエントオクルージョン）。
// 画面の深度から推定するSSAOではなく、部屋の壁の形（上から見た箱）と床・天井の高さから直接計算します。
// 壁の数が少ない1面に向いた方法で、ノイズや画面端のちらつきが出ません。
// litTexturePS（壁・天井・小物）と wetFloorPS（床）で同じ暗がりを使います。
// ============================================================================
#ifndef ROOM_OCCLUSION_INCLUDED
#define ROOM_OCCLUSION_INCLUDED

static const uint MaxOcclusionBoxes = 32;

cbuffer RoomOcclusionBuffer : register(b11)
{
    // 壁を上から見た長方形。xy = 中心のx・z、zw = 幅と奥行きの半分。
    float4 OcclusionBoxes[MaxOcclusionBoxes];
    uint OcclusionBoxCount;     // 0なら暗がりを付けません（2面など）
    float OcclusionFloorY;      // 床の高さ
    float OcclusionCeilingY;    // 天井の下面の高さ
    float OcclusionStrength;    // 角でいちばん暗くなる割合（0〜1）
}

// 点から長方形までの距離です（中にあるときは負）。
float OcclusionBoxDistance(float2 position, float4 box)
{
    const float2 outside = abs(position - box.xy) - box.zw;
    return length(max(outside, 0.0f)) + min(max(outside.x, outside.y), 0.0f);
}

// 近くの壁までの距離です（壁の箱がないときは1000）。自分が乗っている壁（距離がほぼ0）は数えないので、
// 壁の面では「交わる別の壁」まで、床と天井では「いちばん近い壁」までの距離になります。
// 床の壁際の埃（wetFloorPS）にも使います。
float GetNearestWallDistance(float2 worldPositionXZ)
{
    float nearestWall = 1000.0f;
    const uint boxCount = min(OcclusionBoxCount, MaxOcclusionBoxes);
    [loop]
    for (uint i = 0; i < boxCount; ++i)
    {
        const float distanceToBox = OcclusionBoxDistance(worldPositionXZ, OcclusionBoxes[i]);
        if (distanceToBox > 0.05f)
        {
            nearestWall = min(nearestWall, distanceToBox);
        }
    }
    return nearestWall;
}

// 暗がりの計算本体です。GetRoomOcclusion から、壁の箱があるときだけ呼びます。
float ComputeRoomOcclusion(float3 worldPosition, float3 worldNormal)
{
    const float nearestWall = GetNearestWallDistance(worldPosition.xz);
    // 床と壁・天井と壁・壁と壁の角は、角から離れるにつれてなだらかに明るくなります。
    const float cornerOcclusion = exp(-nearestWall / 5.0f);

    // 壁の面だけ、床際と天井際も暗くします（床や天井の面自体には使いません）。
    const float verticalSurface = saturate(1.0f - abs(worldNormal.y) * 1.5f);
    const float floorOcclusion = exp(-max(worldPosition.y - OcclusionFloorY, 0.0f) / 5.0f);
    const float ceilingOcclusion = exp(-max(OcclusionCeilingY - worldPosition.y, 0.0f) / 4.0f) * 0.85f;
    const float edgeOcclusion = verticalSurface * max(floorOcclusion, ceilingOcclusion);

    // 2つの暗がりが重なる角（床と2枚の壁が交わる隅）は、より暗くします。
    const float occlusion = 1.0f - (1.0f - cornerOcclusion) * (1.0f - edgeOcclusion);
    return 1.0f - saturate(OcclusionStrength) * occlusion;
}

// 1なら暗がりなし、小さいほど暗い値を返します。照明（環境光と天井灯）に掛けて使います。
float GetRoomOcclusion(float3 worldPosition, float3 worldNormal)
{
    float result = 1.0f;
    [branch]
    if (OcclusionBoxCount > 0)
    {
        result = ComputeRoomOcclusion(worldPosition, worldNormal);
    }
    return result;
}

#endif
