// ============================================================================
// シェーダーの役割: HUDの色付き四角形（ゲージ・枠・文字の画素など）の頂点を、そのまま画面へ出している。
// 頂点はCPU側（Hud.cpp）で画面の座標（-1〜1）に変換してあるため、行列は掛けていない。
// ============================================================================

// 頂点の並び（VERTEX_3Dと同じ。法線とUVは使っていない）
struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

// ピクセルシェーダーへ渡す値
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