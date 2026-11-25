// ============================================================================
// シェーダーの部品: 1面の濁流の波頭から飛ぶしぶき（頂点シェーダーとピクセルシェーダーの間で渡す値）。
// しぶきの粒は頂点バッファを使わず、頂点番号から位置と飛び方を決めている（corridorFloodSprayVS）。
// ============================================================================

#ifndef SIGNAL_LOST_CORRIDOR_FLOOD_SPRAY_HLSLI
#define SIGNAL_LOST_CORRIDOR_FLOOD_SPRAY_HLSLI

struct FloodSprayPixel
{
    float4 Position : SV_POSITION;
    // 四角形の中での位置（-1〜1）。丸い粒の形を作るのに使う
    float2 Corner : TEXCOORD0;
    float3 ViewPosition : TEXCOORD1;
    // 粒の濃さ（出始め・消え際・波の強さを掛けたもの）
    float Alpha : TEXCOORD2;
    // 1 = 大きく薄い霧のしぶき、0 = 小さな水滴
    float Mist : TEXCOORD3;
};

#endif
