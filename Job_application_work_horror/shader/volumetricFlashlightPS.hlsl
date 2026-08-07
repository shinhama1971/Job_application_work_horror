struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

cbuffer PostProcessBuffer : register(b0)
{
    float time;
    float bloomIntensity;
    float noiseAmount;
    float vignetteStrength;
    float screenAspect;
    float volumeIntensity;
    float padding0;
    float padding1;
};

struct LIGHT
{
    bool Enable;
    bool FlashlightEnabled;
    float Intensity;
    float Range;
    float4 Direction;
    float4 Diffuse;
    float4 Ambient;
    float4 SpotParams;
};

cbuffer LightBuffer : register(b3)
{
    LIGHT Light;
};

cbuffer ShadowBuffer : register(b8)
{
    matrix ShadowViewProjection;
    float4 ShadowParameters;
};

Texture2D<float> FlashlightDepth : register(t5);
SamplerState LinearSampler : register(s0);

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float LinearizeDepth(float depth, float nearPlane, float farPlane)
{
    return nearPlane * farPlane /
        max(farPlane - depth * (farPlane - nearPlane), 0.001f);
}

float4 main(PS_IN input) : SV_Target
{
    if (!Light.Enable || !Light.FlashlightEnabled || volumeIntensity <= 0.0f)
    {
        return 0.0f;
    }

    // Reconstruct the camera ray and project it into the square spotlight map.
    const float2 screenNdc = float2(
        input.uv.x * 2.0f - 1.0f,
        1.0f - input.uv.y * 2.0f);
    const float mainTanHalfFov = tan(radians(30.0f));
    const float shadowTanHalfFov = tan(radians(33.0f));
    const float2 shadowNdc = float2(
        screenNdc.x * screenAspect * mainTanHalfFov / shadowTanHalfFov,
        screenNdc.y * mainTanHalfFov / shadowTanHalfFov);
    const float2 shadowUV = float2(
        shadowNdc.x * 0.5f + 0.5f,
        -shadowNdc.y * 0.5f + 0.5f);

    if (any(shadowUV <= 0.0f) || any(shadowUV >= 1.0f))
    {
        return 0.0f;
    }

    const float depth = FlashlightDepth.SampleLevel(
        LinearSampler,
        shadowUV,
        0.0f);
    const float occluderDistance = LinearizeDepth(
        depth,
        ShadowParameters.z,
        ShadowParameters.w);

    const float radial = saturate(1.0f - length(shadowNdc));
    const float softBeam = radial * radial * (3.0f - 2.0f * radial);
    // Square-root response keeps short beams visible near walls while long
    // corridors still collect more atmospheric light.
    const float visibleLength = sqrt(saturate(
        (occluderDistance - 2.0f) / 120.0f));
    const float floorFade = 1.0f - smoothstep(0.62f, 0.98f, input.uv.y);

    const float frame = floor(time * 24.0f);
    const float grain = Hash(floor(input.pos.xy * 0.45f) + frame * 7.13f);
    const float driftingDust = smoothstep(0.935f, 1.0f, grain) * 0.42f;
    const float slowVariation =
        sin(input.uv.y * 38.0f - time * 1.7f) * 0.5f + 0.5f;

    const float density =
        (0.068f + slowVariation * 0.022f + driftingDust) *
        softBeam * visibleLength * floorFade * volumeIntensity;
    const float3 beamColor = float3(1.0f, 0.78f, 0.50f) *
        saturate(Light.Intensity / 1.6f);

    return float4(beamColor, saturate(density));
}
