// ============================================================================
// シェーダーの役割: DDGI の各プローブから光線を飛ばし、当たった面から返ってくる光（放射輝度）と、当たるまでの距離を求めている。
// 主な技術: 部屋の形（壁を上から見た長方形と、床・天井の高さ。roomOcclusion.hlsli の b11）との交差（スラブ法）、
//           点光源と懐中電灯の直接光（壁との交差で影を判定）、前のフレームの間接光で求める2回目以降の跳ね返り
//
// ・このゲームは DirectX 11 なので、GPU のレイトレーシング（DXR）は使えない。水たまりと鏡の反射のために作った
//   「部屋の形との交差」を、光線を当てる形として使い回している（ソフトウェアのレイトレーシング）。
// ・1スレッドグループ = 1プローブ、1スレッド = 1本の光線。結果は「光線の番号 x プローブの番号」の画像に書く。
// ・プローブが壁の中にあるときは、光線が壁の裏に当たったとして、距離を負にして書く（更新のときに使わない）。
// ============================================================================

// 部屋の形（roomOcclusion.hlsli）と、間の壁の判定（IsWallBetween）は ddgiCommon.hlsli が読み込んでいる
#include "ddgiCommon.hlsli"

// 点光源（common.hlsl の ENVIRONMENT_POINT_LIGHT と同じ並び。TiledLighting の一覧）
struct POINT_LIGHT
{
    float4 PositionRange;   // xyz = 位置、w = 影響の半径
    float4 ColorIntensity;  // rgb = 色、a = 強さ
};
StructuredBuffer<POINT_LIGHT> PointLights : register(t0);
// x = 光線の番号、y = プローブの番号。rgb = 返ってくる光、a = 当たるまでの距離（負なら壁の裏）
RWTexture2D<float4> RayResults : register(u0);

// 壁・床・天井の色（照らす前の色。打ちっぱなしのコンクリートと、床のタイル・天井板）
static const float3 DDGI_WALL_ALBEDO = float3(0.34f, 0.35f, 0.32f);
static const float3 DDGI_FLOOR_ALBEDO = float3(0.24f, 0.23f, 0.21f);
static const float3 DDGI_CEILING_ALBEDO = float3(0.28f, 0.28f, 0.27f);
// 壁の裏に当たった光線の距離（負の値。更新で使わない印）
static const float DDGI_BACKFACE_DISTANCE = -0.01f;
// 懐中電灯の明るさの割合（本描画の懐中電灯は面の向きの影響を 0.22〜0.45 に抑えているので、同じくらいの強さにそろえている）
static const float DDGI_FLASHLIGHT_SCALE = 0.45f;

// 光線（origin から direction）が当たる一番近い壁・床・天井までの距離と、当たった面の法線。
// 光線の出発点が壁の中なら backface を true にする
float TraceRoom(float3 origin, float3 direction, out float3 hitNormal, out bool backface)
{
    float nearest = MaxRayDistance;
    hitNormal = float3(0.0f, 1.0f, 0.0f);
    backface = false;
    if (direction.y > DDGI_EPSILON)
    {
        nearest = (OcclusionCeilingY - origin.y) / direction.y;
        hitNormal = float3(0.0f, -1.0f, 0.0f);
    }
    else if (direction.y < -DDGI_EPSILON)
    {
        nearest = (OcclusionFloorY - origin.y) / direction.y;
        hitNormal = float3(0.0f, 1.0f, 0.0f);
    }
    const float2 safeDirection = float2(
        abs(direction.x) > DDGI_EPSILON ? direction.x : DDGI_EPSILON,
        abs(direction.z) > DDGI_EPSILON ? direction.z : DDGI_EPSILON);
    const float2 inverseDirection = 1.0f / safeDirection;
    const uint boxCount = min(OcclusionBoxCount, MaxOcclusionBoxes);
    [loop]
    for (uint i = 0; i < boxCount; ++i)
    {
        const float4 box = OcclusionBoxes[i];
        const float2 slabNear = (box.xy - box.zw - origin.xz) * inverseDirection;
        const float2 slabFar = (box.xy + box.zw - origin.xz) * inverseDirection;
        const float2 entry = min(slabNear, slabFar);
        const float2 leave = max(slabNear, slabFar);
        const float enter = max(entry.x, entry.y);
        const float exit = min(leave.x, leave.y);
        if (enter <= 0.0f && exit > 0.0f)
        {
            // 出発点が壁の中
            backface = true;
            return 0.0f;
        }
        if (enter > 0.0f && enter <= exit && enter < nearest)
        {
            nearest = enter;
            hitNormal = entry.x > entry.y
                ? float3(-sign(safeDirection.x), 0.0f, 0.0f)
                : float3(0.0f, 0.0f, -sign(safeDirection.y));
        }
    }
    return nearest;
}

// 面（position・normal）が点光源と懐中電灯から直接受ける光
float3 GetDirectLight(float3 position, float3 normal)
{
    const float3 shadowOrigin = position + normal * DDGI_SURFACE_OFFSET;
    float3 light = 0.0f;
    [loop]
    for (uint index = 0; index < LightCount; ++index)
    {
        const POINT_LIGHT pointLight = PointLights[index];
        const float3 toLight = pointLight.PositionRange.xyz - position;
        const float range = max(pointLight.PositionRange.w, DDGI_EPSILON);
        const float lightDistance = length(toLight);
        if (lightDistance >= range)
        {
            continue;
        }
        const float lambert = saturate(dot(normal, toLight / max(lightDistance, DDGI_EPSILON)));
        // 壁に付けたランプの光源は、壁の中や面のすぐそばにあるので、光源の付いた壁は数えない判定を使っている（本描画と同じ）
        if (lambert <= 0.0f || GetRoomLightVisibility(shadowOrigin, pointLight.PositionRange.xyz) <= 0.0f)
        {
            continue;
        }
        // 本描画と同じ、届く範囲の端で0になる弱まり方
        float attenuation = 1.0f - lightDistance / range;
        attenuation *= attenuation;
        light += pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a * attenuation * lambert;
    }

    // 懐中電灯：照らした壁の光が、まわりへ跳ね返る
    if (FlashlightEnabled > 0.5f)
    {
        const float3 toFlashlight = FlashlightPosition - position;
        const float flashlightDistance = length(toFlashlight);
        const float3 toPixel = -toFlashlight / max(flashlightDistance, DDGI_EPSILON);
        const float cone = smoothstep(FlashlightCone.y, FlashlightCone.x, dot(toPixel, FlashlightDirection));
        const float normalizedDistance = saturate(flashlightDistance / max(FlashlightRange, DDGI_EPSILON));
        const float rangeFade = 1.0f - normalizedDistance * normalizedDistance;
        const float lambert = saturate(dot(normal, -toPixel));
        if (cone * rangeFade * lambert > 0.0f && !IsWallBetween(shadowOrigin, FlashlightPosition))
        {
            light += FlashlightColor * FlashlightIntensity * DDGI_FLASHLIGHT_SCALE * cone * rangeFade * rangeFade * lambert;
        }
    }
    return light;
}

[numthreads(DDGI_MAX_RAYS, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID)
{
    const uint probeIndex = GetUpdatingProbeIndex(groupId.x);
    const uint rayIndex = threadId.x;
    if (probeIndex >= GetProbeCount() || rayIndex >= RaysPerProbe)
    {
        return;
    }
    const float3 origin = GetProbePosition(GetProbeGrid(probeIndex));
    const float3 direction = GetProbeRayDirection(rayIndex);

    float3 hitNormal;
    bool backface;
    const float hitDistance = TraceRoom(origin, direction, hitNormal, backface);
    if (backface)
    {
        RayResults[uint2(rayIndex, probeIndex)] = float4(0.0f, 0.0f, 0.0f, DDGI_BACKFACE_DISTANCE);
        return;
    }
    if (hitDistance >= MaxRayDistance)
    {
        // どこにも当たらない（建物の外へ抜けた）光線は、闇
        RayResults[uint2(rayIndex, probeIndex)] = float4(0.0f, 0.0f, 0.0f, MaxRayDistance);
        return;
    }

    const float3 hitPosition = origin + direction * hitDistance;
    const float3 albedo = hitNormal.y > 0.5f ? DDGI_FLOOR_ALBEDO
        : (hitNormal.y < -0.5f ? DDGI_CEILING_ALBEDO : DDGI_WALL_ALBEDO);
    // 直接光に、前のフレームの間接光（プローブから読む）を足して、2回目以降の跳ね返りも少しずつ積み重ねている
    float3 incoming = GetDirectLight(hitPosition, hitNormal);
    // 格子を作り直した最初のフレームは、まだ書き込んでいないマップを読まない。
    [branch]
    if (ResetHistory < 0.5f)
    {
        incoming += GetDDGIIrradiance(hitPosition, hitNormal, 0.0f, false) * BounceStrength;
    }
    RayResults[uint2(rayIndex, probeIndex)] = float4(albedo * incoming, hitDistance);
}
