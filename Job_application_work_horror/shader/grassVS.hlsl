// 葉先の計算結果から、曲がった細い帯を作っている。頂点バッファを持たず、番号から3次ベジェ曲線をたどる。
#include "common.hlsl"

cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}
cbuffer GrassDrawBuffer : register(b9)
{
    uint InstanceOffset;
    uint Segments;
    float WidthScale;
    float FadeStart;
    float3 CameraPosition;
    float FadeEnd;
}
struct GrassBlade
{
    float3 Root; float Height;
    float2 Facing; float Width; float Lean;
    float3 Color; float Stiffness;
};
struct GrassState { float4 Tip; float4 Velocity; };
StructuredBuffer<GrassBlade> g_Blades : register(t0);
StructuredBuffer<GrassState> g_States : register(t1);

struct GRASS_PS_IN
{
    float4 pos : SV_POSITION;
    float3 viewPos : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float4 shadowPos : TEXCOORD4;
    float3 color : COLOR0;
    float ao : TEXCOORD5;
};

GRASS_PS_IN main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const uint index = InstanceOffset + instanceId;
    const GrassBlade blade = g_Blades[index];
    const float3 tip = g_States[index].Tip.xyz;
    const bool atTip = vertexId == 2u * Segments;
    const float t = atTip ? 1.0f : float(vertexId / 2u) / float(Segments);
    const float side = (vertexId & 1u) != 0u ? 1.0f : -1.0f;
    const float3 toCamera = CameraPosition - blade.Root;
    const float horizontalDistance = length(toCamera.xz);
    const float distanceToCamera = length(toCamera);
    // 足元は視界を埋めないよう低くし、遠くは根元へ縮めて描画距離の境目をなじませる。
    const float heightScale = lerp(0.35f, 1.0f, smoothstep(2.0f, 14.0f, horizontalDistance)) *
        (1.0f - smoothstep(FadeStart, FadeEnd, distanceToCamera));
    const float3 p0 = blade.Root;
    const float3 p1 = p0 + float3(0.0f, blade.Height * 0.4f, 0.0f) * heightScale;
    const float3 p2 = p0 + float3(tip.x * 0.5f, tip.y + (blade.Height - tip.y) * 0.35f, tip.z * 0.5f) * heightScale;
    const float3 p3 = p0 + tip * heightScale;
    // 制御点を順に補間する。最後の2点の差から、同じ曲線の接線も求めている。
    const float3 a = lerp(p0, p1, t);
    const float3 b = lerp(p1, p2, t);
    const float3 c = lerp(p2, p3, t);
    const float3 d = lerp(a, b, t);
    const float3 e = lerp(b, c, t);
    const float3 tangent = normalize(e - d + float3(0.0f, 0.00001f, 0.0f));
    float3 widthDirection = float3(blade.Facing.x, 0.0f, blade.Facing.y);
    float3 cameraRight = float3(toCamera.z, 0.0f, -toCamera.x) / max(horizontalDistance, 0.001f);
    // 向きを混ぜたときに打ち消し合わないよう、もとの幅と同じ側へそろえる。
    if (dot(cameraRight, widthDirection) < 0.0f) cameraRight = -cameraRight;
    widthDirection = normalize(lerp(widthDirection, cameraRight, smoothstep(110.0f, 260.0f, horizontalDistance) * 0.6f));
    const float width = blade.Width * WidthScale * (1.0f - t) * lerp(0.6f, 1.0f, smoothstep(0.0f, 0.2f, t));
    const float3 position = lerp(d, e, t) + widthDirection * side * width * 0.5f;
    float3 normal = normalize(normalize(cross(widthDirection, tangent)) + widthDirection * side * 0.35f);
    if (dot(normal, CameraPosition - position) < 0.0f) normal = -normal;
    GRASS_PS_IN output;
    output.worldPos = position;
    output.viewPos = mul(float4(position, 1.0f), View).xyz;
    output.pos = mul(float4(output.viewPos, 1.0f), Projection);
    output.worldNormal = normal;
    output.viewNormal = normalize(mul(normal, (float3x3)View));
    output.shadowPos = mul(float4(position, 1.0f), ShadowViewProjection);
    // 根元の暗さは、ピクセルシェーダーで光に掛けている（色にも掛けると二重に暗くなり、照らしても黒い影に見えていた）
    output.ao = lerp(0.5f, 1.0f, t);
    output.color = blade.Color;
    return output;
}
