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
    uint LodLevel;       // この区画の LOD（1 = 近い、2 = 中くらい、3 = 遠い）
    uint ShowLodColors;  // 1 なら LOD を色で表示する（技術デモ）
    float CameraBend;    // カメラの上の向きへ曲げる強さ（授業資料 p.23）
    float DrawPadding;
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
    float4 debugColor : TEXCOORD6; // a が 1 なら、照明を使わずにこの色で描く（LOD の色の表示）
};

GRASS_PS_IN main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const uint index = InstanceOffset + instanceId;
    const GrassBlade blade = g_Blades[index];
    float3 tip = g_States[index].Tip.xyz;
    const bool atTip = vertexId == 2u * Segments;
    const float t = atTip ? 1.0f : float(vertexId / 2u) / float(Segments);
    const float side = (vertexId & 1u) != 0u ? 1.0f : -1.0f;
    const float3 toCamera = CameraPosition - blade.Root;
    const float horizontalDistance = length(toCamera.xz);
    const float distanceToCamera = length(toCamera);
    // 足元は視界を埋めないよう低くし、遠くは根元へ縮めて描画距離の境目をなじませる。
    const float heightScale = lerp(0.35f, 1.0f, smoothstep(2.0f, 14.0f, horizontalDistance)) *
        (1.0f - smoothstep(FadeStart, FadeEnd, distanceToCamera));
    // 授業資料 p.23：見下ろすほど、葉先をカメラの上の向き（水平にした向き）へ倒している。
    // 真上から見ると葉は細い線や点になってしまうが、倒すと葉の面がカメラへ向き、草むらの密度が保たれる。
    // カメラの上の向きは、ビュー行列の回転の逆（転置）で、ビュー空間の上(0,1,0)をワールドへ戻して求めている
    const float3 cameraForward = mul((float3x3)View, float3(0.0f, 0.0f, 1.0f));
    const float3 cameraUp = mul((float3x3)View, float3(0.0f, 1.0f, 0.0f));
    const float lookDown = smoothstep(0.25f, 0.85f, -cameraForward.y) * CameraBend;
    if (lookDown > 0.0f && dot(cameraUp.xz, cameraUp.xz) > 0.0001f)
    {
        const float2 bendDirection = normalize(cameraUp.xz);
        const float tipLength = length(tip);
        tip += float3(bendDirection.x, 0.0f, bendDirection.y) * blade.Height * 0.85f * lookDown;
        tip = normalize(tip);
        // 倒しても、葉先は地面より少し上に残している（Compute Shader の下限と同じ）
        tip.y = max(tip.y, 0.12f);
        tip = normalize(tip) * tipLength;
    }
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
    // カメラの上の向きへ倒した葉は、幅をその横の向きへ寄せている（幅が倒れる向きと同じだと、上から見て線のままになるため）
    if (lookDown > 0.0f && dot(cameraUp.xz, cameraUp.xz) > 0.0001f)
    {
        const float2 bendDirection = normalize(cameraUp.xz);
        float3 bendSide = float3(-bendDirection.y, 0.0f, bendDirection.x);
        if (dot(bendSide, widthDirection) < 0.0f) bendSide = -bendSide;
        widthDirection = normalize(lerp(widthDirection, bendSide, lookDown * 0.7f));
    }
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
    // LOD の色の表示：近い = 緑、中くらい = 黄、遠い = 赤
    const float3 lodColor = LodLevel <= 1u ? float3(0.15f, 0.85f, 0.25f) : (LodLevel == 2u ? float3(0.95f, 0.80f, 0.15f) : float3(0.95f, 0.20f, 0.12f));
    output.debugColor = float4(lodColor, ShowLodColors != 0u ? 1.0f : 0.0f);
    return output;
}
