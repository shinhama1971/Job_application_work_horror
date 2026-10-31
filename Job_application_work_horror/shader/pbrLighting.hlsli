// ============================================================================
// 共通の処理: 物理ベースの鏡面反射（PBR、金属・粗さの方式）。壁・扉・金属の小物（litTexturePS）で使っている。
// 主な技術: GGX（Trowbridge-Reitz）の法線分布、高さを考えたSmithの遮蔽（GGXの近似）、Schlickのフレネル、
//           環境光の鏡面反射を求める解析的な近似（Karis 2014「Physically Based Shading on Mobile」）
// common.hlslと、g_SamplerState（s0）の宣言の後に読み込んでいる。
//
// ・拡散反射はこれまでどおり「色 × 光の明るさ × 面の向き」で計算しているため（1/πを光の強さに含めている）、
//   鏡面反射にはπを掛けて、同じ光の強さの単位にそろえている。
// ・金属は拡散反射を持たず、色がそのまま鏡面反射の色（F0）になる。金属でない物の鏡面反射の色は4%（F0 = 0.04）。
// ============================================================================
#ifndef PBR_LIGHTING_INCLUDED
#define PBR_LIGHTING_INCLUDED

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
float GetMaterialRoughness()
{
    if (Material.Roughness > 0.0f)
    {
        return Material.Roughness;
    }
    // Blinn-Phongの指数 n とGGXの粗さの対応：α ≒ sqrt(2 / (n + 2))、見た目の粗さ = sqrt(α)
    const float alpha = sqrt(2.0f / (max(Material.Shiness, 1.0f) + 2.0f));
    const float converted = clamp(sqrt(alpha), 0.45f, 0.95f);
    return Material.WeatheringSurface ? max(converted, 0.86f) : converted;
}

// 色・金属らしさ・粗さ・濡れ具合から、PBRの材質を作っている。
// 濡れた所は、表面が水の膜でなめらかになり（粗さが下がる）、光沢がはっきりする。
PbrSurface MakePbrSurface(float3 baseColor, float metallic, float roughness, float wetness)
{
    PbrSurface surface;
    surface.metallic = saturate(metallic);
    surface.roughness = clamp(lerp(roughness, 0.16f, saturate(wetness) * 0.75f), PBR_MIN_ROUGHNESS, 1.0f);
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

// 高さを考えたSmithの遮蔽を、GGX用に近似した可視の項（G / (4 n・l n・v) をまとめた形。Hammon 2017）
float VisibilitySmithGGX(float nDotV, float nDotL, float alpha)
{
    return 0.5f / max(lerp(2.0f * nDotL * nDotV, nDotL + nDotV, alpha), 1e-4f);
}

// Schlickのフレネル：浅い角度で見るほど、どんな物も鏡のように光を返す
float3 FresnelSchlick(float3 f0, float vDotH)
{
    const float weight = pow(1.0f - saturate(vDotH), 5.0f);
    return f0 + (1.0f - f0) * weight;
}

// 1つの光から目へ返る鏡面反射（光の色と明るさを掛ける前の値。n・lまで含む）。
// normal・toView・toLight は同じ空間の、長さ1の向き。
float3 EvaluateSpecularGGX(PbrSurface surface, float3 normal, float3 toView, float3 toLight)
{
    const float nDotL = saturate(dot(normal, toLight));
    // 視線と光がちょうど逆向きのときに長さ0にならないよう、少しだけ足している
    const float3 halfVector = normalize(toView + toLight + float3(0.0f, 0.0f, 1e-5f));
    const float nDotV = max(dot(normal, toView), 1e-4f);
    const float nDotH = saturate(dot(normal, halfVector));
    const float vDotH = saturate(dot(toView, halfVector));

    const float distribution = DistributionGGX(nDotH, surface.alpha);
    const float visibility = VisibilitySmithGGX(nDotV, nDotL, surface.alpha);
    const float3 fresnel = FresnelSchlick(surface.f0, vDotH);
    return distribution * visibility * fresnel * (PBR_PI * nDotL);
}

// 環境マップ（カメラの位置から6方向を撮った景色。GameRendering の EnvironmentProbe、ReflectionProbe.cpp）。
// 縮小画像（ミップ）の段を持ち、粗い面ほど小さくぼやけた段を読んでいる（粗さに応じてぼやけた映り込みの近似）。
// 1面と2面の間だけ設定している。設定していない場面では黒が返るので、呼ぶ側で環境光と合わせている。
TextureCube g_EnvironmentProbe : register(t14);
static const float PBR_ENVIRONMENT_MIP_COUNT = 8.0f; // 128画素から1画素まで

float3 SampleEnvironment(float3 worldDirection, float roughness)
{
    const float mipLevel = roughness * (PBR_ENVIRONMENT_MIP_COUNT - 1.0f);
    // キューブマップの面は左手系、このゲームの座標は右手系なので、z を反転して読んでいる（ReflectionProbe.cpp）
    return g_EnvironmentProbe.SampleLevel(g_SamplerState,
        float3(worldDirection.x, worldDirection.y, -worldDirection.z), mipLevel).rgb;
}

// 環境光の鏡面反射に掛ける割合（あらかじめ積分した表の代わりに、解析的な式で近似している。Karis 2014）
float3 EnvironmentBrdfApprox(float3 f0, float roughness, float nDotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28f * nDotV)) * r.x + r.y;
    const float2 scaleBias = float2(-1.04f, 1.04f) * a004 + r.zw;
    return f0 * scaleBias.x + scaleBias.y;
}

// 粗い面ほど、隙間や角で環境の映り込みがさえぎられる分（Lagarde 2014 の鏡面の遮蔽の近似）
float GetSpecularOcclusion(float nDotV, float ambientOcclusion, float roughness)
{
    return saturate(pow(max(nDotV + ambientOcclusion, 1e-4f), exp2(-16.0f * roughness - 1.0f)) - 1.0f + ambientOcclusion);
}

#endif
