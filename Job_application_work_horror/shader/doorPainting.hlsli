// ============================================================================
// 共通の処理: 2面の偽の扉の板（木の扉）を計算で描いている。本物の出口の扉と見分けがつかないほど「普通の扉」に見せている。
// 主な技術: 角を丸めた四角の距離で描く6枚の羽目板と、その段差の凹凸、縦に流れる木目のノイズ、
//           ノイズのしきい値で描く剥げた塗装（縁を暗く）、7本の線で描く部屋番号の数字、鍵穴の金具
// litTexturePS.hlsl を DOOR_SURFACE を付けて読み込んだとき（doorPS.hlsl）だけ使っている。fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef DOOR_PAINTING_INCLUDED
#define DOOR_PAINTING_INCLUDED

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 扉の色と、凹凸（羽目板の段差・剥げた塗装の縁）
struct DoorPaint
{
    float3 albedo;
    float relief;
};

// 角を丸めた四角（中心center、半分の大きさhalfSize、角の丸みcorner）までの距離（内側が負）
// 数字の中心を札に合わせ、同じ大きさで「204」を描く。
static const float DOOR_NUMBER_PLATE_Y = 7.0f;
static const float DOOR_NUMBER_DIGIT_SCALE = 0.62f;

float DoorRoundedBox(float2 p, float2 center, float2 halfSize, float corner)
{
    const float2 q = abs(p - center) - halfSize + corner;
    return length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - corner;
}

// 点pから線分abまでの距離
// 0で割らないための下限
#define DOOR_MIN_DIVISOR 0.0001f
// 数字1文字の形：幅の半分・高さの半分、どの線より遠い距離（一番近い線を探し始める値）、線の数
#define DOOR_DIGIT_HALF_WIDTH 0.4f
#define DOOR_DIGIT_HALF_HEIGHT 0.8f
#define DOOR_DIGIT_FAR_DISTANCE 10.0f

float DoorSegment(float2 p, float2 a, float2 b)
{
    const float2 ab = b - a;
    const float t = saturate(dot(p - a, ab) / max(dot(ab, ab), DOOR_MIN_DIVISOR));
    return length(p - (a + ab * t));
}

// 7本の線（上・右上・右下・下・左下・左上・中）で描く数字1文字までの距離。localは文字の中心から（高さ1.6、幅0.8）
float DoorDigit(float2 local, uint digit)
{
    // 数字ごとに、使う線をビットで持っている（下の桁から 上・右上・右下・下・左下・左上・中 の順）
    static const uint segments[10] = { 0x3Fu, 0x06u, 0x5Bu, 0x4Fu, 0x66u, 0x6Du, 0x7Du, 0x07u, 0x7Fu, 0x6Fu };
    const uint mask = segments[digit % 10u];
    const float w = DOOR_DIGIT_HALF_WIDTH;
    const float h = DOOR_DIGIT_HALF_HEIGHT;
    const float2 ends[7][2] =
    {
        { float2(-w, h), float2(w, h) },
        { float2(w, h), float2(w, 0.0f) },
        { float2(w, 0.0f), float2(w, -h) },
        { float2(-w, -h), float2(w, -h) },
        { float2(-w, 0.0f), float2(-w, -h) },
        { float2(-w, h), float2(-w, 0.0f) },
        { float2(-w, 0.0f), float2(w, 0.0f) }
    };
    float distance = DOOR_DIGIT_FAR_DISTANCE;
    [unroll]
    for (uint segment = 0u; segment < 7u; ++segment)
    {
        if ((mask >> segment) & 1u)
        {
            distance = min(distance, DoorSegment(local, ends[segment][0], ends[segment][1]));
        }
    }
    return distance;
}

// 扉のUV（u は見る人から見て右から左へ増える。v は上から下）と、取っ手の側（params.x：-1 = 見て左、+1 = 見て右）から、
// その点の色と凹凸を求めている
// 木目は縦に伸ばし、密度の違う筋と節を重ねる。
#define DOOR_GRAIN_COARSE_X 2.6f
#define DOOR_GRAIN_COARSE_Y 0.18f
#define DOOR_GRAIN_COARSE_WEIGHT 0.6f
#define DOOR_GRAIN_FINE_X 9.0f
#define DOOR_GRAIN_FINE_SEED 7.0f
#define DOOR_GRAIN_FINE_WEIGHT 0.4f
#define DOOR_KNOT_START 0.82f
#define DOOR_KNOT_FULL 0.95f
#define DOOR_KNOT_NOISE_SCALE 0.35f
#define DOOR_KNOT_NOISE_SEED 31.0f

// 塗装の色と木目を残す量。床に近い部分は汚れで暗くする。
#define DOOR_PAINT_COLOR float3(0.30f, 0.16f, 0.10f)
#define DOOR_PAINT_BASE_GAIN 0.85f
#define DOOR_PAINT_GRAIN_GAIN 0.25f
#define DOOR_BOTTOM_PAINT_GAIN 0.70f
#define DOOR_BOTTOM_DAMAGE_START_Y (-20.0f)
#define DOOR_BOTTOM_DAMAGE_END_Y (-8.0f)

// 塗装の剥がれは大小のノイズで決め、縁は暗くし、露出した木地は明るくする。
#define DOOR_PEEL_COARSE_SCALE float2(0.45f, 0.30f)
#define DOOR_PEEL_COARSE_SEED 11.0f
#define DOOR_PEEL_COARSE_WEIGHT 0.7f
#define DOOR_PEEL_FINE_SCALE 1.8f
#define DOOR_PEEL_FINE_SEED 4.0f
#define DOOR_PEEL_FINE_WEIGHT 0.3f
#define DOOR_PEEL_START 0.66f
#define DOOR_PEEL_FULL 0.68f
#define DOOR_PEEL_PIXEL_FILTER 0.05f
#define DOOR_PEEL_EDGE_START 0.60f
#define DOOR_EXPOSED_WOOD_COLOR float3(0.42f, 0.33f, 0.22f)
#define DOOR_EXPOSED_WOOD_BASE 0.8f
#define DOOR_EXPOSED_WOOD_GRAIN_GAIN 0.35f
#define DOOR_PEEL_EDGE_DARKENING 0.45f
#define DOOR_KNOT_COLOR_GAIN 0.55f
#define DOOR_KNOT_BLEND 0.6f
#define DOOR_PEEL_DEPTH 0.15f

// --- 扉の絵の残りの値（扉の単位。幅22・高さ40、中心が0、上が+、見る人から見て右が+）
#define DOOR_CANVAS_WIDTH 22.0f
#define DOOR_CANVAS_HEIGHT 40.0f
#define DOOR_MIN_FOOTPRINT 0.0001f
// 細かい木目の縦の細かさ
#define DOOR_GRAIN_FINE_Y 0.5f
// 羽目板（2列3段）：列の横の位置、段の高さ（上・中・下）、幅の半分、高さの半分（中段・上下段）、角の丸み
#define DOOR_PANEL_COLUMN_X 5.0f
#define DOOR_PANEL_TOP_Y 12.5f
#define DOOR_PANEL_MIDDLE_Y 1.0f
#define DOOR_PANEL_BOTTOM_Y -12.0f
#define DOOR_PANEL_HALF_WIDTH 3.6f
#define DOOR_PANEL_MIDDLE_HALF_HEIGHT 3.2f
#define DOOR_PANEL_HALF_HEIGHT 6.0f
#define DOOR_PANEL_CORNER 0.4f
// 縁の斜めの面の幅、へこんだ面の縁のぼかし、斜めの面のへこみ、光の向き（上と左が明るい）、陰影の強さ
#define DOOR_BEVEL_WIDTH 0.7f
#define DOOR_PANEL_INSET_EDGE 0.8f
#define DOOR_BEVEL_DEPTH 0.5f
#define DOOR_BEVEL_LIGHT_DIRECTION float2(0.35f, -0.55f)
#define DOOR_PANEL_SHADE_STRENGTH 0.35f
// 部屋番号の札：縁のぼかし、大きさの半分、角の丸み、くすんだ真鍮の色と明るさ（最低＋木目の幅）、盛り上がり
#define DOOR_PLATE_EDGE 0.05f
#define DOOR_PLATE_HALF_SIZE float2(2.3f, 1.25f)
#define DOOR_PLATE_CORNER 0.3f
#define DOOR_PLATE_COLOR float3(0.30f, 0.22f, 0.10f)
#define DOOR_PLATE_BRIGHTNESS 0.75f
#define DOOR_PLATE_GRAIN_RANGE 0.2f
#define DOOR_PLATE_RELIEF 0.3f
// 数字：横の間隔、線の太さの半分と縁のぼかし（画素の大きさに掛ける）、色
#define DOOR_DIGIT_SPACING 1.25f
#define DOOR_DIGIT_HALF_STROKE 0.10f
#define DOOR_DIGIT_EDGE_FILTER 2.0f
#define DOOR_DIGIT_COLOR float3(0.03f, 0.025f, 0.02f)
// 鍵穴の金具：取っ手の側への横の位置、高さ、大きさの半分、角の丸み、色、盛り上がり
#define DOOR_KEYHOLE_X 8.0f
#define DOOR_KEYHOLE_Y -3.0f
#define DOOR_ESCUTCHEON_HALF_SIZE float2(0.55f, 1.0f)
#define DOOR_ESCUTCHEON_CORNER 0.5f
#define DOOR_ESCUTCHEON_COLOR float3(0.28f, 0.20f, 0.09f)
#define DOOR_ESCUTCHEON_RELIEF 0.3f
// 鍵穴：縁のぼかし、丸い穴（中心のずれ・半径）、縦の溝（中心のずれ・大きさの半分・角の丸み）、色
#define DOOR_KEYHOLE_EDGE 0.03f
#define DOOR_KEYHOLE_ROUND_OFFSET 0.25f
#define DOOR_KEYHOLE_ROUND_RADIUS 0.18f
#define DOOR_KEYHOLE_SLOT_OFFSET 0.2f
#define DOOR_KEYHOLE_SLOT_HALF_SIZE float2(0.07f, 0.3f)
#define DOOR_KEYHOLE_SLOT_CORNER 0.05f
#define DOOR_KEYHOLE_COLOR float3(0.01f, 0.01f, 0.01f)
// 取っ手の周りの手垢：広がり、楕円の形、暗さ
#define DOOR_HAND_WEAR_RADIUS 4.0f
#define DOOR_HAND_WEAR_SHAPE float2(1.0f, 1.6f)
#define DOOR_HAND_WEAR_DARKEN 0.25f

DoorPaint GetDoorPainting(float2 uv, float4 params)
{
    // 扉の単位（幅22・高さ40、中心が0、上が+、見る人から見て右が+）
    const float2 p = float2((0.5f - uv.x) * DOOR_CANVAS_WIDTH, (0.5f - uv.y) * DOOR_CANVAS_HEIGHT);
    const float footprint = max(length(fwidth(p)), DOOR_MIN_FOOTPRINT);
    const float handleSide = params.x < 0.0f ? -1.0f : 1.0f;

    // 木目：縦に流れる筋と、ところどころの節
    const float grain =
        FastValueNoise(float2(p.x * DOOR_GRAIN_COARSE_X, p.y * DOOR_GRAIN_COARSE_Y)) * DOOR_GRAIN_COARSE_WEIGHT +
        FastValueNoise(float2(p.x * DOOR_GRAIN_FINE_X, p.y * DOOR_GRAIN_FINE_Y) + DOOR_GRAIN_FINE_SEED) * DOOR_GRAIN_FINE_WEIGHT;
    const float knot = smoothstep(DOOR_KNOT_START, DOOR_KNOT_FULL, FastValueNoise(p * DOOR_KNOT_NOISE_SCALE + DOOR_KNOT_NOISE_SEED));

    // 塗装：くすんだ茶色のペンキ。木目がうっすら透け、下の方ほど汚れて暗い
    float3 paint = DOOR_PAINT_COLOR * (DOOR_PAINT_BASE_GAIN + grain * DOOR_PAINT_GRAIN_GAIN);
    paint *= lerp(DOOR_BOTTOM_PAINT_GAIN, 1.0f, smoothstep(DOOR_BOTTOM_DAMAGE_START_Y, DOOR_BOTTOM_DAMAGE_END_Y, p.y));

    // 6枚の羽目板（2列3段）：枠より一段へこみ、縁に斜めの面がある
    float relief = 0.0f;
    float panelShade = 0.0f;
    [unroll]
    for (int column = 0; column < 2; ++column)
    {
        [unroll]
        for (int row = 0; row < 3; ++row)
        {
            const float2 center = float2(column == 0 ? -DOOR_PANEL_COLUMN_X : DOOR_PANEL_COLUMN_X,
                row == 0 ? DOOR_PANEL_TOP_Y : (row == 1 ? DOOR_PANEL_MIDDLE_Y : DOOR_PANEL_BOTTOM_Y));
            const float2 halfSize = float2(DOOR_PANEL_HALF_WIDTH, row == 1 ? DOOR_PANEL_MIDDLE_HALF_HEIGHT : DOOR_PANEL_HALF_HEIGHT);
            const float distance = DoorRoundedBox(p, center, halfSize, DOOR_PANEL_CORNER);
            // 縁の斜めの面（幅0.7）と、へこんだ平らな面
            const float bevel = smoothstep(0.0f, -DOOR_BEVEL_WIDTH, distance);
            const float inside = smoothstep(-DOOR_BEVEL_WIDTH, -DOOR_PANEL_INSET_EDGE - footprint, distance);
            relief -= bevel * DOOR_BEVEL_DEPTH;
            // 斜めの面は、上と左が明るく、下と右が暗い（上から光が当たる）
            const float2 toCenter = normalize(center - p + DOOR_MIN_DIVISOR);
            panelShade += (bevel - inside) * dot(toCenter, DOOR_BEVEL_LIGHT_DIRECTION);
        }
    }
    paint *= 1.0f + panelShade * DOOR_PANEL_SHADE_STRENGTH;

    // 剥げた塗装：ノイズのしきい値より上の所が剥げ、下の白っぽい木地が見えている。縁は塗装が浮いて暗い
    const float peelNoise =
        FastValueNoise(p * DOOR_PEEL_COARSE_SCALE + DOOR_PEEL_COARSE_SEED) * DOOR_PEEL_COARSE_WEIGHT +
        FastValueNoise(p * DOOR_PEEL_FINE_SCALE - DOOR_PEEL_FINE_SEED) * DOOR_PEEL_FINE_WEIGHT;
    const float peel = smoothstep(DOOR_PEEL_START, DOOR_PEEL_FULL + footprint * DOOR_PEEL_PIXEL_FILTER, peelNoise);
    const float peelEdge = smoothstep(DOOR_PEEL_EDGE_START, DOOR_PEEL_START, peelNoise) * (1.0f - peel);
    const float3 bareWood = DOOR_EXPOSED_WOOD_COLOR * (DOOR_EXPOSED_WOOD_BASE + grain * DOOR_EXPOSED_WOOD_GRAIN_GAIN);
    float3 color = lerp(paint, bareWood, peel);
    color *= 1.0f - peelEdge * DOOR_PEEL_EDGE_DARKENING;
    color = lerp(color, color * DOOR_KNOT_COLOR_GAIN, knot * DOOR_KNOT_BLEND);
    relief -= peel * DOOR_PEEL_DEPTH;

    // 部屋番号の札（くすんだ真鍮）と、黒い数字「204」
    const float plate = smoothstep(DOOR_PLATE_EDGE + footprint, 0.0f,
        DoorRoundedBox(p, float2(0.0f, DOOR_NUMBER_PLATE_Y), DOOR_PLATE_HALF_SIZE, DOOR_PLATE_CORNER));
    color = lerp(color, DOOR_PLATE_COLOR * (DOOR_PLATE_BRIGHTNESS + grain * DOOR_PLATE_GRAIN_RANGE), plate);
    relief += plate * DOOR_PLATE_RELIEF;
    float digits = DOOR_DIGIT_FAR_DISTANCE;
    digits = min(digits, DoorDigit((p - float2(-DOOR_DIGIT_SPACING, DOOR_NUMBER_PLATE_Y)) * float2(1.0f, 1.0f) / DOOR_NUMBER_DIGIT_SCALE, 2u));
    digits = min(digits, DoorDigit((p - float2(0.0f, DOOR_NUMBER_PLATE_Y)) / DOOR_NUMBER_DIGIT_SCALE, 0u));
    digits = min(digits, DoorDigit((p - float2(DOOR_DIGIT_SPACING, DOOR_NUMBER_PLATE_Y)) / DOOR_NUMBER_DIGIT_SCALE, 4u));
    const float digitInk = (1.0f - smoothstep(DOOR_DIGIT_HALF_STROKE, DOOR_DIGIT_HALF_STROKE + footprint * DOOR_DIGIT_EDGE_FILTER, digits)) * plate;
    color = lerp(color, DOOR_DIGIT_COLOR, digitInk);

    // 取っ手の下の鍵穴の金具と、取っ手の周りの手垢・引っかき傷
    const float2 keyholeCenter = float2(handleSide * DOOR_KEYHOLE_X, DOOR_KEYHOLE_Y);
    const float escutcheon = smoothstep(DOOR_PLATE_EDGE + footprint, 0.0f,
        DoorRoundedBox(p, keyholeCenter, DOOR_ESCUTCHEON_HALF_SIZE, DOOR_ESCUTCHEON_CORNER));
    color = lerp(color, DOOR_ESCUTCHEON_COLOR, escutcheon);
    const float keyhole = smoothstep(DOOR_KEYHOLE_EDGE + footprint, 0.0f,
        min(length(p - keyholeCenter - float2(0.0f, DOOR_KEYHOLE_ROUND_OFFSET)) - DOOR_KEYHOLE_ROUND_RADIUS,
            DoorRoundedBox(p, keyholeCenter - float2(0.0f, DOOR_KEYHOLE_SLOT_OFFSET), DOOR_KEYHOLE_SLOT_HALF_SIZE, DOOR_KEYHOLE_SLOT_CORNER)));
    color = lerp(color, DOOR_KEYHOLE_COLOR, keyhole);
    const float handWear = smoothstep(DOOR_HAND_WEAR_RADIUS, 0.0f, length((p - float2(handleSide * DOOR_KEYHOLE_X, 0.0f)) / DOOR_HAND_WEAR_SHAPE));
    color *= 1.0f - handWear * DOOR_HAND_WEAR_DARKEN;
    relief += escutcheon * DOOR_ESCUTCHEON_RELIEF;

    DoorPaint door;
    door.albedo = color;
    door.relief = relief;
    return door;
}

#endif
