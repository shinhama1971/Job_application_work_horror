// ============================================================================
// シェーダーの役割: 一人称で懐中電灯を握る右手の肌を描いている。
// 主な技術: 回り込む拡散反射（wrap lighting）と、光と影の境目を赤くする近似の表面下散乱、
//           頂点カラーの印（爪・血色）、指のすき間の環境遮蔽、浅い角度で光る薄い皮脂の鏡面反射、
//           汚れ（指先と関節のすき間にたまる、廃墟を進むうちに付いた黒ずみ）
// ・頂点カラー: r = 環境遮蔽、g = 爪、b = 血色（指先・関節・指の付け根）。build_flashlight_hand.py が作っている。
// ・肌の色は Material.Diffuse、汚れの量は ViewModelState.z（FirstPersonHands）。
// ============================================================================

#include "common.hlsl"

SamplerState g_SamplerState : register(s0);

#include "fastNoise.hlsli"
#include "flashlightShadow.hlsli"
#include "pointShadow.hlsli"
#include "flashlightLighting.hlsli"
#include "pbrLighting.hlsli"
#include "roomOcclusion.hlsli"
#include "horrorGrade.hlsli"
#include "viewModel.hlsli"

// 肌の色：血色の印で掛ける赤み（色・混ぜる量）、爪の色、むら（細かさ・高さでのずらし・最低・幅）
#define SKIN_REDNESS_TINT float3(1.08f, 0.80f, 0.76f)
#define SKIN_REDNESS_BLEND 0.6f
#define SKIN_NAIL_COLOR float3(0.70f, 0.60f, 0.56f)
#define SKIN_MOTTLE_SCALE 6.0f
#define SKIN_MOTTLE_HEIGHT_SCALE 3.1f
#define SKIN_MOTTLE_BASE 0.92f
#define SKIN_MOTTLE_RANGE 0.16f
// 汚れ：ノイズの細かさ（横・奥）、へこみと血色に掛ける量、出る範囲（出始め・出きる）、黒ずみの色
#define SKIN_GRIME_SCALE 4.5f
#define SKIN_GRIME_DEPTH_SCALE 2.3f
#define SKIN_GRIME_CAVITY_GAIN 1.6f
#define SKIN_GRIME_REDNESS_GAIN 0.35f
#define SKIN_GRIME_START 0.35f
#define SKIN_GRIME_FULL 0.75f
#define SKIN_GRIME_TINT float3(0.42f, 0.38f, 0.34f)
// 粗さ（肌・汚れた肌・爪）
#define SKIN_ROUGHNESS 0.52f
#define SKIN_GRIME_ROUGHNESS 0.62f
#define NAIL_ROUGHNESS 0.30f
// 点光源の回り込み、照り返しの回り込み、映り込みの強さ
#define SKIN_LIGHT_WRAP 0.45f
#define SKIN_BOUNCE_WRAP 0.5f
#define SKIN_REFLECTION_GAIN 0.6f
// 表面下散乱の近似：輝度の重み、境目の赤い色、赤くする明るさの範囲（始め・終わり）
#define SKIN_LUMINANCE_WEIGHTS float3(0.2126f, 0.7152f, 0.0722f)
#define SKIN_SCATTER_TINT float3(1.12f, 0.62f, 0.50f)
#define SKIN_SCATTER_START 0.02f
#define SKIN_SCATTER_END 0.25f
// 輪郭の縁の明るさ（鋭さ・強さ）
#define SKIN_RIM_EXPONENT 4.0f
#define SKIN_RIM_GAIN 0.35f


float4 main(in VIEW_MODEL_PS_IN input) : SV_Target
{
    const float3 normal = normalize(input.worldNormal);
    const float3 toView = normalize(ViewModelEye.xyz - input.worldPos);
    const float occlusion = input.col.r;
    const float nail = input.col.g;
    const float redness = input.col.b;

    // 肌の色：血色の印で赤みを足し、爪は白っぽく、肌より光沢を強くしている
    float3 baseColor = Material.Diffuse.rgb;
    baseColor = lerp(baseColor, baseColor * SKIN_REDNESS_TINT, redness * SKIN_REDNESS_BLEND);
    baseColor = lerp(baseColor, SKIN_NAIL_COLOR, nail);
    // 手元の空間の位置で作ったむら（手と一緒に動く）
    const float2 mottlePosition = input.localPos.xz * SKIN_MOTTLE_SCALE + input.localPos.y * SKIN_MOTTLE_HEIGHT_SCALE;
    baseColor *= SKIN_MOTTLE_BASE + SKIN_MOTTLE_RANGE * FastValueNoise(mottlePosition);

    // 汚れ：へこんだ所（環境遮蔽が暗い所）と指先ほど黒ずむ
    const float grimeNoise = FastFractalNoise3(input.localPos.xy * SKIN_GRIME_SCALE + input.localPos.z * SKIN_GRIME_DEPTH_SCALE);
    const float grime = ViewModelState.z * saturate((1.0f - occlusion) * SKIN_GRIME_CAVITY_GAIN + redness * SKIN_GRIME_REDNESS_GAIN) *
        smoothstep(SKIN_GRIME_START, SKIN_GRIME_FULL, grimeNoise);
    baseColor = lerp(baseColor, baseColor * SKIN_GRIME_TINT, grime);

    const float roughness = lerp(lerp(SKIN_ROUGHNESS, SKIN_GRIME_ROUGHNESS, grime), NAIL_ROUGHNESS, nail);
    const PbrSurface surface = MakePbrSurface(baseColor, 0.0f, roughness, 0.0f);

    float3 diffuse = GetViewModelAmbient(input.worldPos, normal, occlusion);
    float3 specular = 0.0f;
    AccumulateViewModelPointLights(input.worldPos, normal, toView, SKIN_LIGHT_WRAP, surface, diffuse, specular);
    diffuse += GetViewModelBounce(normal, SKIN_BOUNCE_WRAP) * occlusion;
    specular += ViewModelBounce.rgb * EvaluateSpecularGGX(surface, normal, toView, ViewModelBounceDirection.xyz);
    specular += GetViewModelReflection(surface, normal, toView, occlusion) * SKIN_REFLECTION_GAIN;

    // 表面下散乱の近似：光が弱く当たる所（光と影の境目）ほど、肌の中を通った赤い光の色にしている
    const float brightness = dot(diffuse, SKIN_LUMINANCE_WEIGHTS);
    const float3 scatterTint = lerp(SKIN_SCATTER_TINT, 1.0f.xxx, smoothstep(SKIN_SCATTER_START, SKIN_SCATTER_END, brightness));
    float3 color = surface.diffuseColor * diffuse * scatterTint + specular * occlusion;

    // 輪郭の縁がわずかに明るくなる（産毛と皮脂に光が散る）
    const float rim = pow(1.0f - saturate(dot(normal, toView)), SKIN_RIM_EXPONENT);
    color += rim * diffuse * baseColor * SKIN_RIM_GAIN;

    if (IsFullbrightDebugView())
    {
        return float4(baseColor * (FULLBRIGHT_BASE + FULLBRIGHT_FACING * saturate(dot(normal, toView))) * FULLBRIGHT_GAIN, 1.0f);
    }
    return float4(ApplyFilmicHorrorGrade(color, LoopDepth), 1.0f);
}
