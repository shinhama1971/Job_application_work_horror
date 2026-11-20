// ============================================================================
// シェーダーの役割: 「呼吸する壁」の頂点シェーダー。細かく分けた壁の1面を、息を吸って吐くようにゆっくり膨らませ、
// 懐中電灯の光が当たっている所だけを平らに戻している（照らすと止まり、光を外すとまた動き出す）。
// 出力は litTextureVS と同じ形なので、ピクセルシェーダーは普段の壁（litTexturePS）をそのまま使っている。
// 主な技術: 頂点シェーダーでの変位（法線の向きへ押し出す）、変位後の法線を差分で求め直す計算、
//           懐中電灯の円錐（LightBuffer）による頂点ごとの止まり具合
// ============================================================================

#include "renderingConfig.hlsli"
#include "common.hlsl"
#include "breathingWallDeform.hlsli"

// 影の計算に使う、ライトのビュー×射影の行列（ShadowMap.cppと同じ並び）
cbuffer ShadowBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_FLASHLIGHT_SHADOW_BUFFER_SLOT))
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

// ピクセルシェーダーへ渡す値（litTextureVS と同じ並び）
struct LIT_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
    float4 shadowPos : TEXCOORD6;
    float3 restPosition : TEXCOORD7;
    float3 restNormal : TEXCOORD8;
};

// 変位後の法線を求めるときに隣を調べる距離（ワールドの単位）。
static const float BreathNormalStep = 1.5f;

// 頂点を画面の座標へ変換している（動かす面の頂点だけを膨らませている）
LIT_PS_IN main(in VS_IN input)
{
    LIT_PS_IN output;

    float3 worldPosition = mul(input.pos, World).xyz;
    float3 worldNormal = normalize(mul(input.nrm.xyz, (float3x3)World));
    output.restPosition = worldPosition;
    output.restNormal = worldNormal;

    if (BreathMovesFace(input.nrm.xyz))
    {
        const bool alongX = BreathFaceNormal.z != 0.0f;
        const float3 tangentWorld = BreathTangent();
        const float width = length(tangentWorld);
        const float height = length(World[1].xyz);
        const float3 tangent = tangentWorld / max(width, BreathMinDistance);
        const float3 local = input.pos.xyz;
        const float stepAlong = BreathNormalStep / max(width, BreathMinDistance);
        const float3 stepX = alongX ? float3(stepAlong, 0.0f, 0.0f) : float3(0.0f, 0.0f, stepAlong);
        const float3 stepY = float3(0.0f, BreathNormalStep / max(height, BreathMinDistance), 0.0f);
        worldPosition = BreathWorldPosition(local, worldNormal, tangent, width, height);
        // 両側の接線の外積を、元の面と同じ表側へ向ける。
        const float3 across = BreathWorldPosition(local + stepX, worldNormal, tangent, width, height) - worldPosition;
        const float3 up = BreathWorldPosition(local + stepY, worldNormal, tangent, width, height) - worldPosition;
        const float3 bentNormal = normalize(cross(across, up));
        worldNormal = dot(bentNormal, worldNormal) >= 0.0f ? bentNormal : -bentNormal;
    }

    const float4 world4 = float4(worldPosition, 1.0f);
    const float4 viewPos = mul(world4, View);
    output.pos = mul(viewPos, Projection);
    output.depth = viewPos.z;
    output.viewPos = viewPos.xyz;
    output.worldPos = worldPosition;
    output.worldNormal = worldNormal;
    output.shadowPos = mul(world4, ShadowViewProjection);
    output.viewNormal = normalize(mul(worldNormal, (float3x3)View));

    // 頂点の色に、マテリアルの透明度を掛けている
    output.col = input.col;
    output.col.a *= Material.Diffuse.a;
    output.tex = input.tex;

    return output;
}
