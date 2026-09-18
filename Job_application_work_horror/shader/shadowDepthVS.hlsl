// ============================================================================
// ファイルの役割: 頂点をライト視点へ変換してシャドウマップへ描きます。
// 主な技術: HLSL Vertex Shader、ライトView-Projection、深度座標
// 読み方: この実装ファイルでは宣言された機能の具体的な処理を定義します。
// ============================================================================

// ============================================================================
// シェーダーの役割: ライト視点へ頂点を変換し、シャドウマップへ深度を書きます。
// ============================================================================

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
