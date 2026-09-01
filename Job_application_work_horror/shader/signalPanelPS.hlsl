// ============================================================================
// シェーダーの役割: 2面の信号パネルを状態に応じた発光色で描きます。
// 定数バッファのスロットと入出力構造はCPU側の定義と必ず一致させてください。
// ============================================================================

#include "common.hlsl"

struct SIGNAL_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
    float4 shadowPos : TEXCOORD6;
};

float SignalHash(float2 value)
{
    return frac(sin(dot(value, float2(41.73f, 289.19f))) * 43758.5453f);
}

float4 main(SIGNAL_PS_IN input) : SV_Target
{
    const float2 uv = saturate(input.tex);
    const float2 edgeDistance = min(uv, 1.0f - uv);
    const float border = 1.0f - smoothstep(
        0.025f, 0.085f, min(edgeDistance.x, edgeDistance.y));

    // A procedural diagnostic grid gives each marker a readable electronic
    // surface without adding texture memory or another asset dependency.
    const float2 gridCoordinate = abs(frac(uv * float2(12.0f, 7.0f)) - 0.5f);
    const float grid = 1.0f - smoothstep(
        0.455f, 0.495f, max(gridCoordinate.x, gridCoordinate.y));
    const float traceA = 1.0f - smoothstep(
        0.018f, 0.045f, abs(uv.y - (0.28f + uv.x * 0.18f)));
    const float traceB = 1.0f - smoothstep(
        0.014f, 0.038f, abs(uv.y - (0.73f - uv.x * 0.22f)));
    const float circuit = saturate(traceA + traceB);
    const float cellNoise = SignalHash(floor(uv * float2(12.0f, 7.0f)));
    const float brokenCells = step(0.22f, cellNoise);

    float3 lighting = Light.Ambient.rgb + 0.12f;
    const float3 normal = normalize(input.worldNormal);
    [unroll]
    for (int lightIndex = 0; lightIndex < 8; ++lightIndex)
    {
        if (lightIndex >= EnvironmentLightCount)
        {
            break;
        }
        const float3 toLight =
            EnvironmentLights[lightIndex].PositionRange.xyz - input.worldPos;
        const float distanceToLight = length(toLight);
        const float range = max(
            EnvironmentLights[lightIndex].PositionRange.w, 0.001f);
        float attenuation = saturate(1.0f - distanceToLight / range);
        attenuation *= attenuation;
        const float lambert = 0.24f + 0.76f * saturate(dot(
            normal, toLight / max(distanceToLight, 0.001f)));
        lighting += EnvironmentLights[lightIndex].ColorIntensity.rgb *
            EnvironmentLights[lightIndex].ColorIntensity.a *
            attenuation * lambert;
    }

    const float pattern = saturate(
        border * 0.82f + grid * brokenCells * 0.20f + circuit * 0.48f);
    const float centerGlow = 1.0f - smoothstep(
        0.12f, 0.70f, length((uv - 0.5f) * float2(0.72f, 1.0f)));
    float3 color = Material.Diffuse.rgb * lighting;
    color += Material.Emission.rgb *
        (0.42f + centerGlow * 0.38f + pattern * 0.62f);

    // Fine horizontal phosphor bands remain subtle at normal viewing distance.
    const float phosphor = 0.965f +
        sin(input.pos.y * 3.1415926f) * 0.035f;
    color *= phosphor;

    if (DebugViewMode == 1)
    {
        return float4(normal * 0.5f + 0.5f, 1.0f);
    }
    // Keep HDR values above 1.0 so the existing bloom extraction pass can
    // turn a restored panel into a soft light source in the corridor.
    return float4(max(color, 0.0f), 1.0f);
}
