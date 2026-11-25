// 廊下を横切る格子を立ち上げ、ふくらんだ先端から後ろの浅い水へつなげる。
#include "common.hlsl"
#include "corridorFloodWave.hlsli"
#include "corridorFloodWaveShape.hlsli"

FloodWavePixel main(uint id : SV_VertexID)
{
    // 上向きの面と同じ頂点順にして、床の描画と同じ背面カリング・深度判定を使う。
    const uint2 corners[SIGNAL_LOST_FLOOD_WAVE_VERTICES_PER_CELL] =
    {
        uint2(0, 0), uint2(0, 1), uint2(1, 0),
        uint2(1, 0), uint2(0, 1), uint2(1, 1)
    };
    const uint cell = id / SIGNAL_LOST_FLOOD_WAVE_VERTICES_PER_CELL;
    const uint2 cellPosition = uint2(cell % SIGNAL_LOST_FLOOD_WAVE_COLUMNS, cell / SIGNAL_LOST_FLOOD_WAVE_COLUMNS);
    const float2 uv = (cellPosition + corners[id % SIGNAL_LOST_FLOOD_WAVE_VERTICES_PER_CELL]) /
        float2(SIGNAL_LOST_FLOOD_WAVE_COLUMNS, SIGNAL_LOST_FLOOD_WAVE_ROWS);
    float crest;
    const float3 world = WavePosition(uv, crest);
    const float2 stepUV = rcp(float2(SIGNAL_LOST_FLOOD_WAVE_COLUMNS, SIGNAL_LOST_FLOOD_WAVE_ROWS));

    // 波面の傾きから法線を作って画素へ補間し、三角形ごとの陰影の段差を抑える。
    float unusedCrest;
    const float2 nearUV = saturate(uv - stepUV);
    const float2 farUV = saturate(uv + stepUV);
    const float3 xNear = WavePosition(float2(nearUV.x, uv.y), unusedCrest);
    const float3 xFar = WavePosition(float2(farUV.x, uv.y), unusedCrest);
    const float3 zNear = WavePosition(float2(uv.x, nearUV.y), unusedCrest);
    const float3 zFar = WavePosition(float2(uv.x, farUV.y), unusedCrest);
    const float3 normal = normalize(cross(zFar - zNear, xFar - xNear));

    FloodWavePixel result;
    const float4 view = mul(float4(world, 1.0f), View);
    result.Position = mul(view, Projection);
    result.WorldPosition = world;
    result.ViewPosition = view.xyz;
    result.Grid = uv;
    result.Crest = crest;
    result.ViewNormal = mul(float4(normal, 0.0f), View).xyz;
    return result;
}
