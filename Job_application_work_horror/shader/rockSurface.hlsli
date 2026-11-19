// ============================================================================
// 共通の処理: 外の森の岩の面（rockPS.hlsl）。色・粗さ・濡れ具合・凹凸の高さを、ワールドの位置から計算している（PBRの材質）。
// 主な技術: 3方向から投影したノイズ（トライプラナー）、尾根の形のノイズで作るひび、面の向きで決める苔、
//           マス目の中で位置をばらつかせた鉱物の粒（粗さが低く、懐中電灯の光の芯を点々と返す）、
//           画素の大きさ（footprint）で細かい模様を薄めるちらつき対策
//
// 岩のモデル（forest_rock）は、潰れた球を重ねた形で、UVを持たない。そこで、模様は面の向きに合わせて
// x・y・z の3方向から投影したノイズを混ぜて作り、どの向きの面でも模様が伸びないようにしている。
// 目の高さは地面から約40。岩の大きさは幅9〜19。
// ============================================================================
#ifndef ROCK_SURFACE_INCLUDED
#define ROCK_SURFACE_INCLUDED

// 鉱物の粒は狭く光り、苔やひびの表面は広く光を散らす。
static const float ROCK_TONE_ROUGHNESS_VARIATION = 0.12f;
static const float ROCK_GRAIN_ROUGHNESS_VARIATION = 0.06f;
static const float ROCK_MOSS_ROUGHNESS = 0.97f;
static const float ROCK_CRACK_ROUGHNESS = 0.92f;
static const float ROCK_GLINT_ROUGHNESS = 0.18f;

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 岩の1画素分の材質
struct RockSurface
{
    float3 albedo;      // 色（PBRの元の色）
    float roughness;    // 粗さ（0.045〜1）
    float wetness;      // 濡れ具合（0〜1。地面に近いほど湿っている）
    float height;       // 凹凸の高さ（へこみは負。ApplyHeightToNormal で法線を傾ける）
};

// 森の地面の高さ（配置ファイルの地面と同じ y=-100）
static const float RockGroundY = -100.0f;

// 面の向きに合わせて、3方向から投影したノイズを混ぜている（weights は3方向の重みで、合計1）
float RockTriplanarNoise(float3 position, float3 weights, float scale, float offset)
{
    const float3 p = position * scale + offset;
    return FastValueNoise(p.yz) * weights.x +
        FastValueNoise(p.xz) * weights.y +
        FastValueNoise(p.xy) * weights.z;
}

// 尾根の形のノイズ（ノイズが0.5を横切る所だけ1に近い）。ひびの線に使っている
float RockRidge(float3 position, float3 weights, float scale, float offset)
{
    return 1.0f - abs(RockTriplanarNoise(position, weights, scale, offset) * 2.0f - 1.0f);
}

// 鉱物の粒（0〜1）。3方向のうち一番近い面へ投影し、マス目ごとに乱数で粒があるか・マス目の中の位置を決めている
// （位置をばらつかせないと、粒がマス目どおりに並んで水玉模様に見えるため）。radius は粒の半径（ワールドの単位）
float RockGrainSpark(float3 position, float3 weights, float scale, float radius, float footprint)
{
    const float3 p = position * scale;
    const float2 uv = weights.y > max(weights.x, weights.z)
        ? p.xz
        : (weights.x > weights.z ? p.yz : p.xy);
    const float2 cell = floor(uv);
    const float present = step(0.78f, FastHash21(cell + 37.0f));
    const float2 center = 0.15f + 0.70f * float2(FastHash21(cell + 13.0f), FastHash21(cell + 71.0f));
    const float distanceToGrain = length(frac(uv) - center) / scale;
    return present * (1.0f - smoothstep(radius * 0.4f, radius + footprint, distanceToGrain));
}

// baseColor は配置ファイルの色（頂点の環境遮蔽を掛けた後）、baseRoughness はその色の粗さ（rough。rockColor は0.85）
// 岩の縞の密度とゆがみを別に調整する。
#define ROCK_BAND_WARP_NOISE_SCALE 0.15f
#define ROCK_BAND_WARP_NOISE_SEED 41.0f
#define ROCK_BAND_HEIGHT_RATE 1.35f
#define ROCK_BAND_WARP_GAIN 5.0f

// ひびの太さ・間隔が同じにならないよう、太い線と細い線を重ねて間引く。
#define ROCK_CRACK_WARP_SCALE 1.3f
#define ROCK_CRACK_WARP_X_SEED 3.0f
#define ROCK_CRACK_WARP_Y_SEED 19.0f
#define ROCK_CRACK_WARP_Z_SEED 37.0f
#define ROCK_CRACK_WARP_GAIN 1.6f
#define ROCK_CRACK_MAIN_START 0.93f
#define ROCK_CRACK_MAIN_FULL 0.99f
#define ROCK_CRACK_MAIN_SCALE 0.27f
#define ROCK_CRACK_MAIN_SEED 7.7f
#define ROCK_CRACK_FINE_START 0.95f
#define ROCK_CRACK_FINE_FULL 0.995f
#define ROCK_CRACK_FINE_SCALE 0.61f
#define ROCK_CRACK_FINE_SEED 53.0f
#define ROCK_CRACK_FINE_WEIGHT 0.7f
#define ROCK_CRACK_MASK_START 0.45f
#define ROCK_CRACK_MASK_FULL 0.66f
#define ROCK_CRACK_MASK_SCALE 0.23f
#define ROCK_CRACK_MASK_SEED 29.0f

// 石のざらつきは複数の密度で作り、鉱物の反射点は別に置く。
#define ROCK_GRAIN_NOISE_SCALE 2.1f
#define ROCK_GRAIN_NOISE_SEED 5.5f
#define ROCK_FINE_GRAIN_NOISE_SCALE 5.3f
#define ROCK_FINE_GRAIN_NOISE_SEED 11.0f
#define ROCK_GLINT_GRID_SCALE 2.6f
#define ROCK_GLINT_GRAIN_RADIUS 0.045f

// 上向きの面の苔と、岩の側面にも付く地衣類の分布。
#define ROCK_MOSS_UP_START 0.20f
#define ROCK_MOSS_UP_FULL 0.75f
#define ROCK_MOSS_PATCH_START 0.42f
#define ROCK_MOSS_PATCH_FULL 0.66f
#define ROCK_MOSS_PATCH_SCALE 0.11f
#define ROCK_MOSS_PATCH_SEED 61.0f
#define ROCK_MOSS_GRAIN_GAIN 0.18f
#define ROCK_LICHEN_SPOT_START 0.62f
#define ROCK_LICHEN_SPOT_FULL 0.70f
#define ROCK_LICHEN_SPOT_SCALE 0.85f
#define ROCK_LICHEN_SPOT_SEED 83.0f
#define ROCK_LICHEN_AREA_START 0.50f
#define ROCK_LICHEN_AREA_FULL 0.70f
#define ROCK_LICHEN_AREA_SCALE 0.09f
#define ROCK_LICHEN_AREA_SEED 97.0f

RockSurface GetRockSurface(float3 worldPosition, float3 worldNormal, float3 baseColor, float baseRoughness)
{
    RockSurface rock;
    const float3 normal = normalize(worldNormal);
    // 投影の重み：面の向きに近い軸ほど強くし（4乗）、斜めの面で模様がぼやけて混ざる幅を狭めている
    float3 weights = pow(abs(normal), 4.0f);
    weights /= max(weights.x + weights.y + weights.z, 0.0001f);
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている
    const float footprint = max(length(fwidth(worldPosition)), 0.0001f);
    const float fineFade = saturate(1.0f - footprint / 0.8f);

    // --- 大きな色むら（岩ごと・場所ごとの色の違い）と、うっすらした地層の縞
    const float tone = RockTriplanarNoise(worldPosition, weights, 0.07f, 3.1f) * 0.65f +
        RockTriplanarNoise(worldPosition, weights, 0.21f, 17.4f) * 0.35f;
    const float bandWarp = RockTriplanarNoise(worldPosition, weights, ROCK_BAND_WARP_NOISE_SCALE, ROCK_BAND_WARP_NOISE_SEED);
    const float band = 0.5f + 0.5f * sin(worldPosition.y * ROCK_BAND_HEIGHT_RATE + bandWarp * ROCK_BAND_WARP_GAIN);

    // --- ひび（尾根のノイズの細い線を、別のノイズで途切れさせている）
    // 等高線のままだと、丸い起伏をなぞった輪の形になるため、座標を細かいノイズで揺らして線をギザギザに折り、
    // 大きさの違う2本の線を重ねて、輪に見えないようにしている
    const float3 crackWarp = float3(
        RockTriplanarNoise(worldPosition, weights, ROCK_CRACK_WARP_SCALE, ROCK_CRACK_WARP_X_SEED),
        RockTriplanarNoise(worldPosition, weights, ROCK_CRACK_WARP_SCALE, ROCK_CRACK_WARP_Y_SEED),
        RockTriplanarNoise(worldPosition, weights, ROCK_CRACK_WARP_SCALE, ROCK_CRACK_WARP_Z_SEED)) - 0.5f;
    const float3 crackPosition = worldPosition + crackWarp * ROCK_CRACK_WARP_GAIN;
    const float crackLine = max(
        smoothstep(ROCK_CRACK_MAIN_START, ROCK_CRACK_MAIN_FULL, RockRidge(crackPosition, weights, ROCK_CRACK_MAIN_SCALE, ROCK_CRACK_MAIN_SEED)),
        smoothstep(ROCK_CRACK_FINE_START, ROCK_CRACK_FINE_FULL, RockRidge(crackPosition, weights, ROCK_CRACK_FINE_SCALE, ROCK_CRACK_FINE_SEED)) * ROCK_CRACK_FINE_WEIGHT);
    const float crackPieces = smoothstep(ROCK_CRACK_MASK_START, ROCK_CRACK_MASK_FULL, RockTriplanarNoise(worldPosition, weights, ROCK_CRACK_MASK_SCALE, ROCK_CRACK_MASK_SEED));
    const float crack = crackLine * crackPieces;

    // --- 細かいざらつき（遠くでは薄める）
    const float grain = RockTriplanarNoise(worldPosition, weights, ROCK_GRAIN_NOISE_SCALE, ROCK_GRAIN_NOISE_SEED);
    const float grainFine = RockTriplanarNoise(worldPosition, weights, ROCK_FINE_GRAIN_NOISE_SCALE, ROCK_FINE_GRAIN_NOISE_SEED);

    // --- 鉱物の粒（石英や雲母）：ところどころの小さな粒だけ、なめらかで明るい。懐中電灯で照らすと点々と光る
    const float glint = RockGrainSpark(worldPosition, weights, ROCK_GLINT_GRID_SCALE, ROCK_GLINT_GRAIN_RADIUS, footprint) * fineFade;

    // --- 苔：上を向いた面の、大きなまだらの中に生えている。暗い緑で、光をほとんど返さない
    const float upward = smoothstep(ROCK_MOSS_UP_START, ROCK_MOSS_UP_FULL, normal.y);
    const float mossPatch = smoothstep(ROCK_MOSS_PATCH_START, ROCK_MOSS_PATCH_FULL,
        RockTriplanarNoise(worldPosition, weights, ROCK_MOSS_PATCH_SCALE, ROCK_MOSS_PATCH_SEED) + grain * ROCK_MOSS_GRAIN_GAIN);
    const float moss = upward * mossPatch;

    // --- 地衣類：横を向いた面にも付く、白っぽい灰緑の小さな丸い斑点
    const float lichenSpots = smoothstep(ROCK_LICHEN_SPOT_START, ROCK_LICHEN_SPOT_FULL, RockTriplanarNoise(worldPosition, weights, ROCK_LICHEN_SPOT_SCALE, ROCK_LICHEN_SPOT_SEED));
    const float lichenArea = smoothstep(ROCK_LICHEN_AREA_START, ROCK_LICHEN_AREA_FULL, RockTriplanarNoise(worldPosition, weights, ROCK_LICHEN_AREA_SCALE, ROCK_LICHEN_AREA_SEED));
    const float lichen = lichenSpots * lichenArea * (1.0f - moss) * fineFade;

    // --- 地面との境目の湿り（地面の水を吸い上げて黒ずみ、少し光る）
    const float heightFromGround = worldPosition.y - RockGroundY;
    const float groundDamp = 1.0f - smoothstep(0.5f, 3.5f + grain * 1.5f, heightFromGround);

    // 色：元の色に、色むら（暖かい灰色〜冷たい灰色）・縞・ざらつきを掛け、ひびの奥を暗く、苔・地衣類・粒の色を混ぜている
    const float3 warmGray = baseColor * float3(1.08f, 1.00f, 0.92f);
    const float3 coolGray = baseColor * float3(0.94f, 0.98f, 1.04f);
    float3 albedo = lerp(coolGray, warmGray, saturate(tone * 1.4f - 0.2f));
    albedo *= 0.84f + tone * 0.40f;
    albedo *= 0.94f + band * 0.10f;
    albedo *= lerp(1.0f, 0.86f + grain * 0.22f + grainFine * 0.08f, fineFade);
    albedo *= 1.0f - crack * 0.60f;
    albedo = lerp(albedo, float3(0.075f, 0.095f, 0.045f), moss * 0.85f);
    albedo = lerp(albedo, float3(0.30f, 0.32f, 0.27f), lichen * 0.70f);
    albedo = lerp(albedo, albedo * 1.25f + 0.01f, glint);
    albedo *= 1.0f - groundDamp * 0.30f;
    rock.albedo = albedo;

    // 粗さ：元の粗さを色むらで少し揺らし、苔はさらに粗く、鉱物の粒はなめらかにしている（濡れた所は MakePbrSurface が下げる）
    float roughness = baseRoughness + (tone - 0.5f) * ROCK_TONE_ROUGHNESS_VARIATION + (grain - 0.5f) * ROCK_GRAIN_ROUGHNESS_VARIATION * fineFade;
    roughness = lerp(roughness, ROCK_MOSS_ROUGHNESS, moss);
    roughness = lerp(roughness, ROCK_CRACK_ROUGHNESS, crack);
    roughness = lerp(roughness, ROCK_GLINT_ROUGHNESS, glint);
    rock.roughness = clamp(roughness, 0.045f, 1.0f);

    // 濡れ具合：地面との境目と、ひびの奥にたまった水
    rock.wetness = saturate(groundDamp * 0.55f + crack * 0.25f);

    // 高さ：ゆるい起伏・細かいざらつき・ひびのへこみ・苔の盛り上がり
    const float lumps = RockTriplanarNoise(worldPosition, weights, 0.45f, 23.0f);
    rock.height = lumps * 0.55f +
        (grain * 0.22f + grainFine * 0.08f) * fineFade -
        crack * 0.55f +
        moss * 0.18f * grainFine;
    return rock;
}

#endif
