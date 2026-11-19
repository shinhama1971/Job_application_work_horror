// ============================================================================
// 共通の処理: 床の水たまりの形（どこが水たまりか、岸の線、ゆっくりした波の模様）。
// 主な技術: CPUで決めたマス目の表（b13）、マス目ごとに回した楕円、ノイズでゆがめた岸
// 濡れた床（wetFloorPS）と、水たまりの波のシミュレーション（waterRippleCS）が同じ形を使えるよう、ここにまとめている。
// fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef PUDDLE_MASK_INCLUDED
#define PUDDLE_MASK_INCLUDED
#include "renderingConfig.hlsli"

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// マス目ごとの水たまりの表（b13）。WaterEffectSystem::Init がCPUで決めて渡している。
// x = 水たまりがあれば1、yz = 中心のずれと回転に使う乱数。マス目(PUDDLE_CELL_MIN, PUDDLE_CELL_MIN)から16x16マス。
// sin を使った乱数をシェーダーで計算すると、GPUの種類で sin の精度が違い、水たまりの場所が変わるため、表にしている。
#define PUDDLE_CELL_MIN (-8)
#define PUDDLE_CELL_COUNT 16
cbuffer PuddleCellBuffer : register(b13)
{
    float4 PuddleCells[PUDDLE_CELL_COUNT * PUDDLE_CELL_COUNT];
    // マス目の区切りをずらす量（xy = x・z）。1面は0、2面は廊下の中心にマス目の中心が来るようにずらしている
    float4 PuddleGridOffset;
}

// WaterEffectSystem::PuddleCellSize と同じワールド単位。CPU と水面の区画を揃える。
static const float PUDDLE_GRID_CELL_SIZE = SIGNAL_LOST_PUDDLE_GRID_CELL_SIZE;

// 中心のずれと楕円の大きさ。WaterEffectSystem の PuddleCenterJitter・PuddleAxisX/Z・PuddleAxisX/ZRange と揃える。
static const float PUDDLE_CENTER_JITTER = SIGNAL_LOST_PUDDLE_CENTER_JITTER;
static const float PUDDLE_FULL_TURN = 6.2831853f;
static const float PUDDLE_AXIS_X = SIGNAL_LOST_PUDDLE_AXIS_X;
static const float PUDDLE_AXIS_X_RANGE = SIGNAL_LOST_PUDDLE_AXIS_X_RANGE;
static const float PUDDLE_AXIS_Z = SIGNAL_LOST_PUDDLE_AXIS_Z;
static const float PUDDLE_AXIS_Z_RANGE = SIGNAL_LOST_PUDDLE_AXIS_Z_RANGE;

// 岸のうねりとノイズ。GroundWaterEffects.cpp の ShoreWave*・ShoreNoise* と同じ値で、濡れ判定と輪郭を揃える。
static const float PUDDLE_SHORE_WAVE_A_COUNT = SIGNAL_LOST_PUDDLE_SHORE_WAVE_A_COUNT;
static const float PUDDLE_SHORE_WAVE_B_COUNT = SIGNAL_LOST_PUDDLE_SHORE_WAVE_B_COUNT;
static const float PUDDLE_SHORE_WAVE_A_SEED = SIGNAL_LOST_PUDDLE_SHORE_WAVE_A_SEED;
static const float PUDDLE_SHORE_WAVE_B_SEED = SIGNAL_LOST_PUDDLE_SHORE_WAVE_B_SEED;
static const float PUDDLE_SHORE_WAVE_A_AMOUNT = SIGNAL_LOST_PUDDLE_SHORE_WAVE_A_AMOUNT;
static const float PUDDLE_SHORE_WAVE_B_AMOUNT = SIGNAL_LOST_PUDDLE_SHORE_WAVE_B_AMOUNT;
static const float PUDDLE_SHORE_NOISE_SCALE = SIGNAL_LOST_PUDDLE_SHORE_NOISE_SCALE;
static const float PUDDLE_SHORE_NOISE_CELL_SCALE = SIGNAL_LOST_PUDDLE_SHORE_NOISE_CELL_SCALE;
static const float PUDDLE_SHORE_NOISE_WARP = SIGNAL_LOST_PUDDLE_SHORE_NOISE_WARP;

// 水面の端を滑らかにし、境界の中心に細い岸の線を出す。岸の半径は WaterEffectSystem::PuddleShoreRadius と同じ値。
static const float PUDDLE_EDGE_INNER = 0.34f;
static const float PUDDLE_EDGE_OUTER = 0.40f;
static const float PUDDLE_SHORE_LINE_INNER = 0.012f;
static const float PUDDLE_SHORE_LINE_OUTER = 0.070f;
static const float PUDDLE_SHORE_RADIUS = SIGNAL_LOST_PUDDLE_SHORE_RADIUS;

// 放射状に広がる波と斜めに横切る波を重ね、単調な同心円を減らす。
static const float PUDDLE_RADIAL_WAVE_FREQUENCY = 48.0f;
static const float PUDDLE_RADIAL_WAVE_EDGE_WARP = 18.0f;
static const float PUDDLE_RADIAL_WAVE_SPEED = 1.65f;
static const float PUDDLE_CROSS_WAVE_X_FREQUENCY = 76.0f;
static const float PUDDLE_CROSS_WAVE_Z_FREQUENCY = 41.0f;
static const float PUDDLE_CROSS_WAVE_SPEED = 1.10f;
static const float PUDDLE_RADIAL_WAVE_WEIGHT = 0.72f;
static const float PUDDLE_CROSS_WAVE_WEIGHT = 0.28f;
// マス目に水たまりがあるかの印（0 か 1）を見るしきい値
#define PUDDLE_CELL_THRESHOLD 0.5f

// その位置の水たまりの濃さ（0〜1）を返し、岸の線と波の模様も返している（timeは波を動かす時間）
float GetPuddleMask(
    float2 worldPosition,
    float time,
    out float shore,
    out float ripplePattern)
{
    shore = 0.0f;
    ripplePattern = 0.0f;
    // 大きなワールド空間のマス目ごとに、不規則に回した水たまりを1つ置いている。
    // 半径をマス目の境目より小さくし、床全体を一様に濡らさず、独立した水たまりにしている。
    const float2 gridPosition = (worldPosition + PuddleGridOffset.xy) / PUDDLE_GRID_CELL_SIZE;
    const float2 cell = floor(gridPosition);
    float2 localPosition = frac(gridPosition) - 0.5f;

    // このマス目の値を表から読んでいる（表の外の床は乾いている扱い）
    const int2 tableIndex = int2(cell) - PUDDLE_CELL_MIN;
    float4 cellValue = 0.0f;
    if (all(tableIndex >= 0) && all(tableIndex < PUDDLE_CELL_COUNT))
    {
        cellValue = PuddleCells[tableIndex.y * PUDDLE_CELL_COUNT + tableIndex.x];
    }
    const float hasPuddle = cellValue.x;
    // 大半の床のマス目は乾いているため、対象外のマス目では、回転・輪郭のノイズ・波の計算を省いている。
    // （この分岐はワールド空間の大きなマス目の単位でそろうので、画素ごとにばらつかない）
    if (hasPuddle < PUDDLE_CELL_THRESHOLD)
    {
        shore = 0.0f;
        ripplePattern = 0.0f;
        return 0.0f;
    }

    // 水たまりの中心をずらし、ランダムに回した楕円にしている
    const float2 randomValue = cellValue.yz;
    localPosition -= (randomValue - 0.5f) * PUDDLE_CENTER_JITTER;

    const float angle = randomValue.x * PUDDLE_FULL_TURN;
    const float cosine = cos(angle);
    const float sine = sin(angle);
    const float2 rotated = float2(
        localPosition.x * cosine - localPosition.y * sine,
        localPosition.x * sine + localPosition.y * cosine);
    const float2 axes = float2(
        PUDDLE_AXIS_X + randomValue.x * PUDDLE_AXIS_X_RANGE,
        PUDDLE_AXIS_Z + randomValue.y * PUDDLE_AXIS_Z_RANGE);

    // 縁をノイズと波形でゆがませ、自然な形の水たまりにしている
    const float radialDistance = length(rotated / axes);
    const float polarAngle = atan2(rotated.y, rotated.x);
    const float organicLobes =
        sin(polarAngle * PUDDLE_SHORE_WAVE_A_COUNT + randomValue.x * PUDDLE_SHORE_WAVE_A_SEED) * PUDDLE_SHORE_WAVE_A_AMOUNT +
        sin(polarAngle * PUDDLE_SHORE_WAVE_B_COUNT + randomValue.y * PUDDLE_SHORE_WAVE_B_SEED) * PUDDLE_SHORE_WAVE_B_AMOUNT;
    const float edgeWarp =
        (FastValueNoise(worldPosition * PUDDLE_SHORE_NOISE_SCALE + cell * PUDDLE_SHORE_NOISE_CELL_SCALE) - 0.5f) * PUDDLE_SHORE_NOISE_WARP +
        organicLobes;
    const float irregularDistance = radialDistance + edgeWarp;

    // 水たまりの濃さ、岸の細い線、ゆっくり広がる波と交差する波の模様
    const float puddle =
        (1.0f - smoothstep(PUDDLE_EDGE_INNER, PUDDLE_EDGE_OUTER, irregularDistance)) * hasPuddle;
    shore = (1.0f - smoothstep(
        PUDDLE_SHORE_LINE_INNER,
        PUDDLE_SHORE_LINE_OUTER,
        abs(irregularDistance - PUDDLE_SHORE_RADIUS))) * hasPuddle;
    const float slowWave = sin(
        radialDistance * PUDDLE_RADIAL_WAVE_FREQUENCY + edgeWarp * PUDDLE_RADIAL_WAVE_EDGE_WARP - time * PUDDLE_RADIAL_WAVE_SPEED);
    const float crossingWave = sin(
        rotated.x * PUDDLE_CROSS_WAVE_X_FREQUENCY - rotated.y * PUDDLE_CROSS_WAVE_Z_FREQUENCY + time * PUDDLE_CROSS_WAVE_SPEED);
    ripplePattern =
        (slowWave * PUDDLE_RADIAL_WAVE_WEIGHT + crossingWave * PUDDLE_CROSS_WAVE_WEIGHT) * puddle;
    return puddle;
}

#endif
