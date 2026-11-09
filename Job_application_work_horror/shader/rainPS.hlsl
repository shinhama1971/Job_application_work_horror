// 雨粒の筋と地面のしぶきの明るさ。雨粒は自分では光らず、懐中電灯・街灯・窓の明かりを受けたときだけ光って見える
// （夜の雨は、ライトの光の中でだけ白く浮かぶ）。加算合成で、霧の奥ほど薄くしている。
#include "common.hlsl"
#include "fastNoise.hlsli"
#include "flashlightShadow.hlsli"
#include "flashlightLighting.hlsli"
#include "horrorGrade.hlsli"

struct RAIN_PS_IN
{
    float4 pos : SV_POSITION;
    float3 viewPos : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float4 shadowPos : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float alpha : TEXCOORD4;
    float kind : TEXCOORD5;
    float life : TEXCOORD6;
};

float4 main(RAIN_PS_IN input) : SV_TARGET
{
    if (DebugViewMode >= 1)
    {
        // デバッグ表示（法線・影・光など）では雨を描かない
        return 0.0f;
    }
    // 形：雨粒は細い筋（中心が濃く、先へ行くほど濃い）、しぶきは広がって薄れる輪と、跳ねた瞬間の小さな点
    float shape;
    if (input.kind < 0.5f)
    {
        const float across = 1.0f - abs(input.uv.x);
        shape = across * across * lerp(0.25f, 1.0f, input.uv.y) * smoothstep(0.0f, 0.2f, input.uv.y);
    }
    else
    {
        const float radius = length(input.uv);
        const float ringRadius = lerp(0.15f, 1.0f, sqrt(input.life));
        const float ringOffset = (radius - ringRadius) / 0.11f;
        const float ring = exp(-ringOffset * ringOffset) * pow(1.0f - input.life, 1.5f) * 0.8f;
        const float burst = exp(-radius * radius / 0.03f) * (1.0f - smoothstep(0.0f, 0.3f, input.life)) * 1.6f;
        shape = (ring + burst) * (1.0f - smoothstep(0.9f, 1.0f, radius));
    }
    const float alpha = input.alpha * shape;
    clip(alpha - 0.002f);

    const float distanceFromCamera = length(input.viewPos);
    const float3 pixelDirection = input.viewPos / max(distanceFromCamera, 0.001f);
    // 空の暗い光（雨粒は空の光をわずかに映す）
    float3 lighting = Light.Ambient.rgb * 0.35f;
    // 懐中電灯：光の円錐の中の雨粒だけが白く光る。木の陰になった雨粒は光らない（影の地図）
    if (Light.Enable && Light.FlashlightEnabled)
    {
        const float normalizedDistance = saturate(distanceFromCamera / max(Light.Range, 0.001f));
        const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
        const float beam = GetFlashlightBeamProfile(pixelDirection);
        const float spill = GetFlashlightSpillProfile(pixelDirection);
        const float shadow = beam > 0.001f ? GetFlashlightShadow(input.shadowPos) : 1.0f;
        lighting += Light.Diffuse.rgb * GetFlashlightTint(pixelDirection) * Light.Intensity * rangeFade * rangeFade *
            (beam * shadow * 0.45f + spill * 0.25f);
    }
    // 街灯・玄関の灯り・窓の明かり：近くの雨粒ほど明るく光り、灯りのまわりに雨が浮かぶ
    [loop]
    for (uint index = 0; index < PointLightCount; ++index)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight = g_PointLights[index];
        const float3 offset = pointLight.PositionRange.xyz - input.worldPos;
        const float range = max(pointLight.PositionRange.w, 0.001f);
        const float distanceToLight = length(offset);
        if (distanceToLight >= range) continue;
        const float attenuation = saturate(1.0f - distanceToLight / range);
        // 灯りを背にした雨粒（灯りの手前を落ちる雨）は、光が前へ散って特に明るく見える
        // （ビュー空間で、雨粒から灯りへの向きと、雨粒から目への向きを比べている）
        const float3 lightView = mul(float4(pointLight.PositionRange.xyz, 1.0f), View).xyz;
        const float3 toLightView = normalize(lightView - input.viewPos);
        const float forward = saturate(dot(toLightView, pixelDirection));
        lighting += pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a * attenuation * attenuation * attenuation *
            (0.12f + 0.55f * forward * forward);
    }
    float3 color = lighting * (input.kind < 0.5f ? 0.6f : 0.35f);
    // 遠くの雨は霧に隠れる（加算なので、霧の分だけ薄くしている）
    const float fog = saturate(GetHorrorDistanceFog(distanceFromCamera, LoopDepth));
    color *= 1.0f - fog * 0.9f;
    return float4(color, alpha);
}
