// ============================================================================
// シェーダーの役割: 一人称で見える右腕の袖（作業着の厚い布）を描いている。
// 主な技術: 回り込む拡散反射、布の織り目（手元の空間の位置から作る細かな縞）、
//           浅い角度で光る布の毛羽の光（シーン：視線と面が平行に近いほど、まわりの光を返す）
// ・頂点カラー: r = 環境遮蔽（しわの奥）、g = 布の色むら（build_flashlight_hand.py）。
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

    // 織り目：縦糸と横糸の細かな縞（遠くでちらつかないよう、画面の微分で弱めている）
    const float2 weavePosition = float2(input.localPos.x + input.localPos.y, input.localPos.z) * 95.0f;
    const float weaveFade = 1.0f - smoothstep(0.4f, 1.0f, fwidth(weavePosition.x));
    const float weave = (sin(weavePosition.x) * sin(weavePosition.y) * 0.5f + 0.5f) * weaveFade;
    float3 baseColor = Material.Diffuse.rgb * (0.82f + 0.36f * input.col.g) * (0.9f + 0.2f * weave);
    // 袖口（手元の空間で z が -1.9 あたり）は手首に擦れて、色が少し抜けている
    baseColor *= lerp(1.0f, 1.3f, 1.0f - smoothstep(-2.0f, -2.5f, input.localPos.z));

    const PbrSurface surface = MakePbrSurface(baseColor, 0.0f, 0.86f, 0.0f);
    float3 diffuse = GetViewModelAmbient(input.worldPos, normal, occlusion);
    float3 specular = 0.0f;
    AccumulateViewModelPointLights(input.worldPos, normal, toView, 0.5f, surface, diffuse, specular);
    diffuse += GetViewModelBounce(normal, 0.5f) * occlusion;

    float3 color = surface.diffuseColor * diffuse + specular * occlusion * 0.5f;
    // 布の毛羽の光
    const float sheen = pow(1.0f - saturate(dot(normal, toView)), 3.0f);
    color += sheen * diffuse * float3(0.30f, 0.31f, 0.30f) * 0.5f;

    if (IsFullbrightDebugView())
    {
        return float4(baseColor * (0.6f + 0.4f * saturate(dot(normal, toView))) * 1.35f, 1.0f);
    }
    return float4(ApplyFilmicHorrorGrade(color, LoopDepth), 1.0f);
}
