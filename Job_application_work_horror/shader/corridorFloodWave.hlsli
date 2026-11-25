#ifndef SIGNAL_LOST_CORRIDOR_FLOOD_WAVE_HLSLI
#define SIGNAL_LOST_CORRIDOR_FLOOD_WAVE_HLSLI

#include "corridorFloodWaveConfig.hlsli"

cbuffer CorridorFloodWaveBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_FLOOD_WAVE_BUFFER_SLOT))
{
    float4 FloodShape;  // 床の高さ、経過秒、波の高さの割合、流入口から波頭までの距離
    float4 FloodRefraction; // HDR画像の1画素の大きさ、屈折の有効フラグ、余白
}

struct FloodWavePixel
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 ViewPosition : TEXCOORD1;
    float2 Grid : TEXCOORD2;
    float Crest : TEXCOORD3;
    float3 ViewNormal : TEXCOORD4;
};

#endif
