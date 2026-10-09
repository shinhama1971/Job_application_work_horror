// ============================================================================
// シェーダーの役割: 照明の影響を受けない、テクスチャの色をそのまま出力している（2Dの描画や画面効果で使っている）。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "common.hlsl"

// テクスチャ（t0）と、サンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

// ピクセルシェーダーの入口の関数。
float4 main(in PS_IN input) : SV_Target
{
    float4 color;
	
    // Sampleで、指定したUVの位置のテクスチャの色を読み取っている。
    color = g_Texture.Sample(g_SamplerState, input.tex);
    // 頂点の色を掛けている
    color *= input.col;


    return color;
}
