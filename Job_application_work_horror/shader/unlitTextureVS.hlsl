#include "common.hlsl"

PS_IN main(in VS_IN input)
{
    PS_IN output;
	
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);
    output.pos = mul(input.pos, wvp);
    
    //uv座標を移動させる
    float4 uv;
    uv.xy = input.tex;//行列の掛け算のためfloat4型に移す
    uv.z = 0.0f;
    uv.w = 1.0f;
    uv = mul(uv, matrixTex);//UV座標と移動行列を掛け算
    output.tex = uv.xy;//掛け算の結果を送信用変数にセット
    output.col = input.col;
    
    output.depth = 0.0f; // カメラ空間のz座標を出力に格納
    output.viewPos = float3(0.0f, 0.0f, 0.0f);
    output.viewNormal = float3(0.0f, 0.0f, 0.0f);
    output.worldPos = float3(0.0f, 0.0f, 0.0f);
    output.worldNormal = float3(0.0f, 0.0f, 0.0f); // カメラから見たx,y,z座標を出力に格納
	
    return output;
}

