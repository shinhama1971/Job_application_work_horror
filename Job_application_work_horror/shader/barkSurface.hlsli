// ============================================================================
// 共通の処理: 外の森の枯れ木・倒木・枯れた茂みの樹皮（barkPS.hlsl）。色・粗さ・濡れ具合・凹凸の高さを、ワールドの位置から計算している（PBRの材質）。
// 主な技術: 木目の向きに合わせた座標（木目の向きを y にした向きへ回してから、3方向に投影するノイズ）、
//           まわりの向きに並べて木目に沿って揺らした縦の溝と、溝の間の板の横のひび、剥がれて地の木が出た所、苔と地衣類、
//           格子の形を消すための、回した2つの格子のノイズの平均、
//           画素の大きさ（footprint）で細かい模様を薄めるちらつき対策
//
// 木のモデルは「先の細い円柱」を重ねた形で、樹皮の筋は幹や枝に沿って走る。シェーダーには、モデルの木目の向き
// （木と茂みは上、倒木は長い向き。Wall::UploadMaterial がマテリアルの使っていない環境の色で渡す）だけを渡し、
// その向きを y にした座標で、筋の向き（y）にはゆっくり、まわり（x・z）には細かく変わるノイズを使っている。
// 斜めの枝では筋が幹ほど正確に枝に沿わないが、細く遠いので目立たない。目の高さは地面から約40、幹の太さは半径2〜4。
// ============================================================================
#ifndef BARK_SURFACE_INCLUDED
#define BARK_SURFACE_INCLUDED

// 溝と苔は光を広く散らし、剥がれた樹皮の面は光沢を残す。
static const float BARK_TONE_ROUGHNESS_VARIATION = 0.08f;
static const float BARK_FURROW_ROUGHNESS = 0.98f;
static const float BARK_FURROW_ROUGHNESS_BLEND = 0.7f;
static const float BARK_PEELED_ROUGHNESS = 0.62f;
static const float BARK_MOSS_ROUGHNESS = 0.97f;

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 樹皮の1画素分の材質
struct BarkSurface
{
    float3 albedo;      // 色（PBRの元の色）
    float roughness;    // 粗さ（0.045〜1）
    float wetness;      // 濡れ具合（0〜1。地面に近いほど湿っている）
    float height;       // 凹凸の高さ（へこみは負。ApplyHeightToNormal で法線を傾ける）
};

// 森の地面の高さ（配置ファイルの地面と同じ y=-100）
static const float BarkGroundY = -100.0f;

// 格子の形が出にくい2次元のノイズ（0〜1）。値ノイズは格子の縁で傾きが0になるため、細長く伸ばして等高線を線に使うと、
// 線が格子に沿って階段のように折れ、斑点も四角くなる。そこで、60度回した格子のノイズと平均して格子の向きを消し、
// 平均で狭まった値の幅を広げ直している
// 2つ目のノイズの回転（60度の cos と sin）と乱数のずらし、平均で狭まった幅を広げ直す倍率
#define BARK_SMOOTH_ROTATE_COS 0.5f
#define BARK_SMOOTH_ROTATE_SIN 0.866f
#define BARK_SMOOTH_SEED 17.3f
#define BARK_SMOOTH_CONTRAST 1.7f

float BarkSmoothNoise(float2 position)
{
    const float2 rotated = float2(
        position.x * BARK_SMOOTH_ROTATE_COS - position.y * BARK_SMOOTH_ROTATE_SIN,
        position.x * BARK_SMOOTH_ROTATE_SIN + position.y * BARK_SMOOTH_ROTATE_COS);
    const float average = (FastValueNoise(position) + FastValueNoise(rotated + BARK_SMOOTH_SEED)) * 0.5f;
    return saturate(0.5f + (average - 0.5f) * BARK_SMOOTH_CONTRAST);
}

// 溝の細かい揺れの幅
#define BARK_FURROW_SWAY_FINE_GAIN 0.5f
// 板の横のひび：線の波打ち（細かさ・ずらし・幅）、間隔（狭い・広い）とその乱数のずらし、ずれの乱数のずらしと幅、
// 板の内側とみなす溝からの距離（始め・終わり）、線の太さ（細さ＋画素の幅）、ひびが入る割合のしきい値とその乱数のずらし、板の濃淡の乱数のずらし
#define BARK_CRACK_WOBBLE_SCALE float2(2.2f, 0.6f)
#define BARK_CRACK_WOBBLE_SEED 71.0f
#define BARK_CRACK_WOBBLE_AMOUNT 0.12f
#define BARK_CRACK_SPACING_NARROW 0.10f
#define BARK_CRACK_SPACING_WIDE 0.22f
#define BARK_CRACK_SPACING_SEED 13.0f
#define BARK_CRACK_OFFSET_SEED 29.0f
#define BARK_CRACK_OFFSET_RANGE 7.0f
#define BARK_PLATE_INSIDE_START 0.04f
#define BARK_PLATE_INSIDE_END 0.22f
#define BARK_CRACK_LINE_WIDTH 0.030f
#define BARK_CRACK_LINE_FILTER 0.1f
#define BARK_CRACK_THRESHOLD 0.6f
#define BARK_CRACK_PRESENCE_SEED 47.0f
#define BARK_PLATE_TONE_SEED 61.0f

// 木目の向きを y にした座標 grain で、面の向きの重み weights に合わせて3方向から投影したノイズを混ぜている。
// scale.x はまわりの向きの細かさ、scale.y は木目の向きの細かさ（小さいほど筋が長く伸びる）。
// 切り口の向きの面（木目の向きを向いた面）は、まわりの細かさで両方向をそろえている
float BarkNoise(float3 grain, float3 weights, float2 scale, float offset)
{
    return BarkSmoothNoise(float2(grain.z * scale.x, grain.y * scale.y) + offset) * weights.x +
        BarkSmoothNoise(float2(grain.x, grain.z) * scale.x + offset) * weights.y +
        BarkSmoothNoise(float2(grain.x * scale.x, grain.y * scale.y) + offset) * weights.z;
}

// 樹皮の溝と板（1つの投影面の中）。uv.x はまわりの向き、uv.y は木目の向きの座標。
// まわりの向きに一定の間隔で並ぶ溝を、木目に沿ってゆっくり左右に揺らし（溝が曲がり、寄ったり離れたりする）、
// 所々で途切れさせて隣の板とつなげている。溝と溝の間の板には、板ごとの乱数で短い横のひびを入れている
struct BarkPlates
{
    float furrow;       // 縦の溝（0〜1）
    float crossCrack;   // 板の横のひび（0〜1）
    float plateTone;    // 板ごとの色の違い（0〜1）
};

// 縦溝の間隔を決め、幹を横切るノイズと高さ方向のノイズで線をずらす。
#define BARK_PLATE_SPACING_SCALE 0.85f
#define BARK_FURROW_SWAY_COARSE_X 0.25f
#define BARK_FURROW_SWAY_COARSE_Y 0.045f
#define BARK_FURROW_SWAY_COARSE_SEED 3.0f
#define BARK_FURROW_SWAY_COARSE_GAIN 2.6f
#define BARK_FURROW_SWAY_FINE_X 1.3f
#define BARK_FURROW_SWAY_FINE_Y 0.22f
#define BARK_FURROW_SWAY_FINE_SEED 9.0f

// 板によって溝の幅を変え、溝が途切れる場所を別の模様で決める。
#define BARK_FURROW_MIN_WIDTH 0.07f
#define BARK_FURROW_MAX_WIDTH 0.15f
#define BARK_FURROW_WIDTH_SEED 5.0f
#define BARK_FURROW_BLEND_WIDTH 0.05f
#define BARK_FURROW_BREAK_START 0.30f
#define BARK_FURROW_BREAK_FULL 0.50f
#define BARK_FURROW_BREAK_SCALE_X 0.9f
#define BARK_FURROW_BREAK_SCALE_Y 0.11f
#define BARK_FURROW_BREAK_SEED 31.0f

BarkPlates GetBarkPlates(float2 uv, float footprint)
{
    BarkPlates plates;
    const float spacing = BARK_PLATE_SPACING_SCALE;
    const float sway = (BarkSmoothNoise(float2(uv.x * BARK_FURROW_SWAY_COARSE_X, uv.y * BARK_FURROW_SWAY_COARSE_Y) + BARK_FURROW_SWAY_COARSE_SEED) - 0.5f) * BARK_FURROW_SWAY_COARSE_GAIN +
        (BarkSmoothNoise(float2(uv.x * BARK_FURROW_SWAY_FINE_X, uv.y * BARK_FURROW_SWAY_FINE_Y) + BARK_FURROW_SWAY_FINE_SEED) - 0.5f) * BARK_FURROW_SWAY_FINE_GAIN;
    const float phase = uv.x * spacing + sway;
    const float plateId = floor(phase);
    const float distanceToFurrow = abs(frac(phase) - 0.5f);
    // 溝の幅は板ごとに少し変え、遠くでは画素の大きさの分だけ広げて、ちらつきを抑えている
    const float width = lerp(BARK_FURROW_MIN_WIDTH, BARK_FURROW_MAX_WIDTH, FastHash21(float2(plateId, BARK_FURROW_WIDTH_SEED)));
    const float furrowLine = 1.0f - smoothstep(width, width + BARK_FURROW_BLEND_WIDTH + footprint * spacing, distanceToFurrow);
    // 溝が途切れて、隣の板とつながる所
    const float bridge = smoothstep(BARK_FURROW_BREAK_START, BARK_FURROW_BREAK_FULL, BarkSmoothNoise(float2(uv.x * BARK_FURROW_BREAK_SCALE_X, uv.y * BARK_FURROW_BREAK_SCALE_Y) + BARK_FURROW_BREAK_SEED));
    plates.furrow = furrowLine * bridge;
    // 板の横のひび：板ごとに間隔とずれを変え、4割ほどの板にだけ入れている。ひびの線は少し波打たせ、
    // 板の端（溝の近く）へ向かって細くして消している（端で急に切れると、黒い四角に見えるため）
    const float crackWobble = (BarkSmoothNoise(uv * BARK_CRACK_WOBBLE_SCALE + BARK_CRACK_WOBBLE_SEED) - 0.5f) * BARK_CRACK_WOBBLE_AMOUNT;
    const float crackRow = frac(uv.y * lerp(BARK_CRACK_SPACING_NARROW, BARK_CRACK_SPACING_WIDE, FastHash21(float2(plateId, BARK_CRACK_SPACING_SEED))) +
        FastHash21(float2(plateId, BARK_CRACK_OFFSET_SEED)) * BARK_CRACK_OFFSET_RANGE + crackWobble);
    const float plateInside = smoothstep(width + BARK_PLATE_INSIDE_START, width + BARK_PLATE_INSIDE_END, distanceToFurrow);
    plates.crossCrack = (1.0f - smoothstep(0.0f, (BARK_CRACK_LINE_WIDTH + footprint * BARK_CRACK_LINE_FILTER) * plateInside, abs(crackRow - 0.5f))) *
        step(BARK_CRACK_THRESHOLD, FastHash21(float2(plateId, BARK_CRACK_PRESENCE_SEED))) * plateInside;
    plates.plateTone = FastHash21(float2(plateId, BARK_PLATE_TONE_SEED));
    return plates;
}

// 樹皮の板とは別に、木目に沿う細い繊維を重ねる。
#define BARK_FIBRE_NOISE_SCALE float2(5.5f, 0.45f)
#define BARK_FIBRE_NOISE_SEED 11.0f

// 剥がれた領域は木目に沿って伸ばし、別のノイズで縁をぎざぎざにする。
#define BARK_PEEL_PATCH_SCALE float2(0.22f, 0.06f)
#define BARK_PEEL_PATCH_SEED 41.0f
#define BARK_PEEL_EDGE_SCALE float2(1.6f, 0.4f)
#define BARK_PEEL_EDGE_SEED 43.0f
#define BARK_PEEL_EDGE_VARIATION 0.16f
#define BARK_PEEL_START 0.76f
#define BARK_PEEL_FULL 0.78f
#define BARK_PEEL_RIM_START 0.71f

// 根元と上向きの面で湿りやすさを分け、苔の範囲をまだらにする。
#define BARK_MOSS_PATCH_START 0.45f
#define BARK_MOSS_PATCH_FULL 0.70f
#define BARK_MOSS_PATCH_SCALE float2(0.22f, 0.10f)
#define BARK_MOSS_PATCH_SEED 61.0f
#define BARK_MOSS_FIBRE_WEIGHT 0.15f
#define BARK_MOSS_UP_START 0.30f
#define BARK_MOSS_UP_FULL 0.80f
#define BARK_MOSS_GROUND_FADE_START 2.0f
#define BARK_MOSS_GROUND_FADE_END 14.0f
#define BARK_MOSS_GROUND_GAIN 0.85f

// 地衣類の小さな斑点を大きな分布で間引く。
#define BARK_LICHEN_SPOT_START 0.66f
#define BARK_LICHEN_SPOT_FULL 0.74f
#define BARK_LICHEN_SPOT_SCALE float2(1.2f, 1.0f)
#define BARK_LICHEN_SPOT_SEED 83.0f
#define BARK_LICHEN_AREA_START 0.52f
#define BARK_LICHEN_AREA_FULL 0.72f
#define BARK_LICHEN_AREA_SCALE float2(0.12f, 0.08f)
#define BARK_LICHEN_AREA_SEED 97.0f

// grainAxis は木目の向き（ワールド）、baseColor は配置ファイルの色（頂点の環境遮蔽を掛けた後）、
// baseRoughness はその色の粗さ（rough。bark は0.92）
// --- 樹皮の残りの値
// 木目の向きが渡っていないとみなす長さの2乗、上向きを基準にできないとみなす y、投影の重みの鋭さ、0で割らないための下限、
// 画素の大きさの下限、細かい模様を消す画素の大きさ、2つ目の投影面の座標のずらし、横のひびの濃さ
#define BARK_MIN_AXIS_LENGTH_SQUARED 0.01f
#define BARK_AXIS_UP_LIMIT 0.9f
#define BARK_PROJECTION_SHARPNESS 4.0f
#define BARK_MIN_WEIGHT_SUM 0.0001f
#define BARK_MIN_FOOTPRINT 0.0001f
#define BARK_FINE_FOOTPRINT_LIMIT 0.6f
#define BARK_SECOND_PLANE_OFFSET float2(37.0f, 11.0f)
#define BARK_CROSS_CRACK_STRENGTH 0.8f
// 地面との境目の湿り：高さ（消え始め・消えきる＋繊維の幅）
#define BARK_GROUND_DAMP_START 0.5f
#define BARK_GROUND_DAMP_END 4.0f
#define BARK_GROUND_DAMP_FIBRE_RANGE 2.0f
// 色：板の表の灰色に抜けた色（樹皮の色の倍率と足す量）と混ぜ方（最低＋濃淡の幅）、繊維（最低＋幅）、溝とひびの暗さ、
// 剥がれた地の木（色と明るさ：最低＋繊維の幅）、剥がれの縁の明るさ、苔と地衣類の色と混ぜ方、地面の湿りの暗さ
#define BARK_WEATHERED_SCALE 1.6f
#define BARK_WEATHERED_LIFT 0.02f
#define BARK_WEATHERED_BLEND 0.35f
#define BARK_WEATHERED_TONE_RANGE 0.4f
#define BARK_FIBRE_BRIGHTNESS 0.82f
#define BARK_FIBRE_BRIGHTNESS_RANGE 0.32f
#define BARK_FURROW_DARKEN 0.78f
#define BARK_CROSS_CRACK_DARKEN 0.45f
#define BARK_BARE_WOOD_COLOR float3(0.17f, 0.16f, 0.145f)
#define BARK_BARE_WOOD_BRIGHTNESS 0.80f
#define BARK_BARE_WOOD_FIBRE_RANGE 0.40f
#define BARK_PEEL_RIM_BRIGHTEN 0.25f
#define BARK_MOSS_COLOR float3(0.055f, 0.08f, 0.032f)
#define BARK_MOSS_BLEND 0.85f
#define BARK_LICHEN_COLOR float3(0.26f, 0.28f, 0.23f)
#define BARK_LICHEN_BLEND 0.65f
#define BARK_GROUND_DAMP_DARKEN 0.30f
// 粗さの下限、濡れ具合（地面の湿り・溝）
#define BARK_MIN_ROUGHNESS 0.045f
#define BARK_GROUND_DAMP_WETNESS 0.55f
#define BARK_FURROW_WETNESS 0.12f
// 高さ（溝・横のひび・繊維・剥がれ・剥がれの縁・苔）
#define BARK_FURROW_DEPTH 0.85f
#define BARK_CROSS_CRACK_DEPTH 0.40f
#define BARK_FIBRE_HEIGHT 0.20f
#define BARK_PEEL_DEPTH 0.45f
#define BARK_PEEL_RIM_HEIGHT 0.15f
#define BARK_MOSS_HEIGHT 0.12f

BarkSurface GetBarkSurface(float3 worldPosition, float3 worldNormal, float3 grainAxis, float3 baseColor, float baseRoughness)
{
    BarkSurface bark;
    // 木目の向きを y、それと直交する2つの向きを x・z にした座標へ回している（向きが渡っていなければ上向き）
    const float3 axis = dot(grainAxis, grainAxis) > BARK_MIN_AXIS_LENGTH_SQUARED ? normalize(grainAxis) : float3(0.0f, 1.0f, 0.0f);
    const float3 reference = abs(axis.y) < BARK_AXIS_UP_LIMIT ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 side = normalize(cross(reference, axis));
    const float3 front = cross(axis, side);
    const float3 grain = float3(dot(worldPosition, side), dot(worldPosition, axis), dot(worldPosition, front));
    const float3 normal = normalize(worldNormal);
    const float3 grainNormal = float3(dot(normal, side), dot(normal, axis), dot(normal, front));
    // 投影の重み：面の向きに近い軸ほど強くし（4乗）、斜めの面で模様が混ざる幅を狭めている
    float3 weights = pow(abs(grainNormal), BARK_PROJECTION_SHARPNESS);
    weights /= max(weights.x + weights.y + weights.z, BARK_MIN_WEIGHT_SUM);
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている
    const float footprint = max(length(fwidth(worldPosition)), BARK_MIN_FOOTPRINT);
    const float fineFade = saturate(1.0f - footprint / BARK_FINE_FOOTPRINT_LIMIT);

    // --- 縦の溝と、溝の間の板・板の横のひび（幹の横を向いた2つの投影面で求め、面の向きで混ぜている。
    //     切り口の向きの面には溝を付けない）
    const BarkPlates platesX = GetBarkPlates(float2(grain.z, grain.y), footprint);
    const BarkPlates platesZ = GetBarkPlates(float2(grain.x, grain.y) + BARK_SECOND_PLANE_OFFSET, footprint);
    const float sideWeight = weights.x + weights.z;
    const float furrow = platesX.furrow * weights.x + platesZ.furrow * weights.z;
    const float crossCrack = (platesX.crossCrack * weights.x + platesZ.crossCrack * weights.z) * BARK_CROSS_CRACK_STRENGTH;
    const float plateTone = (platesX.plateTone * weights.x + platesZ.plateTone * weights.z) + 0.5f * weights.y;
    // --- 細かい繊維（木目の向きに長い、細かな筋）
    const float fibre = BarkNoise(grain, weights, BARK_FIBRE_NOISE_SCALE, BARK_FIBRE_NOISE_SEED);

    // --- 剥がれ：樹皮が落ちて、灰色に枯れた地の木が出ている所（枯れ木らしさ）。木目に沿って細長く、縁はぎざぎざで、
    //     縁の樹皮は少し盛り上がって残っている
    const float peelNoise = BarkNoise(grain, weights, BARK_PEEL_PATCH_SCALE, BARK_PEEL_PATCH_SEED) +
        (BarkNoise(grain, weights, BARK_PEEL_EDGE_SCALE, BARK_PEEL_EDGE_SEED) - 0.5f) * BARK_PEEL_EDGE_VARIATION;
    const float peel = smoothstep(BARK_PEEL_START, BARK_PEEL_FULL, peelNoise) * sideWeight;
    const float peelRim = smoothstep(BARK_PEEL_RIM_START, BARK_PEEL_START, peelNoise) * (1.0f - peel) * sideWeight;

    // --- 苔：上を向いた面（倒木の上・太い枝の上）と、根元の湿った所に、まだらに生えている
    const float heightFromGround = worldPosition.y - BarkGroundY;
    const float mossPatch = smoothstep(BARK_MOSS_PATCH_START, BARK_MOSS_PATCH_FULL,
        BarkNoise(grain, weights, BARK_MOSS_PATCH_SCALE, BARK_MOSS_PATCH_SEED) + fibre * BARK_MOSS_FIBRE_WEIGHT);
    const float mossPlace = max(smoothstep(BARK_MOSS_UP_START, BARK_MOSS_UP_FULL, normal.y),
        (1.0f - smoothstep(BARK_MOSS_GROUND_FADE_START, BARK_MOSS_GROUND_FADE_END, heightFromGround)) * BARK_MOSS_GROUND_GAIN);
    const float moss = mossPlace * mossPatch * (1.0f - peel);

    // --- 地衣類：樹皮の上の、白っぽい灰緑の小さな斑点
    const float lichenSpots = smoothstep(BARK_LICHEN_SPOT_START, BARK_LICHEN_SPOT_FULL, BarkNoise(grain, weights, BARK_LICHEN_SPOT_SCALE, BARK_LICHEN_SPOT_SEED));
    const float lichenArea = smoothstep(BARK_LICHEN_AREA_START, BARK_LICHEN_AREA_FULL, BarkNoise(grain, weights, BARK_LICHEN_AREA_SCALE, BARK_LICHEN_AREA_SEED));
    const float lichen = lichenSpots * lichenArea * (1.0f - moss) * (1.0f - peel) * fineFade;

    // --- 地面との境目の湿り（根元と倒木の下側が、地面の水を吸って黒ずんでいる）
    const float groundDamp = 1.0f - smoothstep(BARK_GROUND_DAMP_START, BARK_GROUND_DAMP_END + fibre * BARK_GROUND_DAMP_FIBRE_RANGE, heightFromGround);

    // 色：樹皮の色に板ごとの濃淡と繊維を掛け、溝の奥とひびを暗くし、剥がれ・苔・地衣類の色を混ぜている
    // 板の表は雨と日に晒されて、樹皮の色より少し灰色に抜けている（溝の奥は元の暗い色のまま）
    float3 albedo = lerp(baseColor, baseColor * BARK_WEATHERED_SCALE + BARK_WEATHERED_LIFT,
        BARK_WEATHERED_BLEND + plateTone * BARK_WEATHERED_TONE_RANGE);
    albedo *= lerp(1.0f, BARK_FIBRE_BRIGHTNESS + fibre * BARK_FIBRE_BRIGHTNESS_RANGE, fineFade);
    albedo *= 1.0f - furrow * BARK_FURROW_DARKEN;
    albedo *= 1.0f - crossCrack * BARK_CROSS_CRACK_DARKEN;
    // 剥がれた所の地の木は、雨と日に晒されて灰色に抜けている（樹皮より明るく、木目の筋が見える）
    const float3 bareWood = BARK_BARE_WOOD_COLOR * (BARK_BARE_WOOD_BRIGHTNESS + fibre * BARK_BARE_WOOD_FIBRE_RANGE);
    albedo = lerp(albedo, bareWood, peel);
    albedo *= 1.0f + peelRim * BARK_PEEL_RIM_BRIGHTEN;
    albedo = lerp(albedo, BARK_MOSS_COLOR, moss * BARK_MOSS_BLEND);
    albedo = lerp(albedo, BARK_LICHEN_COLOR, lichen * BARK_LICHEN_BLEND);
    albedo *= 1.0f - groundDamp * BARK_GROUND_DAMP_DARKEN;
    bark.albedo = albedo;

    // 粗さ：樹皮はざらざら、溝の奥と苔はさらに粗く、剥がれた地の木は少しなめらか（濡れた所は MakePbrSurface が下げる）
    float roughness = baseRoughness + (plateTone - 0.5f) * BARK_TONE_ROUGHNESS_VARIATION;
    roughness = lerp(roughness, BARK_FURROW_ROUGHNESS, furrow * BARK_FURROW_ROUGHNESS_BLEND);
    roughness = lerp(roughness, BARK_PEELED_ROUGHNESS, peel);
    roughness = lerp(roughness, BARK_MOSS_ROUGHNESS, moss);
    bark.roughness = clamp(roughness, BARK_MIN_ROUGHNESS, 1.0f);

    // 濡れ具合：地面との境目と、溝の奥にたまった水
    bark.wetness = saturate(groundDamp * BARK_GROUND_DAMP_WETNESS + furrow * BARK_FURROW_WETNESS);

    // 高さ：溝のへこみ・横のひび・繊維・剥がれ（樹皮の厚みの分だけ低い）とその縁・苔の盛り上がり
    bark.height = -furrow * BARK_FURROW_DEPTH -
        crossCrack * BARK_CROSS_CRACK_DEPTH +
        (fibre - 0.5f) * BARK_FIBRE_HEIGHT * fineFade -
        peel * BARK_PEEL_DEPTH +
        peelRim * BARK_PEEL_RIM_HEIGHT +
        moss * BARK_MOSS_HEIGHT;
    return bark;
}

#endif
