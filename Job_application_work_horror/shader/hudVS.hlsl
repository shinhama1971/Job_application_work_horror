// ============================================================================
// シェーダーの役割: HUDの色付き四角形（ゲージ・枠・文字の画素など）の頂点をそのまま画面へ出します。
// 頂点はCPU側（Hud.cpp）で画面座標（-1〜1）に変換済みのため、行列は掛けません。
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