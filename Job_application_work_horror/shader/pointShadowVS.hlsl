// ============================================================================
// シェーダーの役割: 揺れる裸電球の影（PointShadowMap）を描くとき、頂点を電球から見たキューブマップの1面へ変換し、
// ワールドの位置をピクセルシェーダーへ渡している（ビューと射影の行列は、その面を見る行列に差し替えている）。
// ============================================================================

#include "common.hlsl"

struct POINT_SHADOW_PS_IN
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

POINT_SHADOW_PS_IN main(in VS_IN input)
{
    POINT_SHADOW_PS_IN output;
    const float4 worldPosition = mul(input.pos, World);
    output.pos = mul(mul(worldPosition, View), Projection);
    output.worldPos = worldPosition.xyz;
    return output;
}
