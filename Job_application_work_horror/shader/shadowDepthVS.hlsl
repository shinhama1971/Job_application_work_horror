#include "common.hlsl"

cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

float4 main(in VS_IN input) : SV_POSITION
{
    const float4 worldPosition = mul(input.pos, World);
    return mul(worldPosition, ShadowViewProjection);
}
