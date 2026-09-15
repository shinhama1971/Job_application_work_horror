// ============================================================================
// ファイルの役割: HUD頂点をクリップ空間へ変換し、UVと色を渡します。
// 主な技術: HLSL Vertex Shader、スクリーンスペース座標、頂点属性補間
// 読み方: この実装ファイルでは宣言された機能の具体的な処理を定義します。
// ============================================================================

// ============================================================================
// シェーダーの役割: HUD頂点をスクリーン座標へ変換します。
// ============================================================================

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}
