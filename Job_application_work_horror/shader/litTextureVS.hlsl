#include "common.hlsl"

PS_IN main(in VS_IN input)
{
    PS_IN output;
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

    float3x3 normalMatrix = (float3x3)wv;
    output.viewNormal = normalize(mul(input.nrm.xyz, normalMatrix));

    output.col = input.col;
    output.col.a *= Material.Diffuse.a;
    output.tex = input.tex;

    return output;
}

