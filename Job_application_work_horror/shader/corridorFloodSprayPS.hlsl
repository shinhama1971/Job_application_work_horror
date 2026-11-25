// ============================================================================
// ファイルの役割: 1面の濁流の波頭から飛ぶしぶき（ピクセルシェーダー）。
// 主な技術: 丸くぼかした粒、懐中電灯の円錐の中だけ光を受けて明るく浮かぶ赤い水滴（暗い所ではほとんど見えない）
//
// 半透明の合成で、深度は判定するが書き込まない（壁の奥のしぶきは隠れ、しぶきどうしは重なる）。
// ============================================================================
#include "common.hlsl"
#include "corridorFloodWave.hlsli"
#include "corridorFloodSpray.hlsli"
#include "flashlightLighting.hlsli"

// 水滴の色（濁流と同じ赤黒い色）と、光を受けたときの照り返しの色（白に近い薄い赤）
static const float3 DropletTint = float3(0.13f, 0.02f, 0.02f);
static const float3 DropletGlint = float3(0.9f, 0.62f, 0.58f);
// 霧のしぶきの色（細かい粒が光を散らすので、水滴より明るく白っぽい）
static const float3 MistTint = float3(0.26f, 0.10f, 0.095f);
// 懐中電灯の光を受けたときの明るさ（水滴の照り返し・霧の散乱）と、光が無くても残る明るさ
static const float DropletGlintGain = 0.4f;
static const float MistScatterGain = 0.3f;
static const float AmbientGain = 0.35f;
// 水滴の中心だけを光らせる範囲（四角形の中心からの距離）
static const float DropletCoreRadius = 0.5f;
// 0で割らないための下限
static const float SprayMinDivisor = 0.001f;

float4 main(in FloodSprayPixel input) : SV_Target
{
    // 四角形を丸くぼかした粒にしている（水滴は縁がはっきり、霧はふんわり）
    const float radius = length(input.Corner);
    const float edge = lerp(1.0f - smoothstep(0.25f, 1.0f, radius), pow(saturate(1.0f - radius), 2.0f), input.Mist);
    const float alpha = edge * input.Alpha;
    clip(alpha - SprayMinDivisor);

    // 懐中電灯の円錐の中にある粒だけ、光を受けて浮かぶ
    const float3 pixelDirection = input.ViewPosition / max(length(input.ViewPosition), SprayMinDivisor);
    const float beam = Light.FlashlightEnabled ? GetFlashlightBeamProfile(pixelDirection) : 0.0f;
    const float range = saturate(1.0f - length(input.ViewPosition) / max(Light.Range, SprayMinDivisor));
    const float3 flashlight = Light.Diffuse.rgb * (beam * range * Light.Intensity);
    const float3 ambient = Light.Ambient.rgb * AmbientGain;

    const float core = 1.0f - smoothstep(0.0f, DropletCoreRadius, radius);
    const float3 droplet = DropletTint * (ambient + flashlight) + DropletGlint * flashlight * core * DropletGlintGain;
    const float3 mist = MistTint * (ambient + flashlight * MistScatterGain);
    return float4(lerp(droplet, mist, input.Mist), alpha);
}
