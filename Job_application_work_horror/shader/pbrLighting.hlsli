// ============================================================================
// 共通の処理: 物理ベースの鏡面反射（PBR、金属・粗さの方式）。壁・扉・金属の小物（litTexturePS）で使っている。
// 主な技術: GGX（Trowbridge-Reitz）の法線分布、高さを考えたSmithの遮蔽（GGXの近似）、Schlickのフレネル、
//           環境光の鏡面反射を求める解析的な近似（Karis 2014「Physically Based Shading on Mobile」）
// common.hlsl・fastNoise.hlsliと、g_SamplerState（s0）の宣言の後に読み込んでいる。
//
// ・拡散反射はこれまでどおり「色 × 光の明るさ × 面の向き」で計算しているため（1/πを光の強さに含めている）、
//   鏡面反射にはπを掛けて、同じ光の強さの単位にそろえている。
// ・金属は拡散反射を持たず、色がそのまま鏡面反射の色（F0）になる。金属でない物の鏡面反射の色は4%（F0 = 0.04）。
// ============================================================================
#ifndef PBR_LIGHTING_INCLUDED
#define PBR_LIGHTING_INCLUDED
#include "renderingConfig.hlsli"

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

static const float PBR_PI = 3.14159265f;
// 金属でない物（コンクリート・木・塗装・プラスチック）の、正面から見たときの反射率
static const float PBR_DIELECTRIC_F0 = 0.04f;
// 粗さの下限。小さすぎると、光の点が1画素より小さくなってちらつく
static const float PBR_MIN_ROUGHNESS = 0.045f;

// 1つの面の、PBRの材質
struct PbrSurface
{
    float3 diffuseColor;   // 拡散反射の色（金属ほど黒くなる）
    float3 f0;             // 正面から見たときの鏡面反射の色
    float roughness;       // 見た目の粗さ（0 = 鏡、1 = ざらざら）
    float alpha;           // 計算に使う粗さ（見た目の粗さの2乗）
    float metallic;
};

// マテリアルの粗さを返している。0（指定なし）のときは、これまでの光沢の強さ（Phongの指数）から換算している。
// 建物の壁と天井は、打ちっぱなしのコンクリートとして粗くしている。
// 従来の光沢指数から変換した粗さの範囲。風化した面は粗い側に寄せる。
#define PBR_LEGACY_MIN_ROUGHNESS 0.45f
#define PBR_LEGACY_MAX_ROUGHNESS 0.95f
#define PBR_WEATHERED_MIN_ROUGHNESS 0.86f

// 濡れた面の粗さを水膜の値に近づけ、元の材質の違いを残す。
#define PBR_WET_SURFACE_ROUGHNESS 0.16f
#define PBR_WET_SURFACE_ROUGHNESS_BLEND 0.75f

float GetMaterialRoughness()
{
    if (Material.Roughness > 0.0f)
    {
        return Material.Roughness;
    }
    // Blinn-Phongの指数 n とGGXの粗さの対応：α ≒ sqrt(2 / (n + 2))、見た目の粗さ = sqrt(α)
    const float alpha = sqrt(2.0f / (max(Material.Shiness, 1.0f) + 2.0f));
    const float converted = clamp(sqrt(alpha), PBR_LEGACY_MIN_ROUGHNESS, PBR_LEGACY_MAX_ROUGHNESS);
    return Material.WeatheringSurface ? max(converted, PBR_WEATHERED_MIN_ROUGHNESS) : converted;
}

// 色・金属らしさ・粗さ・濡れ具合から、PBRの材質を作っている。
// 濡れた所は、表面が水の膜でなめらかになり（粗さが下がる）、光沢がはっきりする。
PbrSurface MakePbrSurface(float3 baseColor, float metallic, float roughness, float wetness)
{
    PbrSurface surface;
    surface.metallic = saturate(metallic);
    surface.roughness = clamp(lerp(roughness, PBR_WET_SURFACE_ROUGHNESS, saturate(wetness) * PBR_WET_SURFACE_ROUGHNESS_BLEND), PBR_MIN_ROUGHNESS, 1.0f);
    surface.alpha = surface.roughness * surface.roughness;
    surface.diffuseColor = baseColor * (1.0f - surface.metallic);
    surface.f0 = lerp(PBR_DIELECTRIC_F0.xxx, baseColor, surface.metallic);
    return surface;
}

// GGX（Trowbridge-Reitz）の法線分布：細かな面のうち、ちょうど光を目へ返す向きの面の割合
float DistributionGGX(float nDotH, float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float denominator = nDotH * nDotH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / (PBR_PI * denominator * denominator);
}

// 浅い角度の可視項が発散しないよう、分母の下限を置く。
static const float PBR_VISIBILITY_DENOMINATOR_MIN = 1e-4f;

// 正面の反射率 F0 から斜めの全反射へ近づく Schlick 近似の指数。
static const float PBR_SCHLICK_EXPONENT = 5.0f;

// 高さを考えたSmithの遮蔽を、GGX用に近似した可視の項（G / (4 n・l n・v) をまとめた形。Hammon 2017）
float VisibilitySmithGGX(float nDotV, float nDotL, float alpha)
{
    return 0.5f / max(lerp(2.0f * nDotL * nDotV, nDotL + nDotV, alpha), PBR_VISIBILITY_DENOMINATOR_MIN);
}

// Schlickのフレネル：浅い角度で見るほど、どんな物も鏡のように光を返す
float3 FresnelSchlick(float3 f0, float vDotH)
{
    const float weight = pow(1.0f - saturate(vDotH), PBR_SCHLICK_EXPONENT);
    return f0 + (1.0f - f0) * weight;
}

// 1つの光から目へ返る鏡面反射（光の色と明るさを掛ける前の値。n・lまで含む）。
// normal・toView・toLight は同じ空間の、長さ1の向き。
// 視線と光がちょうど逆向きのときに足すわずかな向きと、視線との内積の下限
#define PBR_HALF_VECTOR_NUDGE 1e-5f
#define PBR_MIN_N_DOT_V 1e-4f

float3 EvaluateSpecularGGX(PbrSurface surface, float3 normal, float3 toView, float3 toLight)
{
    const float nDotL = saturate(dot(normal, toLight));
    // 視線と光がちょうど逆向きのときに長さ0にならないよう、少しだけ足している
    const float3 halfVector = normalize(toView + toLight + float3(0.0f, 0.0f, PBR_HALF_VECTOR_NUDGE));
    const float nDotV = max(dot(normal, toView), PBR_MIN_N_DOT_V);
    const float nDotH = saturate(dot(normal, halfVector));
    const float vDotH = saturate(dot(toView, halfVector));

    const float distribution = DistributionGGX(nDotH, surface.alpha);
    const float visibility = VisibilitySmithGGX(nDotV, nDotL, surface.alpha);
    const float3 fresnel = FresnelSchlick(surface.f0, vDotH);
    return distribution * visibility * fresnel * (PBR_PI * nDotL);
}

// 使い込まれた金属の跡（錆・擦れ傷・拭き跡のむら）。金属らしさのある面だけに、場所ごとに色・金属らしさ・粗さを変えている。
// 一様な金属は、暗い場所では形の分からない黒い板に見えやすい。光り方がまだらになることで、金属だと分かる。
struct MetalWear
{
    float3 baseColor;
    float metallic;
    float roughness;
    // 錆の盛り上がり（法線を傾ける高さ）
    float height;
};

// --- 金属の錆と擦れ傷の値
// 錆と傷を付ける金属らしさの下限
#define METAL_WEAR_MIN_METALLIC 0.05f
// 錆：床からの高さで濃くなる範囲と足す量、まだらのノイズ（細かさ・ずらし）、出る範囲（出始め・出きる）、粒のノイズ（細かさ・ずらし）
#define RUST_FLOOR_DAMP_HEIGHT 45.0f
#define RUST_FLOOR_DAMP_BOOST 0.15f
#define RUST_PATCH_SCALE 0.11f
#define RUST_PATCH_SEED 17.3f
#define RUST_MASK_START 0.57f
#define RUST_MASK_FULL 0.76f
#define RUST_GRAIN_SCALE 1.7f
#define RUST_GRAIN_SEED 5.3f
// 色の付いた金属かを見る：0で割らないための下限と、彩度を強める量
#define METAL_MIN_CHANNEL 0.001f
#define METAL_SATURATION_GAIN 2.5f
// 鉄の赤茶の錆（暗い・明るい）と、色の付いた金属の黒ずみ（暗さ：暗い・明るい）
#define IRON_RUST_DARK float3(0.20f, 0.075f, 0.030f)
#define IRON_RUST_LIGHT float3(0.36f, 0.16f, 0.060f)
#define TARNISH_DARK 0.28f
#define TARNISH_LIGHT 0.42f
// 擦れ傷：斜めの向き（回転の cos・sin）、細長いノイズ（細かさ・ずらし）、散らす範囲（出始め・出きる、細かさ・ずらし）、線のしきい値（始め・終わり）
#define SCRATCH_ROTATE_COS 0.8f
#define SCRATCH_ROTATE_SIN 0.6f
#define SCRATCH_LINE_SCALE float2(0.35f, 9.0f)
#define SCRATCH_LINE_SEED 3.1f
#define SCRATCH_AREA_START 0.35f
#define SCRATCH_AREA_FULL 0.65f
#define SCRATCH_AREA_SCALE 0.07f
#define SCRATCH_AREA_SEED 41.0f
#define SCRATCH_LINE_START 0.86f
#define SCRATCH_LINE_FULL 0.95f
// 拭き跡（細かさ・ずらし・粗さのむら）、傷の粗さ、錆の粗さ、傷の明るさ（倍率・足す量・混ぜ方）、錆の盛り上がり（最低＋粒の幅）
#define SMUDGE_SCALE 0.45f
#define SMUDGE_SEED 9.0f
#define SMUDGE_ROUGHNESS_RANGE 0.20f
#define SCRATCH_ROUGHNESS 0.14f
#define RUST_ROUGHNESS 0.88f
#define SCRATCH_BRIGHTEN 1.25f
#define SCRATCH_LIFT 0.04f
#define SCRATCH_BLEND 0.6f
#define RUST_HEIGHT_BASE 0.4f
#define RUST_HEIGHT_GRAIN 0.6f

MetalWear GetMetalWear(float3 baseColor, float metallic, float roughness, float3 worldPosition, float3 worldNormal)
{
    MetalWear wear;
    wear.baseColor = baseColor;
    wear.metallic = metallic;
    wear.roughness = roughness;
    wear.height = 0.0f;
    [branch]
    if (metallic > METAL_WEAR_MIN_METALLIC)
    {
        // 面の向きで、模様を作る2次元の座標を選んでいる（箱のどの面でも、ワールドの大きさのまま模様を作る）
        const float3 axis = abs(normalize(worldNormal));
        const float2 uv = axis.y > max(axis.x, axis.z)
            ? worldPosition.xz
            : (axis.x > axis.z ? worldPosition.zy : worldPosition.xy);

        // 錆：大きなまだらの染みに、床に近いほど濃くなる分（水と湿気が溜まる）を足している。
        // 錆は鉄の上にできた酸化物なので、金属ではなく、ざらざらで光らない
        const float floorDamp = saturate(1.0f - (worldPosition.y - SCENE_BASE_FLOOR_Y) / RUST_FLOOR_DAMP_HEIGHT);
        const float rustPatches = FastFractalNoise3(uv * RUST_PATCH_SCALE + RUST_PATCH_SEED) + floorDamp * RUST_FLOOR_DAMP_BOOST;
        const float rustMask = smoothstep(RUST_MASK_START, RUST_MASK_FULL, rustPatches);
        const float rustGrain = FastValueNoise(uv * RUST_GRAIN_SCALE + RUST_GRAIN_SEED);
        // 鉄のような灰色の金属は赤茶の錆、真鍮や金のような色の付いた金属は黒ずんだくすみにしている
        const float maxChannel = max(baseColor.r, max(baseColor.g, baseColor.b));
        const float minChannel = min(baseColor.r, min(baseColor.g, baseColor.b));
        const float grayness = 1.0f - saturate((maxChannel - minChannel) / max(maxChannel, METAL_MIN_CHANNEL) * METAL_SATURATION_GAIN);
        const float3 ironRust = lerp(IRON_RUST_DARK, IRON_RUST_LIGHT, rustGrain);
        const float3 tarnish = baseColor * lerp(TARNISH_DARK, TARNISH_LIGHT, rustGrain);
        const float3 rustColor = lerp(tarnish, ironRust, grayness);

        // 擦れ傷：斜めの向きに細長く引き伸ばしたノイズで細い線を作り、所々にだけ散らしている。
        // 傷の所は地の金属が出て、なめらかに光る
        const float2 scratchUV = float2(uv.x * SCRATCH_ROTATE_COS + uv.y * SCRATCH_ROTATE_SIN, uv.y * SCRATCH_ROTATE_COS - uv.x * SCRATCH_ROTATE_SIN);
        const float scratchLines = FastValueNoise(scratchUV * SCRATCH_LINE_SCALE + SCRATCH_LINE_SEED);
        const float scratchArea = smoothstep(SCRATCH_AREA_START, SCRATCH_AREA_FULL, FastValueNoise(uv * SCRATCH_AREA_SCALE + SCRATCH_AREA_SEED));
        const float scratchMask = smoothstep(SCRATCH_LINE_START, SCRATCH_LINE_FULL, scratchLines) * scratchArea * (1.0f - rustMask);

        // 拭き跡や手の脂のような、光沢の細かな濃淡
        const float smudge = FastValueNoise(uv * SMUDGE_SCALE + SMUDGE_SEED);

        wear.roughness = clamp(roughness + (smudge - 0.5f) * SMUDGE_ROUGHNESS_RANGE, PBR_MIN_ROUGHNESS, 1.0f);
        wear.roughness = lerp(wear.roughness, SCRATCH_ROUGHNESS, scratchMask);
        wear.roughness = lerp(wear.roughness, RUST_ROUGHNESS, rustMask);
        wear.baseColor = lerp(baseColor, baseColor * SCRATCH_BRIGHTEN + SCRATCH_LIFT, scratchMask * SCRATCH_BLEND);
        wear.baseColor = lerp(wear.baseColor, rustColor, rustMask);
        wear.metallic = metallic * (1.0f - rustMask);
        wear.height = rustMask * (RUST_HEIGHT_BASE + rustGrain * RUST_HEIGHT_GRAIN);
    }
    return wear;
}

// 環境マップ（カメラの位置から6方向を撮った景色。GameRendering の EnvironmentProbe、ReflectionProbe.cpp）。
// 縮小画像（ミップ）の段を持ち、粗い面ほど小さくぼやけた段を読んでいる（粗さに応じてぼやけた映り込みの近似）。
// 1面と2面の間だけ設定している。設定していない場面では黒が返るので、呼ぶ側で環境光と合わせている。
TextureCube g_EnvironmentProbe : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_ENVIRONMENT_PROBE_TEXTURE_SLOT));
static const float PBR_ENVIRONMENT_MIP_COUNT = 9.0f; // 256画素から1画素まで（ReflectionProbe::FaceSize と合わせている）

float3 SampleEnvironment(float3 worldDirection, float roughness)
{
    const float mipLevel = roughness * (PBR_ENVIRONMENT_MIP_COUNT - 1.0f);
    // キューブマップの面は左手系、このゲームの座標は右手系なので、z を反転して読んでいる（ReflectionProbe.cpp）
    return g_EnvironmentProbe.SampleLevel(g_SamplerState,
        float3(worldDirection.x, worldDirection.y, -worldDirection.z), mipLevel).rgb;
}

// 環境光の鏡面反射に掛ける割合（あらかじめ積分した表の代わりに、解析的な式で近似している。Karis 2014）
// Karis 2014 の近似式の係数（論文のまま）
#define KARIS_BRDF_C0 float4(-1.0f, -0.0275f, -0.572f, 0.022f)
#define KARIS_BRDF_C1 float4(1.0f, 0.0425f, 1.04f, -0.04f)
#define KARIS_BRDF_FALLOFF -9.28f
#define KARIS_BRDF_SCALE_BIAS float2(-1.04f, 1.04f)

float3 EnvironmentBrdfApprox(float3 f0, float roughness, float nDotV)
{
    const float4 c0 = KARIS_BRDF_C0;
    const float4 c1 = KARIS_BRDF_C1;
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(KARIS_BRDF_FALLOFF * nDotV)) * r.x + r.y;
    const float2 scaleBias = KARIS_BRDF_SCALE_BIAS * a004 + r.zw;
    return f0 * scaleBias.x + scaleBias.y;
}

// 粗い面ほど、隙間や角で環境の映り込みがさえぎられる分（Lagarde 2014 の鏡面の遮蔽の近似）
// Lagarde 2014 の近似式の係数（論文のまま）と、0の累乗を避ける下限
#define LAGARDE_OCCLUSION_ROUGHNESS_SCALE -16.0f
#define SPECULAR_OCCLUSION_MIN_BASE 1e-4f

float GetSpecularOcclusion(float nDotV, float ambientOcclusion, float roughness)
{
    return saturate(pow(max(nDotV + ambientOcclusion, SPECULAR_OCCLUSION_MIN_BASE),
        exp2(LAGARDE_OCCLUSION_ROUGHNESS_SCALE * roughness - 1.0f)) - 1.0f + ambientOcclusion);
}

#endif
