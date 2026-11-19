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

// 雨粒は落ちる向きの先を強くし、筋の入り口は滑らかに消す。
static const float RAIN_STREAK_HEAD_MIN_GAIN = 0.25f;
static const float RAIN_STREAK_START_FADE_END = 0.2f;

// しぶきは中心の小さな点と広がる輪を重ね、寿命に応じて消す。
static const float RAIN_SPLASH_INITIAL_RADIUS = 0.15f;
static const float RAIN_SPLASH_RING_WIDTH = 0.11f;
static const float RAIN_SPLASH_RING_FADE_EXPONENT = 1.5f;
static const float RAIN_SPLASH_RING_GAIN = 0.8f;
static const float RAIN_SPLASH_BURST_WIDTH = 0.03f;
static const float RAIN_SPLASH_BURST_FADE_END = 0.3f;
static const float RAIN_SPLASH_BURST_GAIN = 1.6f;
static const float RAIN_SPLASH_OUTER_FADE_START = 0.9f;

// 透明に近い雨を描画せず、空の散乱光を抑える。
static const float RAIN_MIN_ALPHA = 0.002f;
static const float RAIN_AMBIENT_LIGHT_GAIN = 0.35f;

// 雨粒の懐中電灯への反応は中心と外周で異なる。
static const float RAIN_FLASHLIGHT_BEAM_GAIN = 0.45f;
static const float RAIN_FLASHLIGHT_SPILL_GAIN = 0.25f;

// 背後の照明で雨粒が浮かぶよう、前方散乱の量を調整する。
static const float RAIN_POINT_LIGHT_BASE_SCATTER = 0.12f;
static const float RAIN_POINT_LIGHT_FORWARD_SCATTER = 0.55f;

// 雨粒としぶきで明るさを分け、遠方の雨を霧の中へなじませる。
static const float RAIN_STREAK_LIGHT_GAIN = 0.6f;
static const float RAIN_SPLASH_LIGHT_GAIN = 0.35f;
static const float RAIN_DISTANCE_FOG_GAIN = 0.9f;

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
        shape = across * across * lerp(RAIN_STREAK_HEAD_MIN_GAIN, 1.0f, input.uv.y) * smoothstep(0.0f, RAIN_STREAK_START_FADE_END, input.uv.y);
    }
    else
    {
        const float radius = length(input.uv);
        const float ringRadius = lerp(RAIN_SPLASH_INITIAL_RADIUS, 1.0f, sqrt(input.life));
        const float ringOffset = (radius - ringRadius) / RAIN_SPLASH_RING_WIDTH;
        const float ring = exp(-ringOffset * ringOffset) * pow(1.0f - input.life, RAIN_SPLASH_RING_FADE_EXPONENT) * RAIN_SPLASH_RING_GAIN;
        const float burst = exp(-radius * radius / RAIN_SPLASH_BURST_WIDTH) * (1.0f - smoothstep(0.0f, RAIN_SPLASH_BURST_FADE_END, input.life)) * RAIN_SPLASH_BURST_GAIN;
        shape = (ring + burst) * (1.0f - smoothstep(RAIN_SPLASH_OUTER_FADE_START, 1.0f, radius));
    }
    const float alpha = input.alpha * shape;
    clip(alpha - RAIN_MIN_ALPHA);

    const float distanceFromCamera = length(input.viewPos);
    const float3 pixelDirection = input.viewPos / max(distanceFromCamera, 0.001f);
    // 空の暗い光（雨粒は空の光をわずかに映す）
    float3 lighting = Light.Ambient.rgb * RAIN_AMBIENT_LIGHT_GAIN;
    // 懐中電灯：光の円錐の中の雨粒だけが白く光る。木の陰になった雨粒は光らない（影の地図）
    if (Light.Enable && Light.FlashlightEnabled)
    {
        const float normalizedDistance = saturate(distanceFromCamera / max(Light.Range, 0.001f));
        const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
        const float beam = GetFlashlightBeamProfile(pixelDirection);
        const float spill = GetFlashlightSpillProfile(pixelDirection);
        const float shadow = beam > 0.001f ? GetFlashlightShadow(input.shadowPos) : 1.0f;
        lighting += Light.Diffuse.rgb * GetFlashlightTint(pixelDirection) * Light.Intensity * rangeFade * rangeFade *
            (beam * shadow * RAIN_FLASHLIGHT_BEAM_GAIN + spill * RAIN_FLASHLIGHT_SPILL_GAIN);
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
            (RAIN_POINT_LIGHT_BASE_SCATTER + RAIN_POINT_LIGHT_FORWARD_SCATTER * forward * forward);
    }
    float3 color = lighting * (input.kind < 0.5f ? RAIN_STREAK_LIGHT_GAIN : RAIN_SPLASH_LIGHT_GAIN);
    // 遠くの雨は霧に隠れる（加算なので、霧の分だけ薄くしている）
    const float fog = saturate(GetHorrorDistanceFog(distanceFromCamera, LoopDepth));
    color *= 1.0f - fog * RAIN_DISTANCE_FOG_GAIN;
    return float4(color, alpha);
}
