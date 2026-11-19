// ============================================================================
// シェーダーの役割: 濡れた床を描いている（床の古さ、水たまりと浸水した範囲、波紋、足音と水滴の波（波のシミュレーション）、水面の反射、フレネル、懐中電灯と天井灯の照明、霧）。
// ============================================================================

#include "common.hlsl"
#include "renderingConfig.hlsli"

// 床のテクスチャ（t0）とサンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"
#include "pointShadow.hlsli"

// 反射の方式の比較で使うキューブマップ（t7、ReflectionProbe）。SSRの画面と深度（t4・t8）は rayReflection.hlsli で読んでいる
TextureCube g_ReflectionProbe : register(t7);

// Ground.cppのWetFloorBufferと同じ並び（b10）。経過時間、波紋の強さ、反射の強さ、水面の細かさ
// 濡れた足跡の最大数（WetFootprints::MaxCount と同じ）
#define FOOTPRINT_COUNT SIGNAL_LOST_FOOTPRINT_COUNT
// 天井の雨漏りの最大数（WaterEffectSystem::MaxLeakCount と同じ）
#define LEAK_COUNT SIGNAL_LOST_MAX_WATER_LEAKS
cbuffer WetFloorBuffer : register(b10)
{
    float WetTime;
    float RippleStrength;
    float ReflectionStrength;
    // 今描いている面の、水面の細かさ（Ground::DrawWaterSurfaces）。
    // 0 = 床の本体（乾いた床。水たまり・浸水・雨漏りは計算しない）、1〜3 = 水たまりのメッシュ（カメラから遠いほど小さい。WATER_LEVEL_*）
    float WaterSurfaceLevel;
    // 床一面が水に浸かった範囲（xy = x・zの最小、zw = x・zの最大）。範囲がないときは、最小が最大より大きい値にしている。
    float4 FloodRect;
    // 濡れた足跡（WetFootprints::Fill）。x = 数。1つにつき2つ（xy = 位置、z = sin(向き)、w = cos(向き)/ x = 濡れ具合、y = 左右、z = 裸足か）
    float4 FootprintInfo;
    float4 Footprints[FOOTPRINT_COUNT * SIGNAL_LOST_FOOTPRINT_STRIDE];
    // x = 雨漏りの数、y = 反射の距離による簡略化。Leaks は xy = 中心のx・z、z = 半径。
    float4 LeakInfo;
    float4 Leaks[LEAK_COUNT];
}

// 水たまりのメッシュの細かさ（Ground::WaterLevel と同じ値）。メッシュ1つの中では一定なので、分岐は画素ごとにばらつかない
#define WATER_LEVEL_DRY_FLOOR SIGNAL_LOST_WATER_LEVEL_DRY_FLOOR
#define WATER_LEVEL_MIDDLE SIGNAL_LOST_WATER_LEVEL_MIDDLE
#define WATER_LEVEL_NEAR SIGNAL_LOST_WATER_LEVEL_NEAR
// 水たまりの表示（デバッグ）：メッシュの細かさの色（近い = 緑、中くらい = 黄、遠い = 赤）と、濃さの塗り方（最低＋幅）
#define DEBUG_PUDDLE_NEAR_COLOR float3(0.25f, 1.0f, 0.30f)
#define DEBUG_PUDDLE_MIDDLE_COLOR float3(1.0f, 0.85f, 0.20f)
#define DEBUG_PUDDLE_FAR_COLOR float3(1.0f, 0.30f, 0.25f)
#define DEBUG_PUDDLE_BASE 0.10f
#define DEBUG_PUDDLE_RANGE 0.90f
// 床の画像を読むときの Mip のずらし（草のような細かい模様を弱める）
#define FLOOR_TEXTURE_MIP_BIAS 1.15f
// 0で割らないための下限、懐中電灯が届いているとみなす下限、部屋の遮りを懐中電灯に効かせる割合
#define FLOOR_MIN_DIVISOR 0.001f
#define FLOOR_MIN_BEAM 0.001f
#define FLOOR_FLASHLIGHT_ROOM_OCCLUSION 0.4f
// 遠くの波の輪の光（波の傾きに掛ける強さ）と、水たまりで直射光の光沢を抑える割合
#define PUDDLE_WAVE_GLINT_SLOPE_GAIN 3.0f
#define PUDDLE_SPECULAR_SCALE 0.58f
// 定数の印（0 か 1）を見るしきい値（雨漏りの部屋の反射を使うか、SSR を使うか）
#define FLOOR_FLAG_THRESHOLD 0.5f

// 頂点シェーダー（litTextureVS）から受け取る値（影と反射の座標も使っている）
struct LIT_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
    float4 shadowPos : TEXCOORD6;
};


// 2次元の値から0〜1の疑似乱数を作っている（水滴の輪とノイズに使う。水たまりの位置は PuddleCells の表で決めている）
// GroundWaterEffects.cpp の Hash21FrequencyX/Y と HashAmplitude に対応する係数。
static const float2 WATER_HASH21_FREQUENCY = float2(127.1f, 311.7f);
static const float WATER_HASH21_AMPLITUDE = 43758.5453f;

float Hash21(float2 value)
{
    return frac(sin(dot(value, WATER_HASH21_FREQUENCY)) * WATER_HASH21_AMPLITUDE);
}

// 格子の4隅の乱数を、なめらかに補間した値のノイズ
float ValueNoise(float2 value)
{
    const float2 cell = floor(value);
    float2 blend = frac(value);
    blend = blend * blend * (3.0f - 2.0f * blend);

    const float a = Hash21(cell);
    const float b = Hash21(cell + float2(1.0f, 0.0f));
    const float c = Hash21(cell + float2(0.0f, 1.0f));
    const float d = Hash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);
}

#include "fastNoise.hlsli"
#include "puddleMask.hlsli"
#include "flashlightLighting.hlsli"
#include "waterCaustics.hlsli"
#include "roomOcclusion.hlsli"
#include "screenAmbientOcclusion.hlsli"
#include "surfaceDetail.hlsli"
#include "wetFootprints.hlsli"

// ----------------------------------------------------------------------------
// 水面の反射の方式（ReflectionParams.x：0 反射なし、1 キューブマップ、2 SSR、3 レイ反射）。ゲームの普段の方式はレイ反射。
// SSRとレイ反射の本体（TraceScreenSpaceReflection・TraceRoomReflection）は、壁の鏡と共通の rayReflection.hlsli にある。
// ----------------------------------------------------------------------------
// キューブマップ：反射の向きで、カメラの位置から撮った6方向の画像を読む。
// 撮った位置からの景色なので軽いが、撮った位置から離れた所や近くの物では、映る位置がずれる。
float3 SampleProbeReflection(float3 worldPosition, float3 worldNormal)
{
    const float3 eye = -mul((float3x3)CausticView, CausticView[3].xyz);
    const float3 reflected = reflect(normalize(worldPosition - eye), worldNormal);
    // キューブマップの面は左手系、このゲームの座標は右手系なので、z を反転して読んでいる（ReflectionProbe.cpp）
    return g_ReflectionProbe.SampleLevel(g_SamplerState, float3(reflected.x, reflected.y, -reflected.z), 0.0f).rgb;
}

// ----------------------------------------------------------------------------
// 床の古さ（1面と2面）。壁の古さと同じ WallWeathering で有効になり、0の面では何もしない。
// コンクリートの床版の目地・ひびを凹凸として、油や水の染み・物を引きずった跡・壁際の埃を色として描いている。
// ----------------------------------------------------------------------------
struct FloorAgeing
{
    float height;   // 凹凸に足す高さ（へこみは負）
    float3 tint;    // 元の色に掛ける色（1なら変化なし）
};

// 床の古さを計算している（amountは古さの濃さ）
// 床材の係数はプリプロセッサで展開する。FXC が型付き定数で生じさせる丸め差を避けるため。
// 床版の大きさと継ぎ目の幅。大きな床版にしてタイルに見えないようにする。
#define FLOOR_SLAB_SIZE (56.0f)
#define FLOOR_JOINT_CORE (0.05f)
#define FLOOR_JOINT_EDGE (0.20f)
#define FLOOR_SLAB_TINT_SEED (11.0f)

// ひびの太さ、現れる領域、遠くで薄める境界を別々に調整する。
#define FLOOR_CRACK_LINE_WIDTH (0.014f)
#define FLOOR_CRACK_LINE_FILTER (0.04f)
#define FLOOR_CRACK_NOISE_SCALE (0.06f)
#define FLOOR_CRACK_NOISE_SEED (17.0f)
#define FLOOR_CRACK_MASK_START (0.60f)
#define FLOOR_CRACK_MASK_FULL (0.78f)
#define FLOOR_CRACK_MASK_SCALE (0.015f)
#define FLOOR_CRACK_MASK_SEED (4.0f)
#define FLOOR_CRACK_DISTANCE_FILTER (0.35f)

// 引きずった跡は一方向に伸ばし、遠くでは薄めてちらつきを抑える。
#define FLOOR_SCUFF_LINE_START (0.72f)
#define FLOOR_SCUFF_LINE_FULL (0.90f)
#define FLOOR_SCUFF_NOISE_X (0.04f)
#define FLOOR_SCUFF_NOISE_Z (0.9f)
#define FLOOR_SCUFF_NOISE_SEED (53.0f)
#define FLOOR_SCUFF_MASK_FULL (0.70f)
#define FLOOR_SCUFF_MASK_SCALE (0.012f)
#define FLOOR_SCUFF_MASK_SEED (2.0f)
#define FLOOR_SCUFF_DISTANCE_FILTER (0.6f)

// 壁際の埃は壁までの距離が増えるほど薄くする。
#define FLOOR_WALL_DUST_FALLOFF_DISTANCE (4.5f)

// 床の古さの値は #define にしている（型付きの定数にすると FXC の最適化が変わり、浮動小数点の丸めが変わるため）。
// 画素の大きさの下限（0で割らないため）と、ひびを描く画素の大きさの上限
#define FLOOR_MIN_FOOTPRINT (0.0001f)
#define FLOOR_CRACK_FOOTPRINT_LIMIT (0.5f)
// 油や水の染み：出る場所（出始め・濃くなる）とノイズの細かさ・ずらし、濃さのむら（最低＋幅、細かさ・ずらし）
#define FLOOR_STAIN_START (0.50f)
#define FLOOR_STAIN_FULL (0.82f)
#define FLOOR_STAIN_SCALE (0.03f)
#define FLOOR_STAIN_SEED (31.0f)
#define FLOOR_STAIN_SHADE_BASE (0.6f)
#define FLOOR_STAIN_SHADE_RANGE (0.4f)
#define FLOOR_STAIN_SHADE_SCALE (0.2f)
#define FLOOR_STAIN_SHADE_SEED (7.0f)
// 引きずった跡を出す場所の出始め
#define FLOOR_SCUFF_MASK_START (0.50f)
// へこみの高さ（目地・ひび）
#define FLOOR_JOINT_DEPTH (0.5f)
#define FLOOR_CRACK_DEPTH (0.4f)
// 色：床版の明るさ（最低＋むらの幅）、目地とひびの暗さ、染みの色と混ぜ方、引きずった跡の明るさと混ぜ方、埃の色と混ぜ方
#define FLOOR_SLAB_TINT_BASE (0.93f)
#define FLOOR_SLAB_TINT_RANGE (0.12f)
#define FLOOR_JOINT_DARKEN (0.22f)
#define FLOOR_CRACK_DARKEN (0.50f)
#define FLOOR_STAIN_COLOR (float3(0.55f, 0.52f, 0.48f))
#define FLOOR_STAIN_BLEND (0.75f)
#define FLOOR_SCUFF_BRIGHTEN (1.18f)
#define FLOOR_SCUFF_BLEND (0.50f)
#define FLOOR_DUST_COLOR (float3(1.30f, 1.25f, 1.15f))
#define FLOOR_DUST_BLEND (0.55f)

FloorAgeing ComputeFloorAgeing(float2 position, float amount)
{
    FloorAgeing ageing;
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている。
    const float footprint = max(length(fwidth(position)), FLOOR_MIN_FOOTPRINT);

    // --- 床版の目地（56四方。細く浅くして、タイル張りに見えないようにしている）。床版ごとに色をわずかに変えている
    const float slabSize = FLOOR_SLAB_SIZE;
    const float2 slabUV = position / slabSize;
    const float2 edgeDistance = (0.5f - abs(frac(slabUV) - 0.5f)) * slabSize;
    const float joint = 1.0f - smoothstep(FLOOR_JOINT_CORE, FLOOR_JOINT_EDGE + footprint, min(edgeDistance.x, edgeDistance.y));
    const float slabShade = FastHash21(floor(slabUV) + FLOOR_SLAB_TINT_SEED);

    // --- ひび（遠くでは1画素より細くなるため、計算を省いている）
    float crack = 0.0f;
    [branch]
    if (footprint < FLOOR_CRACK_FOOTPRINT_LIMIT)
    {
        const float crackLine = 1.0f - smoothstep(0.0f, FLOOR_CRACK_LINE_WIDTH + footprint * FLOOR_CRACK_LINE_FILTER,
            abs(FastValueNoise(position * FLOOR_CRACK_NOISE_SCALE + FLOOR_CRACK_NOISE_SEED) - 0.5f));
        crack = crackLine * smoothstep(FLOOR_CRACK_MASK_START, FLOOR_CRACK_MASK_FULL, FastValueNoise(position * FLOOR_CRACK_MASK_SCALE + FLOOR_CRACK_MASK_SEED)) *
            saturate(1.0f - footprint / FLOOR_CRACK_DISTANCE_FILTER);
    }

    // --- 油や水の染み（大きな黒ずみ）
    const float stain = smoothstep(FLOOR_STAIN_START, FLOOR_STAIN_FULL, FastValueNoise(position * FLOOR_STAIN_SCALE + FLOOR_STAIN_SEED)) *
        (FLOOR_STAIN_SHADE_BASE + FLOOR_STAIN_SHADE_RANGE * FastValueNoise(position * FLOOR_STAIN_SHADE_SCALE + FLOOR_STAIN_SHADE_SEED));

    // --- 物を引きずった跡（一方向に伸びた細い筋。出る場所を別のノイズで絞っている）
    const float scuffLine = smoothstep(FLOOR_SCUFF_LINE_START, FLOOR_SCUFF_LINE_FULL,
        FastValueNoise(float2(position.x * FLOOR_SCUFF_NOISE_X, position.y * FLOOR_SCUFF_NOISE_Z) + FLOOR_SCUFF_NOISE_SEED));
    const float scuff = scuffLine * smoothstep(FLOOR_SCUFF_MASK_START, FLOOR_SCUFF_MASK_FULL, FastValueNoise(position * FLOOR_SCUFF_MASK_SCALE + FLOOR_SCUFF_MASK_SEED)) *
        saturate(1.0f - footprint / FLOOR_SCUFF_DISTANCE_FILTER);

    // --- 壁際にたまった埃（部屋の角の暗がりと同じ壁の形から、壁までの距離を求めている）
    const float dust = exp(-GetNearestWallDistance(position) / FLOOR_WALL_DUST_FALLOFF_DISTANCE);

    // 目地とひびを、へこみの高さにしている
    ageing.height = -(joint * FLOOR_JOINT_DEPTH + crack * FLOOR_CRACK_DEPTH) * amount;

    // 床版ごとの色、目地・ひびの暗さ、染み、引きずった跡、埃を順に重ねている
    float3 tint = (FLOOR_SLAB_TINT_BASE + FLOOR_SLAB_TINT_RANGE * slabShade).xxx;
    tint *= 1.0f - joint * FLOOR_JOINT_DARKEN;
    tint *= 1.0f - crack * FLOOR_CRACK_DARKEN;
    tint = lerp(tint, tint * FLOOR_STAIN_COLOR, stain * FLOOR_STAIN_BLEND);
    tint = lerp(tint, tint * FLOOR_SCUFF_BRIGHTEN, scuff * FLOOR_SCUFF_BLEND);
    tint = lerp(tint, FLOOR_DUST_COLOR, dust * FLOOR_DUST_BLEND);
    ageing.tint = lerp(1.0f.xxx, tint, amount);
    return ageing;
}

// 床の古さを返している（古さが0なら何も計算しない）
FloorAgeing GetFloorAgeing(float3 worldPosition, float amount)
{
    FloorAgeing ageing;
    ageing.height = 0.0f;
    ageing.tint = 1.0f.xxx;
    // amount は面ごとに一定なので、この分岐は画素ごとにばらつかない。
    [branch]
    if (amount > 0.0f)
    {
        ageing = ComputeFloorAgeing(worldPosition.xz, amount);
    }
    return ageing;
}

// ホラー向けの色調と、遠くの闇の色（litTexturePSと共通。horrorGrade.hlsli）
#include "horrorGrade.hlsli"
#include "horrorMist.hlsli"
#include "figureShadow.hlsli"
#include "rayReflection.hlsli"
#include "figureVolume.hlsli"

// 2面の「水たまり」の異変の人影（PuddleFigure。xyz = 足もと、w = 見え方）が、反射の光線をどれだけ遮るか（0〜1）を返している。
// 人影のモデルと同じ形の距離場に光線を通し（figureVolume.hlsli）、形に一番近づいた距離から、輪郭のぼけた影の濃さを求めている。
// 人影は目の方を向き、頭が足もとから36の高さになる大きさにしている（PuddleReflectionAnomaly::FigureHeadHeight と合わせている）。
// 物として置いていないので、画面にも鏡にも映らず、レイ反射の水面にだけ映る（PuddleReflectionAnomaly.h）
// PuddleFigure.w が負のときは、1面の西棟の天井に逆さまに張り付いた人影（足もとが天井で、頭が下）として、同じ形を上下逆に描いている
// 水面に映る人影：見えていないとみなす見え方、胸の高さ（囲む球の中心）と球の半径、頭の高さ（PuddleReflectionAnomaly::FigureHeadHeight と同じ）、
// 向きを決めるときに足すわずかなずれ（目の真上でも向きが決まるように）、距離場の輪郭のぼけ
static const float FIGURE_MIN_VISIBILITY = 0.001f;
static const float FIGURE_CHEST_HEIGHT = 19.0f;
static const float FIGURE_BOUND_RADIUS = 28.0f;
static const float FIGURE_HEAD_HEIGHT = 36.0f;
static const float3 FIGURE_FORWARD_NUDGE = float3(1e-4f, 0.0f, 0.0f);
static const float FIGURE_EDGE_SOFTNESS = 1.6f;

float TracePuddleFigure(float3 origin, float3 direction)
{
    const float visibility = abs(PuddleFigure.w);
    [branch]
    if (visibility <= FIGURE_MIN_VISIBILITY)
    {
        return 0.0f;
    }
    const float3 foot = PuddleFigure.xyz;
    // 足もとから頭への向き（床に立つ人影は上、天井に張り付いた人影は下）
    const float upSign = PuddleFigure.w < 0.0f ? -1.0f : 1.0f;
    // 人影を囲む球（中心は胸の高さ、半径28）のそばを通らない光線は、点と光線の距離だけで先に飛ばしている
    const float3 toCenter = foot + float3(0.0f, upSign * FIGURE_CHEST_HEIGHT, 0.0f) - origin;
    const float alongCenter = max(dot(toCenter, direction), 0.0f);
    const float3 missCenter = toCenter - direction * alongCenter;
    [branch]
    if (dot(missCenter, missCenter) > FIGURE_BOUND_RADIUS * FIGURE_BOUND_RADIUS)
    {
        return 0.0f;
    }
    // 人影の空間（前 = 目の方、右、上）へ光線を移し、大きさで割っている。天井の人影は上下を反転している
    const float scale = FIGURE_HEAD_HEIGHT / FIGURE_VOLUME_HEAD_HEIGHT;
    const float2 toEye = GetMistEyePosition().xz - foot.xz;
    const float3 forward = normalize(float3(toEye.x, 0.0f, toEye.y) + FIGURE_FORWARD_NUDGE);
    const float3 right = float3(forward.z, 0.0f, -forward.x);
    const float3 fromFoot = (origin - foot) / scale;
    const float3 localOrigin = float3(dot(fromFoot, right), fromFoot.y * upSign, dot(fromFoot, forward));
    const float3 localDirection = float3(dot(direction, right), direction.y * upSign, dot(direction, forward));
    float depth;
    return TraceFigureVolume(localOrigin, localDirection, FIGURE_EDGE_SOFTNESS, depth) * saturate(visibility);
}

// 水たまりのSSR：光線をたどる回数と、1歩ごとに歩幅を広げる割合
// 波紋で反射像が揺れる強さ。傾きを制限して、反射が大きく飛ばないようにする。
static const float PUDDLE_REFLECTION_SLOPE_SCALE = 0.28f;
static const float PUDDLE_REFLECTION_MAX_SLOPE = 0.045f;

// 水を正面から見たときと斜めから見たときの反射を調整し、汚れのある箇所では弱める。
static const float PUDDLE_REFLECTION_FACE = 0.07f;
static const float PUDDLE_REFLECTION_NEAR_WALL = 0.62f;
static const float PUDDLE_REFLECTION_GRAZING_START = 0.04f;
static const float PUDDLE_REFLECTION_GRAZING_END = 0.27f;
static const float PUDDLE_REFLECTION_GRAZING = 0.72f;
static const float PUDDLE_REFLECTION_GRIME_MIN = 0.80f;
static const float PUDDLE_REFLECTION_GRIME_START = 0.25f;
static const float PUDDLE_REFLECTION_GRIME_END = 0.75f;

// 反射が強いところでは、映り込みを読み取れるよう流れる霧を少し薄める。
static const float PUDDLE_REFLECTION_MIST_REDUCTION = 0.16f;

// 水面は正面から見たときに反射を抑え、浅い角度で見たときに強く出す。
static const float PUDDLE_FRESNEL_EXPONENT = 4.0f;

// 法線の上向き成分で、床の水・岸・濡れた足跡を同じ範囲に制限する。
static const float WET_FLOOR_UPWARD_START = 0.55f;
static const float WET_FLOOR_UPWARD_FULL = 0.92f;

// 水面SSRは反射距離でぼけを増やし、床への自己交差を除外する。
static const float WATER_SSR_DISTANCE_PER_MIP = 85.0f;
static const float WATER_SSR_BASE_BLUR = 0.55f;
static const float WATER_SSR_MIN_HIT_HEIGHT = 1.5f;
// 通常のSSRと、レイ反射を補うSSRはそれぞれの歩数と歩幅を使う。
static const int WATER_SSR_STEP_COUNT = 40;
static const float WATER_SSR_STEP_GROWTH = 1.12f;
// 遠くでも波紋が分かるよう、距離に応じて波の傾きと光沢を強める。
static const float WATER_RIPPLE_FAR_BOOST = 1.5f;
static const float WATER_RIPPLE_BOOST_START_DISTANCE = 40.0f;
static const float WATER_RIPPLE_BOOST_FULL_DISTANCE = 240.0f;

// 排水が進む境界をなだらかにし、残った水の周囲に湿った床を出す。
static const float DRAIN_WATER_EDGE_BLEND_DISTANCE = 5.0f;
static const float DRAIN_POOL_EDGE_START = 0.55f;
static const float DRAIN_DAMP_EDGE_START = 0.35f;

static const int SsrStepCount = 48;
static const float SsrStepGrowth = 1.10f;
// 水たまりのSSR：カメラからこの距離の間で、SSRの映り込みを薄めて消している（遠くは部屋の形と環境マップで映す）
static const float SsrFadeStartDistance = 160.0f;
static const float SsrFadeEndDistance = 230.0f;
// 部屋の形の反射を霧の色へなじませる距離（始め・終わり）
static const float ROOM_REFLECTION_FADE_START = 300.0f;
static const float ROOM_REFLECTION_FADE_END = 380.0f;
// 反射の光線が床へ潜らないよう上へ向ける下限と、光線を出す位置を水面から上げる量
static const float REFLECTION_MIN_UPWARD = 0.02f;
static const float REFLECTION_ORIGIN_LIFT = 0.05f;
// SSRが当たった所で、画面の色を混ぜる割合
static const float SSR_SCENE_WEIGHT = 0.85f;
// 水面に映る人影の暗さ（霧の色に掛ける）
static const float FIGURE_REFLECTION_SHADE = 0.12f;

// レイ反射の本体：部屋の形との交差を基準にし、近くで前後の画面が一致する所だけSSRの細部を足す。
// SSRは深度プリパスの深度と前のフレームの画面を使うため、それが用意されているとき（ReflectionParams.y）だけ使っている。
// figure は「水たまり」の異変の人影が光線を遮った度合い（0〜1）で、床の色そのものも沈めるのに使っている。
// 同じ位置の水面には、重なったメッシュのどちらから描いても同じ距離で反射を求める。
float3 TraceRayReflection(float3 worldPosition, float3 worldNormal, float roughness, float distanceFromCamera,
    out float figure, out float nearVertical)
{
    figure = 0.0f;
    nearVertical = 0.0f;
    // 遠くでは部屋の反射を霧の色へなじませる。技術デモでは距離による簡略化を切り替えられる。
    const float roomWeight = LeakInfo.y > FLOOR_FLAG_THRESHOLD ? 1.0f - smoothstep(ROOM_REFLECTION_FADE_START, ROOM_REFLECTION_FADE_END, distanceFromCamera) : 1.0f;
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    [branch]
    if (roomWeight <= 0.0f)
    {
        return fogColor;
    }
    float3 direction = reflect(normalize(worldPosition - GetMistEyePosition()), normalize(worldNormal));
    // 波の法線で光線が床へ潜らないよう、少しだけ上へ向けている
    direction = normalize(float3(direction.x, max(direction.y, REFLECTION_MIN_UPWARD), direction.z));
    float roomDistance;
    // 画面の外の物は、部屋の形に当たった位置から、環境マップ（本物の景色）の色を読んでいる（rayReflection.hlsli）
    float3 color = TraceRoomReflection(worldPosition + float3(0.0f, REFLECTION_ORIGIN_LIFT, 0.0f), direction,
        roughness, roomDistance, nearVertical, true);
    // SSRの寄与は画素までの距離で薄め、薄くなりきった場所では光線をたどる計算も省く。
    // 60fpsに抑えて空いたGPUの時間で、SSRで本物の景色を映す範囲を、カメラから105〜155から160〜230へ広げている
    const float nearWeight = 1.0f - smoothstep(SsrFadeStartDistance, SsrFadeEndDistance, distanceFromCamera);
    [branch]
    if (nearWeight > 0.0f && ReflectionParams.y > FLOOR_FLAG_THRESHOLD)
    {
        // 近い水たまりは、画面の中を細かい歩幅で多めにたどっている（48歩、1歩ごとに歩幅を1.10倍。届く距離は約146）。
        // 歩幅が粗いと光線が物をすり抜けて外れ、部屋の形の簡単な色で埋めることになり、映り込みが濁るため。
        // 以前の32歩・1.14倍（届く距離は約99）より歩幅が細かく、遠くの物まで届く。
        // 映る物が水面から離れているほど、前フレームの画面のぼやけた段を読む（距離85で1段、最大3段）。
        // 粗い水面（浅い縁や波の強い所）は、最初から少しぼかしている
        // 水面より1.5以上高い物に当たったときだけ映し、床そのものへの誤った当たりは捨てている（かくかくした映り込みを防ぐ）
        const float4 screen = TraceScreenSpaceReflection(worldPosition, worldNormal, SsrStepCount, SsrStepGrowth,
            1.0f / WATER_SSR_DISTANCE_PER_MIP, WATER_SSR_BASE_BLUR + roughness, WATER_SSR_MIN_HIT_HEIGHT);
        // 画面に見えている物は、部屋の形で近づけた色より本物なので、SSRが当たった所はその色を主にしている
        color = lerp(color, screen.rgb, screen.w * nearWeight * SSR_SCENE_WEIGHT);
        // SSRが水面より上に立つ物（ポンプ・机・扉など）に当たったときは、箱で映す小物と同じく「近くの縦の面」として扱い、
        // 見下ろす角度でも輪郭が読める強さで映している（これがないと、見下ろしたときの映り込みは約7%しか混ざらない）
        nearVertical = max(nearVertical, screen.w * nearWeight);
    }
    // 人影は画面に描いていないため、SSRで拾った物より手前に立っているものとして、最後に重ねている（ほぼ黒い影）。
    // 人の形が崩れないよう、人影も細かい凹凸を除いた同じ向きで映す。
    const float3 calmNormal = normalize(worldNormal);
    float3 figureDirection = reflect(normalize(worldPosition - GetMistEyePosition()), calmNormal);
    figureDirection = normalize(float3(figureDirection.x, max(figureDirection.y, REFLECTION_MIN_UPWARD), figureDirection.z));
    figure = TracePuddleFigure(worldPosition + float3(0.0f, REFLECTION_ORIGIN_LIFT, 0.0f), figureDirection);
    color = lerp(color, fogColor * FIGURE_REFLECTION_SHADE, figure);
    figure *= roomWeight;
    nearVertical *= roomWeight;
    return lerp(fogColor, color, roomWeight);
}

// 2次元の値から、0〜1の乱数を2つ作っている
// GroundWaterEffects.cpp の Hash22OffsetA/B と同じ2組のずらしを使う。
static const float2 WATER_HASH22_OFFSET_A = float2(17.3f, 41.7f);
static const float2 WATER_HASH22_OFFSET_B = float2(93.1f, 11.8f);

// 水滴を一部のマスにだけ作り、波紋が薄くなりながら広がる範囲を決める。
static const float WATER_DRIP_CELL_SIZE = 15.0f;
static const float WATER_DRIP_CELL_MARGIN = 0.18f;
static const float WATER_DRIP_CENTER_RANGE = 0.64f;
static const float2 WATER_DRIP_SEED_OFFSET = float2(31.7f, 19.3f);
static const float WATER_DRIP_ACTIVITY_THRESHOLD = 0.76f;
static const float WATER_DRIP_CYCLES_PER_SECOND = 0.11f;
static const float WATER_DRIP_RING_MAX_RADIUS = 0.48f;
static const float WATER_DRIP_RING_CORE = 0.012f;
static const float WATER_DRIP_RING_EDGE = 0.035f;

float2 Hash22(float2 value)
{
    const float first = Hash21(value + WATER_HASH22_OFFSET_A);
    const float second = Hash21(value + WATER_HASH22_OFFSET_B);
    return float2(first, second);
}

// 水たまりに落ちる水滴の、広がる輪を計算している（0〜1）
float GetDripRing(float2 worldPosition)
{
    // 水滴はまばらに起こし、雨には見せずに、水たまりへ小さな動きを加えている。
    const float cellSize = WATER_DRIP_CELL_SIZE;
    const float2 cell = floor(worldPosition / cellSize);
    const float2 localPosition = frac(worldPosition / cellSize);
    const float2 dropPosition = WATER_DRIP_CELL_MARGIN + Hash22(cell) * WATER_DRIP_CENTER_RANGE;
    const float dropSeed = Hash21(cell + WATER_DRIP_SEED_OFFSET);
    const float activeDrop = step(WATER_DRIP_ACTIVITY_THRESHOLD, dropSeed);
    const float phase = frac(WetTime * WATER_DRIP_CYCLES_PER_SECOND + dropSeed);
    const float radius = phase * WATER_DRIP_RING_MAX_RADIUS;
    const float distanceToDrop = length(localPosition - dropPosition);
    const float ring = 1.0f - smoothstep(
        WATER_DRIP_RING_CORE,
        WATER_DRIP_RING_EDGE,
        abs(distanceToDrop - radius));
    return ring * activeDrop * (1.0f - phase);
}

// --- 床の色の値（main。型付きの定数にすると FXC の最適化が変わるため #define にしている）
// 明るさを求める重み（Rec.709）、床の素材の色を灰色に寄せる明るさと強さ（古さに掛ける）、素材の色を残す割合
#define LUMINANCE_WEIGHTS float3(0.2126f, 0.7152f, 0.0722f)
#define FLOOR_GRAY_LEVEL (0.40f)
#define FLOOR_GRAY_STRENGTH (0.65f)
#define FLOOR_TEXTURE_KEEP (0.10f)
// 広い範囲の明るさのむら（細かさ・ずらし・暗い所・明るい所）と、少し緑がかった灰色
#define FLOOR_BROAD_SCALE (0.020f)
#define FLOOR_BROAD_SEED (6.4f)
#define FLOOR_BROAD_DARK (0.88f)
#define FLOOR_BROAD_BRIGHT (1.04f)
#define FLOOR_CONCRETE_TINT float3(0.72f, 0.75f, 0.73f)
// 目地とひびの凹凸の強さ
#define FLOOR_BUMP_STRENGTH (0.9f)
// 床一面が水に浸かった範囲：内側とみなす距離（出始め・出きる）、ゆるい波（x・z の細かさ、速さ、強さ）
#define FLOOD_EDGE_START (-0.5f)
#define FLOOD_EDGE_FULL (1.5f)
#define FLOOD_WAVE_SCALE_X (0.21f)
#define FLOOD_WAVE_SCALE_Z (0.13f)
#define FLOOD_WAVE_SPEED (0.5f)
#define FLOOD_WAVE_STRENGTH (0.35f)
// 雨漏りの水たまり：調べる範囲に足すゆとり、縁のゆがみ（細かさ・強さ）、半径の下限、水たまりの縁（ぼかし始め）、
// 岸の線（幅・位置・濃さ）、水滴の輪（間隔・速さ・強さ・遠くで弱める量）
#define LEAK_REACH_MARGIN (3.0f)
#define LEAK_EDGE_NOISE_SCALE (0.35f)
#define LEAK_EDGE_NOISE_STRENGTH (0.35f)
#define LEAK_MIN_RADIUS (0.01f)
#define LEAK_PUDDLE_EDGE (0.80f)
#define LEAK_SHORE_WIDTH (0.08f)
#define LEAK_SHORE_POSITION (0.97f)
#define LEAK_SHORE_STRENGTH (0.6f)
#define LEAK_RING_FREQUENCY (2.2f)
#define LEAK_RING_SPEED (2.6f)
#define LEAK_RING_STRENGTH (0.30f)
#define LEAK_RING_FALLOFF (0.25f)
// 水が引いた柱の脇：調べる範囲（x・z の半分）、縁のゆがみ（細かさ・強さ）、残った水の楕円（中心のずれと半径）、
// 残った水を水たまりにする割合、湿りの楕円（中心のずれと半径）
#define DRAIN_AREA_HALF_X (32.0f)
#define DRAIN_AREA_HALF_Z (27.0f)
#define DRAIN_EDGE_NOISE_SCALE (0.22f)
#define DRAIN_EDGE_NOISE_STRENGTH (0.28f)
#define DRAIN_POOL_CENTER float2(13.0f, -8.0f)
#define DRAIN_POOL_RADIUS float2(13.0f, 8.0f)
#define DRAIN_POOL_PUDDLE (0.92f)
#define DRAIN_DAMP_CENTER float2(7.0f, -5.0f)
#define DRAIN_DAMP_RADIUS float2(23.0f, 18.0f)
// 足跡を出さない水たまりの濃さ（水たまりの濃さに掛ける）
#define FOOTPRINT_PUDDLE_EXCLUDE (2.0f)
// 水たまり・岸とみなす濃さの下限（これより薄いと波の計算を省く）。光と反射の計算の下限も同じ
#define WATER_EPSILON (0.001f)
// 波：水滴の輪を波の模様に足す量、流れるノイズの速さ（x・z）、細かさ、傾きを求めるずらし、
// 平らな面へ寄せる強さ、凹凸の強さ（最低＋波の模様の幅）、本物の波を模様に足す量と法線を傾ける強さ
#define DRIP_RING_PATTERN (1.35f)
#define RIPPLE_FLOW_X (0.018f)
#define RIPPLE_FLOW_Z (0.013f)
#define RIPPLE_NOISE_SCALE (0.095f)
#define RIPPLE_SLOPE_OFFSET (0.035f)
#define PUDDLE_FLATTEN_GAIN (1.5f)
#define RIPPLE_BUMP_BASE (0.72f)
#define RIPPLE_BUMP_PATTERN (0.55f)
#define SIMULATED_WAVE_PATTERN (1.4f)
#define SIMULATED_WAVE_TILT (1.6f)
// 濡れた床の色：水を通して透ける床の暗さ（浅い所・深い所）、水の色と明るさに足す量、混ぜ方、水たまりの濃さに掛ける割合
#define WET_SHALLOW_SHADE (0.76f)
#define WET_DEEP_SHADE (0.58f)
#define WET_WATER_TINT float3(0.018f, 0.030f, 0.034f)
#define WET_LUMINANCE_LIFT (0.035f)
#define WET_DEPTH_BLEND (0.38f)
#define WET_PUDDLE_BLEND (0.84f)
// 暗くする量：岸の線、水が引いた後の湿り、残った水
#define SHORE_DARKEN (0.035f)
#define DRAINED_DAMP_DARKEN (0.24f)
#define REMAINING_WATER_DARKEN (0.15f)
// 濡れた足跡：暗さ、水の色、混ぜ方
#define FOOTPRINT_SHADE (0.38f)
#define FOOTPRINT_WATER_TINT float3(0.004f, 0.007f, 0.008f)
#define FOOTPRINT_BLEND (0.90f)
// 点光源（litTexturePS と同じ）：なだらかな当たり方（最低＋割合）、横へ残す光の量
#define SOFT_LAMBERT_BASE (0.20f)
#define SOFT_LAMBERT_RANGE (0.80f)
#define FIXTURE_SIDE_LIGHT (0.22f)
// 水たまりの点光源の光沢：鋭さと強さ。天井の照明が水面に映る範囲（光の届く距離の割合）、鋭さ、強さ
#define POINT_GLOSS_EXPONENT (12.0f)
#define POINT_GLOSS_STRENGTH (0.58f)
#define FIXTURE_REFLECTION_RADIUS (0.46f)
#define FIXTURE_REFLECTION_EXPONENT (4.5f)
#define FIXTURE_REFLECTION_STRENGTH (0.38f)
// 懐中電灯：なだらかな当たり方（最低＋割合）、水面で反射へ回す分（水たまりで残す割合）
#define FLASHLIGHT_SOFT_LAMBERT_BASE (0.25f)
#define FLASHLIGHT_SOFT_LAMBERT_RANGE (0.75f)
#define FLASHLIGHT_PUDDLE_DIFFUSE (0.74f)
// 濡れた面の鏡面反射の鋭さ（乾いた床・湿りで下げる量・水たまり）と強さ（乾いた床・湿りで足す量・水たまり）
#define WET_SPECULAR_DRY_EXPONENT (18.0f)
#define WET_SPECULAR_DAMP_EXPONENT (6.0f)
#define WET_SPECULAR_PUDDLE_EXPONENT (92.0f)
#define WET_SPECULAR_DRY (0.025f)
#define WET_SPECULAR_DAMP (0.035f)
#define WET_SPECULAR_PUDDLE (0.20f)
// 濡れた足跡の光沢（鋭さと強さ）、浅い水の広い反射（最低＋フレネルの幅）、波の光の輪（傾きに掛ける量と強さ）
#define FOOTPRINT_GLOSS_EXPONENT (48.0f)
#define FOOTPRINT_GLOSS_STRENGTH (0.55f)
#define SHALLOW_REFLECTION_BASE (0.025f)
#define SHALLOW_REFLECTION_FRESNEL (0.08f)
#define WAVE_GLINT_SLOPE_GAIN (3.0f)
#define WAVE_GLINT_STRENGTH (0.40f)
// 比べ用の反射の重み、人影で水面を沈める量（水たまりの濃さに掛ける量と暗さ）
#define ALTERNATE_REFLECTION_WEIGHT (0.38f)
#define FIGURE_PUDDLE_GAIN (1.4f)
#define FIGURE_PUDDLE_DARKEN (0.82f)

// 床の画素の色を計算している：床の色と古さ → 水たまり → 照明 → 光沢 → 反射 → 霧 → 色調
// 水面と濡れた足跡は別の色と強さで光を返す。
#define PUDDLE_SKY_GLINT_TINT float3(0.12f, 0.15f, 0.16f)
#define PUDDLE_SKY_GLINT_BASE 0.07f
#define PUDDLE_SKY_GLINT_FRESNEL 0.54f
#define WET_FOOTPRINT_GLINT_TINT float3(0.08f, 0.10f, 0.11f)
#define WET_FOOTPRINT_GLINT_BASE 0.06f
#define WET_FOOTPRINT_GLINT_FRESNEL 0.55f

// 水滴の輪と波の傾き、岸の縁に明るさの差を付け、照明が弱くても水が分かるようにする。
#define PUDDLE_PATTERN_GLINT_EXPONENT 10.0f
#define PUDDLE_PATTERN_GLINT_TINT float3(0.12f, 0.16f, 0.17f)
#define PUDDLE_PATTERN_GLINT_GAIN 0.22f
#define PUDDLE_WAVE_GLINT_TINT float3(0.07f, 0.09f, 0.10f)
#define PUDDLE_WAVE_GLINT_BASE 0.30f
#define PUDDLE_WAVE_GLINT_FRESNEL 0.70f
#define PUDDLE_DRIP_GLINT_TINT float3(0.28f, 0.34f, 0.35f)
#define PUDDLE_DRIP_GLINT_GAIN 0.20f
#define PUDDLE_SHORE_GLINT_TINT float3(0.10f, 0.13f, 0.125f)
#define PUDDLE_SHORE_GLINT_BASE 0.24f
#define PUDDLE_SHORE_GLINT_FRESNEL 0.46f

// 水面の油膜と汚れに時間変化を与え、荒さは波と水の浅さでも変える。
#define PUDDLE_FILM_FLOW_X 0.006f
#define PUDDLE_FILM_FLOW_Z 0.004f
#define PUDDLE_FILM_NOISE_SCALE 0.045f
#define PUDDLE_GRIME_NOISE_SCALE 0.021f
#define PUDDLE_GRIME_NOISE_SEED 37.0f
#define PUDDLE_BASE_ROUGHNESS 0.18f
#define PUDDLE_FILM_ROUGHNESS_WEIGHT 0.50f
#define PUDDLE_WAVE_ROUGHNESS_WEIGHT 0.22f
#define PUDDLE_SHALLOW_MASK_GAIN 1.6f
#define PUDDLE_SHALLOW_ROUGHNESS_WEIGHT 0.35f

float4 main(in LIT_PS_IN input) : SV_Target
{
    float4 color = input.col;
    // 床の古さ（目地・ひび）の凹凸を入れた法線。古さのない面では元の法線のまま。
    float3 agedWorldNormal = normalize(input.worldNormal);

    if (Material.TextureEnable)
    {
        // 元の画像の草のような細かい模様を、正のMipバイアスで弱めようとしている。
        // 色の幅を抑えて、湿った汚いコンクリートに見せ、素材を足さずに済ませている。
        const float3 sampledFloor = g_Texture.SampleBias(
            g_SamplerState,
            input.tex,
            FLOOR_TEXTURE_MIP_BIAS).rgb;
        // 1面（床の古さあり）は、素材の明るさを平均（field.jpgで約0.40）へ寄せて砂利のようなざらつきを抑え、
        // 床の模様は目地・染み・引きずった跡（GetFloorAgeing）で作っている。
        // 素材にMipマップがないため、Mipバイアスではぼかせない。
        const float floorLuminance = lerp(
            dot(sampledFloor, LUMINANCE_WEIGHTS),
            FLOOR_GRAY_LEVEL,
            FLOOR_GRAY_STRENGTH * WallWeathering);
        float3 concreteFloor = lerp(
            floorLuminance.xxx,
            sampledFloor,
            FLOOR_TEXTURE_KEEP);
        // 広い範囲の明るさのむらを付け、少し緑がかった灰色にしている
        const float broadVariation = saturate(FastValueNoise(
            input.worldPos.xz * FLOOR_BROAD_SCALE + FLOOR_BROAD_SEED));
        concreteFloor *= FLOOR_CONCRETE_TINT *
            lerp(FLOOR_BROAD_DARK, FLOOR_BROAD_BRIGHT, broadVariation);
        color.rgb *= concreteFloor;

        // 床の古さ（WallWeathering が0より大きい面だけ）。色を掛け、目地とひびの凹凸で法線を作っている。
        // TextureEnable は描画の単位で一定なので、この中で画面上の偏微分を使っても問題ない。
        const FloorAgeing floorAgeing = GetFloorAgeing(input.worldPos, WallWeathering);
        color.rgb *= floorAgeing.tint;
        agedWorldNormal = ApplyHeightToNormal(
            input.worldPos, agedWorldNormal, floorAgeing.height, FLOOR_BUMP_STRENGTH);
    }
    else
    {
        color *= Material.Diffuse;
    }

    if (IsFullbrightDebugView() && DebugViewMode != DEBUG_VIEW_FULLBRIGHT_REFLECTION)
    {
        return GetFullbrightColor(color.rgb, input.viewNormal, input.viewPos);
    }

    // 水たまりの濃さ・岸の線・波の模様を求めている。
    // 床の本体（WaterSurfaceLevel が0）は乾いた床として描き、水たまり・浸水・雨漏りは、その形を覆うメッシュ（Ground::DrawWaterSurfaces）が描き重ねる。
    // メッシュは床より少し上にあり、深度プリパスにも描くので、メッシュの下の床の画素はこの計算まで来ない
    const int waterLevel = (int)(WaterSurfaceLevel + 0.5f);
    float shore = 0.0f;
    float ripplePattern = 0.0f;
    float puddle = 0.0f;
    [branch]
    if (waterLevel > WATER_LEVEL_DRY_FLOOR)
    {
        puddle = GetPuddleMask(
            input.worldPos.xz,
            WetTime,
            shore,
            ripplePattern);
        // 床一面が水に浸かった範囲（1面の西棟）。範囲の内側は全面を水たまりと同じに扱い、ゆるい波を立てている。
        {
            const float2 fromMinimum = input.worldPos.xz - FloodRect.xy;
            const float2 toMaximum = FloodRect.zw - input.worldPos.xz;
            const float insideDistance = min(
                min(fromMinimum.x, fromMinimum.y), min(toMaximum.x, toMaximum.y));
            const float flood = smoothstep(FLOOD_EDGE_START, FLOOD_EDGE_FULL, insideDistance);
            ripplePattern += sin(input.worldPos.x * FLOOD_WAVE_SCALE_X + input.worldPos.z * FLOOD_WAVE_SCALE_Z + WetTime * FLOOD_WAVE_SPEED) *
                FLOOD_WAVE_STRENGTH * flood * (1.0f - puddle);
            puddle = max(puddle, flood);
        }
        // 天井の雨漏りの真下にできた小さな水たまり（少しずつ広がる。縁はノイズでゆがめ、落ちてくる水滴の輪が広がる）
        {
            const int leakCount = (int)(LeakInfo.x + 0.5f);
            [loop]
            for (int leakIndex = 0; leakIndex < leakCount; ++leakIndex)
            {
                const float4 leak = Leaks[leakIndex];
                const float2 leakOffset = input.worldPos.xz - leak.xy;
                const float reach = leak.z + LEAK_REACH_MARGIN;
                if (dot(leakOffset, leakOffset) > reach * reach)
                {
                    continue;
                }
                const float leakDistance = length(leakOffset);
                const float edgeNoise = (FastValueNoise(input.worldPos.xz * LEAK_EDGE_NOISE_SCALE + leak.xy) - 0.5f) * LEAK_EDGE_NOISE_STRENGTH;
                const float normalizedDistance = leakDistance / max(leak.z, LEAK_MIN_RADIUS) + edgeNoise;
                const float leakPuddle = 1.0f - smoothstep(LEAK_PUDDLE_EDGE, 1.0f, normalizedDistance);
                shore = max(shore, (1.0f - smoothstep(0.0f, LEAK_SHORE_WIDTH, abs(normalizedDistance - LEAK_SHORE_POSITION))) * LEAK_SHORE_STRENGTH);
                ripplePattern += sin(leakDistance * LEAK_RING_FREQUENCY - WetTime * LEAK_RING_SPEED) * LEAK_RING_STRENGTH * leakPuddle * exp(-leakDistance * LEAK_RING_FALLOFF);
                puddle = max(puddle, leakPuddle);
            }
        }
    }
    // 柱の脇に残る薄い水と、その周りの水を吸ったコンクリート。水が通り過ぎた画素だけに出す。
    float drainedDamp = 0.0f;
    float remainingWater = 0.0f;
    const float2 fromDrainColumn = input.worldPos.xz - DrainTrace.xy;
    [branch]
    if (DrainTrace.w > 0.0f && DrainTrace.z > input.worldPos.z &&
        abs(fromDrainColumn.x) < DRAIN_AREA_HALF_X && abs(fromDrainColumn.y) < DRAIN_AREA_HALF_Z)
    {
        const float exposed = smoothstep(0.0f, DRAIN_WATER_EDGE_BLEND_DISTANCE, DrainTrace.z - input.worldPos.z);
        const float edgeNoise = (FastValueNoise(input.worldPos.xz * DRAIN_EDGE_NOISE_SCALE) - 0.5f) * DRAIN_EDGE_NOISE_STRENGTH;
        const float poolDistance = length((fromDrainColumn - DRAIN_POOL_CENTER) / DRAIN_POOL_RADIUS) + edgeNoise;
        remainingWater = (1.0f - smoothstep(DRAIN_POOL_EDGE_START, 1.0f, poolDistance)) * exposed;
        puddle = max(puddle, remainingWater * DRAIN_POOL_PUDDLE);
        const float dampDistance = length((fromDrainColumn - DRAIN_DAMP_CENTER) / DRAIN_DAMP_RADIUS) + edgeNoise;
        drainedDamp = (1.0f - smoothstep(DRAIN_DAMP_EDGE_START, 1.0f, dampDistance)) * exposed * (1.0f - remainingWater);
    }
    // 上を向いた面（床）だけを水たまりにしている
    const float upwardMask = smoothstep(WET_FLOOR_UPWARD_START, WET_FLOOR_UPWARD_FULL,
        saturate(input.worldNormal.y));
    puddle *= upwardMask;
    shore *= upwardMask;
    // 濡れた足跡（水たまりの外の乾いた床だけ。WetFootprints が場所と濡れ具合を渡している）
    const float footprintWet = GetWetFootprints(input.worldPos.xz) *
        upwardMask * (1.0f - saturate(puddle * FOOTPRINT_PUDDLE_EXCLUDE));

    float dripRing = 0.0f;
    float3 detailWorldNormal = agedWorldNormal;
    // 足音や水滴でできた波（WaterRippleSimulation）の高さ（x）と傾き（yz）
    float3 rippleWave = 0.0f;
    // 動く波の法線を作り直す処理は、水たまりと細い岸だけにしている。
    // 乾いた床では、3回のノイズの計算を省いている。
    [branch]
    if (puddle > WATER_EPSILON || shore > WATER_EPSILON)
    {
        dripRing = GetDripRing(input.worldPos.xz) * puddle;
        ripplePattern += dripRing * DRIP_RING_PATTERN;
        const float2 animatedNoiseOffset = float2(
            WetTime * RIPPLE_FLOW_X,
            -WetTime * RIPPLE_FLOW_Z);
        // 少しずつ流れるノイズの傾きから、波の凹凸の法線を作っている
        const float ripple = FastValueNoise(
            input.worldPos.xz * RIPPLE_NOISE_SCALE + animatedNoiseOffset);
        const float rippleX = FastValueNoise(
            input.worldPos.xz * RIPPLE_NOISE_SCALE + animatedNoiseOffset +
            float2(RIPPLE_SLOPE_OFFSET, 0.0f));
        const float rippleZ = FastValueNoise(
            input.worldPos.xz * RIPPLE_NOISE_SCALE + animatedNoiseOffset +
            float2(0.0f, RIPPLE_SLOPE_OFFSET));
        // 水たまりの中は、目地やひびが水で埋まるため、平らな面から波の凹凸を作っている。
        const float3 puddleBaseNormal = normalize(lerp(
            agedWorldNormal, normalize(input.worldNormal), saturate(puddle * PUDDLE_FLATTEN_GAIN)));
        detailWorldNormal = normalize(
            puddleBaseNormal +
            float3(ripple - rippleX, 0.0f, ripple - rippleZ) *
            (RIPPLE_BUMP_BASE + abs(ripplePattern) * RIPPLE_BUMP_PATTERN) * puddle * RippleStrength);

        // 足音や水滴でできた本物の波。広がって重なり、岸で跳ね返る輪の傾きで法線を傾け、
        // 波の山を光らせている（波の模様に足すと、下の光沢の計算で輪が光る）。
        rippleWave = GetRippleHeightAndSlope(input.worldPos.xz) * puddle;
        ripplePattern += rippleWave.x * SIMULATED_WAVE_PATTERN;
        detailWorldNormal = normalize(
            detailWorldNormal + float3(-rippleWave.y, 0.0f, -rippleWave.z) * SIMULATED_WAVE_TILT);
    }

    const float baseLuminance = dot(
        color.rgb,
        LUMINANCE_WEIGHTS);
    // 浅い室内の水は、上から見たときに床を透かして見せる。
    // 暗くしすぎて、黒いシールのように見えるのを防いでいる。
    const float3 wetColor = lerp(
        color.rgb * WET_SHALLOW_SHADE,
        color.rgb * WET_DEEP_SHADE +
            WET_WATER_TINT + baseLuminance.xxx * WET_LUMINANCE_LIFT,
        WET_DEPTH_BLEND);
    color.rgb = lerp(color.rgb, wetColor, puddle * WET_PUDDLE_BLEND);
    color.rgb *= 1.0f - shore * SHORE_DARKEN - drainedDamp * DRAINED_DAMP_DARKEN - remainingWater * REMAINING_WATER_DARKEN;
    // 濡れた足跡は、水を吸って暗くなっている
    color.rgb = lerp(color.rgb, color.rgb * FOOTPRINT_SHADE + FOOTPRINT_WATER_TINT, footprintWet * FOOTPRINT_BLEND);

    // 上下で色を変えた環境光と DDGI の間接光（壁や床で跳ね返った光）と、浅い角度ほど強い反射（フレネル）
    float3 lighting = GetHemisphereAmbient(detailWorldNormal) + GetScreenIndirectLight(input.pos.xy);
    float3 specularLighting = 0.0f;
    const float distanceFromCamera = length(input.viewPos);
    const float3 viewDirection = distanceFromCamera > FLOOR_MIN_DIVISOR
        ? -input.viewPos / distanceFromCamera
        : float3(0.0f, 1.0f, 0.0f);
    // 離れるほど波の輪が細く見えるので、距離40から240にかけて輪の光り方を最大2.5倍まで強め、遠くの水たまりでも雨の輪が分かるようにしている
    const float farRippleBoost = 1.0f + WATER_RIPPLE_FAR_BOOST * smoothstep(WATER_RIPPLE_BOOST_START_DISTANCE, WATER_RIPPLE_BOOST_FULL_DISTANCE, distanceFromCamera);
    const float fresnel = pow(
        1.0f - saturate(dot(normalize(input.viewNormal), viewDirection)),
        PUDDLE_FRESNEL_EXPONENT);

    // 天井の点光源は、パネルだけでなく、近くの床と壁も照らしている。
    uint pointLightListOffset;
    const uint pointLightCount =
        GetPixelLightCount(input.pos.xy, pointLightListOffset);
    // 天井の照明の接触影（一番強く照らしている点光源の向きで求めてある）
    const float screenContactShadow = GetScreenContactShadow(input.pos.xy);
    [loop]
    for (uint i = 0; i < pointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight =
            GetPixelLight(pointLightListOffset, i);

        const float3 offsetToLight =
            pointLight.PositionRange.xyz - input.worldPos;
        const float lightRange = max(pointLight.PositionRange.w, WATER_EPSILON);
        const float distanceSquaredToLight = dot(offsetToLight, offsetToLight);
        // 光が届かない距離の光源は、計算を省いている
        [branch]
        if (distanceSquaredToLight >= lightRange * lightRange)
        {
            continue;
        }
        const float distanceToLight = sqrt(distanceSquaredToLight);
        const float3 directionToPointLight =
            offsetToLight / max(distanceToLight, WATER_EPSILON);
        float pointAttenuation = saturate(1.0f - distanceToLight / lightRange);
        pointAttenuation *= pointAttenuation;
        // 棚・机・ロッカーの足元の細い影（接触影。SSAOと一緒に求めた画像。screenAmbientOcclusion.hlsli）
        pointAttenuation *= screenContactShadow;
        // 光源との間に建物の壁があれば、光は届かない（壁を抜けて隣の部屋を照らさないように。roomOcclusion.hlsli）
        pointAttenuation *= GetRoomLightVisibility(input.worldPos, pointLight.PositionRange.xyz,
            GetPixelLightWallMask(pointLightListOffset, i));
        // 揺れる裸電球の光は、電球との間に物があれば届かない（床に落ちる、揺れる影。pointShadow.hlsli）
        pointAttenuation *= GetPointLightShadow(input.worldPos, input.worldNormal, pointLight.PositionRange.xyz);
        // 見えない影の異変では、姿の見えない人影が、この光源の光を遮って床に影だけを落とす（figureShadow.hlsli）
        pointAttenuation *= GetUnseenFigureShadow(input.worldPos, pointLight.PositionRange.xyz) *
            GetPlayerBodyShadow(input.worldPos, pointLight.PositionRange.xyz);

        const float pointLambert = saturate(dot(
            detailWorldNormal, directionToPointLight));
        const float softPointLambert = SOFT_LAMBERT_BASE + pointLambert * SOFT_LAMBERT_RANGE;
        // 天井のパネルを裸の電球ではなく、下向きに広がる面の光源として近似している。
        // 床へ光を集めつつ、横の方向にも少しだけ光を残している。
        const float downwardAmount = saturate(directionToPointLight.y);
        const float fixtureDistribution = lerp(
            FIXTURE_SIDE_LIGHT,
            1.0f,
            smoothstep(FIXTURE_BEAM_DOT_START, FIXTURE_BEAM_DOT_END, downwardAmount));

        lighting += pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * pointAttenuation
            * softPointLambert
            * fixtureDistribution;

        // 水たまりでは、点光源の鋭い光沢を足している
        specularLighting += pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * pointAttenuation
            * pow(pointLambert, POINT_GLOSS_EXPONENT)
            * puddle * POINT_GLOSS_STRENGTH;

        // 柔らかい光の範囲を作り、天井の照明が水面へ映っていることを分かりやすくしている。
        const float horizontalDistance = length(offsetToLight.xz);
        const float reflectedFixture = pow(saturate(
            1.0f - horizontalDistance / max(lightRange * FIXTURE_REFLECTION_RADIUS, WATER_EPSILON)),
            FIXTURE_REFLECTION_EXPONENT);
        specularLighting += pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * reflectedFixture * puddle * FIXTURE_REFLECTION_STRENGTH;
    }

    // 部屋の角の暗がり。環境光と天井灯には全部、懐中電灯には一部だけ掛けている（照らせば角も見えるように）。
    // 画面空間の環境遮蔽（SSAO）も重ね、柱や棚の根元の床まで暗くしている（懐中電灯には一部だけ）
    const float screenOcclusion = GetScreenAmbientOcclusion(input.pos.xy);
    const float roomOcclusion = GetRoomOcclusion(input.worldPos, normalize(input.worldNormal)) * screenOcclusion;
    lighting *= roomOcclusion;
    const float flashlightOcclusion = lerp(1.0f, roomOcclusion, FLOOR_FLASHLIGHT_ROOM_OCCLUSION);

    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > FLOOR_MIN_DIVISOR)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
        // 円錐の外の床では、レンズの汚れ・影・鏡面反射の計算を行っていない。
        [branch]
        if (beamProfile > FLOOR_MIN_BEAM)
        {
            const float normalizedDistance = saturate(
                distanceFromCamera / max(Light.Range, FLOOR_MIN_DIVISOR));
            const float rangeFade = saturate(
                1.0f - normalizedDistance * normalizedDistance);
            const float attenuation = rangeFade * rangeFade;
            const float lensPattern = GetFlashlightLensPattern(pixelDirection);
            const float3 flashlightColor = Light.Diffuse.rgb * GetFlashlightTint(pixelDirection);
            const float physicalFalloff = rcp(
                1.0f + distanceFromCamera * distanceFromCamera * FLASHLIGHT_PHYSICAL_FALLOFF_SCALE);
            const float naturalAttenuation = attenuation *
                lerp(1.0f, physicalFalloff, FLASHLIGHT_PHYSICAL_FALLOFF_BLEND);

            // 1面は目地・ひび・水の波の凹凸を懐中電灯にも反映している（2面は前と同じく平らな面で計算）。
            const float3 normal = WallWeathering > 0.0f
                ? normalize(mul(float4(detailWorldNormal, 0.0f), View).xyz)
                : normalize(input.viewNormal);
            const float3 directionToLight = -pixelDirection;
            const float lambert = saturate(dot(normal, directionToLight));
            const float softenedLambert = FLASHLIGHT_SOFT_LAMBERT_BASE + lambert * FLASHLIGHT_SOFT_LAMBERT_RANGE;

            const float shadow = GetFlashlightShadow(input.shadowPos) * GetUnseenFigureFlashlightShadow(input.worldPos);
            const float flashlightAmount = Light.Intensity
                * beamProfile * naturalAttenuation * lensPattern * shadow * flashlightOcclusion;

            // 水面は光の一部を反射へ回すため、透けて見える床の白飛びを抑える。
            lighting += flashlightColor
                * flashlightAmount
                * softenedLambert
                * lerp(1.0f, FLASHLIGHT_PUDDLE_DIFFUSE, puddle);

            // 濡れた面の鏡面反射（水たまりほど鋭く強い）
            const float3 halfVector = normalize(directionToLight + viewDirection);
            const float wetSpecular = pow(
                saturate(dot(normal, halfVector)),
                lerp(WET_SPECULAR_DRY_EXPONENT - drainedDamp * WET_SPECULAR_DAMP_EXPONENT, WET_SPECULAR_PUDDLE_EXPONENT, puddle));
            specularLighting += flashlightColor
                * flashlightAmount
                * wetSpecular
                * lerp(WET_SPECULAR_DRY + drainedDamp * WET_SPECULAR_DAMP, WET_SPECULAR_PUDDLE, puddle);
            // 濡れた足跡も、懐中電灯の光を細く返す
            specularLighting += flashlightColor
                * flashlightAmount
                * pow(saturate(dot(normal, halfVector)), FOOTPRINT_GLOSS_EXPONENT)
                * FOOTPRINT_GLOSS_STRENGTH * footprintWet;

            // 完全な鏡面反射の方向がカメラを外れても、浅い水は懐中電灯を弱く広く反射する。
            // 水面が黒く沈むのを防いでいる。
            specularLighting += flashlightColor
                * flashlightAmount
                * puddle
                * (SHALLOW_REFLECTION_BASE + fresnel * SHALLOW_REFLECTION_FRESNEL);

            // 足音や水滴の波：傾いた斜面が懐中電灯の光をきらっと返し、照らした所に光の輪が広がって見える
            specularLighting += flashlightColor
                * flashlightAmount
                * saturate(length(rippleWave.yz) * WAVE_GLINT_SLOPE_GAIN * farRippleBoost)
                * WAVE_GLINT_STRENGTH;
        }
    }
    // 懐中電灯の翼の光：明るい中心の外にも広がる、ぼんやりした弱い光（flashlightLighting.hlsli）
    lighting += GetFlashlightSpillLight(input.viewPos, normalize(input.viewNormal)) * flashlightOcclusion;

    const float3 floorBaseColor = color.rgb;
    color.rgb *= lighting;
    // 水面の空の映り込み・波の光・水滴の輪・岸の光を足している
    specularLighting += PUDDLE_SKY_GLINT_TINT
        * puddle * (PUDDLE_SKY_GLINT_BASE + fresnel * PUDDLE_SKY_GLINT_FRESNEL);
    specularLighting += WET_FOOTPRINT_GLINT_TINT * footprintWet * (WET_FOOTPRINT_GLINT_BASE + fresnel * WET_FOOTPRINT_GLINT_FRESNEL);
    const float rippleHighlight = pow(
        saturate(ripplePattern * 0.5f + 0.5f),
        PUDDLE_PATTERN_GLINT_EXPONENT) * puddle;
    specularLighting += PUDDLE_PATTERN_GLINT_TINT * rippleHighlight * PUDDLE_PATTERN_GLINT_GAIN;
    // 足音や水滴の波の、傾いた斜面が光を返して、輪が細く光る
    // （懐中電灯が当たっていない所でも、暗い中でうっすら輪が分かる程度に光らせている）
    specularLighting += PUDDLE_WAVE_GLINT_TINT *
        saturate(length(rippleWave.yz) * PUDDLE_WAVE_GLINT_SLOPE_GAIN * farRippleBoost) * (PUDDLE_WAVE_GLINT_BASE + fresnel * PUDDLE_WAVE_GLINT_FRESNEL) * farRippleBoost;
    specularLighting += PUDDLE_DRIP_GLINT_TINT
        * dripRing * PUDDLE_DRIP_GLINT_GAIN * RippleStrength;
    specularLighting += PUDDLE_SHORE_GLINT_TINT
        * shore * (PUDDLE_SHORE_GLINT_BASE + fresnel * PUDDLE_SHORE_GLINT_FRESNEL);
    // 床が透けるようになった分、直射光の光沢で白い金属板のように見えない強さに抑える。
    color.rgb += specularLighting * lerp(1.0f, PUDDLE_SPECULAR_SCALE, puddle);
    color.rgb += Material.Emission.rgb;

    // 水面のむら：浮いた埃や油膜を、ゆっくり流れる2つのノイズで表している。
    // filmは水面の荒さ（反射のぼけ）に、grimeは映り込みの強さに使い、場所ごとに映り方が変わるようにしている。
    const float2 filmFlow = float2(WetTime * PUDDLE_FILM_FLOW_X, -WetTime * PUDDLE_FILM_FLOW_Z);
    const float film = FastValueNoise(input.worldPos.xz * PUDDLE_FILM_NOISE_SCALE + filmFlow);
    const float grime = FastValueNoise(input.worldPos.xz * PUDDLE_GRIME_NOISE_SCALE - filmFlow * 0.5f + PUDDLE_GRIME_NOISE_SEED);
    // 荒さ（0=鏡、1=ぼやけた反射）：浅い縁（水たまりの濃さが低い所）と、波が強い所ほど荒くしている
    const float waterRoughness = saturate(
        PUDDLE_BASE_ROUGHNESS + film * PUDDLE_FILM_ROUGHNESS_WEIGHT +
        abs(ripplePattern) * PUDDLE_WAVE_ROUGHNESS_WEIGHT +
        (1.0f - saturate(puddle * PUDDLE_SHALLOW_MASK_GAIN)) * PUDDLE_SHALLOW_ROUGHNESS_WEIGHT);

    // 水は正面に近いほど床が透け、浅い角度で反射が強くなる。床を見下ろしても鏡のように光らない幅にする。
    const float grazing = 1.0f - saturate(dot(normalize(input.viewNormal), viewDirection));
    const float viewAngleReflection = fresnel * grazing;
    // 反射の方式の比較：キューブマップ・SSR・レイ反射の色と映る度合いを、ここで求めている（反射なしのときは映さない）
    const int reflectionMode = int(ReflectionParams.x + 0.5f);
    float4 alternateReflection = 0.0f;
    // 反射像は実際に広がる波の傾きで少しだけ揺らす。床の凹凸と細かいノイズは光沢と波の輪に使う。
    // 遠くの水たまりは、映った景色の揺れも少し大きくして、輪が映り込みを乱す様子が見えるようにしている
    const float2 reflectionSlope = clamp(-rippleWave.yz * PUDDLE_REFLECTION_SLOPE_SCALE * farRippleBoost,
        -PUDDLE_REFLECTION_MAX_SLOPE * farRippleBoost, PUDDLE_REFLECTION_MAX_SLOPE * farRippleBoost);
    const float3 reflectionNormal = normalize(float3(reflectionSlope.x, 1.0f, reflectionSlope.y));
    // 「水たまり」の異変の人影が、反射の光線を遮った度合い（レイ反射のときだけ求めている）
    float puddleFigure = 0.0f;
    float nearVertical = 0.0f;
    [branch]
    if (reflectionMode != SIGNAL_LOST_REFLECTION_OFF && puddle > WATER_EPSILON)
    {
        if (reflectionMode == SIGNAL_LOST_REFLECTION_CUBE_MAP)
        {
            alternateReflection = float4(SampleProbeReflection(input.worldPos, reflectionNormal), 1.0f);
        }
        else if (reflectionMode == SIGNAL_LOST_REFLECTION_SCREEN_SPACE)
        {
            if (ReflectionParams.y > FLOOR_FLAG_THRESHOLD)
            {
                alternateReflection = TraceScreenSpaceReflection(input.worldPos, reflectionNormal,
                    WATER_SSR_STEP_COUNT, WATER_SSR_STEP_GROWTH, 1.0f / WATER_SSR_DISTANCE_PER_MIP,
                    WATER_SSR_BASE_BLUR + waterRoughness, WATER_SSR_MIN_HIT_HEIGHT);
            }
        }
        else
        {
            alternateReflection = float4(
                TraceRayReflection(input.worldPos, reflectionNormal, waterRoughness, distanceFromCamera, puddleFigure, nearVertical),
                1.0f);
        }
    }
    // 映り込みの強さ：水たまりの濃さ・光線が当たった度合い・見る角度（フレネル）・反射の強さの設定・水面の汚れを掛け合わせている。
    // 濡れた床と波紋を残しつつ、近くの壁や扉は斜めからでも輪郭が分かる強さで映す。
    const float faceReflection = lerp(PUDDLE_REFLECTION_FACE, PUDDLE_REFLECTION_NEAR_WALL, nearVertical * smoothstep(PUDDLE_REFLECTION_GRAZING_START, PUDDLE_REFLECTION_GRAZING_END, grazing));
    const float reflectionStrength = saturate(
        puddle * alternateReflection.w *
        lerp(faceReflection, PUDDLE_REFLECTION_GRAZING, viewAngleReflection) * ReflectionStrength *
        lerp(PUDDLE_REFLECTION_GRIME_MIN, 1.0f, smoothstep(PUDDLE_REFLECTION_GRIME_START, PUDDLE_REFLECTION_GRIME_END, grime)));
    if (DebugViewMode == DEBUG_VIEW_FULLBRIGHT_REFLECTION)
    {
        // 床の材質を明るくし、通常と同じ水面マスクと反射像を重ねる。
        const float3 floor = GetFullbrightColor(floorBaseColor, input.viewNormal, input.viewPos).rgb;
        const float weight = saturate(max(reflectionStrength,
            puddle * alternateReflection.w * ReflectionStrength * ALTERNATE_REFLECTION_WEIGHT));
        return float4(lerp(floor, alternateReflection.rgb, weight), 1.0f);
    }
    float3 reflectedScene = 0.0f;
    [branch]
    if (reflectionStrength > WATER_EPSILON)
    {
        reflectedScene = alternateReflection.rgb;
    }
    color.rgb = lerp(
        color.rgb,
        reflectedScene,
        reflectionStrength);
    // 真上から見ると反射は弱い（フレネル）ため、人影が映る所は床の色ごと沈め、水面の黒い人の形として見えるようにしている
    color.rgb *= 1.0f - puddleFigure * saturate(puddle * FIGURE_PUDDLE_GAIN) * FIGURE_PUDDLE_DARKEN;

    // 高さの違う霧を重ね、近くの見やすさを保ちながら、遠くの輪郭を分けている。
    // 部屋全体を一様に白くせず、床の近くに霧を集めている。
    // 2面の周回が進むほど、霧が近くから濃くなる（LoopDepth。horrorGrade.hlsli）
    const float distanceFog = GetHorrorDistanceFog(distanceFromCamera, LoopDepth);
    const float heightFromFloor = max(input.worldPos.y - SCENE_BASE_FLOOR_Y, 0.0f);
    const float heightDensity = exp(-heightFromFloor * HEIGHT_FOG_DECAY);
    const float nearSuppression =
        smoothstep(HEIGHT_FOG_NEAR_START, HEIGHT_FOG_NEAR_END, distanceFromCamera);
    const float fogVariation = HEIGHT_FOG_BASE_VARIATION +
        saturate(FastValueNoise(input.worldPos.xz * HEIGHT_FOG_NOISE_SCALE + HEIGHT_FOG_NOISE_OFFSET)) * HEIGHT_FOG_NOISE_WEIGHT;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * HEIGHT_FOG_STRENGTH * GetHorrorHeightFogScale(LoopDepth);
    const float fogFactor = saturate(distanceFog + heightFog);
    // 遠くは灰色のもやではなく、闇に溶けていく
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    color.rgb = lerp(color.rgb, fogColor, fogFactor);
    // 床の近くに溜まって流れる霧（懐中電灯の円錐の中だけ白く浮かぶ。horrorMist.hlsli）。
    // 映り込みの強い所だけ霧を少し薄める。
    const float3 beforeMist = color.rgb;
    color.rgb = ApplyHorrorMist(color.rgb, input.worldPos, input.viewPos, lighting);
    color.rgb = lerp(color.rgb, beforeMist, reflectionStrength * PUDDLE_REFLECTION_MIST_REDUCTION);
    // 天井の照明や非常灯から下へ落ちる光の筋（霧に散った光。horrorMist.hlsli）
    color.rgb += GetLightShafts(input.worldPos, pointLightListOffset, pointLightCount, input.pos.xy);
    color.rgb = ApplyFilmicHorrorGrade(color.rgb, LoopDepth);

    // デバッグ表示：1=法線、2=懐中電灯の影、3=照明だけ、4=水たまりの濃さとメッシュの細かさ、5=反射の画像、7=タイルごとの光源の数（6は壁用なので黒）
    if (DebugViewMode == DEBUG_VIEW_NORMAL)
    {
        return float4(detailWorldNormal * 0.5f + 0.5f, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_FLASHLIGHT_SHADOW)
    {
        const float shadowVisibility =
            GetFlashlightShadow(input.shadowPos);
        return float4(shadowVisibility.xxx, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_LIGHTING)
    {
        return float4(saturate(lighting * DEBUG_VIEW_LIGHTING_SCALE), 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_PUDDLE)
    {
        // 水たまりのメッシュを細かさの色（近い=緑、中くらい=黄、遠い=赤）で薄く塗り、水たまりの濃さをその色で重ねている。床の本体は黒
        const float3 levelColor = waterLevel >= WATER_LEVEL_NEAR ? DEBUG_PUDDLE_NEAR_COLOR
            : waterLevel == WATER_LEVEL_MIDDLE ? DEBUG_PUDDLE_MIDDLE_COLOR
            : DEBUG_PUDDLE_FAR_COLOR;
        return waterLevel == WATER_LEVEL_DRY_FLOOR
            ? float4(0.0f, 0.0f, 0.0f, 1.0f)
            : float4(levelColor * (DEBUG_PUDDLE_BASE + puddle * DEBUG_PUDDLE_RANGE), 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_REFLECTION)
    {
        return float4(reflectedScene, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_LIGHT_TILES)
    {
        return GetLightTileHeatmap(input.pos.xy, pointLightCount);
    }
    if (DebugViewMode == DEBUG_VIEW_AMBIENT_OCCLUSION)
    {
        return float4(screenOcclusion.xxx, 1.0f);
    }
    if (DebugViewMode >= DEBUG_VIEW_WALL_DAMP)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return color;
}

