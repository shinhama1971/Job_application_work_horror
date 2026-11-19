// ============================================================================
// 共通の処理: 外の森の研究所の外壁（facadePS.hlsl）。色・粗さ・濡れ具合・凹凸の高さを、ワールドの位置から計算している（PBRの材質）。
// 長く放置された打ちっぱなしコンクリートの建物として、次の物を描いている。
//   ・型枠のパネル（横22・縦44。約0.9m×1.8m）の継ぎ目と、パネルごとの色の違い、型枠の板の木目の跡
//   ・Pコンの穴（型枠を留めた金物の跡。パネルごとに2列×3段の丸いくぼみ）
//   ・窓の下の水切りと屋上の縁から垂れた、黒い雨だれの跡（縦の帯。上ほど濃く、下へ薄れる）
//   ・継ぎ目からにじみ出た白い白華（エフロレッセンス）
//   ・根元の泥はねと、湿った所に生えた苔・藻
//   ・ひび
// 主な技術: 面に沿った2次元の座標（縦の面は横の位置と高さ、上を向いた面は x・z）、マス目ごとの乱数、
//           画素の大きさ（footprint）で細かい模様を薄めるちらつき対策
// 目の高さは地面から約40、1階の高さは50（窓の下の水切りは、各階の床から約19の高さ）、建物の高さは150。
// ============================================================================
#ifndef FACADE_SURFACE_INCLUDED
#define FACADE_SURFACE_INCLUDED

// 外壁は雨だれを滑らかに、苔を乾いた面より粗くする。
static const float FACADE_TONE_ROUGHNESS_VARIATION = 0.10f;
static const float FACADE_DRIP_ROUGHNESS = 0.70f;
static const float FACADE_DRIP_ROUGHNESS_BLEND = 0.6f;
static const float FACADE_MOSS_ROUGHNESS = 0.97f;

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 外壁の1画素分の材質
struct FacadeSurface
{
    float3 albedo;      // 色（PBRの元の色）
    float roughness;    // 粗さ
    float wetness;      // 濡れ具合（0〜1）
    float height;       // 凹凸の高さ（へこみは負）
};

// 森の地面の高さ、1階の高さ、窓の下の水切りの床からの高さ、建物の屋上の高さ（地面から）
static const float FacadeGroundY = -100.0f;
static const float FacadeStoryHeight = 50.0f;
static const float FacadeSillHeight = 19.0f;
static const float FacadeRoofHeight = 150.0f;

// 雨が当たる列は場所でばらつかせ、筋の長さも列ごとに変える。
#define FACADE_DRIP_COLUMN_WIDTH 4.0f
#define FACADE_DRIP_REGION_MIN 0.35f
#define FACADE_DRIP_REGION_FADE_START 0.25f
#define FACADE_DRIP_REGION_FADE_FULL 0.60f
#define FACADE_DRIP_REGION_NOISE_SCALE 0.07f
#define FACADE_DRIP_LENGTH_MIN_RATIO 0.25f
#define FACADE_DRIP_LENGTH_SEED_OFFSET 7.3f

// 筋は流れ始めが太く、下に向かって細くしながら少しくねらせる。
#define FACADE_DRIP_TOP_HALF_WIDTH 1.2f
#define FACADE_DRIP_BOTTOM_HALF_WIDTH 0.2f
#define FACADE_DRIP_WIDTH_MIN_RATIO 0.35f
#define FACADE_DRIP_WIDTH_SEED_OFFSET 3.1f
#define FACADE_DRIP_CENTER_MIN 0.25f
#define FACADE_DRIP_CENTER_SEED_OFFSET 19.0f
#define FACADE_DRIP_WOBBLE_COLUMN_SCALE 2.3f
#define FACADE_DRIP_WOBBLE_HEIGHT_SCALE 0.12f
#define FACADE_DRIP_WOBBLE_OFFSET_GAIN 0.15f

// 縦の帯の雨だれ（0〜1）。startHeight は垂れ始める高さ（地面から）、height は今の画素の高さ、column は横の位置。
// 帯は幅がまちまちで、垂れ始めは濃く、下へ行くほど細く薄れていく
float FacadeDripStreak(float along, float height, float startHeight, float maxLength, float density, float seed, float footprint)
{
    const float below = startHeight - height;
    if (below < 0.0f || below > maxLength)
    {
        return 0.0f;
    }
    // 列の幅は4。列ごとに筋があるかを決め、筋の位置も列の中でずらしている（等間隔のしま模様に見えないように）
    const float columnWidth = FACADE_DRIP_COLUMN_WIDTH;
    const float column = floor(along / columnWidth);
    const float columnSeed = FastHash21(float2(column, seed));
    // 筋の多い所と少ない所を、まだら（雨の当たり方の違い）で分けている（まったく出ない所は作らない）
    const float region = lerp(FACADE_DRIP_REGION_MIN, 1.0f, smoothstep(FACADE_DRIP_REGION_FADE_START, FACADE_DRIP_REGION_FADE_FULL, FastValueNoise(float2(along * FACADE_DRIP_REGION_NOISE_SCALE, seed))));
    const float present = step(1.0f - density * region, columnSeed);
    const float lengthScale = lerp(FACADE_DRIP_LENGTH_MIN_RATIO, 1.0f, FastHash21(float2(column, seed + FACADE_DRIP_LENGTH_SEED_OFFSET)));
    const float streakLength = maxLength * lengthScale;
    // 下へ行くほど細くなり、薄れる
    const float progress = saturate(below / max(streakLength, 0.001f));
    const float halfWidth = lerp(FACADE_DRIP_TOP_HALF_WIDTH, FACADE_DRIP_BOTTOM_HALF_WIDTH, progress) * lerp(FACADE_DRIP_WIDTH_MIN_RATIO, 1.0f, FastHash21(float2(column, seed + FACADE_DRIP_WIDTH_SEED_OFFSET)));
    // 帯の中心を、列の中でずらし、高さに沿ってわずかにくねらせている
    const float center = FACADE_DRIP_CENTER_MIN + 0.5f * FastHash21(float2(column, seed + FACADE_DRIP_CENTER_SEED_OFFSET));
    const float wobble = (FastValueNoise(float2(column * FACADE_DRIP_WOBBLE_COLUMN_SCALE, height * FACADE_DRIP_WOBBLE_HEIGHT_SCALE)) - 0.5f) * 0.5f;
    const float centerOffset = abs(frac(along / columnWidth) - center - wobble * FACADE_DRIP_WOBBLE_OFFSET_GAIN) * columnWidth;
    const float band = 1.0f - smoothstep(halfWidth * 0.4f, halfWidth + footprint, centerOffset);
    return present * band * (1.0f - progress * progress) *
        (0.6f + 0.4f * FastValueNoise(float2(column * 5.1f, height * 0.6f)));
}

// つなぎ目と木目の大きさを別にし、型枠の跡が読めるようにする。
#define FACADE_PANEL_SIZE float2(22.0f, 44.0f)
#define FACADE_PANEL_SEAM_CORE 0.08f
#define FACADE_PANEL_SEAM_EDGE 0.30f
#define FACADE_PANEL_SHADE_SEED 11.0f
#define FACADE_BOARD_GRAIN_X 0.06f
#define FACADE_BOARD_GRAIN_Y 1.4f
#define FACADE_BOARD_GRAIN_PANEL_SEED_SCALE 3.7f

// くぼみをパネルごとに配置し、画素が広いところでは縁をなじませる。
#define FACADE_TIE_HOLE_GRID float2(2.0f, 3.0f)
#define FACADE_TIE_HOLE_CORE 0.30f
#define FACADE_TIE_HOLE_EDGE 0.55f

// 水切りからの短い筋と、屋上からの長い筋をそれぞれ調整する。
#define FACADE_DRIP_SILL_HEIGHT_OFFSET 0.6f
#define FACADE_DRIP_SILL_MAX_LENGTH 32.0f
#define FACADE_DRIP_SILL_DENSITY 0.70f
#define FACADE_DRIP_SILL_SEED 41.0f
#define FACADE_DRIP_ROOF_MAX_LENGTH 90.0f
#define FACADE_DRIP_ROOF_DENSITY 0.65f
#define FACADE_DRIP_ROOF_SEED 77.0f
#define FACADE_DRIP_ROOF_WEIGHT 0.9f

// 白華は継ぎ目の一部にだけ出し、下へ行くほど薄くする。
#define FACADE_EFFLORESCENCE_PANEL_THRESHOLD 0.88f
#define FACADE_EFFLORESCENCE_PANEL_SEED 53.0f
#define FACADE_EFFLORESCENCE_COLUMN_SCALE 0.55f
#define FACADE_EFFLORESCENCE_ROW_SCALE 7.0f
#define FACADE_EFFLORESCENCE_COLUMN_START 0.55f
#define FACADE_EFFLORESCENCE_COLUMN_FULL 0.75f
#define FACADE_EFFLORESCENCE_FADE_END 7.0f

// baseColor は配置ファイルの色（concrete）、baseRoughness はその粗さ
FacadeSurface GetFacadeSurface(float3 worldPosition, float3 worldNormal, float3 baseColor, float baseRoughness)
{
    FacadeSurface facade;
    const float3 normal = normalize(worldNormal);
    const float3 axis = abs(normal);
    const bool vertical = axis.y < 0.5f;
    // 面に沿った2次元の座標：縦の面は（横の位置, 地面からの高さ）、上や下を向いた面は（x, z）
    const float heightFromGround = worldPosition.y - FacadeGroundY;
    const float along = axis.x > axis.z ? worldPosition.z : worldPosition.x;
    const float2 uv = vertical ? float2(along, heightFromGround) : worldPosition.xz;
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている
    const float footprint = max(length(fwidth(uv)), 0.0001f);
    const float fineFade = saturate(1.0f - footprint / 0.6f);

    // --- 型枠のパネル（横22・縦44）の継ぎ目と、パネルごとの色の違い
    const float2 panelSize = FACADE_PANEL_SIZE;
    const float2 panelUV = uv / panelSize;
    const float2 panelId = floor(panelUV);
    const float2 local = frac(panelUV);
    const float2 edgeDistance = (0.5f - abs(local - 0.5f)) * panelSize;
    const float seam = 1.0f - smoothstep(FACADE_PANEL_SEAM_CORE, FACADE_PANEL_SEAM_EDGE + footprint, min(edgeDistance.x, edgeDistance.y));
    const float panelShade = FastHash21(panelId + FACADE_PANEL_SHADE_SEED);

    // --- 型枠の板の木目の跡（パネルの中の、横に長いうっすらした縞）
    const float boardGrain = FastValueNoise(float2(uv.x * FACADE_BOARD_GRAIN_X, uv.y * FACADE_BOARD_GRAIN_Y) + panelId * FACADE_BOARD_GRAIN_PANEL_SEED_SCALE);

    // --- Pコンの穴（パネルごとに2列×3段。丸く浅いくぼみ）
    float tieHole = 0.0f;
    [branch]
    if (vertical && fineFade > 0.0f)
    {
        const float2 holeGrid = FACADE_TIE_HOLE_GRID;
        const float2 holeCell = frac(local * holeGrid) - 0.5f;
        const float holeDistance = length(holeCell * panelSize / holeGrid);
        tieHole = (1.0f - smoothstep(FACADE_TIE_HOLE_CORE, FACADE_TIE_HOLE_EDGE + footprint, holeDistance)) * fineFade;
    }

    // --- 大きな色むら（雨の当たり方と、打った日の違い）
    const float tone = FastValueNoise(uv * 0.025f + 5.0f) * 0.6f + FastValueNoise(uv * 0.09f + 13.0f) * 0.4f;

    // --- 雨だれの黒い跡（縦の面だけ）：各階の窓の下の水切りと、屋上の縁から垂れている
    float drip = 0.0f;
    float efflorescence = 0.0f;
    [branch]
    if (vertical)
    {
        // 今の高さの真上にある、水切りの高さ（各階の床 + 19）
        const float storyIndex = ceil((heightFromGround - FacadeSillHeight) / FacadeStoryHeight);
        const float sillAbove = storyIndex * FacadeStoryHeight + FacadeSillHeight;
        const float fromSill = sillAbove < FacadeRoofHeight
            ? FacadeDripStreak(along, heightFromGround, sillAbove - FACADE_DRIP_SILL_HEIGHT_OFFSET, FACADE_DRIP_SILL_MAX_LENGTH, FACADE_DRIP_SILL_DENSITY, FACADE_DRIP_SILL_SEED, footprint)
            : 0.0f;
        // 屋上の縁からの、長い雨だれ（建物の上の方ほど濃い）
        const float fromRoof = FacadeDripStreak(along, heightFromGround, FacadeRoofHeight, FACADE_DRIP_ROOF_MAX_LENGTH, FACADE_DRIP_ROOF_DENSITY, FACADE_DRIP_ROOF_SEED, footprint);
        drip = saturate(max(fromSill, fromRoof * FACADE_DRIP_ROOF_WEIGHT));
        // 白華：パネルの横の継ぎ目からにじんで、少しだけ下へ垂れた白い跡（1割ほどのパネル）
        const float effloPanel = step(FACADE_EFFLORESCENCE_PANEL_THRESHOLD, FastHash21(panelId + FACADE_EFFLORESCENCE_PANEL_SEED));
        const float belowJoint = (1.0f - local.y) * panelSize.y;
        const float effloColumn = FastValueNoise(float2(uv.x * FACADE_EFFLORESCENCE_COLUMN_SCALE, panelId.y * FACADE_EFFLORESCENCE_ROW_SCALE));
        efflorescence = effloPanel * smoothstep(FACADE_EFFLORESCENCE_COLUMN_START, FACADE_EFFLORESCENCE_COLUMN_FULL, effloColumn) *
            (1.0f - smoothstep(0.5f, FACADE_EFFLORESCENCE_FADE_END, belowJoint)) * fineFade;
    }

    // --- 根元の泥はね（地面から5くらいまで）と、湿った所の苔・藻（地面から20くらいまで、まだらに）
    const float splash = vertical ? 1.0f - smoothstep(1.5f, 6.0f + tone * 3.0f, heightFromGround) : 0.0f;
    const float mossArea = smoothstep(0.50f, 0.72f, FastValueNoise(uv * 0.05f + 31.0f) + drip * 0.15f);
    const float mossLow = vertical ? 1.0f - smoothstep(6.0f, 22.0f + tone * 10.0f, heightFromGround) : 0.6f;
    const float moss = mossArea * mossLow * (0.6f + 0.4f * FastValueNoise(uv * 0.6f + 7.0f));

    // --- ひび（ノイズの等高線を細い線にし、別のノイズで出る場所を絞っている）
    float crack = 0.0f;
    [branch]
    if (fineFade > 0.0f)
    {
        const float2 crackWarp = float2(FastValueNoise(uv * 0.7f + 3.0f), FastValueNoise(uv * 0.7f + 17.0f)) - 0.5f;
        const float crackLine = 1.0f - smoothstep(0.0f, 0.014f + footprint * 0.04f,
            abs(FastValueNoise(uv * 0.07f + 61.0f + crackWarp * 0.25f) - 0.5f));
        const float crackMask = smoothstep(0.60f, 0.76f, FastValueNoise(uv * 0.018f + 9.0f));
        crack = crackLine * crackMask * fineFade;
    }

    // 色：元の色に、色むら・パネルの違い・木目を掛け、継ぎ目・穴・ひびの奥を暗く、雨だれ・白華・泥・苔の色を混ぜている
    float3 albedo = baseColor * (0.80f + tone * 0.35f) * (0.93f + panelShade * 0.12f);
    albedo *= lerp(1.0f, 0.94f + boardGrain * 0.10f, fineFade);
    albedo *= 1.0f - seam * 0.35f - tieHole * 0.55f - crack * 0.50f;
    albedo = lerp(albedo, albedo * float3(0.36f, 0.38f, 0.36f), drip * 0.85f);
    albedo = lerp(albedo, float3(0.62f, 0.63f, 0.60f), efflorescence * 0.55f);
    albedo = lerp(albedo, albedo * float3(0.55f, 0.48f, 0.40f), splash * 0.70f);
    albedo = lerp(albedo, float3(0.07f, 0.10f, 0.05f), moss * 0.80f);
    facade.albedo = albedo;

    // 粗さ：コンクリートは粗く、雨だれの所は少しなめらか、苔はさらに粗い（濡れた所は MakePbrSurface が下げる）
    float roughness = baseRoughness + (tone - 0.5f) * FACADE_TONE_ROUGHNESS_VARIATION;
    roughness = lerp(roughness, FACADE_DRIP_ROUGHNESS, drip * FACADE_DRIP_ROUGHNESS_BLEND);
    roughness = lerp(roughness, FACADE_MOSS_ROUGHNESS, moss);
    facade.roughness = clamp(roughness, 0.10f, 1.0f);

    // 濡れ具合：雨だれの跡は水の通り道なので湿っていて、根元と苔の所も湿っている
    facade.wetness = saturate(drip * 0.45f + splash * 0.35f + moss * 0.40f + seam * 0.10f);

    // 高さ：継ぎ目・Pコンの穴・ひびのへこみと、木目のわずかな凹凸、苔の盛り上がり
    facade.height = -(seam * 0.55f + tieHole * 0.90f + crack * 0.35f) +
        boardGrain * 0.10f * fineFade + moss * 0.12f;
    return facade;
}

#endif
