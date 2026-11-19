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
    float4 debugColor : TEXCOORD6;
};

// LODの確認画面でも、葉の向きと根元に弱い陰影を残す。
#define GRASS_LOD_DEBUG_BASE_LIGHT 0.45f
#define GRASS_LOD_DEBUG_FACING_WEIGHT 0.35f
#define GRASS_LOD_DEBUG_OCCLUSION_WEIGHT 0.20f

// 薄い葉は裏からも照らされ、斜め方向にも光が少し残る。
#define GRASS_POINT_LIGHT_GRAZING 0.2f
#define GRASS_POINT_LIGHT_FACING_WEIGHT 0.8f

// 懐中電灯が照らす根元の明るさと、葉の光沢の広さ・強さ。
#define GRASS_FLASHLIGHT_ROOT_MIN_GAIN 0.75f
#define GRASS_FLASHLIGHT_SPECULAR_EXPONENT 32.0f
#define GRASS_FLASHLIGHT_SPECULAR_GAIN 0.035f

float4 main(GRASS_PS_IN input) : SV_TARGET
{
    // 草の LOD の色分け：照明を使わず、LOD の色に葉の向きの陰影だけを付けて描いている（暗い森でも色が分かるように）
    if (input.debugColor.a > 0.5f || DebugViewMode == DEBUG_VIEW_GRASS_LOD)
    {
        const float facing = abs(dot(normalize(input.viewNormal), normalize(-input.viewPos)));
        return float4(input.debugColor.rgb * (GRASS_LOD_DEBUG_BASE_LIGHT + GRASS_LOD_DEBUG_FACING_WEIGHT * facing + GRASS_LOD_DEBUG_OCCLUSION_WEIGHT * input.ao), 1.0f);
    }
    const float3 n = normalize(input.worldNormal);
    const float3 viewNormal = normalize(input.viewNormal);
    if (IsFullbrightDebugView())
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
            (GRASS_POINT_LIGHT_GRAZING + GRASS_POINT_LIGHT_FACING_WEIGHT * abs(dot(n, lightDirection)));
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
            const float falloff = lerp(1.0f, rcp(1.0f + distanceFromCamera * distanceFromCamera * FLASHLIGHT_PHYSICAL_FALLOFF_SCALE), FLASHLIGHT_PHYSICAL_FALLOFF_BLEND);
            const float3 radiance = Light.Diffuse.rgb * GetFlashlightTint(pixelDirection) * Light.Intensity * beam *
                rangeFade * rangeFade * falloff * GetFlashlightLensPattern(pixelDirection) * GetFlashlightShadow(input.shadowPos);
            // 根元は少しだけ暗くし（葉が重なって光が届きにくい）、葉先は地面と同じくらいに照らしている
            lighting += radiance * GetFlashlightDiffuseResponse(abs(dot(viewNormal, viewDirection))) *
                lerp(GRASS_FLASHLIGHT_ROOT_MIN_GAIN, 1.0f, input.ao);
            // 目の近くから照らすので、光と視線の中間の向きは視線と同じ。光沢は葉の色を隠さない強さにする。
            specular = radiance * pow(saturate(dot(viewNormal, viewDirection)), GRASS_FLASHLIGHT_SPECULAR_EXPONENT) * GRASS_FLASHLIGHT_SPECULAR_GAIN;
        }
    }
    // 懐中電灯の翼の光（明るい中心の外にも広がる、ぼんやりした弱い光。葉は両面なので、カメラ側を向けた法線で受けている）
    lighting += GetFlashlightSpillLight(input.viewPos, viewNormal);
    float3 color = input.color * lighting + specular;
    const float distanceFog = GetHorrorDistanceFog(distanceFromCamera, LoopDepth);
    const float heightDensity = exp(-max(input.worldPos.y - SCENE_BASE_FLOOR_Y, 0.0f) * HEIGHT_FOG_DECAY);
    const float heightFog = heightDensity * smoothstep(HEIGHT_FOG_NEAR_START, HEIGHT_FOG_NEAR_END, distanceFromCamera) *
        (HEIGHT_FOG_BASE_VARIATION + FastValueNoise(input.worldPos.xz * HEIGHT_FOG_NOISE_SCALE + HEIGHT_FOG_NOISE_OFFSET) * HEIGHT_FOG_NOISE_WEIGHT) *
        HEIGHT_FOG_STRENGTH * GetHorrorHeightFogScale(LoopDepth);
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
