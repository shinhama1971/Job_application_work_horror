#include "common.hlsl"

cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

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
};

LIT_PS_IN main(in VS_IN input)
{
    LIT_PS_IN output;
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);
    output.pos = mul(input.pos, wvp);

    matrix wv = mul(World, View);
    float4 viewPos = mul(input.pos, wv);
    output.depth = viewPos.z;
    output.viewPos = viewPos.xyz;

    float4 worldPosition = mul(input.pos, World);
    output.worldPos = worldPosition.xyz;
    output.worldNormal = normalize(mul(input.nrm.xyz, (float3x3)World));
    output.shadowPos = mul(worldPosition, ShadowViewProjection);

    float3x3 normalMatrix = (float3x3)wv;
    output.viewNormal = normalize(mul(input.nrm.xyz, normalMatrix));

    output.col = input.col;
    output.col.a *= Material.Diffuse.a;
    output.tex = input.tex;

    return output;
}

