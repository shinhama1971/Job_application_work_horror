// 薄い葉に裏から透ける光と弱い濡れた光沢を付け、森の地面と同じ霧・色調へなじませている。
#include "common.hlsl"
#include "fastNoise.hlsli"
#include "flashlightShadow.hlsli"
#include "flashlightLighting.hlsli"
#include "waterCaustics.hlsli"
#include "horrorGrade.hlsli"
#include "horrorMist.hlsli"

struct GRASS_PS_IN
{
    float4 pos : SV_POSITION;
    float3 viewPos : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float4 shadowPos : TEXCOORD4;
    float3 color : COLOR0;
    float ao : TEXCOORD5;
};

float4 main(GRASS_PS_IN input) : SV_TARGET
{
    const float3 n = normalize(input.worldNormal);
    const float3 viewNormal = normalize(input.viewNormal);
    if (DebugViewMode == DEBUG_VIEW_FULLBRIGHT)
        return GetFullbrightColor(input.color, viewNormal, input.viewPos);
    const float distanceFromCamera = length(input.viewPos);
    const float3 viewDirection = -input.viewPos / max(distanceFromCamera, 0.001f);
    float3 lighting = GetHemisphereAmbient(n) * input.ao;
    uint listOffset;
    const uint lightCount = GetPixelLightCount(input.pos.xy, listOffset);
    [loop]
    for (uint index = 0; index < lightCount; ++index)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight = GetPixelLight(listOffset, index);
        const float3 offset = pointLight.PositionRange.xyz - input.worldPos;
        const float distanceToLight = length(offset);
        const float range = max(pointLight.PositionRange.w, 0.001f);
        if (distanceToLight >= range) continue;
        const float3 lightDirection = offset / max(distanceToLight, 0.001f);
        const float attenuation = saturate(1.0f - distanceToLight / range);
        // 薄い葉なので、裏からの光も通す。根元側の暗さは頂点の色と環境光に残している。
        lighting += pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a * attenuation * attenuation *
            (0.2f + 0.8f * abs(dot(n, lightDirection)));
    }
    float3 specular = 0.0f;
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > 0.001f)
    {
        const float3 pixelDirection = -viewDirection;
        const float beam = GetFlashlightBeamProfile(pixelDirection);
        [branch]
        if (beam > 0.001f)
        {
            const float normalizedDistance = saturate(distanceFromCamera / max(Light.Range, 0.001f));
            const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
            const float falloff = lerp(1.0f, rcp(1.0f + distanceFromCamera * distanceFromCamera * 0.000018f), 0.32f);
            const float3 radiance = Light.Diffuse.rgb * GetFlashlightTint(pixelDirection) * Light.Intensity * beam *
                rangeFade * rangeFade * falloff * GetFlashlightLensPattern(pixelDirection) * GetFlashlightShadow(input.shadowPos);
            // 根元は少しだけ暗くし（葉が重なって光が届きにくい）、葉先は地面と同じくらいに照らしている
            lighting += radiance * GetFlashlightDiffuseResponse(abs(dot(viewNormal, viewDirection))) *
                lerp(0.75f, 1.0f, input.ao);
            // 目の近くから照らすので、光と視線の中間の向きは視線と同じ。光沢は葉の色を隠さない強さにする。
            specular = radiance * pow(saturate(dot(viewNormal, viewDirection)), 32.0f) * 0.035f;
        }
    }
    float3 color = input.color * lighting + specular;
    const float distanceFog = GetHorrorDistanceFog(distanceFromCamera, LoopDepth);
    const float heightDensity = exp(-max(input.worldPos.y + 100.0f, 0.0f) * 0.055f);
    const float heightFog = heightDensity * smoothstep(35.0f, 150.0f, distanceFromCamera) *
        (0.78f + FastValueNoise(input.worldPos.xz * 0.018f + 21.0f) * 0.22f) * 0.22f * GetHorrorHeightFogScale(LoopDepth);
    color = lerp(color, GetHorrorFogColor(Light.Ambient.rgb, LoopDepth), saturate(distanceFog + heightFog));
    color = ApplyHorrorMist(color, input.worldPos, input.viewPos, lighting);
    color += GetLightShafts(input.worldPos, listOffset, lightCount, input.pos.xy);
    color = ApplyFilmicHorrorGrade(color, LoopDepth);
    if (DebugViewMode == 1) return float4(n * 0.5f + 0.5f, 1.0f);
    if (DebugViewMode == 2) return float4(GetFlashlightShadow(input.shadowPos).xxx, 1.0f);
    if (DebugViewMode == 3) return float4(saturate(lighting * 0.5f), 1.0f);
    if (DebugViewMode == 7) return GetLightTileHeatmap(input.pos.xy, lightCount);
    if (DebugViewMode >= 4) return float4(0.0f, 0.0f, 0.0f, 1.0f);
    return float4(color, 1.0f);
}
