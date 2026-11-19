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

// 擦り傷：出る範囲（出始め・出きる）、ノイズの細かさ（軸のまわり・軸の向き）、塗装の上で地が出る量
#define TORCH_SCRATCH_START 0.80f
#define TORCH_SCRATCH_FULL 0.97f
#define TORCH_SCRATCH_AROUND_SCALE 9.0f
#define TORCH_SCRATCH_ALONG_SCALE 160.0f
#define TORCH_SCRATCH_BARE 0.35f
// はげた所の地のアルミ（色・粗さ）
#define TORCH_BARE_METAL_COLOR float3(0.56f, 0.56f, 0.54f)
#define TORCH_BARE_ROUGHNESS 0.34f
// ローレット加工：使う範囲のしきい値、軸のまわりの縮め方、溝の間隔（約3mm）、細かすぎるときに弱める範囲、溝の深さ、粗さとその混ぜ方
#define KNURL_MIN_AREA 0.01f
#define KNURL_AROUND_SCALE 0.35f
#define KNURL_PITCH 0.06f
#define KNURL_FADE_START 0.35f
#define KNURL_FADE_END 0.9f
#define KNURL_DEPTH 0.010f
#define KNURL_ROUGHNESS 0.55f
#define KNURL_ROUGHNESS_BLEND 0.5f
// 照り返しの回り込み
#define TORCH_BOUNCE_WRAP 0.2f
// レンズ：反射鏡の輪（最低・幅・本数）、中心の明るさ（縁の明るさ・ぼかし終わり）、電池が弱ったときの色
#define LENS_RING_BASE 0.85f
#define LENS_RING_RANGE 0.15f
#define LENS_RING_FREQUENCY 38.0f
#define LENS_EDGE_BRIGHTNESS 0.55f
#define LENS_HOTSPOT_RADIUS 0.75f
#define LENS_WEAK_TINT float3(1.0f, 0.78f, 0.45f)


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
    const float scratches = smoothstep(TORCH_SCRATCH_START, TORCH_SCRATCH_FULL,
        FastValueNoise(float2(around * TORCH_SCRATCH_AROUND_SCALE, input.localPos.z * TORCH_SCRATCH_ALONG_SCALE)));
    const float bare = saturate(wear + scratches * TORCH_SCRATCH_BARE * (1.0f - metallic));
    baseColor = lerp(baseColor, TORCH_BARE_METAL_COLOR, bare);
    metallic = lerp(metallic, 1.0f, bare);
    roughness = lerp(roughness, TORCH_BARE_ROUGHNESS, bare);

    // ローレット加工：軸の周りの長さと軸の向きで、斜めの2本の溝が交わる菱形の模様（ピッチ約3mm）
    [branch]
    if (knurlArea > KNURL_MIN_AREA)
    {
        const float2 grip = float2(around * KNURL_AROUND_SCALE, input.localPos.z) / KNURL_PITCH;
        const float diamond = abs(frac(grip.x + grip.y) - 0.5f) + abs(frac(grip.x - grip.y) - 0.5f);
        // 遠くて溝が画素より細かいときは、ちらつかないよう弱めている
        const float detail = knurlArea * (1.0f - smoothstep(KNURL_FADE_START, KNURL_FADE_END, fwidth(grip.x + grip.y)));
        normal = PerturbNormal(normal, input.worldPos, diamond * KNURL_DEPTH, detail);
        roughness = lerp(roughness, KNURL_ROUGHNESS, knurlArea * KNURL_ROUGHNESS_BLEND);
    }

    const PbrSurface surface = MakePbrSurface(baseColor, metallic, roughness, 0.0f);
    float3 diffuse = GetViewModelAmbient(input.worldPos, normal, occlusion);
    float3 specular = 0.0f;
    AccumulateViewModelPointLights(input.worldPos, normal, toView, 0.0f, surface, diffuse, specular);
    const float3 bounce = GetViewModelBounce(normal, TORCH_BOUNCE_WRAP);
    diffuse += bounce * occlusion;
    specular += ViewModelBounce.rgb * EvaluateSpecularGGX(surface, normal, toView, ViewModelBounceDirection.xyz);
    specular += GetViewModelReflection(surface, normal, toView, occlusion);

    float3 color = surface.diffuseColor * diffuse + specular * occlusion;

    // レンズ：中心ほど明るく、反射鏡の輪がうっすら見える。電池が弱るほど黄色く暗い
    [branch]
    if (dot(Material.Emission.rgb, 1.0f) > 0.0f)
    {
        const float radius = length(input.tex - 0.5f) * 2.0f;
        const float rings = LENS_RING_BASE + LENS_RING_RANGE * sin(radius * LENS_RING_FREQUENCY);
        const float hotspot = lerp(LENS_EDGE_BRIGHTNESS, 1.0f, 1.0f - smoothstep(0.0f, LENS_HOTSPOT_RADIUS, radius));
        const float3 weakTint = lerp(float3(1.0f, 1.0f, 1.0f), LENS_WEAK_TINT, ViewModelState.y);
        color += Material.Emission.rgb * weakTint * hotspot * rings * ViewModelState.x;
    }

    if (IsFullbrightDebugView())
    {
        return float4(baseColor * (FULLBRIGHT_BASE + FULLBRIGHT_FACING * saturate(dot(normal, toView))) * FULLBRIGHT_GAIN, 1.0f);
    }
    return float4(ApplyFilmicHorrorGrade(color, LoopDepth), 1.0f);
}
