#include "common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

float4 main(in PS_IN input) : SV_Target
{
    float4 color = input.col;

    if (Material.TextureEnable)
    {
        color *= g_Texture.Sample(g_SamplerState, input.tex);
    }
    else
    {
        color *= Material.Diffuse;
    }

    float3 lighting = Light.Ambient.rgb;
    const float distanceFromCamera = length(input.viewPos);

    // Ceiling point lights illuminate nearby floors and walls, not only the panels.
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        if (i >= EnvironmentLightCount)
        {
            break;
        }

        const float3 offsetToLight =
            EnvironmentLights[i].PositionRange.xyz - input.worldPos;
        const float distanceToLight = length(offsetToLight);
        const float lightRange = max(EnvironmentLights[i].PositionRange.w, 0.001f);
        const float3 directionToPointLight =
            offsetToLight / max(distanceToLight, 0.001f);
        float pointAttenuation = saturate(1.0f - distanceToLight / lightRange);
        pointAttenuation *= pointAttenuation;

        const float pointLambert = saturate(dot(
            normalize(input.worldNormal), directionToPointLight));
        const float softPointLambert = 0.20f + pointLambert * 0.80f;

        lighting += EnvironmentLights[i].ColorIntensity.rgb
            * EnvironmentLights[i].ColorIntensity.a
            * pointAttenuation
            * softPointLambert;
    }

    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > 0.001f)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float3 flashlightDirection = normalize(Light.Direction.xyz);
        const float coneDot = dot(pixelDirection, flashlightDirection);
        const float cone = smoothstep(Light.SpotParams.y, Light.SpotParams.x, coneDot);
        const float shapedCone = pow(saturate(cone), max(Light.SpotParams.z, 0.01f));

        const float normalizedDistance = saturate(
            distanceFromCamera / max(Light.Range, 0.001f)
        );
        const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
        const float attenuation = rangeFade * rangeFade;

        const float3 normal = normalize(input.viewNormal);
        const float3 directionToLight = -pixelDirection;
        const float lambert = saturate(dot(normal, directionToLight));
        const float softenedLambert = 0.25f + lambert * 0.75f;

        lighting += Light.Diffuse.rgb
            * Light.Intensity
            * shapedCone
            * attenuation
            * softenedLambert;
    }

    color.rgb *= lighting;
    color.rgb += Material.Emission.rgb;

    const float fogStart = 180.0f;
    const float fogEnd = 320.0f;
    const float fogFactor = saturate((input.depth - fogStart) / (fogEnd - fogStart));
    color.rgb = lerp(color.rgb, float3(0.0f, 0.0f, 0.0f), fogFactor);

    return color;
}

