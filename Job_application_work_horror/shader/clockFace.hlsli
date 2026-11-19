// ============================================================================
// 共通の処理: 2面の時計の文字盤（真鍮の縁・黄ばんだ文字盤・目盛り・ローマ数字・針・ひびの入ったガラス）を計算で描いている。
// 主な技術: 極座標の目盛り、線分の距離で描くローマ数字（I・V・X の組み合わせを表で持つ）、角度から向きを決める針、
//           一番近い数字だけを計算して画素ごとの負荷を抑える工夫
// litTexturePS.hlsl を CLOCK_SURFACE を付けて読み込んだとき（clockPS.hlsl）だけ使っている。fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef CLOCK_FACE_INCLUDED
#define CLOCK_FACE_INCLUDED

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 文字盤の色と、凹凸（目盛り・数字の刻み）
struct ClockPaint
{
    float3 albedo;
    float relief;
};

// 点pから線分abまでの距離
// 0で割らないための下限
#define CLOCK_MIN_DIVISOR 0.0001f
// ローマ数字1文字の高さの半分と幅の半分
#define CLOCK_GLYPH_HALF_HEIGHT 0.65f
#define CLOCK_GLYPH_HALF_WIDTH 0.30f
// 針の先の細さ（根元の太さに対する割合）と、縁のぼかし（画素の大きさに掛ける）
#define CLOCK_HAND_TIP_TAPER 0.25f
#define CLOCK_HAND_EDGE_FILTER 1.5f

float ClockSegmentDistance(float2 p, float2 a, float2 b)
{
    const float2 ab = b - a;
    const float t = saturate(dot(p - a, ab) / max(dot(ab, ab), CLOCK_MIN_DIVISOR));
    return length(p - (a + ab * t));
}

// ローマ数字の1文字（1 = I、2 = V、3 = X）までの距離。localは文字の中心からの位置（高さ1.3、幅0.6）
float ClockGlyphDistance(float2 local, uint glyph)
{
    const float halfHeight = CLOCK_GLYPH_HALF_HEIGHT;
    const float halfWidth = CLOCK_GLYPH_HALF_WIDTH;
    if (glyph == 1u)
    {
        return ClockSegmentDistance(local, float2(0.0f, -halfHeight), float2(0.0f, halfHeight));
    }
    if (glyph == 2u)
    {
        return min(
            ClockSegmentDistance(local, float2(-halfWidth, halfHeight), float2(0.0f, -halfHeight)),
            ClockSegmentDistance(local, float2(halfWidth, halfHeight), float2(0.0f, -halfHeight)));
    }
    return min(
        ClockSegmentDistance(local, float2(-halfWidth, halfHeight), float2(halfWidth, -halfHeight)),
        ClockSegmentDistance(local, float2(halfWidth, halfHeight), float2(-halfWidth, -halfHeight)));
}

// 時計の数字（12, 1, 2 … 11）を、1文字2ビット（下の桁から左の文字の順）で表にしている。古い時計に多い「IIII」で4を書いている
static const uint ClockNumerals[12] =
{
    3u | (1u << 2) | (1u << 4),              // XII
    1u,                                      // I
    1u | (1u << 2),                          // II
    1u | (1u << 2) | (1u << 4),              // III
    1u | (1u << 2) | (1u << 4) | (1u << 6),  // IIII
    2u,                                      // V
    2u | (1u << 2),                          // VI
    2u | (1u << 2) | (1u << 4),              // VII
    2u | (1u << 2) | (1u << 4) | (1u << 6),  // VIII
    1u | (3u << 2),                          // IX
    3u,                                      // X
    3u | (1u << 2)                           // XI
};

// 針（中心から angle の向きへ handLength の長さ、根元の太さ width、先が細い。反対側に tail だけ出る）までの近さ（0〜1）
// 長針と短針は見分けが付くよう、長さと線の太さを変える。座標は時計の文字盤を基準にする。
static const float CLOCK_HOUR_HAND_LENGTH = 4.6f;
static const float CLOCK_HOUR_HAND_HALF_WIDTH = 0.34f;
static const float CLOCK_HOUR_HAND_TAIL = 0.9f;
static const float CLOCK_MINUTE_HAND_LENGTH = 7.3f;
static const float CLOCK_MINUTE_HAND_HALF_WIDTH = 0.24f;
static const float CLOCK_MINUTE_HAND_TAIL = 1.3f;

float ClockHand(float2 p, float angle, float handLength, float width, float tail, float footprint)
{
    const float2 direction = float2(sin(angle), cos(angle));
    const float along = dot(p, direction);
    const float across = abs(dot(p, float2(direction.y, -direction.x)));
    const float taper = width * lerp(1.0f, CLOCK_HAND_TIP_TAPER, saturate(along / handLength));
    const float inside = step(-tail, along) * step(along, handLength);
    return inside * (1.0f - smoothstep(taper, taper + footprint * CLOCK_HAND_EDGE_FILTER, across));
}

// 文字盤のUV（u は見る人から見て右から左へ増える。v は上から下）と、針の角度（x = 時針、y = 分針。12時が0、右回りが+）から、
// その点の色と凹凸を求めている
// 木枠の木目は縦方向に細かくし、木の色と凹凸を別に調整する。
#define CLOCK_CASE_GRAIN_X 0.35f
#define CLOCK_CASE_GRAIN_Y 4.0f
#define CLOCK_CASE_GRAIN_SEED 3.0f
#define CLOCK_CASE_COLOR float3(0.075f, 0.050f, 0.032f)
#define CLOCK_CASE_BASE_GAIN 0.80f
#define CLOCK_CASE_GRAIN_GAIN 0.35f
#define CLOCK_CASE_GRAIN_RELIEF 0.2f

// 真鍮の縁と黄ばんだ文字盤の境界を分け、染みが文字盤にだけ出るようにする。
#define CLOCK_BEZEL_OUTER_RADIUS 9.3f
#define CLOCK_BEZEL_INNER_RADIUS 9.2f
#define CLOCK_BEZEL_TARNISH_SCALE 0.8f
#define CLOCK_BEZEL_TARNISH_SEED 17.0f
#define CLOCK_BEZEL_COLOR float3(0.32f, 0.23f, 0.10f)
#define CLOCK_DIAL_OUTER_RADIUS 8.65f
#define CLOCK_DIAL_INNER_RADIUS 8.55f
#define CLOCK_DIAL_STAIN_START 0.62f
#define CLOCK_DIAL_STAIN_FULL 0.85f
#define CLOCK_DIAL_FOXING_START 0.86f
#define CLOCK_DIAL_FOXING_FULL 0.92f
#define CLOCK_DIAL_PAPER_COLOR float3(0.46f, 0.42f, 0.33f)
#define CLOCK_DIAL_STAIN_COLOR float3(0.30f, 0.22f, 0.13f)

// 時と分を見分けやすくするため、目盛りの長さと幅を変える。
#define CLOCK_MINUTE_TICKS_PER_HOUR_TICK 5.0f
#define CLOCK_MINUTE_TICK_INNER_RADIUS 7.75f
#define CLOCK_HOUR_TICK_INNER_RADIUS 7.10f
#define CLOCK_MINUTE_TICK_HALF_WIDTH 0.05f
#define CLOCK_HOUR_TICK_HALF_WIDTH 0.14f
#define CLOCK_TICK_OUTER_RADIUS 8.3f
#define CLOCK_TICK_PIXEL_FILTER 1.5f

// 染みの上のインクを薄くし、針と中心軸の色を文字盤から分ける。
#define CLOCK_INK_CLEAR_PAPER_GAIN 0.92f
#define CLOCK_INK_STAINED_PAPER_GAIN 0.6f
#define CLOCK_INK_RECESS 0.3f
#define CLOCK_HAND_COLOR float3(0.035f, 0.035f, 0.055f)
#define CLOCK_AXLE_COLOR float3(0.30f, 0.22f, 0.10f)

// --- 文字盤の残りの値（文字盤の単位。幅・高さ20、中心が0、上が+、右が+）
#define CLOCK_CANVAS_SIZE 20.0f
#define CLOCK_MIN_FOOTPRINT 0.0001f
#define CLOCK_PI 3.14159265f
// 目盛りの数（分の目盛り60本は π/30 ずつ、時の位置12は π/6 ずつ）
#define CLOCK_MINUTE_STEPS 30.0f
#define CLOCK_HOUR_STEPS 6.0f
// 真鍮の縁：黒ずんだ所の暗さ、盛り上がり
#define CLOCK_BEZEL_TARNISH_DARK 0.55f
#define CLOCK_BEZEL_RELIEF 0.4f
// 文字盤：染み（細かさ・ずらし）、点々の染み（細かさ・ずらし）、縁ほど暗くする（暗さ・始まる半径・終わる半径）、
// 染みの混ぜ方（大きな染み・点々）、へこみ
#define CLOCK_DIAL_STAIN_SCALE 0.45f
#define CLOCK_DIAL_STAIN_SEED 5.0f
#define CLOCK_DIAL_FOXING_SCALE 2.3f
#define CLOCK_DIAL_FOXING_SEED 9.0f
#define CLOCK_DIAL_EDGE_DARK 0.70f
#define CLOCK_DIAL_EDGE_START 5.0f
#define CLOCK_DIAL_EDGE_END 8.6f
#define CLOCK_DIAL_STAIN_BLEND 0.45f
#define CLOCK_DIAL_FOXING_BLEND 0.55f
#define CLOCK_DIAL_RECESS 0.4f
// インクの色
#define CLOCK_INK_COLOR float3(0.045f, 0.038f, 0.035f)
// ローマ数字：数字を並べる半径、文字の間隔、どの文字より遠い距離（一番近い文字を探し始める値）、
// 上下の横線の高さ、線の太さの半分、横線を太らせる量
#define CLOCK_NUMERAL_RADIUS 5.9f
#define CLOCK_GLYPH_SPACING 0.68f
#define CLOCK_GLYPH_FAR_DISTANCE 10.0f
#define CLOCK_SERIF_Y 0.66f
#define CLOCK_NUMERAL_HALF_STROKE 0.10f
#define CLOCK_SERIF_THICKEN 0.03f
// 針の盛り上がり、中心の軸の大きさ（外側・内側）
#define CLOCK_HAND_RELIEF 0.6f
#define CLOCK_AXLE_OUTER 0.50f
#define CLOCK_AXLE_INNER 0.45f
// ガラスの斜めの映り込み：幅、向き（x・y に掛ける量）、位置、明るさ。くもり（細かさ・ずらし・明るさ）
#define CLOCK_SHEEN_WIDTH 1.2f
#define CLOCK_SHEEN_SLANT 0.7f
#define CLOCK_SHEEN_OFFSET 3.0f
#define CLOCK_SHEEN_BRIGHTNESS 0.06f
#define CLOCK_HAZE_SCALE 0.6f
#define CLOCK_HAZE_SEED 23.0f
#define CLOCK_HAZE_BRIGHTNESS 0.05f
// ガラスのひび：線の傾き・高さ、波打ち（細かさ・幅）、太さ（芯・縁）、始まる x、色と濃さ
#define CLOCK_CRACK_SLOPE 0.55f
#define CLOCK_CRACK_OFFSET 2.0f
#define CLOCK_CRACK_WAVE_FREQUENCY 2.3f
#define CLOCK_CRACK_WAVE_AMOUNT 0.35f
#define CLOCK_CRACK_CORE 0.02f
#define CLOCK_CRACK_EDGE 0.06f
#define CLOCK_CRACK_START_X 2.0f
#define CLOCK_CRACK_COLOR float3(0.55f, 0.55f, 0.50f)
#define CLOCK_CRACK_BLEND 0.6f

ClockPaint GetClockFace(float2 uv, float4 hands)
{
    // 文字盤の単位（幅・高さ20、中心が0、上が+、右が+）。見る人から見て右が+になるよう、uを反転している
    const float2 p = float2((0.5f - uv.x) * CLOCK_CANVAS_SIZE, (0.5f - uv.y) * CLOCK_CANVAS_SIZE);
    const float footprint = max(length(fwidth(p)), CLOCK_MIN_FOOTPRINT);
    const float radius = length(p);

    // 外側：黒ずんだ木の箱（木目のノイズ）
    const float grain = FastValueNoise(float2(p.x * CLOCK_CASE_GRAIN_X, p.y * CLOCK_CASE_GRAIN_Y) + CLOCK_CASE_GRAIN_SEED);
    float3 color = CLOCK_CASE_COLOR * (CLOCK_CASE_BASE_GAIN + grain * CLOCK_CASE_GRAIN_GAIN);
    float relief = grain * CLOCK_CASE_GRAIN_RELIEF;

    // 真鍮の縁（くすんで、ところどころ黒ずんでいる）
    const float bezel = smoothstep(CLOCK_BEZEL_OUTER_RADIUS + footprint, CLOCK_BEZEL_INNER_RADIUS, radius);
    const float tarnish = FastValueNoise(p * CLOCK_BEZEL_TARNISH_SCALE + CLOCK_BEZEL_TARNISH_SEED);
    color = lerp(color, CLOCK_BEZEL_COLOR * lerp(CLOCK_BEZEL_TARNISH_DARK, 1.0f, tarnish), bezel);
    relief += bezel * CLOCK_BEZEL_RELIEF;

    // 文字盤：黄ばんだ象牙色の紙。縁ほど暗く、茶色い染みが散っている
    const float dial = smoothstep(CLOCK_DIAL_OUTER_RADIUS + footprint, CLOCK_DIAL_INNER_RADIUS, radius);
    const float stain = smoothstep(CLOCK_DIAL_STAIN_START, CLOCK_DIAL_STAIN_FULL, FastValueNoise(p * CLOCK_DIAL_STAIN_SCALE + CLOCK_DIAL_STAIN_SEED));
    const float foxing = smoothstep(CLOCK_DIAL_FOXING_START, CLOCK_DIAL_FOXING_FULL, FastValueNoise(p * CLOCK_DIAL_FOXING_SCALE - CLOCK_DIAL_FOXING_SEED));
    float3 paper = CLOCK_DIAL_PAPER_COLOR * lerp(1.0f, CLOCK_DIAL_EDGE_DARK, smoothstep(CLOCK_DIAL_EDGE_START, CLOCK_DIAL_EDGE_END, radius));
    paper = lerp(paper, CLOCK_DIAL_STAIN_COLOR, stain * CLOCK_DIAL_STAIN_BLEND + foxing * CLOCK_DIAL_FOXING_BLEND);
    color = lerp(color, paper, dial);
    relief -= dial * CLOCK_DIAL_RECESS;

    const float3 ink = CLOCK_INK_COLOR;
    float inkAmount = 0.0f;

    // 目盛り：分の細い目盛り（60本）と、時の太い目盛り（12本）。一番近い目盛りだけを計算している
    const float angle = atan2(p.x, p.y);
    const float minuteIndex = round(angle / (CLOCK_PI / CLOCK_MINUTE_STEPS));
    const float minuteAngle = minuteIndex * (CLOCK_PI / CLOCK_MINUTE_STEPS);
    const float2 minuteDirection = float2(sin(minuteAngle), cos(minuteAngle));
    const float isHourTick = step(abs(fmod(abs(minuteIndex), CLOCK_MINUTE_TICKS_PER_HOUR_TICK)), 0.5f);
    const float tickInner = lerp(CLOCK_MINUTE_TICK_INNER_RADIUS, CLOCK_HOUR_TICK_INNER_RADIUS, isHourTick);
    const float tickWidth = lerp(CLOCK_MINUTE_TICK_HALF_WIDTH, CLOCK_HOUR_TICK_HALF_WIDTH, isHourTick);
    const float tickDistance = ClockSegmentDistance(p, minuteDirection * tickInner, minuteDirection * CLOCK_TICK_OUTER_RADIUS);
    inkAmount = max(inkAmount, 1.0f - smoothstep(tickWidth, tickWidth + footprint * CLOCK_TICK_PIXEL_FILTER, tickDistance));

    // ローマ数字：一番近い時の位置の数字だけを計算している（文字は傾けず、立てたまま並べている）
    const float hourIndex = round(angle / (CLOCK_PI / CLOCK_HOUR_STEPS));
    const uint hour = ((uint)((int)hourIndex + 12)) % 12u;
    const float hourAngle = hourIndex * (CLOCK_PI / CLOCK_HOUR_STEPS);
    const float2 numeralCenter = float2(sin(hourAngle), cos(hourAngle)) * CLOCK_NUMERAL_RADIUS;
    const uint numeral = ClockNumerals[hour];
    uint glyphCount = 0u;
    [unroll]
    for (uint countIndex = 0u; countIndex < 4u; ++countIndex)
    {
        glyphCount += ((numeral >> (countIndex * 2u)) & 3u) != 0u ? 1u : 0u;
    }
    const float glyphSpacing = CLOCK_GLYPH_SPACING;
    float glyphDistance = CLOCK_GLYPH_FAR_DISTANCE;
    [unroll]
    for (uint glyphIndex = 0u; glyphIndex < 4u; ++glyphIndex)
    {
        const uint glyph = (numeral >> (glyphIndex * 2u)) & 3u;
        if (glyph != 0u)
        {
            const float offset = (float(glyphIndex) - (float(glyphCount) - 1.0f) * 0.5f) * glyphSpacing;
            glyphDistance = min(glyphDistance, ClockGlyphDistance(p - numeralCenter - float2(offset, 0.0f), glyph));
        }
    }
    // 文字の上下の横線（セリフ）を、文字の幅いっぱいに引いている
    const float serifHalf = float(glyphCount) * glyphSpacing * 0.5f;
    const float2 numeralLocal = p - numeralCenter;
    const float serif = min(
        ClockSegmentDistance(numeralLocal, float2(-serifHalf, CLOCK_SERIF_Y), float2(serifHalf, CLOCK_SERIF_Y)),
        ClockSegmentDistance(numeralLocal, float2(-serifHalf, -CLOCK_SERIF_Y), float2(serifHalf, -CLOCK_SERIF_Y)));
    inkAmount = max(inkAmount, 1.0f - smoothstep(CLOCK_NUMERAL_HALF_STROKE, CLOCK_NUMERAL_HALF_STROKE + footprint * CLOCK_HAND_EDGE_FILTER,
        min(glyphDistance, serif + CLOCK_SERIF_THICKEN)));

    // 色あせたインク：染みの所は薄く
    color = lerp(color, ink, inkAmount * dial * lerp(CLOCK_INK_CLEAR_PAPER_GAIN, CLOCK_INK_STAINED_PAPER_GAIN, stain));
    relief -= inkAmount * dial * CLOCK_INK_RECESS;

    // 針（青く焼けた鋼）と、真鍮の中心の軸
    const float hourHand = ClockHand(p, hands.x, CLOCK_HOUR_HAND_LENGTH, CLOCK_HOUR_HAND_HALF_WIDTH, CLOCK_HOUR_HAND_TAIL, footprint);
    const float minuteHand = ClockHand(p, hands.y, CLOCK_MINUTE_HAND_LENGTH, CLOCK_MINUTE_HAND_HALF_WIDTH, CLOCK_MINUTE_HAND_TAIL, footprint);
    const float3 steel = CLOCK_HAND_COLOR;
    color = lerp(color, steel, saturate(hourHand + minuteHand));
    relief += saturate(hourHand + minuteHand) * CLOCK_HAND_RELIEF;
    const float cap = smoothstep(CLOCK_AXLE_OUTER + footprint, CLOCK_AXLE_INNER, radius);
    color = lerp(color, CLOCK_AXLE_COLOR, cap);

    // ガラス：くもりと、斜めの淡い映り込み、それに右側から走る1本のひび
    const float sheen = smoothstep(CLOCK_SHEEN_WIDTH, 0.0f, abs(p.x * CLOCK_SHEEN_SLANT + p.y * CLOCK_SHEEN_SLANT - CLOCK_SHEEN_OFFSET)) * CLOCK_SHEEN_BRIGHTNESS;
    const float haze = FastValueNoise(p * CLOCK_HAZE_SCALE + CLOCK_HAZE_SEED) * CLOCK_HAZE_BRIGHTNESS;
    const float crackPath = abs(p.y - (p.x * CLOCK_CRACK_SLOPE + CLOCK_CRACK_OFFSET + sin(p.x * CLOCK_CRACK_WAVE_FREQUENCY) * CLOCK_CRACK_WAVE_AMOUNT));
    const float crack = (1.0f - smoothstep(CLOCK_CRACK_CORE, CLOCK_CRACK_EDGE + footprint, crackPath)) * step(CLOCK_CRACK_START_X, p.x) * dial;
    color += (sheen + haze) * dial;
    color = lerp(color, CLOCK_CRACK_COLOR, crack * CLOCK_CRACK_BLEND);

    ClockPaint paint;
    paint.albedo = color;
    paint.relief = relief;
    return paint;
}

#endif
