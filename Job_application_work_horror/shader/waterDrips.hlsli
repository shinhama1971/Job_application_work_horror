// ============================================================================
// シェーダーの部品: 濡れた床の水たまりに落ちる水滴の、広がる輪（wetFloorPS.hlsl が使う）
// ============================================================================

#ifndef SIGNAL_LOST_WATERDRIPS_HLSLI
#define SIGNAL_LOST_WATERDRIPS_HLSLI

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

#endif
