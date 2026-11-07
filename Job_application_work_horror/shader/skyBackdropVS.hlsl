// ============================================================================
// シェーダーの役割: 画面全体を覆う三角形1枚を、頂点の番号から作っている（外の森の空。SkyBackdrop.cpp）。
// 深度は1.0（描画の奥の端）にし、画素ごとの視線の向き（ビュー空間とワールド空間）をピクセルシェーダーへ渡している。
// ============================================================================

#include "common.hlsl"

struct SKY_PS_IN
{
    float4 pos : SV_POSITION;
    float3 viewDirection : TEXCOORD0;
    float3 worldDirection : TEXCOORD1;
};

SKY_PS_IN main(uint vertexId : SV_VertexID)
{
    // (-1,-1)・(-1,3)・(3,-1) の三角形で、画面（-1〜1）全体を覆っている
    const float2 clip = float2(vertexId == 2u ? 3.0f : -1.0f, vertexId == 1u ? 3.0f : -1.0f);
    SKY_PS_IN output;
    output.pos = float4(clip, 1.0f, 1.0f);
    // 射影行列の逆：画面の位置から、ビュー空間の視線の向き（奥行き1あたり）を求めている
    output.viewDirection = float3(clip.x / Projection._11, clip.y / Projection._22, 1.0f);
    // ビュー行列の回転の逆（転置）で、ワールド空間の向きへ戻している
    output.worldDirection = mul((float3x3)View, output.viewDirection);
    return output;
}
