// ============================================================================
// シェーダーの役割: 一人称で手に持った懐中電灯（本体・先の金属の輪・レンズ・スイッチ）を描いている。
// 主な技術: PBR（GGX）、頂点カラーの印による塗装のはげ（地の金属が出る）、
//           ローレット加工（握りの菱形の溝）の凹凸を画面の微分で法線に足す方法（Mikkelsen 2010 の surface gradient）、
//           点灯の度合いで光るレンズ（ブルームでにじむ）
// ・材質の色・金属らしさ・粗さは、FirstPersonHands がマテリアルの名前ごとに決めている。
// ・頂点カラー: r = 環境遮蔽（指が触れている所の暗がり）、g = 塗装のはげ、b = ローレット加工の範囲（build_flashlight_hand.py）。
// ・レンズはマテリアルの Emission を光の色にし、ViewModelState.x（点灯の度合い）を掛けて光らせている。
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

// 高さの模様を、画面の微分から法線の傾きに変えている（接線のないメッシュでも使える）
float3 PerturbNormal(float3 normal, float3 worldPosition, float height, float strength)
{
    const float3 dpdx = ddx(worldPosition);
    const float3 dpdy = ddy(worldPosition);
    const float3 r1 = cross(dpdy, normal);
    const float3 r2 = cross(normal, dpdx);
    const float determinant = dot(dpdx, r1);
    const float3 gradient = sign(determinant) * (ddx(height) * r1 + ddy(height) * r2);
    return normalize(abs(determinant) * normal - gradient * strength);
}

float4 main(in VIEW_MODEL_PS_IN input) : SV_Target
{
    float3 normal = normalize(input.worldNormal);
    const float3 toView = normalize(ViewModelEye.xyz - input.worldPos);
    const float occlusion = input.col.r;
    const float wear = input.col.g;
    const float knurlArea = input.col.b;

    // 塗装（Material）と、はげた所の地のアルミ
    float3 baseColor = Material.Diffuse.rgb;
    float metallic = Material.Metallic;
    float roughness = Material.Roughness;

    // 細かな擦り傷（軸のまわりに長く伸びた線）
    const float around = atan2(input.localPos.y, input.localPos.x);
    const float scratches = smoothstep(0.80f, 0.97f,
        FastValueNoise(float2(around * 9.0f, input.localPos.z * 160.0f)));
    const float bare = saturate(wear + scratches * 0.35f * (1.0f - metallic));
    baseColor = lerp(baseColor, float3(0.56f, 0.56f, 0.54f), bare);
    metallic = lerp(metallic, 1.0f, bare);
    roughness = lerp(roughness, 0.34f, bare);

    // ローレット加工：軸の周りの長さと軸の向きで、斜めの2本の溝が交わる菱形の模様（ピッチ約3mm）
    [branch]
    if (knurlArea > 0.01f)
    {
        const float2 grip = float2(around * 0.35f, input.localPos.z) / 0.06f;
        const float diamond = abs(frac(grip.x + grip.y) - 0.5f) + abs(frac(grip.x - grip.y) - 0.5f);
        // 遠くて溝が画素より細かいときは、ちらつかないよう弱めている
        const float detail = knurlArea * (1.0f - smoothstep(0.35f, 0.9f, fwidth(grip.x + grip.y)));
        normal = PerturbNormal(normal, input.worldPos, diamond * 0.010f, detail);
        roughness = lerp(roughness, 0.55f, knurlArea * 0.5f);
    }

    const PbrSurface surface = MakePbrSurface(baseColor, metallic, roughness, 0.0f);
    float3 diffuse = GetViewModelAmbient(input.worldPos, normal, occlusion);
    float3 specular = 0.0f;
    AccumulateViewModelPointLights(input.worldPos, normal, toView, 0.0f, surface, diffuse, specular);
    const float3 bounce = GetViewModelBounce(normal, 0.2f);
    diffuse += bounce * occlusion;
    specular += ViewModelBounce.rgb * EvaluateSpecularGGX(surface, normal, toView, ViewModelBounceDirection.xyz);
    specular += GetViewModelReflection(surface, normal, toView, occlusion);

    float3 color = surface.diffuseColor * diffuse + specular * occlusion;

    // レンズ：中心ほど明るく、反射鏡の輪がうっすら見える。電池が弱るほど黄色く暗い
    [branch]
    if (dot(Material.Emission.rgb, 1.0f) > 0.0f)
    {
        const float radius = length(input.tex - 0.5f) * 2.0f;
        const float rings = 0.85f + 0.15f * sin(radius * 38.0f);
        const float hotspot = lerp(0.55f, 1.0f, 1.0f - smoothstep(0.0f, 0.75f, radius));
        const float3 weakTint = lerp(float3(1.0f, 1.0f, 1.0f), float3(1.0f, 0.78f, 0.45f), ViewModelState.y);
        color += Material.Emission.rgb * weakTint * hotspot * rings * ViewModelState.x;
    }

    if (IsFullbrightDebugView())
    {
        return float4(baseColor * (0.6f + 0.4f * saturate(dot(normal, toView))) * 1.35f, 1.0f);
    }
    return float4(ApplyFilmicHorrorGrade(color, LoopDepth), 1.0f);
}
