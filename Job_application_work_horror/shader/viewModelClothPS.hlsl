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

// 織り目：縞の細かさ、細かすぎるときに弱める範囲（始め・終わり）
#define CLOTH_WEAVE_FREQUENCY 95.0f
#define CLOTH_WEAVE_FADE_START 0.4f
#define CLOTH_WEAVE_FADE_END 1.0f
// 色のむら：頂点カラーによる明るさ（最低・幅）、織り目による明るさ（最低・幅）
#define CLOTH_SHADE_BASE 0.82f
#define CLOTH_SHADE_RANGE 0.36f
#define CLOTH_WEAVE_SHADE_BASE 0.9f
#define CLOTH_WEAVE_SHADE_RANGE 0.2f
// 袖口の色抜け（明るくする割合、手元の空間の z の範囲：始め・終わり）
#define CLOTH_CUFF_FADE 1.3f
#define CLOTH_CUFF_START -2.0f
#define CLOTH_CUFF_END -2.5f
// 布の粗さ、点光源と照り返しの回り込み、鏡面反射の弱め方、毛羽の光（鋭さ・色・強さ）
#define CLOTH_ROUGHNESS 0.86f
#define CLOTH_LIGHT_WRAP 0.5f
#define CLOTH_BOUNCE_WRAP 0.5f
#define CLOTH_SPECULAR_SCALE 0.5f
#define CLOTH_SHEEN_EXPONENT 3.0f
#define CLOTH_SHEEN_COLOR float3(0.30f, 0.31f, 0.30f)
#define CLOTH_SHEEN_GAIN 0.5f

float4 main(in VIEW_MODEL_PS_IN input) : SV_Target
{
    const float3 normal = normalize(input.worldNormal);
    const float3 toView = normalize(ViewModelEye.xyz - input.worldPos);
    const float occlusion = input.col.r;

    // 織り目：縦糸と横糸の細かな縞（遠くでちらつかないよう、画面の微分で弱めている）
    const float2 weavePosition = float2(input.localPos.x + input.localPos.y, input.localPos.z) * CLOTH_WEAVE_FREQUENCY;
    const float weaveFade = 1.0f - smoothstep(CLOTH_WEAVE_FADE_START, CLOTH_WEAVE_FADE_END, fwidth(weavePosition.x));
    const float weave = (sin(weavePosition.x) * sin(weavePosition.y) * 0.5f + 0.5f) * weaveFade;
    float3 baseColor = Material.Diffuse.rgb * (CLOTH_SHADE_BASE + CLOTH_SHADE_RANGE * input.col.g) *
        (CLOTH_WEAVE_SHADE_BASE + CLOTH_WEAVE_SHADE_RANGE * weave);
    // 袖口（手元の空間で z が -1.9 あたり）は手首に擦れて、色が少し抜けている
    baseColor *= lerp(1.0f, CLOTH_CUFF_FADE, 1.0f - smoothstep(CLOTH_CUFF_START, CLOTH_CUFF_END, input.localPos.z));

    const PbrSurface surface = MakePbrSurface(baseColor, 0.0f, CLOTH_ROUGHNESS, 0.0f);
    float3 diffuse = GetViewModelAmbient(input.worldPos, normal, occlusion);
    float3 specular = 0.0f;
    AccumulateViewModelPointLights(input.worldPos, normal, toView, CLOTH_LIGHT_WRAP, surface, diffuse, specular);
    diffuse += GetViewModelBounce(normal, CLOTH_BOUNCE_WRAP) * occlusion;

    float3 color = surface.diffuseColor * diffuse + specular * occlusion * CLOTH_SPECULAR_SCALE;
    // 布の毛羽の光
    const float sheen = pow(1.0f - saturate(dot(normal, toView)), CLOTH_SHEEN_EXPONENT);
    color += sheen * diffuse * CLOTH_SHEEN_COLOR * CLOTH_SHEEN_GAIN;

    if (IsFullbrightDebugView())
    {
        return float4(baseColor * (FULLBRIGHT_BASE + FULLBRIGHT_FACING * saturate(dot(normal, toView))) * FULLBRIGHT_GAIN, 1.0f);
    }
    return float4(ApplyFilmicHorrorGrade(color, LoopDepth), 1.0f);
}
