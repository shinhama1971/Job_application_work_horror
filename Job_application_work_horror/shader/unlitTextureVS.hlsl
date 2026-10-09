// ============================================================================
// シェーダーの役割: 照明を使わない描画のために、頂点を画面へ変換している。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "common.hlsl"

PS_IN main(in VS_IN input)
{
    PS_IN output;
	
	// ワールド×ビュー×射影の行列を作り、頂点を画面の座標へ変換している
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);
    output.pos = mul(input.pos, wvp);
    
    // UV座標に、テクスチャを動かす行列を掛けている。
    float4 uv;
    uv.xy = input.tex; // 行列を掛けるため、2次元のUVをfloat4に広げている。
    uv.z = 0.0f;
    uv.w = 1.0f;
    uv = mul(uv, matrixTex); // UVの移動・拡大縮小の行列を掛けている。
    output.tex = uv.xy; // 変換した後のUVを、ピクセルシェーダーへ渡している。
    output.col = input.col;
    
    output.depth = 0.0f; // 照明を使わない描画では深度の情報を使わないため、0で初期化している。
    output.viewPos = float3(0.0f, 0.0f, 0.0f);
    output.viewNormal = float3(0.0f, 0.0f, 0.0f);
    output.worldPos = float3(0.0f, 0.0f, 0.0f);
    output.worldNormal = float3(0.0f, 0.0f, 0.0f); // 法線を使わない描画なので、0で初期化している。
	
    return output;
}

