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

float4 main(in VIEW_MODEL_PS_IN input) : SV_Target
{
    const float3 normal = normalize(input.worldNormal);
    const float3 toView = normalize(ViewModelEye.xyz - input.worldPos);
    const float occlusion = input.col.r;
    const float nail = input.col.g;
    const float redness = input.col.b;

    // 肌の色：血色の印で赤みを足し、爪は白っぽく、肌より光沢を強くしている
    float3 baseColor = Material.Diffuse.rgb;
    baseColor = lerp(baseColor, baseColor * float3(1.08f, 0.80f, 0.76f), redness * 0.6f);
    baseColor = lerp(baseColor, float3(0.70f, 0.60f, 0.56f), nail);
    // 手元の空間の位置で作ったむら（手と一緒に動く）
    const float2 mottlePosition = input.localPos.xz * 6.0f + input.localPos.y * 3.1f;
    baseColor *= 0.92f + 0.16f * FastValueNoise(mottlePosition);

    // 汚れ：へこんだ所（環境遮蔽が暗い所）と指先ほど黒ずむ
    const float grimeNoise = FastFractalNoise3(input.localPos.xy * 4.5f + input.localPos.z * 2.3f);
    const float grime = ViewModelState.z * saturate((1.0f - occlusion) * 1.6f + redness * 0.35f) *
        smoothstep(0.35f, 0.75f, grimeNoise);
    baseColor = lerp(baseColor, baseColor * float3(0.42f, 0.38f, 0.34f), grime);

    const float roughness = lerp(lerp(0.52f, 0.62f, grime), 0.30f, nail);
    const PbrSurface surface = MakePbrSurface(baseColor, 0.0f, roughness, 0.0f);

    float3 diffuse = GetViewModelAmbient(input.worldPos, normal, occlusion);
    float3 specular = 0.0f;
    AccumulateViewModelPointLights(input.worldPos, normal, toView, 0.45f, surface, diffuse, specular);
    diffuse += GetViewModelBounce(normal, 0.5f) * occlusion;
    specular += ViewModelBounce.rgb * EvaluateSpecularGGX(surface, normal, toView, ViewModelBounceDirection.xyz);
    specular += GetViewModelReflection(surface, normal, toView, occlusion) * 0.6f;

    // 表面下散乱の近似：光が弱く当たる所（光と影の境目）ほど、肌の中を通った赤い光の色にしている
    const float brightness = dot(diffuse, float3(0.2126f, 0.7152f, 0.0722f));
    const float3 scatterTint = lerp(float3(1.12f, 0.62f, 0.50f), 1.0f.xxx, smoothstep(0.02f, 0.25f, brightness));
    float3 color = surface.diffuseColor * diffuse * scatterTint + specular * occlusion;

    // 輪郭の縁がわずかに明るくなる（産毛と皮脂に光が散る）
    const float rim = pow(1.0f - saturate(dot(normal, toView)), 4.0f);
    color += rim * diffuse * baseColor * 0.35f;

    if (IsFullbrightDebugView())
    {
        return float4(baseColor * (0.6f + 0.4f * saturate(dot(normal, toView))) * 1.35f, 1.0f);
    }
    return float4(ApplyFilmicHorrorGrade(color, LoopDepth), 1.0f);
}
