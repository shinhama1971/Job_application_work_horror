// 呼吸で動いた壁を懐中電灯のシャドウマップに描く。
#include "renderingConfig.hlsli"
#include "common.hlsl"
#include "breathingWallDeform.hlsli"

// ライトのビュー×射影の行列と、影のパラメーター（ShadowMap.cppのShadowBufferと同じ並び）
cbuffer ShadowBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_FLASHLIGHT_SHADOW_BUFFER_SLOT))
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
}

// 頂点をワールド座標にしてから、ライトから見た画面の座標へ変換している
float4 main(in VS_IN input) : SV_POSITION
{
    const float4 worldPosition = BreathShadowPosition(input);
    return mul(worldPosition, ShadowViewProjection);
}
