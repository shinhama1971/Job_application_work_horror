// ============================================================================
// シェーダーの役割: 頂点をライト（懐中電灯）から見た座標へ変換し、シャドウマップへ深度を書かせている。
// ============================================================================

#include "common.hlsl"

// ライトのビュー×射影の行列と、影のパラメーター（ShadowMap.cppのShadowBufferと同じ並び）
cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

// 頂点をワールド座標にしてから、ライトから見た画面の座標へ変換している
float4 main(in VS_IN input) : SV_POSITION
{
    const float4 worldPosition = mul(input.pos, World);
    return mul(worldPosition, ShadowViewProjection);
}
