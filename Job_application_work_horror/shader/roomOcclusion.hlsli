// ============================================================================
// 共通の処理: 部屋の角の暗がり（簡易的なアンビエントオクルージョン）。
// 画面の深度から推定するSSAOではなく、部屋の壁の形（上から見た箱）と、床・天井の高さから直接計算している。
// 壁の数が少ない1面に向いた方法で、ノイズや画面の端のちらつきが出ない。
// litTexturePS（壁・天井・小物）と wetFloorPS（床）で同じ暗がりを使っている。
// ============================================================================
#ifndef ROOM_OCCLUSION_INCLUDED
#define ROOM_OCCLUSION_INCLUDED
#include "renderingConfig.hlsli"

// 壁の数の上限（Renderer.hのROOM_OCCLUSION_BUFFERと同じ）
static const uint MaxOcclusionBoxes = SIGNAL_LOST_MAX_ROOM_OCCLUSION_BOXES;

cbuffer RoomOcclusionBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_ROOM_OCCLUSION_BUFFER_SLOT))
{
    // 壁を上から見た長方形。xy = 中心のx・z、zw = 幅と奥行きの半分。
    float4 OcclusionBoxes[MaxOcclusionBoxes];
    uint OcclusionBoxCount;     // 0なら暗がりを付けない（2面など）
    float OcclusionFloorY;      // 床の高さ
    float OcclusionCeilingY;    // 天井の下面の高さ
    float OcclusionStrength;    // 角で一番暗くなる割合（0〜1）
    // 音で輪郭が浮かぶ部屋の、広がる音の輪（xyz = 音が出た所、w = 今の半径）と、強さ（x = 0〜1。0なら使わない）。
    // 部屋の形とは別に毎フレーム書き換える（Renderer::SetEchoPulses）。echoPulse.hlsli が読む
    float4 EchoPulses[SIGNAL_LOST_MAX_ECHO_PULSES];
    float4 EchoPulseStrength[SIGNAL_LOST_MAX_ECHO_PULSES];
}

// 壁がない場合の距離と、現在乗っている壁を数えないための余白。
#define ROOM_NEAREST_WALL_FALLBACK_DISTANCE 1000.0f
#define ROOM_SELF_WALL_DISTANCE_MARGIN 0.05f

// 床際と天井際の暗がりは面から離れるほど弱める。壁同士の隅も別に調整する。
#define ROOM_CORNER_FALLOFF_DISTANCE 5.0f
#define ROOM_VERTICAL_OCCLUSION_GAIN 1.5f
#define ROOM_FLOOR_EDGE_FALLOFF_DISTANCE 5.0f
#define ROOM_CEILING_EDGE_FALLOFF_DISTANCE 4.0f
#define ROOM_CEILING_EDGE_GAIN 0.85f

// 近くの壁までの距離を返している（壁の箱がないときは1000）。自分が乗っている壁（距離がほぼ0）は数えないので、
// 壁の面では「交わる別の壁」まで、床と天井では「一番近い壁」までの距離になる。
// 床の壁際の埃（wetFloorPS）にも使っている。
float GetNearestWallDistance(float2 worldPositionXZ)
{
    // 壁ごとの比較は距離の二乗で行い、最も近い壁が決まってから平方根を取る。
    float nearestWallSquared = ROOM_NEAREST_WALL_FALLBACK_DISTANCE * ROOM_NEAREST_WALL_FALLBACK_DISTANCE;
    const uint boxCount = min(OcclusionBoxCount, MaxOcclusionBoxes);
    [loop]
    for (uint i = 0; i < boxCount; ++i)
    {
        const float4 box = OcclusionBoxes[i];
        const float2 outside = max(abs(worldPositionXZ - box.xy) - box.zw, 0.0f);
        const float distanceSquared = dot(outside, outside);
        nearestWallSquared = min(nearestWallSquared,
            distanceSquared > ROOM_SELF_WALL_DISTANCE_MARGIN * ROOM_SELF_WALL_DISTANCE_MARGIN ? distanceSquared : nearestWallSquared);
    }
    return sqrt(nearestWallSquared);
}

// 壁までの距離と床・天井の高さから暗がりを求める。
float ComputeRoomOcclusion(float3 worldPosition, float3 worldNormal, float nearestWall)
{
    // 床と壁・天井と壁・壁と壁の角は、角から離れるにつれてなだらかに明るくしている。
    const float cornerOcclusion = exp(-nearestWall / ROOM_CORNER_FALLOFF_DISTANCE);

    // 壁の面だけ、床際と天井際も暗くしている（床や天井の面自体には使わない）。
    const float verticalSurface = saturate(1.0f - abs(worldNormal.y) * ROOM_VERTICAL_OCCLUSION_GAIN);
    const float floorOcclusion = exp(-max(worldPosition.y - OcclusionFloorY, 0.0f) / ROOM_FLOOR_EDGE_FALLOFF_DISTANCE);
    const float ceilingOcclusion = exp(-max(OcclusionCeilingY - worldPosition.y, 0.0f) / ROOM_CEILING_EDGE_FALLOFF_DISTANCE) * ROOM_CEILING_EDGE_GAIN;
    const float edgeOcclusion = verticalSurface * max(floorOcclusion, ceilingOcclusion);

    // 2つの暗がりが重なる角（床と2枚の壁が交わる隅）は、より暗くしている。
    const float occlusion = 1.0f - (1.0f - cornerOcclusion) * (1.0f - edgeOcclusion);
    return 1.0f - saturate(OcclusionStrength) * occlusion;
}

// 点光源が壁を抜けて照らさないための判定に使う値。
// 光源がこの幅より深く壁の箱の中にあるときだけ、その壁を数えない（TiledLighting.cpp の LightMountMargin と同じ値）。
// 壁に付けたランプの光源は壁の面から少し外にあるので、付いている壁も数える（数えないと、ランプが壁の裏の部屋まで照らした）
static const float ROOM_LIGHT_MOUNT_MARGIN = SIGNAL_LOST_ROOM_LIGHT_MOUNT_MARGIN;
// 自分の乗っている壁を「間の壁」と見ないよう、線分の両端から外す長さ
static const float ROOM_LIGHT_SURFACE_OFFSET = 0.1f;
// 0で割らないための小さな値
static const float ROOM_LIGHT_EPSILON = 1e-4f;

// 線分（position から lightPosition）が、壁 i の上から見た長方形を通るか（スラブ法）
bool DoesSegmentCrossWall(float2 positionXZ, float2 inverseDirection, float segmentLength, uint i)
{
    const float4 box = OcclusionBoxes[i];
    const float2 slabNear = (box.xy - box.zw - positionXZ) * inverseDirection;
    const float2 slabFar = (box.xy + box.zw - positionXZ) * inverseDirection;
    const float enter = max(min(slabNear.x, slabFar.x), min(slabNear.y, slabFar.y));
    const float exit = min(max(slabNear.x, slabFar.x), max(slabNear.y, slabFar.y));
    return enter <= exit && exit > ROOM_LIGHT_SURFACE_OFFSET && enter < segmentLength - ROOM_LIGHT_SURFACE_OFFSET;
}

// 線分の長さと、上から見た向きの逆数（0で割らないよう向きを丸めている）
float GetRoomLightSegment(float3 position, float3 lightPosition, out float2 inverseDirection)
{
    const float3 offset = lightPosition - position;
    const float segmentLength = length(offset);
    const float3 direction = offset / max(segmentLength, ROOM_LIGHT_EPSILON);
    const float2 safeDirection = float2(
        abs(direction.x) > ROOM_LIGHT_EPSILON ? direction.x : ROOM_LIGHT_EPSILON,
        abs(direction.z) > ROOM_LIGHT_EPSILON ? direction.z : ROOM_LIGHT_EPSILON);
    inverseDirection = 1.0f / safeDirection;
    return segmentLength;
}

// 点光源（lightPosition）と点（position）の間に壁があれば0、なければ1を返している。
// 点光源には影の画像がないので、光の届く範囲が壁をまたぐと、壁の向こうの部屋まで照らしていた（作者が遊んで見つけた）。
// 壁は床から天井までの箱なので、上から見た長方形と線分の交差だけで分かる（スラブ法）。
// wallMask は、この光源の届く範囲に重なる壁の印（1ビット = 1枚。TiledLighting が光源ごとに CPU で求めて t19 で渡している）。
// 全部の壁を調べると、照明の多い1面で本描画が 3.5〜6ms 重くなったので、印の付いた数枚だけを調べている
float GetRoomLightVisibility(float3 position, float3 lightPosition, uint wallMask)
{
    float2 inverseDirection;
    const float segmentLength = GetRoomLightSegment(position, lightPosition, inverseDirection);
    uint remaining = wallMask;
    [loop]
    while (remaining != 0)
    {
        // 一番下の印の番号（配列の外を読まないよう、上限に収めている）
        const uint i = min(firstbitlow(remaining), MaxOcclusionBoxes - 1u);
        remaining &= remaining - 1u;
        if (DoesSegmentCrossWall(position.xz, inverseDirection, segmentLength, i))
        {
            return 0.0f;
        }
    }
    return 1.0f;
}

// 印を使わず全部の壁を調べる版（光源の付いている壁は、ここで除く）。DDGI の光線の当たった面で使っている
float GetRoomLightVisibility(float3 position, float3 lightPosition)
{
    float2 inverseDirection;
    const float segmentLength = GetRoomLightSegment(position, lightPosition, inverseDirection);
    const uint boxCount = min(OcclusionBoxCount, MaxOcclusionBoxes);
    [loop]
    for (uint i = 0; i < boxCount; ++i)
    {
        const float4 box = OcclusionBoxes[i];
        // 光源が箱の中にある壁は数えない
        if (all(abs(lightPosition.xz - box.xy) < box.zw - ROOM_LIGHT_MOUNT_MARGIN))
        {
            continue;
        }
        if (DoesSegmentCrossWall(position.xz, inverseDirection, segmentLength, i))
        {
            return 0.0f;
        }
    }
    return 1.0f;
}

// 1なら暗がりなし、小さいほど暗い値を返している。照明（環境光と天井灯）に掛けて使っている。
float GetRoomOcclusion(float3 worldPosition, float3 worldNormal)
{
    float result = 1.0f;
    [branch]
    if (OcclusionBoxCount > 0)
    {
        result = ComputeRoomOcclusion(worldPosition, worldNormal, GetNearestWallDistance(worldPosition.xz));
    }
    return result;
}

#endif
