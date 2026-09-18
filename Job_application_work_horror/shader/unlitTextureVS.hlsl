// ============================================================================
// シェーダーの役割: 非ライティング描画用に頂点を画面へ変換します。
// 定数バッファのスロットと入出力構造はCPU側の定義と必ず一致させてください。
// ============================================================================

#include "common.hlsl"

PS_IN main(in VS_IN input)
{
    PS_IN output;
	
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);
    output.pos = mul(input.pos, wvp);
    
    // UV座標へテクスチャ移動行列を適用します。
    float4 uv;
    uv.xy = input.tex; // 行列乗算のため2次元UVをfloat4へ展開します。
    uv.z = 0.0f;
    uv.w = 1.0f;
    uv = mul(uv, matrixTex); // UV移動・拡縮行列を適用します。
    output.tex = uv.xy; // 変換後のUVをピクセルシェーダーへ渡します。
    output.col = input.col;
    
    output.depth = 0.0f; // 非ライティング描画では深度情報を使わないため0で初期化します。
    output.viewPos = float3(0.0f, 0.0f, 0.0f);
    output.viewNormal = float3(0.0f, 0.0f, 0.0f);
    output.worldPos = float3(0.0f, 0.0f, 0.0f);
    output.worldNormal = float3(0.0f, 0.0f, 0.0f); // 法線を使わない描画パスなので0で初期化します。
	
    return output;
}

