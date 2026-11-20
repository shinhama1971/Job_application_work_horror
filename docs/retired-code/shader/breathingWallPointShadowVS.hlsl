// 呼吸で動いた壁を裸電球のシャドウマップに描く。
#include "common.hlsl"
#include "breathingWallDeform.hlsli"

struct POINT_SHADOW_PS_IN
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

POINT_SHADOW_PS_IN main(in VS_IN input)
{
    POINT_SHADOW_PS_IN output;
    const float4 worldPosition = BreathShadowPosition(input);
    output.pos = mul(mul(worldPosition, View), Projection);
    output.worldPos = worldPosition.xyz;
    return output;
}
