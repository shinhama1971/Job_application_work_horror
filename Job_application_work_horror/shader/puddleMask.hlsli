// ============================================================================
// 共通の処理: 床の水たまりの形（どこが水たまりか、岸の線、ゆっくりした波の模様）。
// 主な技術: CPUで決めたマス目の表（b13）、マス目ごとに回した楕円、ノイズでゆがめた岸
// 濡れた床（wetFloorPS）と、水たまりの波のシミュレーション（waterRippleCS）が同じ形を使えるよう、ここにまとめている。
// fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef PUDDLE_MASK_INCLUDED
#define PUDDLE_MASK_INCLUDED

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
    const float cellSize = 82.0f;
    const float2 gridPosition = (worldPosition + PuddleGridOffset.xy) / cellSize;
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
    if (hasPuddle < 0.5f)
    {
        shore = 0.0f;
        ripplePattern = 0.0f;
        return 0.0f;
    }

    // 水たまりの中心をずらし、ランダムに回した楕円にしている
    const float2 randomValue = cellValue.yz;
    localPosition -= (randomValue - 0.5f) * 0.22f;

    const float angle = randomValue.x * 6.2831853f;
    const float cosine = cos(angle);
    const float sine = sin(angle);
    const float2 rotated = float2(
        localPosition.x * cosine - localPosition.y * sine,
        localPosition.x * sine + localPosition.y * cosine);
    const float2 axes = float2(
        0.72f + randomValue.x * 0.25f,
        0.43f + randomValue.y * 0.20f);

    // 縁をノイズと波形でゆがませ、自然な形の水たまりにしている
    const float radialDistance = length(rotated / axes);
    const float polarAngle = atan2(rotated.y, rotated.x);
    const float organicLobes =
        sin(polarAngle * 5.0f + randomValue.x * 9.0f) * 0.045f +
        sin(polarAngle * 9.0f + randomValue.y * 13.0f) * 0.022f;
    const float edgeWarp =
        (FastValueNoise(worldPosition * 0.052f + cell * 1.73f) - 0.5f) * 0.22f +
        organicLobes;
    const float irregularDistance = radialDistance + edgeWarp;

    // 水たまりの濃さ、岸の細い線、ゆっくり広がる波と交差する波の模様
    const float puddle =
        (1.0f - smoothstep(0.34f, 0.40f, irregularDistance)) * hasPuddle;
    shore = (1.0f - smoothstep(
        0.012f,
        0.070f,
        abs(irregularDistance - 0.37f))) * hasPuddle;
    const float slowWave = sin(
        radialDistance * 48.0f + edgeWarp * 18.0f - time * 1.65f);
    const float crossingWave = sin(
        rotated.x * 76.0f - rotated.y * 41.0f + time * 1.10f);
    ripplePattern =
        (slowWave * 0.72f + crossingWave * 0.28f) * puddle;
    return puddle;
}

#endif
