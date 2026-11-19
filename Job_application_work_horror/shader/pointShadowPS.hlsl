// ============================================================================
// シェーダーの役割: 揺れる裸電球の影（PointShadowMap）に、電球からの距離（光の届く範囲で割った0〜1）を書いている。
// 影を受ける面は、自分と電球の距離をこの値と比べて、間に物があるかを判定している（pointShadow.hlsli）。
// ============================================================================

#include "common.hlsl"
#include "flashlightShadow.hlsli"

// 光の届く範囲で0を割らないための下限
#define POINT_SHADOW_DEPTH_MIN_RANGE 0.001f

struct POINT_SHADOW_PS_IN
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

float main(in POINT_SHADOW_PS_IN input) : SV_Target
{
    return saturate(length(input.worldPos - PointShadowLight.xyz) / max(PointShadowLight.w, POINT_SHADOW_DEPTH_MIN_RANGE));
}
