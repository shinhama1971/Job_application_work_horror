// 壁の本描画、深度プリパス、影で同じ頂点の変位を使う。
#include "breathingWallShape.hlsli"

// 呼吸の値（Wall::Draw が b9 に入れている。Wall.h の BREATHING_WALL_BUFFER と同じ並び）
cbuffer BreathingWallBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_OBJECT_DRAW_BUFFER_SLOT))
{
    // 時間・量・固定幅・全面の変形と、面の法線。
    float4 BreathParams;
    float4 BreathFaceNormal;
};

float3 BreathWorldPosition(float3 local, float3 normal, float3 tangent, float width, float height)
{
    const float3 position = mul(float4(local, 1.0f), World).xyz;
    const float along = BreathFaceNormal.z != 0.0f ? local.x : local.z;
    const float planeAlong = dot(position, tangent);
    const float offset = BreathOffset(BreathParams.x, BreathParams.y, BreathParams.z, BreathParams.w > 0.0f,
        (0.5f - abs(along)) * width, (0.5f - abs(local.y)) * height,
        planeAlong, position.y);
    return position + normal * offset;
}

bool BreathMovesFace(float3 localNormal)
{
    return BreathParams.y > 0.0f && dot(localNormal, BreathFaceNormal.xyz) > 0.5f;
}

float3 BreathTangent()
{
    return BreathFaceNormal.z != 0.0f ? World[0].xyz : World[2].xyz;
}
