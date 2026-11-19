// ============================================================================
// シェーダーの役割: マテリアル・環境光・懐中電灯・天井灯・影・壁の古さ・霧を合わせて、3Dの面を照らしている（壁・天井・小物など、床以外のほとんどの面）。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "common.hlsl"

// テクスチャ（t0）とサンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"
#include "pointShadow.hlsli"


#include "fastNoise.hlsli"
#include "horrorMarks.hlsli"
#include "flashlightLighting.hlsli"
#include "pbrLighting.hlsli"
#include "waterCaustics.hlsli"
#include "roomOcclusion.hlsli"
#include "screenAmbientOcclusion.hlsli"
#include "surfaceDetail.hlsli"
#if defined(PORTRAIT_SURFACE)
#include "portraitPainting.hlsli"
#endif
#if defined(CLOCK_SURFACE)
#include "clockFace.hlsli"
#endif
#if defined(DOOR_SURFACE)
#include "doorPainting.hlsli"
#endif
#if defined(ROCK_SURFACE)
#include "rockSurface.hlsli"
#endif
#if defined(BARK_SURFACE)
#include "barkSurface.hlsli"
#endif
#if defined(FACADE_SURFACE)
#include "facadeSurface.hlsli"
#endif
#if defined(ROCK_SURFACE) || defined(BARK_SURFACE) || defined(FACADE_SURFACE)
#include "forestWetness.hlsli"
#endif

// --- 2面の「血の手形」の異変の値
// 手形の数（WallHandprints の数）、画素の大きさの下限（0で割らないため）
static const float MIN_PIXEL_FOOTPRINT = 0.0001f;
// 手形の壁の面とみなす範囲：x の離れ具合と、法線の x の大きさ
static const float HANDPRINT_SURFACE_DEPTH = 0.6f;
static const float HANDPRINT_SURFACE_NORMAL = 0.7f;
// 手形ごとの傾き：番号の中心と、1つあたりの角度（ラジアン）。手形の大きさ（座標を割る値）
static const float HANDPRINT_TILT_CENTER = 1.5f;
static const float HANDPRINT_TILT_STEP = 0.18f;
static const float HANDPRINT_SCALE = 1.25f;
// 手のひらの下の引きずった跡：横の幅（消え始め・消えきる）、下へ伸びる長さ、横の縞の細かさ、濃さ
static const float HANDPRINT_DRAG_EDGE_START = 1.4f;
static const float HANDPRINT_DRAG_EDGE_END = 1.9f;
static const float HANDPRINT_DRAG_LENGTH = 5.0f;
static const float HANDPRINT_DRAG_STRIPES = 7.0f;
static const float HANDPRINT_DRAG_STRENGTH = 0.6f;
// にじみ：最低の濃さとむらの幅、むらの細かさ、手形ごとのずらし
static const float HANDPRINT_SMUDGE_BASE = 0.6f;
static const float HANDPRINT_SMUDGE_RANGE = 0.4f;
static const float HANDPRINT_SMUDGE_SCALE = 0.8f;
static const float HANDPRINT_SMUDGE_SEED_STEP = 13.0f;

// 2面の「血の手形」の異変：Stage2Scene が渡した手形（WallHandprints）を、その壁の面に描き、濃さ（0〜1）を返している。
// 形は1面の壁の血の跡と同じ式（HorrorHandprint）。見え方（w）で少しずつ浮かぶ。
float GetAnomalyHandprint(float3 worldPosition, float3 worldNormal)
{
    // 1画素がワールドで何単位か（手形の縁のぼかしに使う。分岐の外で求めている）
    const float footprint = max(length(fwidth(worldPosition.zy)), MIN_PIXEL_FOOTPRINT);
    float mask = 0.0f;
    [unroll]
    for (int index = 0; index < WATER_CAUSTICS_MAX_HANDPRINTS; ++index)
    {
        const float4 print = WallHandprints[index];
        // 浮かんでいない手形と、その手形の壁の面ではない所（x が離れている・壁の向きではない）は描かない
        if (print.w <= 0.0f || abs(worldPosition.x - print.x) > HANDPRINT_SURFACE_DEPTH || abs(worldNormal.x) < HANDPRINT_SURFACE_NORMAL)
        {
            continue;
        }
        // 壁を正面から見たときの右向きを横の軸にし（右の壁は+z、左の壁は-z）、手形ごとに少し傾けている
        const float side = print.x > 0.0f ? 1.0f : -1.0f;
        const float2 local = float2((worldPosition.z - print.z) * side, worldPosition.y - print.y);
        const float angle = (float(index) - HANDPRINT_TILT_CENTER) * HANDPRINT_TILT_STEP;
        const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
        const float2 handPoint = mul(rotation, local) / HANDPRINT_SCALE;
        const float hand = HorrorHandprint(handPoint, ((uint(index) & 1u) == 0u) ? 1.0f : -1.0f, footprint / HANDPRINT_SCALE);
        // 手のひらの下へ、少し引きずった跡
        const float drag = (1.0f - smoothstep(HANDPRINT_DRAG_EDGE_START, HANDPRINT_DRAG_EDGE_END, abs(handPoint.x))) * step(handPoint.y, 0.0f) *
            (1.0f - saturate(-handPoint.y / HANDPRINT_DRAG_LENGTH)) * (0.5f + 0.5f * sin(handPoint.x * HANDPRINT_DRAG_STRIPES)) * HANDPRINT_DRAG_STRENGTH;
        const float smudge = HANDPRINT_SMUDGE_BASE + HANDPRINT_SMUDGE_RANGE * FastValueNoise(local * HANDPRINT_SMUDGE_SCALE + float(index) * HANDPRINT_SMUDGE_SEED_STEP);
        mask = max(mask, max(hand, drag) * smudge * print.w);
    }
    return mask;
}

// 頂点シェーダー（litTextureVS）から受け取る値（反射の座標は使わないので省いている）
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


// 壁の汚れの量（0〜1）を計算している：床際の湿り、大きな染み、上から垂れた跡、細かい埃
// 床に近い湿りと染み・雨だれ・埃を別々に混ぜ、壁の汚れに高さの差を付ける。
static const float WALL_DAMP_HEIGHT_DECAY = 0.060f;
static const float WALL_GRIME_DAMP_WEIGHT = 0.38f;
static const float WALL_GRIME_STAIN_WEIGHT = 0.25f;
static const float WALL_GRIME_DRIP_WEIGHT = 0.17f;
static const float WALL_GRIME_DUST_WEIGHT = 0.055f;

// 染みは広いまだらに、埃は小さいマスごとの粒にする。
static const float2 WALL_STAIN_NOISE_SCALE = float2(0.026f, 0.019f);
static const float WALL_STAIN_NOISE_SEED = 37.2f;
static const float WALL_STAIN_THRESHOLD = 0.43f;
static const float WALL_STAIN_CONTRAST = 1.65f;
static const float2 WALL_DUST_NOISE_SCALE = float2(0.115f, 0.082f);
static const float WALL_DUST_NOISE_SEED = 11.8f;

// 雨だれは横方向で位置を決め、細かいノイズで筋をところどころ途切れさせる。
static const float WALL_DRIP_COLUMN_NOISE_SCALE = 0.052f;
static const float WALL_DRIP_COLUMN_NOISE_SEED = 8.7f;
static const float2 WALL_DRIP_BREAKUP_NOISE_SCALE = float2(0.017f, 0.033f);
static const float WALL_DRIP_BREAKUP_NOISE_SEED = 4.1f;
static const float WALL_DRIP_START = 0.64f;
static const float WALL_DRIP_FULL = 0.91f;
static const float WALL_DRIP_BREAKUP_START = 0.30f;
static const float WALL_DRIP_BREAKUP_FULL = 0.78f;

float GetProceduralGrime(float3 worldPosition, float3 worldNormal)
{
    const float3 normal = abs(normalize(worldNormal));
    const float verticalSurface =
        saturate(1.0f - normal.y * normal.y);

    // 壁の主な向きを選んで、ワールド座標の大きさで模様を作り、UVが大きく引き伸ばされた物でも密度を保っている。
    const float wallCoordinate = normal.x > normal.z
        ? worldPosition.z
        : worldPosition.x;
    const float2 wallUV = float2(wallCoordinate, worldPosition.y);

    const float broadStain = saturate(
        (FastValueNoise(wallUV * WALL_STAIN_NOISE_SCALE + WALL_STAIN_NOISE_SEED) - WALL_STAIN_THRESHOLD)
        * WALL_STAIN_CONTRAST);
    // 細かい粒は、補間するノイズではなく、マス目ごとの乱数で十分。
    // 壁が画面の大半を占める場面の、画素ごとの負荷を抑えている。
    const float fineDust = FastHash21(floor(
        wallUV * WALL_DUST_NOISE_SCALE - WALL_DUST_NOISE_SEED));

    // 主に1次元のマスクから、重力の方向へ伸びる縦長の汚れを作っている。
    const float dripSeed = FastValueNoise(
        float2(wallCoordinate * WALL_DRIP_COLUMN_NOISE_SCALE, WALL_DRIP_COLUMN_NOISE_SEED));
    const float dripBreakup = FastValueNoise(
        wallUV * WALL_DRIP_BREAKUP_NOISE_SCALE + WALL_DRIP_BREAKUP_NOISE_SEED);
    const float drip = smoothstep(WALL_DRIP_START, WALL_DRIP_FULL, dripSeed) *
        smoothstep(WALL_DRIP_BREAKUP_START, WALL_DRIP_BREAKUP_FULL, dripBreakup);

    // ステージの床はワールドのY=-100あたりにあり、壁の足元に湿気がたまる。
    const float heightFromFloor = max(worldPosition.y - SCENE_BASE_FLOOR_Y, 0.0f);
    const float floorDamp = exp(-heightFromFloor * WALL_DAMP_HEIGHT_DECAY);

    return saturate(verticalSurface *
        (floorDamp * WALL_GRIME_DAMP_WEIGHT + broadStain * WALL_GRIME_STAIN_WEIGHT +
         drip * WALL_GRIME_DRIP_WEIGHT + fineDust * WALL_GRIME_DUST_WEIGHT));
}

// 壁の凹凸は、離れても大きな左官跡が残るよう、三つの大きさのノイズを組み合わせる。
static const float WALL_PLASTER_BROAD_SCALE = 0.19f;
static const float WALL_PLASTER_BROAD_SEED = 3.7f;
static const float WALL_PLASTER_MID_SCALE = 0.63f;
static const float WALL_PLASTER_MID_SEED = 12.4f;
static const float WALL_PLASTER_FINE_SCALE = 1.45f;
static const float WALL_PLASTER_FINE_SEED = 27.1f;
static const float WALL_PLASTER_BROAD_WEIGHT = 0.52f;
static const float WALL_PLASTER_MID_WEIGHT = 0.33f;
static const float WALL_PLASTER_FINE_WEIGHT = 0.15f;

// 壁のざらつき（左官の跡のような凹凸）の高さを計算している
float GetProceduralSurfaceHeight(float3 worldPosition, float3 worldNormal)
{
    const float3 normal = abs(normalize(worldNormal));
    const float wallCoordinate = normal.x > normal.z
        ? worldPosition.z
        : worldPosition.x;
    const float2 wallUV = float2(wallCoordinate, worldPosition.y);

    const float broad = FastValueNoise(wallUV * WALL_PLASTER_BROAD_SCALE + WALL_PLASTER_BROAD_SEED);
    const float plaster = FastValueNoise(wallUV * WALL_PLASTER_MID_SCALE - WALL_PLASTER_MID_SEED);
    // 細かいざらつきは、マス目ごとの乱数（段差のある値）ではなく、なめらかなノイズにしている。
    // 段差があると、画面の微分で求める法線がマス目の縁で折れ、明るく照らした壁にレンガのような格子が見えるため。
    const float fine = FastValueNoise(wallUV * WALL_PLASTER_FINE_SCALE + WALL_PLASTER_FINE_SEED);
    return broad * WALL_PLASTER_BROAD_WEIGHT + plaster * WALL_PLASTER_MID_WEIGHT + fine * WALL_PLASTER_FINE_WEIGHT;
}

// ----------------------------------------------------------------------------
// 壁の古さ（1面と2面）。WallWeathering（面ごと）と Material.WeatheringSurface（建物の壁と天井だけ）で有効になる。
// コンクリートのパネルの継ぎ目・小さな穴・ひびを凹凸として、水の垂れた跡・床際の水位線・カビを色として描いている。
// 画像の素材は使わず、ワールド座標から計算するため、壁の長さが違っても模様の大きさはそろう。
// ----------------------------------------------------------------------------
struct WallAgeing
{
    float height;   // 凹凸に足す高さ（へこみは負）
    float cavity;   // 継ぎ目・穴・ひびの奥の暗さ（0〜1）
    float3 tint;    // 染みの色（元の色に掛けている。1なら変化なし）
    float damp;     // 湿り（0〜1）。浅い角度で見たときの濡れた光沢に使っている
};

// 1面の床の高さ（y=-100）と、壁の上端までの高さ（天井の下面 y≒-48.5）。
// コンクリートの縦目地と型枠の横線の幅。横線は目地より浅く見せる。
static const float WALL_VERTICAL_JOINT_CORE = 0.06f;
static const float WALL_VERTICAL_JOINT_EDGE = 0.26f;
static const float WALL_FORM_LINE_CORE = 0.03f;
static const float WALL_FORM_LINE_EDGE = 0.12f;
static const float WALL_FORM_LINE_WEIGHT = 0.45f;

// 画面で見分けられる大きさのひびだけを描き、ノイズで出る場所と途切れる場所を決める。
static const float WALL_CRACK_MASK_START = 0.62f;
static const float WALL_CRACK_MASK_FULL = 0.78f;
static const float WALL_CRACK_MASK_SCALE = 0.021f;
static const float WALL_CRACK_FOOTPRINT_LIMIT = 0.35f;
static const float WALL_CRACK_LINE_WIDTH = 0.012f;
static const float WALL_CRACK_LINE_FILTER = 0.04f;
static const float WALL_CRACK_WARP_WEIGHT = 0.22f;
static const float WALL_CRACK_BREAK_START = 0.42f;
static const float WALL_CRACK_BREAK_END = 0.58f;

// 水が溜まっていた高さの上下で色をなじませ、境目を線として残す。
static const float WALL_TIDE_MARK_BOTTOM_BLEND = 1.5f;
static const float WALL_TIDE_MARK_TOP_BLEND = 0.3f;
static const float WALL_TIDE_MARK_LINE_FALLOFF = 1.6f;

static const float WallFloorY = -100.0f;
static const float WallHeight = 51.0f;

// --- 壁の古さの値（ComputeWallAgeing）
// 打ちっぱなしコンクリートのパネルの幅（高さは壁の3分の1）と、パネルごとの色の乱数のずらし
static const float WALL_PANEL_WIDTH = 60.0f;
static const float WALL_PANEL_ROWS = 3.0f;
static const float WALL_PANEL_SHADE_SEED = 71.0f;
// 気泡の穴とひびを描く画素の大きさの上限（これより遠いと1画素より小さい）
static const float WALL_DETAIL_FOOTPRINT_LIMIT = 0.5f;
// 気泡の穴：マス目の大きさ、乱数のずらし（出るか・横・縦・大きさ）、出る割合のしきい値、位置のずらしの幅、
// 大きさ（最小＋乱数の幅）、穴の芯の割合
static const float WALL_PIT_CELL_SIZE = 1.6f;
static const float WALL_PIT_SEED = 17.3f;
static const float WALL_PIT_OFFSET_SEED_X = 3.9f;
static const float WALL_PIT_OFFSET_SEED_Y = 8.6f;
static const float WALL_PIT_SIZE_SEED = 29.4f;
static const float WALL_PIT_THRESHOLD = 0.92f;
static const float WALL_PIT_OFFSET_RANGE = 0.5f;
static const float WALL_PIT_OFFSET_CENTER = 0.25f;
static const float WALL_PIT_MIN_SIZE = 0.05f;
static const float WALL_PIT_SIZE_RANGE = 0.12f;
static const float WALL_PIT_CORE = 0.4f;
// ひび：出る場所のノイズのずらし、線を揺らすノイズ（細かさとずらし）、等高線のノイズ（細かさとずらし）、途切れさせるノイズ（細かさとずらし）
static const float WALL_CRACK_MASK_SEED = 9.0f;
static const float WALL_CRACK_WARP_SCALE = 0.9f;
static const float WALL_CRACK_WARP_SEED_X = 3.0f;
static const float WALL_CRACK_WARP_SEED_Y = 17.0f;
static const float WALL_CRACK_LINE_SCALE = 0.085f;
static const float WALL_CRACK_LINE_SEED = 61.0f;
static const float WALL_CRACK_BREAK_SCALE = 0.32f;
static const float WALL_CRACK_BREAK_SEED = 41.0f;
// 床際の水位線：高さ（最低＋揺らぎの幅）、揺らぎの細かさ、ノイズの縦の座標
static const float WALL_WATER_LINE_BASE = 6.0f;
static const float WALL_WATER_LINE_RANGE = 6.0f;
static const float WALL_WATER_LINE_SCALE = 0.04f;
static const float WALL_WATER_LINE_SEED = 3.1f;
// 上から垂れた錆と水の跡：帯の幅、出るかの乱数のずらしとしきい値（2割ほど）、半分の幅（最小＋乱数の幅）とその乱数のずらし、
// 長さ（最小＋乱数の幅）とその乱数のずらし、芯の割合、消え始める長さの割合、濃さのむら（最低＋幅、細かさ）
static const float WALL_STREAK_WIDTH = 2.4f;
static const float WALL_STREAK_SEED = 41.7f;
static const float WALL_STREAK_THRESHOLD = 0.80f;
static const float WALL_STREAK_MIN_HALF_WIDTH = 0.10f;
static const float WALL_STREAK_HALF_WIDTH_RANGE = 0.22f;
static const float WALL_STREAK_WIDTH_SEED = 7.7f;
static const float WALL_STREAK_MIN_LENGTH = 14.0f;
static const float WALL_STREAK_LENGTH_RANGE = 26.0f;
static const float WALL_STREAK_LENGTH_SEED = 13.1f;
static const float WALL_STREAK_CORE = 0.35f;
static const float WALL_STREAK_FADE_START = 0.45f;
static const float WALL_STREAK_SHADE_BASE = 0.55f;
static const float WALL_STREAK_SHADE_RANGE = 0.45f;
static const float WALL_STREAK_SHADE_COLUMN_SCALE = 3.1f;
static const float WALL_STREAK_SHADE_HEIGHT_SCALE = 0.35f;
// カビ：塊のしきい値（出始め・濃くなる）とノイズの細かさ・ずらし、床からの減り方と壁の途中に残す量、
// 斑点を描く画素の大きさの上限、回転（30度の cos と sin）、斑点のノイズ2つ（細かさ・ずらし・割合）、斑点の濃淡（最低・しきい値）
static const float WALL_MOLD_START = 0.58f;
static const float WALL_MOLD_FULL = 0.82f;
static const float WALL_MOLD_SCALE = 0.055f;
static const float WALL_MOLD_SEED = 23.0f;
static const float WALL_MOLD_HEIGHT_FALLOFF = 0.05f;
static const float WALL_MOLD_HEIGHT_FLOOR = 0.30f;
static const float WALL_MOLD_FOOTPRINT_LIMIT = 0.6f;
static const float WALL_MOLD_ROTATE_COS = 0.866f;
static const float WALL_MOLD_ROTATE_SIN = 0.5f;
static const float WALL_MOLD_GRAIN_SCALE_A = 1.1f;
static const float WALL_MOLD_GRAIN_SEED_A = 5.0f;
static const float WALL_MOLD_GRAIN_WEIGHT_A = 0.6f;
static const float WALL_MOLD_GRAIN_SCALE_B = 2.7f;
static const float WALL_MOLD_GRAIN_SEED_B = 13.0f;
static const float WALL_MOLD_GRAIN_WEIGHT_B = 0.4f;
static const float WALL_MOLD_SPECKLE_MIN = 0.45f;
static const float WALL_MOLD_SPECKLE_START = 0.30f;
static const float WALL_MOLD_SPECKLE_END = 0.75f;
// へこみの高さ（継ぎ目・穴・ひび・引っかき傷）と、奥の暗さ（同じ順）
static const float WALL_SEAM_DEPTH = 0.7f;
static const float WALL_PIT_DEPTH = 0.5f;
static const float WALL_CRACK_DEPTH = 0.35f;
static const float WALL_GOUGE_DEPTH = 0.9f;
static const float WALL_SEAM_CAVITY = 0.32f;
static const float WALL_PIT_CAVITY = 0.40f;
static const float WALL_CRACK_CAVITY = 0.42f;
static const float WALL_GOUGE_CAVITY = 0.25f;
// 色（元の色に掛ける）：錆、カビ、水に浸かっていた部分、パネルの明るさ（最低＋むらの幅）、それぞれの混ぜ方、
// 引っかき傷で明るくする量と混ぜ方、血の色と混ぜ方
static const float3 WALL_RUST_COLOR = float3(0.66f, 0.50f, 0.38f);
static const float3 WALL_MOLD_COLOR = float3(0.26f, 0.32f, 0.24f);
static const float3 WALL_SOAKED_COLOR = float3(0.72f, 0.76f, 0.70f);
static const float WALL_PANEL_TINT_BASE = 0.94f;
static const float WALL_PANEL_TINT_RANGE = 0.10f;
static const float WALL_SOAKED_BLEND = 0.60f;
static const float WALL_TIDE_RUST_DARKEN = 0.85f;
static const float WALL_TIDE_BLEND = 0.50f;
static const float WALL_STREAK_BLEND = 0.65f;
static const float WALL_MOLD_BLEND = 0.75f;
static const float WALL_GOUGE_BRIGHTEN = 1.35f;
static const float WALL_GOUGE_BLEND = 0.8f;
static const float3 WALL_BLOOD_COLOR = float3(0.46f, 0.035f, 0.030f);
static const float WALL_BLOOD_BLEND = 0.92f;
// 湿りの光沢（水に浸かっていた部分・垂れ跡・血）
static const float WALL_SOAKED_DAMP = 0.6f;
static const float WALL_STREAK_DAMP = 0.4f;
static const float WALL_BLOOD_DAMP = 0.9f;

// --- 天井の古さの値（ComputeCeilingAgeing）
// 天井板の大きさ（12四方）、枠の幅（芯・ぼかし）、板ごとの乱数のずらし、外れ落ちた板の割合のしきい値（7%ほど）
static const float CEILING_TILE_SIZE = 12.0f;
static const float CEILING_FRAME_CORE = 0.15f;
static const float CEILING_FRAME_EDGE = 0.35f;
static const float CEILING_TILE_SEED = 5.0f;
static const float CEILING_MISSING_THRESHOLD = 0.93f;
// 雨漏りの染み：出るかの乱数のずらしとしきい値（25%ほど）、中心（最小＋乱数の幅）とその乱数のずらし、半径（最小＋乱数の幅）とその乱数のずらし、
// 縁のゆがみ（最低＋ノイズの幅、細かさ・ずらし）、にじみの芯の割合、輪の鋭さ
static const float CEILING_STAIN_SEED = 19.0f;
static const float CEILING_STAIN_THRESHOLD = 0.75f;
static const float CEILING_STAIN_CENTER_MIN = 0.3f;
static const float CEILING_STAIN_CENTER_RANGE = 0.4f;
static const float CEILING_STAIN_CENTER_SEED_X = 31.0f;
static const float CEILING_STAIN_CENTER_SEED_Y = 47.0f;
static const float CEILING_STAIN_MIN_RADIUS = 2.0f;
static const float CEILING_STAIN_RADIUS_RANGE = 3.0f;
static const float CEILING_STAIN_RADIUS_SEED = 61.0f;
static const float CEILING_STAIN_WARP_BASE = 0.85f;
static const float CEILING_STAIN_WARP_RANGE = 0.3f;
static const float CEILING_STAIN_WARP_SCALE = 0.4f;
static const float CEILING_STAIN_WARP_SEED = 3.0f;
static const float CEILING_STAIN_CORE = 0.55f;
static const float CEILING_STAIN_RING_SHARPNESS = 3.0f;
// 高さ（枠は少し下に出る、外れた板の穴はへこむ）と穴の暗さ
static const float CEILING_FRAME_HEIGHT = 0.4f;
static const float CEILING_MISSING_DEPTH = 1.5f;
static const float CEILING_MISSING_CAVITY = 0.9f;
// 色：板の明るさ（最低＋乱数の幅）、枠、染みのにじみと輪（色と混ぜ方）、穴の暗さ、染みの湿り
static const float CEILING_TILE_BRIGHTNESS = 2.3f;
static const float CEILING_TILE_BRIGHTNESS_RANGE = 0.4f;
static const float3 CEILING_FRAME_COLOR = float3(1.6f, 1.6f, 1.55f);
static const float3 CEILING_STAIN_COLOR = float3(0.78f, 0.62f, 0.44f);
static const float CEILING_STAIN_BLEND = 0.65f;
static const float3 CEILING_STAIN_RING_COLOR = float3(0.50f, 0.40f, 0.30f);
static const float CEILING_STAIN_RING_BLEND = 0.55f;
static const float CEILING_MISSING_SHADE = 0.20f;
static const float CEILING_STAIN_DAMP = 0.3f;

// normal はワールドの法線の絶対値、weight は古さの濃さ（0より大きい）。GetWallAgeing から呼んでいる。
WallAgeing ComputeWallAgeing(float3 worldPosition, float3 normal, float weight)
{
    WallAgeing ageing;

    // 壁に沿った横方向と高さの2次元の座標（GetProceduralGrimeと同じ選び方）。
    const float2 uv = float2(
        normal.x > normal.z ? worldPosition.z : worldPosition.x,
        worldPosition.y);
    const float heightFromFloor = worldPosition.y - WallFloorY;
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている。
    const float footprint = max(length(fwidth(uv)), MIN_PIXEL_FOOTPRINT);

    // --- 打ちっぱなしコンクリートの継ぎ目（幅60・高さ17のパネル。壁の文字の高さ y=-70 は横の継ぎ目を避けている）
    // 縦の継ぎ目はくっきり、横の継ぎ目（型枠の跡）は細く浅くして、タイル張りに見えないようにしている。
    const float2 panelSize = float2(WALL_PANEL_WIDTH, WallHeight / WALL_PANEL_ROWS);
    const float2 panelUV = float2(uv.x, heightFromFloor) / panelSize;
    const float2 edgeDistance = (0.5f - abs(frac(panelUV) - 0.5f)) * panelSize;
    const float verticalJoint = 1.0f - smoothstep(WALL_VERTICAL_JOINT_CORE, WALL_VERTICAL_JOINT_EDGE + footprint, edgeDistance.x);
    const float formLine = (1.0f - smoothstep(WALL_FORM_LINE_CORE, WALL_FORM_LINE_EDGE + footprint, edgeDistance.y)) * WALL_FORM_LINE_WEIGHT;
    const float seam = max(verticalJoint, formLine);
    // パネルごとに、打った日の違いで色をわずかに変えている。
    const float panelShade = FastHash21(floor(panelUV) + WALL_PANEL_SHADE_SEED);

    // 気泡の穴とひびは、遠くて1画素より小さくなる場所では見えないため、計算を省いている。
    float pit = 0.0f;
    float crack = 0.0f;
    [branch]
    if (footprint < WALL_DETAIL_FOOTPRINT_LIMIT)
    {
        // --- 小さな気泡の穴（マス目の中で位置と大きさをばらつかせ、格子状に並ばないようにしている）
        const float pitCellSize = WALL_PIT_CELL_SIZE;
        const float2 pitCell = floor(uv / pitCellSize);
        const float pitSeed = FastHash21(pitCell + WALL_PIT_SEED);
        [branch]
        if (pitSeed >= WALL_PIT_THRESHOLD)
        {
            const float2 pitOffset = float2(
                FastHash21(pitCell + WALL_PIT_OFFSET_SEED_X), FastHash21(pitCell + WALL_PIT_OFFSET_SEED_Y)) * WALL_PIT_OFFSET_RANGE - WALL_PIT_OFFSET_CENTER;
            const float pitSize = WALL_PIT_MIN_SIZE + WALL_PIT_SIZE_RANGE * FastHash21(pitCell + WALL_PIT_SIZE_SEED);
            const float pitRadius = length(frac(uv / pitCellSize) - 0.5f - pitOffset) * pitCellSize;
            pit = (1.0f - smoothstep(pitSize * WALL_PIT_CORE, pitSize + footprint, pitRadius)) *
                saturate(1.0f - footprint / WALL_DETAIL_FOOTPRINT_LIMIT);
        }

        // --- ひび（ノイズの等高線を細い線として使い、別のノイズで出る場所を絞っている）
        // 等高線だけだと大きな輪のようななめらかな曲線になるため、細かいノイズで線をギザギザに揺らし、
        // さらに別のノイズで途切れ途切れにして、割れたひびらしくしている。
        const float crackMask = smoothstep(WALL_CRACK_MASK_START, WALL_CRACK_MASK_FULL, FastValueNoise(uv * WALL_CRACK_MASK_SCALE + WALL_CRACK_MASK_SEED));
        [branch]
        if (crackMask > 0.0f && footprint < WALL_CRACK_FOOTPRINT_LIMIT)
        {
            const float2 crackWarp = float2(
                FastValueNoise(uv * WALL_CRACK_WARP_SCALE + WALL_CRACK_WARP_SEED_X), FastValueNoise(uv * WALL_CRACK_WARP_SCALE + WALL_CRACK_WARP_SEED_Y)) - 0.5f;
            const float crackLine = 1.0f - smoothstep(0.0f, WALL_CRACK_LINE_WIDTH + footprint * WALL_CRACK_LINE_FILTER,
                abs(FastValueNoise(uv * WALL_CRACK_LINE_SCALE + WALL_CRACK_LINE_SEED + crackWarp * WALL_CRACK_WARP_WEIGHT) - 0.5f));
            const float crackPieces = smoothstep(WALL_CRACK_BREAK_START, WALL_CRACK_BREAK_END, FastValueNoise(uv * WALL_CRACK_BREAK_SCALE + WALL_CRACK_BREAK_SEED));
            crack = crackLine * crackMask * crackPieces * saturate(1.0f - footprint / WALL_CRACK_FOOTPRINT_LIMIT);
        }
    }

    // --- 床際の水位線（水がたまっていた跡）。高さは場所によって6〜12で揺らいでいる
    const float waterLine = WALL_WATER_LINE_BASE + WALL_WATER_LINE_RANGE * FastValueNoise(float2(uv.x * WALL_WATER_LINE_SCALE, WALL_WATER_LINE_SEED));
    const float belowWater = 1.0f - smoothstep(waterLine - WALL_TIDE_MARK_BOTTOM_BLEND, waterLine + WALL_TIDE_MARK_TOP_BLEND, heightFromFloor);
    const float tideMark = exp(-abs(heightFromFloor - waterLine) * WALL_TIDE_MARK_LINE_FALLOFF);

    // --- 上から垂れた錆と水の跡（幅2.4ごとの縦の帯のうち、2割ほどに出している）
    const float streakWidth = WALL_STREAK_WIDTH;
    const float column = floor(uv.x / streakWidth);
    const float streakSeed = FastHash21(float2(column, WALL_STREAK_SEED));
    float streak = 0.0f;
    [branch]
    if (streakSeed >= WALL_STREAK_THRESHOLD)
    {
        const float streakX = abs(frac(uv.x / streakWidth) - 0.5f);
        const float streakHalfWidth = WALL_STREAK_MIN_HALF_WIDTH + WALL_STREAK_HALF_WIDTH_RANGE * FastHash21(float2(column, WALL_STREAK_WIDTH_SEED));
        const float streakLength = WALL_STREAK_MIN_LENGTH + WALL_STREAK_LENGTH_RANGE * FastHash21(float2(column, WALL_STREAK_LENGTH_SEED));
        const float fromTop = WallHeight - heightFromFloor;
        streak = (1.0f - smoothstep(streakHalfWidth * WALL_STREAK_CORE, streakHalfWidth + footprint, streakX)) *
            (1.0f - smoothstep(streakLength * WALL_STREAK_FADE_START, streakLength, fromTop)) *
            (WALL_STREAK_SHADE_BASE + WALL_STREAK_SHADE_RANGE * FastValueNoise(float2(column * WALL_STREAK_SHADE_COLUMN_SCALE, heightFromFloor * WALL_STREAK_SHADE_HEIGHT_SCALE)));
    }

    // --- カビ（床際に多く、壁の途中にも固まって生えている）。細かい斑点はなめらかなノイズで丸くにじませている
    // 値ノイズは格子に沿った四角い形が出やすいため、座標を回転させ、大きさの違う2つを混ぜて格子を崩している。
    const float moldCluster = smoothstep(WALL_MOLD_START, WALL_MOLD_FULL, FastValueNoise(uv * WALL_MOLD_SCALE + WALL_MOLD_SEED));
    float mold = moldCluster * saturate(exp(-heightFromFloor * WALL_MOLD_HEIGHT_FALLOFF) + WALL_MOLD_HEIGHT_FLOOR);
    [branch]
    if (moldCluster > 0.0f && footprint < WALL_MOLD_FOOTPRINT_LIMIT)
    {
        const float2 rotatedUV = float2(
            uv.x * WALL_MOLD_ROTATE_COS - uv.y * WALL_MOLD_ROTATE_SIN,
            uv.x * WALL_MOLD_ROTATE_SIN + uv.y * WALL_MOLD_ROTATE_COS);
        const float moldGrain =
            FastValueNoise(rotatedUV * WALL_MOLD_GRAIN_SCALE_A + WALL_MOLD_GRAIN_SEED_A) * WALL_MOLD_GRAIN_WEIGHT_A +
            FastValueNoise(uv * WALL_MOLD_GRAIN_SCALE_B - WALL_MOLD_GRAIN_SEED_B) * WALL_MOLD_GRAIN_WEIGHT_B;
        // 斑点はカビの塊の中の濃淡として使う。
        const float moldSpeckle = lerp(WALL_MOLD_SPECKLE_MIN, 1.0f, smoothstep(WALL_MOLD_SPECKLE_START, WALL_MOLD_SPECKLE_END, moldGrain));
        mold *= lerp(1.0f, moldSpeckle, saturate(1.0f - footprint / WALL_MOLD_FOOTPRINT_LIMIT));
    }

    // --- ホラーらしい跡（血の手形と引きずった跡、引っかき傷、血の垂れ跡。horrorMarks.hlsli）
    // 壁の面の位置（法線の向きの座標）も渡し、平行な別の壁に同じ跡が並ばないようにしている。
    const float planeCoordinate = normal.x > normal.z ? worldPosition.x : worldPosition.z;
    // 2面（HorrorMarksStrength = 0）は、手形や傷を異変として出すため、決まった跡を描かない（計算も省いている）
    HorrorMarks marks;
    marks.blood = 0.0f;
    marks.gouge = 0.0f;
    [branch]
    if (HorrorMarksStrength > 0.0f)
    {
        marks = ComputeHorrorMarks(uv.x, heightFromFloor, planeCoordinate, footprint);
        marks.blood *= HorrorMarksStrength;
        marks.gouge *= HorrorMarksStrength;
    }

    // 継ぎ目・穴・ひび・引っかき傷を、へこみの高さと、奥の暗さにしている
    ageing.height = -(seam * WALL_SEAM_DEPTH + pit * WALL_PIT_DEPTH + crack * WALL_CRACK_DEPTH + marks.gouge * WALL_GOUGE_DEPTH) * weight;
    ageing.cavity = saturate(seam * WALL_SEAM_CAVITY + pit * WALL_PIT_CAVITY + crack * WALL_CRACK_CAVITY + marks.gouge * WALL_GOUGE_CAVITY) * weight;

    // 色は元の色に掛けている（茶色＝錆、くすんだ緑＝カビ、少し暗い灰緑＝水に浸かっていた部分）。
    float3 tint = (WALL_PANEL_TINT_BASE + WALL_PANEL_TINT_RANGE * panelShade).xxx;
    tint = lerp(tint, WALL_SOAKED_COLOR, belowWater * WALL_SOAKED_BLEND);
    tint = lerp(tint, WALL_RUST_COLOR * WALL_TIDE_RUST_DARKEN, tideMark * WALL_TIDE_BLEND);
    tint = lerp(tint, WALL_RUST_COLOR, streak * WALL_STREAK_BLEND);
    tint = lerp(tint, WALL_MOLD_COLOR, mold * WALL_MOLD_BLEND);
    // 引っかき傷は、汚れた表面が削れて下の明るいコンクリートが見えている。血は赤黒く、青と緑を強く吸う
    tint = lerp(tint, tint * WALL_GOUGE_BRIGHTEN, marks.gouge * WALL_GOUGE_BLEND);
    tint = lerp(tint, WALL_BLOOD_COLOR, marks.blood * WALL_BLOOD_BLEND);
    ageing.tint = lerp(1.0f.xxx, tint, weight);
    // 血は乾ききっておらず、浅い角度で懐中電灯の光を返す（湿りの光沢を使っている）
    ageing.damp = saturate(belowWater * WALL_SOAKED_DAMP + streak * WALL_STREAK_DAMP + marks.blood * WALL_BLOOD_DAMP) * weight;
    return ageing;
}

// 天井の古さ。金属の枠に並んだ天井板（12四方。この世界では約60cm）、外れ落ちた板の穴、雨漏りの染みを描いている。
// 天井の元の色はほぼ黒（0.055）なので、板の部分は色を掛けて暗い灰色まで明るくしている（ホラーらしく、見上げても暗く沈む程度）。
WallAgeing ComputeCeilingAgeing(float2 position, float weight)
{
    WallAgeing ageing;
    const float footprint = max(length(fwidth(position)), MIN_PIXEL_FOOTPRINT);

    // --- 天井板と金属の枠
    const float tileSize = CEILING_TILE_SIZE;
    const float2 tileUV = position / tileSize;
    const float2 tileId = floor(tileUV);
    const float2 local = frac(tileUV);
    const float2 edgeDistance = (0.5f - abs(local - 0.5f)) * tileSize;
    const float frame = 1.0f - smoothstep(CEILING_FRAME_CORE, CEILING_FRAME_EDGE + footprint, min(edgeDistance.x, edgeDistance.y));
    const float tileSeed = FastHash21(tileId + CEILING_TILE_SEED);

    // --- 外れ落ちた板（7%ほど）。枠だけ残り、奥の暗い空間が見えている
    const float missing = step(CEILING_MISSING_THRESHOLD, tileSeed) * (1.0f - frame);

    // --- 雨漏りの染み（板の25%ほど）。茶色いにじみと、縁の濃い輪
    const float stainSeed = FastHash21(tileId + CEILING_STAIN_SEED);
    const float2 stainCenter = CEILING_STAIN_CENTER_MIN + CEILING_STAIN_CENTER_RANGE * float2(
        FastHash21(tileId + CEILING_STAIN_CENTER_SEED_X), FastHash21(tileId + CEILING_STAIN_CENTER_SEED_Y));
    const float stainRadius = CEILING_STAIN_MIN_RADIUS + CEILING_STAIN_RADIUS_RANGE * FastHash21(tileId + CEILING_STAIN_RADIUS_SEED);
    const float stainDistance = length(local - stainCenter) * tileSize *
        (CEILING_STAIN_WARP_BASE + CEILING_STAIN_WARP_RANGE * FastValueNoise(position * CEILING_STAIN_WARP_SCALE + CEILING_STAIN_WARP_SEED));
    const float stainOn = step(CEILING_STAIN_THRESHOLD, stainSeed) * (1.0f - missing) * (1.0f - frame);
    const float stainFill = (1.0f - smoothstep(stainRadius * CEILING_STAIN_CORE, stainRadius, stainDistance)) * stainOn;
    const float stainRing = exp(-abs(stainDistance - stainRadius) * CEILING_STAIN_RING_SHARPNESS) * stainOn;

    // 枠は板より少し下に出ていて、外れた板の穴は奥へへこんでいる。
    ageing.height = (frame * CEILING_FRAME_HEIGHT - missing * CEILING_MISSING_DEPTH) * weight;
    ageing.cavity = missing * CEILING_MISSING_CAVITY * weight;

    // 板・枠・染み・穴の色
    const float3 tileColor = (CEILING_TILE_BRIGHTNESS + CEILING_TILE_BRIGHTNESS_RANGE * tileSeed).xxx;
    float3 tint = lerp(tileColor, CEILING_FRAME_COLOR, frame);
    tint = lerp(tint, tint * CEILING_STAIN_COLOR, stainFill * CEILING_STAIN_BLEND);
    tint = lerp(tint, tint * CEILING_STAIN_RING_COLOR, stainRing * CEILING_STAIN_RING_BLEND);
    tint = lerp(tint, CEILING_MISSING_SHADE.xxx, missing);
    ageing.tint = lerp(1.0f.xxx, tint, weight);
    ageing.damp = stainFill * CEILING_STAIN_DAMP * weight;
    return ageing;
}

// その面の古さを返している（壁・天井・それ以外で分けている）
// 縦の面とみなす範囲（壁の古さが出始める・出きる）と、下を向いた面とみなす範囲（天井の古さ）
static const float WALL_AGEING_VERTICAL_START = 0.5f;
static const float WALL_AGEING_VERTICAL_FULL = 0.9f;
static const float CEILING_AGEING_DOWN_START = 0.7f;
static const float CEILING_AGEING_DOWN_FULL = 0.95f;

WallAgeing GetWallAgeing(float3 worldPosition, float3 worldNormal, float amount)
{
    WallAgeing ageing;
    ageing.height = 0.0f;
    ageing.cavity = 0.0f;
    ageing.tint = 1.0f.xxx;
    ageing.damp = 0.0f;

    // 古さを描かない面（小物・2面）は何も計算していない。
    // amount は描画の単位で一定（定数バッファとマテリアル）なので、この分岐は画素ごとにばらつかず、安く済む。
    // 建物の面のうち、縦の面は壁の古さ、下を向いた面（天井）は天井の古さを描いている。
    const float3 signedNormal = normalize(worldNormal);
    const float3 normal = abs(signedNormal);
    const float verticalSurface = saturate(1.0f - normal.y * normal.y);
    const float weight = amount * smoothstep(WALL_AGEING_VERTICAL_START, WALL_AGEING_VERTICAL_FULL, verticalSurface);
    const float ceilingWeight = amount * smoothstep(CEILING_AGEING_DOWN_START, CEILING_AGEING_DOWN_FULL, -signedNormal.y);
    [branch]
    if (weight > 0.0f)
    {
        ageing = ComputeWallAgeing(worldPosition, normal, weight);
    }
    else if (ceilingWeight > 0.0f)
    {
        ageing = ComputeCeilingAgeing(worldPosition.xz, ceilingWeight);
    }
    return ageing;
}

// extraHeight は GetProceduralSurfaceHeight に足す高さ（壁の古さの継ぎ目など）、strength は凹凸の強さ。
float3 GetBumpedWorldNormal(
    float3 worldPosition,
    float3 worldNormal,
    float extraHeight,
    float strength)
{
    const float3 normal = normalize(worldNormal);
    const float height = GetProceduralSurfaceHeight(
        worldPosition,
        worldNormal) + extraHeight;
    return ApplyHeightToNormal(worldPosition, normal, height, strength);
}

// 壁を伝う水の筋（建物の壁の、湿った所だけ）。0〜1の濡れ具合を返している。
// 壁の横方向を細い列に分け、列ごとに乱数で筋があるか・太さ・流れる速さを決め、少しくねらせた細い線にしている。
// 線の中では、薄い水の膜の上を、水の粒が上から下へゆっくり流れ落ちていく（時間で縦方向の濃淡をずらしている）。
// 濡れた所は粗さが下がるので、懐中電灯で照らすと筋だけが細く光る（pbrLighting.hlsli）。
// 垂直に近い面だけで、水が流れる列を決める。
static const float WALL_TRICKLE_TILT_FADE = 4.0f;
static const float WALL_TRICKLE_COLUMN_WIDTH = 3.6f;
static const float WALL_TRICKLE_COLUMN_SEED = 3.7f;
static const float WALL_TRICKLE_COLUMN_THRESHOLD = 0.80f;

// 列に沿って線をくねらせ、太さも列ごとに変える。
static const float WALL_TRICKLE_WOBBLE_COLUMN_SCALE = 1.3f;
static const float WALL_TRICKLE_WOBBLE_HEIGHT_SCALE = 0.08f;
static const float WALL_TRICKLE_MIN_WIDTH = 0.07f;
static const float WALL_TRICKLE_MAX_WIDTH = 0.17f;
static const float WALL_TRICKLE_WIDTH_SEED = 9.1f;

// 粒の流れをワールド座標に固定し、経過時間で下へ移動させる。
static const float WALL_TRICKLE_MIN_SPEED = 2.5f;
static const float WALL_TRICKLE_MAX_SPEED = 6.0f;
static const float WALL_TRICKLE_SPEED_SEED = 5.3f;
static const float WALL_TRICKLE_BEAD_FREQUENCY = 0.11f;
static const float WALL_TRICKLE_PHASE_SEED_SCALE = 7.0f;

// 天井付近で水が流れ始め、湿った区間からはみ出さないようにする。
static const float WALL_TRICKLE_DAMP_NOISE_SCALE = 0.035f;
static const float WALL_TRICKLE_DAMP_NOISE_SEED = 7.0f;
static const float WALL_TRICKLE_DAMP_BIAS = 0.25f;
static const float WALL_TRICKLE_DAMP_START = 0.56f;
static const float WALL_TRICKLE_DAMP_FULL = 0.72f;

// 水の筋のくねりの幅（ノイズに掛ける量と、線をずらす量）
static const float WALL_TRICKLE_WOBBLE_AMOUNT = 0.6f;
// 流れ落ちる水の粒：粒になる範囲（濃くなり始め・濃くなりきる）と、水の膜の濃さ（最低＋粒の幅）
static const float WALL_TRICKLE_BEAD_START = 0.55f;
static const float WALL_TRICKLE_BEAD_END = 0.95f;
static const float WALL_TRICKLE_FILM_BASE = 0.55f;
static const float WALL_TRICKLE_FILM_RANGE = 0.45f;
// 天井の際から流れ始める速さ（高さの割合に掛ける）と、天井近くの水の量（床に近いほど1）
static const float WALL_TRICKLE_CEILING_FADE = 8.0f;
static const float WALL_TRICKLE_TOP_AMOUNT = 0.55f;

float GetWallTrickle(float3 worldPosition, float3 worldNormal, float damp)
{
    const float3 axis = abs(normalize(worldNormal));
    // 縦の面だけ（天井や床、上を向いた面には流れない）
    const float verticalSurface = saturate(1.0f - axis.y * WALL_TRICKLE_TILT_FADE);
    const float along = axis.x > axis.z ? worldPosition.z : worldPosition.x;

    const float columnWidth = WALL_TRICKLE_COLUMN_WIDTH;
    const float column = floor(along / columnWidth);
    const float local = frac(along / columnWidth) - 0.5f;
    const float seed = FastHash21(float2(column, WALL_TRICKLE_COLUMN_SEED));
    // 水の筋が出る列だけで、流れや濡れ具合を計算する。
    [branch]
    if (seed < WALL_TRICKLE_COLUMN_THRESHOLD || verticalSurface <= 0.0f)
    {
        return 0.0f;
    }
    // 筋の線：列ごとの太さで、高さに沿ってわずかにくねらせている
    const float wobble = (FastValueNoise(float2(column * WALL_TRICKLE_WOBBLE_COLUMN_SCALE, worldPosition.y * WALL_TRICKLE_WOBBLE_HEIGHT_SCALE)) - 0.5f) * WALL_TRICKLE_WOBBLE_AMOUNT;
    const float width = lerp(WALL_TRICKLE_MIN_WIDTH, WALL_TRICKLE_MAX_WIDTH, FastHash21(float2(column, WALL_TRICKLE_WIDTH_SEED)));
    const float trickleLine = 1.0f - smoothstep(width * 0.5f, width, abs(local - wobble * WALL_TRICKLE_WOBBLE_AMOUNT));

    // 上から下へ流れ落ちる水の粒（濃淡の模様を、時間とともに下へずらしている）
    const float speed = lerp(WALL_TRICKLE_MIN_SPEED, WALL_TRICKLE_MAX_SPEED, FastHash21(float2(column, WALL_TRICKLE_SPEED_SEED)));
    const float flowPhase = worldPosition.y * WALL_TRICKLE_BEAD_FREQUENCY + CausticTime * speed * WALL_TRICKLE_BEAD_FREQUENCY + seed * WALL_TRICKLE_PHASE_SEED_SCALE;
    const float beads = smoothstep(WALL_TRICKLE_BEAD_START, WALL_TRICKLE_BEAD_END, frac(flowPhase));
    const float film = WALL_TRICKLE_FILM_BASE + beads * WALL_TRICKLE_FILM_RANGE;

    // 天井の際から流れ始め、床に近いほど水が多い
    const float height = (worldPosition.y - WallFloorY) / WallHeight;
    const float fromCeiling = saturate((1.0f - height) * WALL_TRICKLE_CEILING_FADE);
    const float lowerAmount = lerp(1.0f, WALL_TRICKLE_TOP_AMOUNT, saturate(height));

    // 壁の湿った区間（壁に沿った大きなまだら）だけに流している。床際の水の跡（damp）がある所は、少し流れやすくしている
    const float wetSection = FastValueNoise(float2(along * WALL_TRICKLE_DAMP_NOISE_SCALE, WALL_TRICKLE_DAMP_NOISE_SEED)) + damp * WALL_TRICKLE_DAMP_BIAS;
    const float dampArea = smoothstep(WALL_TRICKLE_DAMP_START, WALL_TRICKLE_DAMP_FULL, wetSection);
    return trickleLine * film * fromCeiling * lowerAmount * dampArea * verticalSurface;
}

// ホラー向けの色調と、遠くの闇の色（wetFloorPSと共通。horrorGrade.hlsli）
#include "horrorGrade.hlsli"
#include "horrorMist.hlsli"
#include "figureShadow.hlsli"
#include "toneMapping.hlsli"


// --- 画素の色の計算（main）の値
// 計算で描く絵の、普段のマテリアルの色（C++ の Stage2Layout と同じ値。異変で変わった色は、この色との比で掛ける）
static const float3 PORTRAIT_BASE_DIFFUSE = float3(SIGNAL_LOST_PORTRAIT_BASE_RGB);
static const float3 CLOCK_BASE_DIFFUSE = float3(SIGNAL_LOST_CLOCK_BASE_RGB);
static const float3 DOOR_BASE_DIFFUSE = float3(SIGNAL_LOST_FALSE_DOOR_BASE_RGB);
// 崩れて現れる・消える物：ノイズの細かさ、3つのノイズ（ずらし・細かさ・割合）、しきい値の広げ方、燃える縁の幅
static const float DISSOLVE_NOISE_SCALE = 0.55f;
static const float DISSOLVE_DEPTH_SKEW = 0.37f;
static const float DISSOLVE_WEIGHT_A = 0.55f;
static const float DISSOLVE_SCALE_B = 2.3f;
static const float DISSOLVE_SEED_B = 7.0f;
static const float DISSOLVE_WEIGHT_B = 0.30f;
static const float DISSOLVE_SCALE_C = 5.1f;
static const float DISSOLVE_SEED_C = 3.0f;
static const float DISSOLVE_WEIGHT_C = 0.15f;
static const float DISSOLVE_THRESHOLD_SCALE = 1.08f;
static const float DISSOLVE_THRESHOLD_OFFSET = 0.04f;
static const float DISSOLVE_EDGE_WIDTH = 0.07f;
// 崩れている縁：焦げて黒ずむ量、燃える赤い光の色と強さ
static const float DISSOLVE_CHAR_DARKEN = 0.55f;
static const float3 DISSOLVE_GLOW_COLOR = float3(0.95f, 0.34f, 0.07f);
static const float DISSOLVE_GLOW_STRENGTH = 1.5f;
// 光る色の強さを平均で求める重みと、光らない面とみなす強さ
static const float3 AVERAGE_WEIGHTS = float3(0.3333f, 0.3333f, 0.3333f);
static const float EMISSION_EPSILON = 0.001f;
// 外の森の材質の雨の濡れ：水の筋の列の細かさ（岩・樹皮・外壁）、水の筋を凹凸に足す量、凹凸の強さ
static const float ROCK_RAIN_COLUMN_SCALE = 0.9f;
static const float BARK_RAIN_COLUMN_SCALE = 1.6f;
static const float FACADE_RAIN_COLUMN_SCALE = 0.7f;
static const float RIVULET_HEIGHT = 0.12f;
static const float FACADE_RIVULET_HEIGHT = 0.10f;
static const float ROCK_BUMP_STRENGTH = 0.85f;
static const float BARK_BUMP_STRENGTH = 1.10f;
static const float FACADE_BUMP_STRENGTH = 0.9f;
// 建物の汚れ：暗くする量、くすんだ緑の色と混ぜ方
static const float GRIME_DARKEN = 0.22f;
static const float3 GRIME_TINT = float3(0.72f, 0.80f, 0.69f);
static const float GRIME_TINT_BLEND = 0.20f;
// 壁の古さ：奥の暗さの効き方、湿りの光沢に足す割合。水の筋で暗くする量
static const float AGEING_CAVITY_DARKEN = 0.55f;
static const float AGEING_DAMP_WETNESS = 0.8f;
static const float TRICKLE_DARKEN = 0.30f;
// 水が引いた柱の根元の湿り：横を向いた面とみなす法線の y、柱からの範囲、水位からの高さの上限、
// 水が引いてから湿りが出きる距離、湿りの上端（最低＋むら）とむらの細かさ、帯の下端（出始め・出きる）と上端のぼかし、色と光沢
static const float DRAIN_SIDE_NORMAL_LIMIT = 0.2f;
static const float DRAIN_COLUMN_HALF_WIDTH = 5.2f;
static const float DRAIN_MAX_HEIGHT = 7.5f;
static const float DRAIN_EXPOSE_DISTANCE = 3.0f;
static const float DRAIN_DAMP_TOP = 4.7f;
static const float DRAIN_DAMP_TOP_RANGE = 1.5f;
static const float DRAIN_DAMP_NOISE_SCALE = 0.55f;
static const float DRAIN_BAND_BOTTOM_START = -0.1f;
static const float DRAIN_BAND_BOTTOM_END = 0.5f;
static const float DRAIN_BAND_TOP_BLUR = 0.9f;
static const float3 DRAIN_DAMP_COLOR = float3(0.51f, 0.56f, 0.55f);
static const float DRAIN_DAMP_WETNESS = 0.50f;
// 2面の血の手形の異変：血の色と混ぜ方、濡れた光沢
static const float3 ANOMALY_BLOOD_COLOR = float3(0.85f, 0.05f, 0.04f);
static const float ANOMALY_BLOOD_BLEND = 0.92f;
static const float ANOMALY_BLOOD_WETNESS = 0.9f;
// 凹凸の強さ：建物の壁（古さで強める量）と小物（壁の半分）、金属の錆の凹凸の効き方
static const float BUILDING_BUMP_STRENGTH = 0.72f;
static const float BUILDING_BUMP_WEATHERING = 0.8f;
static const float PROP_BUMP_STRENGTH = 0.36f;
static const float RUST_HEIGHT_SCALE = 0.6f;
// 点光源：範囲と距離の下限（0で割らないため）、なだらかな当たり方（最低＋当たり方の割合）、横へ残す光の量
static const float LIGHT_EPSILON = 0.001f;
static const float SOFT_LAMBERT_BASE = 0.20f;
static const float SOFT_LAMBERT_RANGE = 0.80f;
static const float FIXTURE_SIDE_LIGHT = 0.22f;
// 懐中電灯に掛ける部屋の角の暗がりの割合
static const float FLASHLIGHT_ROOM_OCCLUSION = 0.4f;
// 技術デモで PBR を止める印（PointShadowOptions.y が 0 か 1）を見るしきい値
#define PBR_DISABLED_THRESHOLD 0.5f

// 画素の色を計算している：元の色 → 汚れと古さ → 環境光・天井灯・懐中電灯 → 濡れた光沢 → 自己発光 → 霧 → 色調
#if defined(PRINTED_SURFACE)
[earlydepthstencil]
#endif
float4 main(in LIT_PS_IN input) : SV_Target
{
    float4 color = input.col;

    // テクスチャがあればその色、なければマテリアルの色を使っている
    if (Material.TextureEnable)
    {
        color *= g_Texture.Sample(g_SamplerState, input.tex);
    }
    else
    {
        color *= Material.Diffuse;
    }
    // 2面の肖像画（portraitPS.hlsl）：元の色を、計算で描いた古い油絵の色に差し替えている。
    // マテリアルの色（肖像画の異変で赤黒く変わる）は、普段の色との比として掛けている。
    float paintingRelief = 0.0f;
    float3 paintingEmission = 0.0f;
#if defined(PORTRAIT_SURFACE)
    {
        // 目の状態は、肖像画の面ではマテリアルの Ambuent（使っていない値）で受け取っている（Wall::SetPortraitEyes）
        const PortraitPaint painting = GetPortraitPainting(saturate(input.tex), Material.Ambuent);
        const float3 portraitBaseDiffuse = PORTRAIT_BASE_DIFFUSE;
        color.rgb = painting.albedo * (Material.Diffuse.rgb / portraitBaseDiffuse);
        paintingRelief = painting.relief;
        paintingEmission = painting.emission;
    }
#endif
    // 2面の時計（clockPS.hlsl）：元の色を、計算で描いた文字盤の色に差し替えている。針の角度はマテリアルの Ambuent で受け取り
    // （Wall::SetClockHands）、マテリアルの色（3周目以降に赤黒く変わる）は、普段の色との比として掛けている。
#if defined(CLOCK_SURFACE)
    {
        const ClockPaint clockPaint = GetClockFace(saturate(input.tex), Material.Ambuent);
        const float3 clockBaseDiffuse = CLOCK_BASE_DIFFUSE;
        color.rgb = clockPaint.albedo * (Material.Diffuse.rgb / clockBaseDiffuse);
        paintingRelief = clockPaint.relief;
    }
#endif
    // 2面の偽の扉（doorPS.hlsl）：元の色を、計算で描いた木の扉の色に差し替えている。取っ手の側はマテリアルの Ambuent で受け取っている
#if defined(DOOR_SURFACE)
    {
        const DoorPaint doorPaint = GetDoorPainting(saturate(input.tex), Material.Ambuent);
        const float3 doorBaseDiffuse = DOOR_BASE_DIFFUSE;
        color.rgb = doorPaint.albedo * (Material.Diffuse.rgb / doorBaseDiffuse);
        paintingRelief = doorPaint.relief;
    }
#endif

    // 崩れて現れる・消える物（Wall::DissolveIn / DissolveOut）：ワールドの位置から作ったノイズが崩れ具合より小さい画素を捨て、
    // 残った所の縁を、焦げて燃えているように赤く光らせている（縁の明るさは下の「自ら光る色」の後で足している）。
    float dissolveEdge = 0.0f;
#if !defined(PRINTED_SURFACE)
    [branch]
    if (Material.Dissolve > 0.0f)
    {
        const float3 dissolvePosition = input.worldPos * DISSOLVE_NOISE_SCALE;
        const float dissolveNoise =
            FastValueNoise(dissolvePosition.xy + dissolvePosition.z * DISSOLVE_DEPTH_SKEW) * DISSOLVE_WEIGHT_A +
            FastValueNoise(dissolvePosition.zy * DISSOLVE_SCALE_B + DISSOLVE_SEED_B) * DISSOLVE_WEIGHT_B +
            FastValueNoise(dissolvePosition.xz * DISSOLVE_SCALE_C - DISSOLVE_SEED_C) * DISSOLVE_WEIGHT_C;
        // 崩れ具合0で何も捨てず、1ですべて捨てるよう、しきい値を少し広げている
        const float dissolveThreshold = Material.Dissolve * DISSOLVE_THRESHOLD_SCALE - DISSOLVE_THRESHOLD_OFFSET;
        clip(dissolveNoise - dissolveThreshold);
        dissolveEdge = 1.0f - smoothstep(0.0f, DISSOLVE_EDGE_WIDTH, dissolveNoise - dissolveThreshold);
    }
#endif

    // デバッグのフルブライト表示では、照明を使わずに返している
    if (IsFullbrightDebugView())
    {
        return GetFullbrightColor(color.rgb, input.viewNormal, input.viewPos);
    }

    // 計算で作る汚れは、不透明でテクスチャのない建材だけに使っている。
    // 光るパネルは汚さず、ブルームと天井の光の明るさを保っている。
    const float emissionEnergy = dot(
        abs(Material.Emission.rgb),
        AVERAGE_WEIGHTS);
    float3 detailWorldNormal = normalize(input.worldNormal);
    float grime = 0.0f;
    float wetness = 0.0f;
    // PBRの金属らしさと粗さ（金属の面は、下で錆と擦れ傷の分だけ場所ごとに変える）
    float pbrMetallic = Material.Metallic;
    float pbrRoughness = GetMaterialRoughness();
    float metalRustHeight = 0.0f;
#if !defined(PRINTED_SURFACE)
    if (!Material.TextureEnable && emissionEnergy < EMISSION_EPSILON)
    {
#if defined(ROCK_SURFACE)
        // 外の森の岩（rockPS.hlsl）：壁の汚れと古さの代わりに、岩の色むら・ひび・苔・地衣類・鉱物の粒・地面際の湿りを
        // 色・粗さ・濡れ具合・凹凸として付けている（rockSurface.hlsli）。粗さは配置ファイルの色の rough を元にしている
        const RockSurface rock = GetRockSurface(input.worldPos, input.worldNormal, color.rgb, pbrRoughness);
        // 雨で濡れた分（暗く・光沢・横の面を流れる水の筋）を足している（forestWetness.hlsli）
        const ForestWetSurface rockWet = ApplyForestRain(
            input.worldPos, input.worldNormal, rock.albedo, rock.wetness, rock.height, ROCK_RAIN_COLUMN_SCALE);
        color.rgb = rockWet.albedo;
        pbrRoughness = rock.roughness;
        wetness = rockWet.wetness;
        detailWorldNormal = ApplyHeightToNormal(
            input.worldPos, normalize(input.worldNormal), rock.height + rockWet.rivulet * RIVULET_HEIGHT, ROCK_BUMP_STRENGTH);
#elif defined(BARK_SURFACE)
        // 外の森の枯れ木・倒木・茂み（barkPS.hlsl）：木目に沿った縦の溝・横のひび・繊維・剥がれて出た地の木・苔・地衣類・根元の湿りを
        // 色・粗さ・濡れ具合・凹凸として付けている（barkSurface.hlsli）。木目の向きはマテリアルの環境の色で受け取っている（Wall::UploadMaterial）
        const BarkSurface bark = GetBarkSurface(
            input.worldPos, input.worldNormal, Material.Ambuent.xyz, color.rgb, pbrRoughness);
        // 雨で濡れた分を足している（樹皮は溝が細かいので、水の筋の列も細かくしている）
        const ForestWetSurface barkWet = ApplyForestRain(
            input.worldPos, input.worldNormal, bark.albedo, bark.wetness, bark.height, BARK_RAIN_COLUMN_SCALE);
        color.rgb = barkWet.albedo;
        pbrRoughness = bark.roughness;
        wetness = barkWet.wetness;
        detailWorldNormal = ApplyHeightToNormal(
            input.worldPos, normalize(input.worldNormal), bark.height + barkWet.rivulet * RIVULET_HEIGHT, BARK_BUMP_STRENGTH);
#elif defined(FACADE_SURFACE)
        // 外の森の研究所の外壁（facadePS.hlsl）：型枠の跡・Pコンの穴・雨だれ・白華・根元の泥と苔・ひびを、
        // 色・粗さ・濡れ具合・凹凸として付けている（facadeSurface.hlsli）。雨で濡れた分と、壁を流れる水の筋も重ねている
        const FacadeSurface facade = GetFacadeSurface(input.worldPos, input.worldNormal, color.rgb, pbrRoughness);
        const ForestWetSurface facadeWet = ApplyForestRain(
            input.worldPos, input.worldNormal, facade.albedo, facade.wetness, facade.height, FACADE_RAIN_COLUMN_SCALE);
        color.rgb = facadeWet.albedo;
        pbrRoughness = facade.roughness;
        wetness = facadeWet.wetness;
        detailWorldNormal = ApplyHeightToNormal(
            input.worldPos, normalize(input.worldNormal), facade.height + facadeWet.rivulet * FACADE_RIVULET_HEIGHT, FACADE_BUMP_STRENGTH);
#else
        grime = GetProceduralGrime(
            input.worldPos,
            input.worldNormal) * WallDampStrength;
        color.rgb *= 1.0f - grime * GRIME_DARKEN;
        color.rgb = lerp(
            color.rgb,
            color.rgb * GRIME_TINT,
            grime * GRIME_TINT_BLEND);

        // 壁の古さ（建物の壁と天井だけ）。0のときは前と同じ見た目になる。
        const float weathering = Material.WeatheringSurface ? WallWeathering : 0.0f;
        const WallAgeing ageing = GetWallAgeing(
            input.worldPos, input.worldNormal, weathering);
        color.rgb *= ageing.tint;
        color.rgb *= 1.0f - ageing.cavity * AGEING_CAVITY_DARKEN;
        wetness = saturate(grime + ageing.damp * AGEING_DAMP_WETNESS);
        // 壁を伝う水の筋：湿った壁の所々を、細い水の筋が流れ落ちている（濡れて少し暗く、光沢が出る）
        const float trickle = weathering > 0.0f
            ? GetWallTrickle(input.worldPos, input.worldNormal, ageing.damp)
            : 0.0f;
        color.rgb *= 1.0f - trickle * TRICKLE_DARKEN;
        wetness = max(wetness, trickle);

        // 水が引いた柱の根元には、元の水位の少し上まで吸い上げた水が残る。
        const float2 fromDrainColumn = abs(input.worldPos.xz - DrainTrace.xy);
        const float heightAboveWater = input.worldPos.y - CausticWaterY;
        [branch]
        if (DrainTrace.w > 0.0f && Material.WeatheringSurface && abs(input.worldNormal.y) < DRAIN_SIDE_NORMAL_LIMIT &&
            max(fromDrainColumn.x, fromDrainColumn.y) < DRAIN_COLUMN_HALF_WIDTH && heightAboveWater < DRAIN_MAX_HEIGHT)
        {
            const float exposed = smoothstep(0.0f, DRAIN_EXPOSE_DISTANCE, DrainTrace.z - input.worldPos.z);
            const float dampTop = DRAIN_DAMP_TOP + FastValueNoise(input.worldPos.xz * DRAIN_DAMP_NOISE_SCALE) * DRAIN_DAMP_TOP_RANGE;
            const float heightFromFloor = heightAboveWater + DrainTrace.w;
            const float band = exposed * smoothstep(DRAIN_BAND_BOTTOM_START, DRAIN_BAND_BOTTOM_END, heightFromFloor) *
                (1.0f - smoothstep(dampTop - DRAIN_BAND_TOP_BLUR, dampTop, heightFromFloor));
            color.rgb *= lerp(1.0f.xxx, DRAIN_DAMP_COLOR, band);
            wetness = max(wetness, band * DRAIN_DAMP_WETNESS);
        }

        // 2面の「血の手形」の異変（その周回だけ、照らすと次の手形が奥に現れる）。濡れた血として光沢も返す
        const float anomalyHandprint = GetAnomalyHandprint(input.worldPos, normalize(input.worldNormal));
        color.rgb *= lerp(1.0f.xxx, ANOMALY_BLOOD_COLOR, anomalyHandprint * ANOMALY_BLOOD_BLEND);
        wetness = max(wetness, anomalyHandprint * ANOMALY_BLOOD_WETNESS);

        // 金属の錆と擦れ傷（金属らしさのある面だけ。pbrLighting.hlsli）。色・金属らしさ・粗さを場所ごとに変えている
        const MetalWear metalWear = GetMetalWear(
            color.rgb, pbrMetallic, pbrRoughness, input.worldPos, input.worldNormal);
        color.rgb = metalWear.baseColor;
        pbrMetallic = metalWear.metallic;
        pbrRoughness = metalWear.roughness;
        metalRustHeight = metalWear.height;

        // 汚れと古さの凹凸で、法線を傾けている（古さが濃いほど強く）。
        // 扉や家具などの小物は、コンクリートの壁より平らな面なので、凹凸を半分にしている
        // （懐中電灯で正面から照らすと、壁と同じ強さでは木の扉が叩いた金属のように見えるため）
        const float bumpStrength = Material.WeatheringSurface
            ? BUILDING_BUMP_STRENGTH * (1.0f + BUILDING_BUMP_WEATHERING * weathering)
            : PROP_BUMP_STRENGTH;
        detailWorldNormal = GetBumpedWorldNormal(
            input.worldPos,
            input.worldNormal,
            ageing.height + paintingRelief + metalRustHeight * RUST_HEIGHT_SCALE,
            bumpStrength);
#endif
    }
#endif
    const float3 detailViewNormal = normalize(mul(
        float4(detailWorldNormal, 0.0f),
        View).xyz);

    // PBRの材質（金属・粗さの方式。pbrLighting.hlsli）。汚れと古さを付けた後の色を元の色にし、濡れた所は粗さを下げている。
    // 拡散反射（lighting）と鏡面反射（specularLighting）を光ごとに分けて足し、最後に材質の色と合わせている。
    const PbrSurface surface = MakePbrSurface(
        color.rgb, pbrMetallic, pbrRoughness, wetness);
    const float3 viewDirection = normalize(-input.viewPos);
    float3 specularLighting = 0.0f;

    // 上下で色を変えた環境光と、DDGI の間接光（壁や床で跳ね返った光）から始めている
    float3 lighting = GetHemisphereAmbient(detailWorldNormal) + GetScreenIndirectLight(input.pos.xy);
    const float distanceFromCamera = length(input.viewPos);

    // 天井の点光源は、パネルだけでなく、近くの床と壁も照らしている。
    uint pointLightListOffset;
    const uint pointLightCount =
        GetPixelLightCount(input.pos.xy, pointLightListOffset);
    // 天井の照明の接触影（棚・机の足元の細い影。一番強く照らしている点光源の向きで求めてある）
    const float screenContactShadow = GetScreenContactShadow(input.pos.xy);
    [loop]
    for (uint i = 0; i < pointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight =
            GetPixelLight(pointLightListOffset, i);

        const float3 offsetToLight =
            pointLight.PositionRange.xyz - input.worldPos;
        const float lightRange = max(pointLight.PositionRange.w, LIGHT_EPSILON);
        const float distanceSquaredToLight = dot(offsetToLight, offsetToLight);
        // 光が届かない距離の光源は、計算を省いている
        [branch]
        if (distanceSquaredToLight >= lightRange * lightRange)
        {
            continue;
        }
        const float distanceToLight = sqrt(distanceSquaredToLight);
        const float3 directionToPointLight =
            offsetToLight / max(distanceToLight, LIGHT_EPSILON);
        float pointAttenuation = saturate(1.0f - distanceToLight / lightRange);
        pointAttenuation *= pointAttenuation;

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

        // 揺れる裸電球の光は、電球との間に物があれば届かない（pointShadow.hlsli）。
        // 物と触れている所の細い影（接触影。SSAOと一緒に求めた画像。screenAmbientOcclusion.hlsli）も掛けている。
        // 光源との間に建物の壁があれば、光は届かない（壁を抜けて隣の部屋を照らさないように。roomOcclusion.hlsli）
        const float3 pointRadiance = pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * pointAttenuation
            * fixtureDistribution
            * screenContactShadow
            * GetRoomLightVisibility(input.worldPos, pointLight.PositionRange.xyz,
                GetPixelLightWallMask(pointLightListOffset, i))
            * GetPointLightShadow(input.worldPos, input.worldNormal, pointLight.PositionRange.xyz)
            * GetUnseenFigureShadow(input.worldPos, pointLight.PositionRange.xyz)
            * GetPlayerBodyShadow(input.worldPos, pointLight.PositionRange.xyz);
        lighting += pointRadiance * softPointLambert;

#if !defined(PRINTED_SURFACE)
        // 鏡面反射は、懐中電灯と同じビュー空間で計算している（光の向きだけをビュー空間へ回している）
        const float3 viewSpaceDirectionToLight = normalize(mul(
            float4(directionToPointLight, 0.0f), View).xyz);
        specularLighting += pointRadiance * EvaluateSpecularGGX(
            surface, detailViewNormal, viewDirection, viewSpaceDirectionToLight);
#endif
    }

    // 部屋の角の暗がり。環境光と天井灯には全部、懐中電灯には一部だけ掛けている（照らせば角も見えるように）。
    // 画面空間の環境遮蔽（SSAO）も重ね、小物の下や物と物の隙間まで暗くしている（懐中電灯には一部だけ）
    const float screenOcclusion = GetScreenAmbientOcclusion(input.pos.xy);
#if defined(PRINTED_SURFACE)
    // 貼り付けた壁までの距離は配置時に分かるので、画素ごとに壁の一覧を走査しない。
    const float roomOcclusion = screenOcclusion * (OcclusionBoxCount > 0
        ? ComputeRoomOcclusion(input.worldPos, detailWorldNormal, Material.Ambuent.x) : 1.0f);
#else
    const float roomOcclusion = GetRoomOcclusion(input.worldPos, normalize(input.worldNormal)) * screenOcclusion;
#endif
    lighting *= roomOcclusion;
    specularLighting *= roomOcclusion;
    const float flashlightOcclusion = lerp(1.0f, roomOcclusion, FLASHLIGHT_ROOM_OCCLUSION);

#if !defined(PRINTED_SURFACE)
    // 環境の映り込み（IBL）：視線を面で反射した向きの景色を、環境マップから粗さに応じたぼやけ具合で読み、
    // 粗さと見る角度に応じた割合で足している。金属は拡散反射を持たないので、この映り込みで金属に見える
    // （非常灯や照明の光だまりが、配管や額縁に赤く・白く映る）。環境マップのない所でも、環境光だけは映している。
    {
        const float nDotV = saturate(dot(detailViewNormal, viewDirection));
        const float3 reflectedView = reflect(-viewDirection, detailViewNormal);
        // ビュー空間の向きをワールドへ戻している（回転の逆 = 転置）
        const float3 reflectedWorld = mul((float3x3)View, reflectedView);
        const float3 environment = max(
            GetHemisphereAmbient(reflectedWorld),
            SampleEnvironment(reflectedWorld, surface.roughness));
        specularLighting += environment
            * EnvironmentBrdfApprox(surface.f0, surface.roughness, nDotV)
            * GetSpecularOcclusion(nDotV, roomOcclusion, surface.roughness);
    }

#endif
    // 懐中電灯の光：円錐の形・距離による弱まり・レンズの模様・面の向き・影を掛け合わせている
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > LIGHT_EPSILON)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
        // 円錐の外では、レンズの汚れのノイズとシャドウマップの参照を丸ごと省いている。
        [branch]
        if (beamProfile > LIGHT_EPSILON)
        {
            const float normalizedDistance = saturate(
                distanceFromCamera / max(Light.Range, LIGHT_EPSILON));
            const float rangeFade = saturate(
                1.0f - normalizedDistance * normalizedDistance);
            const float attenuation = rangeFade * rangeFade;
            const float lensPattern = GetFlashlightLensPattern(pixelDirection);
            const float3 flashlightColor = Light.Diffuse.rgb * GetFlashlightTint(pixelDirection);
            // 実際の光のように距離の2乗で弱まる分を、少しだけ混ぜている
            const float physicalFalloff = rcp(
                1.0f + distanceFromCamera * distanceFromCamera * FLASHLIGHT_PHYSICAL_FALLOFF_SCALE);
            const float naturalAttenuation = attenuation *
                lerp(1.0f, physicalFalloff, FLASHLIGHT_PHYSICAL_FALLOFF_BLEND);

            const float3 normal = detailViewNormal;
            const float3 directionToLight = -pixelDirection;
            const float lambert = saturate(dot(normal, directionToLight));
            // 横から当たる面も真っ暗にならず、正面の面も明るくなりすぎないよう、幅を抑えている（flashlightLighting.hlsli）
            const float softenedLambert = GetFlashlightDiffuseResponse(lambert);

            const float3 flashlightRadiance = flashlightColor
                * Light.Intensity
                * beamProfile
                * naturalAttenuation
                * lensPattern
                * GetFlashlightShadow(input.shadowPos)
                * GetUnseenFigureFlashlightShadow(input.worldPos)
                * flashlightOcclusion;
            lighting += flashlightRadiance * softenedLambert;
#if !defined(PRINTED_SURFACE)
            // 懐中電灯は目のすぐそばにあるので、金属や濡れた面を正面から照らすと、光の芯がはっきり映る
            specularLighting += flashlightRadiance * EvaluateSpecularGGX(
                surface, normal, viewDirection, directionToLight);
#endif
        }
    }

    // 懐中電灯の翼の光：明るい中心の外にも広がる、ぼんやりした弱い光（flashlightLighting.hlsli）
    lighting += GetFlashlightSpillLight(input.viewPos, detailViewNormal) * flashlightOcclusion;

#if !defined(PRINTED_SURFACE)
    // 浸水した床（西棟）や、波の立った水たまりの水面で跳ね返った懐中電灯の光が、壁や天井にゆらゆら映る（waterCaustics.hlsli）
    lighting += Light.Diffuse.rgb * GetWaterCausticLight(input.worldPos, normalize(input.worldNormal));

    // 拡散反射（金属ほど弱い）と鏡面反射を合わせている。湿った壁の冷たい光沢も、濡れて粗さが下がった鏡面反射として出ている。
    // 技術デモでPBRを止めているときは、これまでの拡散反射だけの描き方にしている（金属も色のまま、鏡面反射なし）
    color.rgb = PointShadowOptions.y > PBR_DISABLED_THRESHOLD
        ? color.rgb * lighting
        : surface.diffuseColor * lighting + specularLighting;
#else
    // つや消しの印刷物は、受けた光を紙面の色に掛ける。
    color.rgb *= lighting;
#endif

    // 自ら光る色を足している。0〜1の画面に出したい明るさで決めてあるので、トーンマッピングの後にその明るさで見えるHDRの値に直している（toneMapping.hlsli）
    color.rgb += InverseToneMapEmission(Material.Emission.rgb + paintingEmission);
    // 崩れている縁：焦げて黒ずみ、その外側が燃えるように赤く光る
    color.rgb *= 1.0f - dissolveEdge * DISSOLVE_CHAR_DARKEN;
    color.rgb += DISSOLVE_GLOW_COLOR * (dissolveEdge * dissolveEdge) * DISSOLVE_GLOW_STRENGTH;

    // 高さの違う霧を重ね、近くの見やすさを保ちながら、遠くの輪郭を分けている。
    // 部屋全体を一様に白くせず、床の近くに霧を集めている。
    // 2面の周回が進むほど、霧が近くから濃くなる（LoopDepth。horrorGrade.hlsli）
    const float distanceFog = GetHorrorDistanceFog(distanceFromCamera, LoopDepth);
    const float heightFromFloor = max(input.worldPos.y - SCENE_BASE_FLOOR_Y, 0.0f);
    const float heightDensity = exp(-heightFromFloor * HEIGHT_FOG_DECAY);
    const float nearSuppression =
        smoothstep(HEIGHT_FOG_NEAR_START, HEIGHT_FOG_NEAR_END, distanceFromCamera);
    const float fogVariation = HEIGHT_FOG_BASE_VARIATION +
        FastValueNoise(input.worldPos.xz * HEIGHT_FOG_NOISE_SCALE + HEIGHT_FOG_NOISE_OFFSET) * HEIGHT_FOG_NOISE_WEIGHT;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * HEIGHT_FOG_STRENGTH * GetHorrorHeightFogScale(LoopDepth);
    const float fogFactor = saturate(distanceFog + heightFog);
    // 遠くは灰色のもやではなく、闇に溶けていく
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    color.rgb = lerp(color.rgb, fogColor, fogFactor);
    // 床の近くに溜まって流れる霧（懐中電灯の円錐の中だけ白く浮かぶ。horrorMist.hlsli）
    color.rgb = ApplyHorrorMist(color.rgb, input.worldPos, input.viewPos, lighting);
    // 天井の照明や非常灯から下へ落ちる光の筋（霧に散った光。horrorMist.hlsli）
    color.rgb += GetLightShafts(input.worldPos, pointLightListOffset, pointLightCount, input.pos.xy);
    color.rgb = ApplyFilmicHorrorGrade(color.rgb, LoopDepth);

    // デバッグ表示：1=法線、2=懐中電灯の影、3=照明だけ、6=壁の湿り気、7=タイルごとの光源の数。4・5は床（水たまり・反射）用の表示なので、壁は黒にしている
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
    if (DebugViewMode == DEBUG_VIEW_WALL_DAMP)
    {
        return float4(saturate(grime).xxx, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_LIGHT_TILES)
    {
        return GetLightTileHeatmap(input.pos.xy, pointLightCount);
    }
    // 9 = PBRの材質（赤 = 金属らしさ、緑 = 粗さ、青 = 濡れ具合）
    if (DebugViewMode == DEBUG_VIEW_AMBIENT_OCCLUSION)
    {
        return float4(screenOcclusion.xxx, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_PBR_MATERIAL)
    {
        return float4(surface.metallic, surface.roughness, saturate(wetness), 1.0f);
    }
    if (DebugViewMode >= DEBUG_VIEW_PUDDLE)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return color;
}

