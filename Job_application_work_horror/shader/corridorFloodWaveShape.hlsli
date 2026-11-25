// ============================================================================
// シェーダーの部品: 1面の濁流の形（波頭の高さ・幅・曲がり角の道筋）。
// 波の面（corridorFloodWaveVS）と、波頭から飛ぶしぶき（corridorFloodSprayVS）が同じ形を使うため、ここにまとめている。
// ============================================================================

#ifndef SIGNAL_LOST_CORRIDOR_FLOOD_WAVE_SHAPE_HLSLI
#define SIGNAL_LOST_CORRIDOR_FLOOD_WAVE_SHAPE_HLSLI

#include "corridorFloodWave.hlsli"

// 波の高さ（壁の高さ約51の3分の1強。腰より高い濁流）
static const float WaveHeight = 19.0f;
// 波頭の頂点の位置と、後ろへ下がり終える位置。頂点を前に寄せ、平らな台の形にせず丸い山にしている
static const float FirstCrestWidth = 0.24f;
// 先端から頂点まで立ち上がる長さ（格子の前後の割合）。短いと前の面が垂直な板になるので、斜めに盛り上がる長さにしている
static const float CrestStart = 0.2f;
static const float CrestDrift = 0.04f;
static const float CrestFall = 0.62f;
static const float TailHeight = 4.1f;
static const float TailStart = 0.35f;
static const float TailPeak = 0.6f;
static const float TailFade = 0.93f;
static const float TailBands = 26.0f;
static const float TailRippleSpeed = 8.0f;
static const float CrestChopSpeed = 4.4f;
static const float CrestChopFrequency = 13.0f;
// 幅の場所ごとの波の高さのむら（盛り上がった所と低い所が、ゆっくり横へ流れる）。むらが無いと、そろった高さの壁に見える
static const float CrestSwellAmount = 0.32f;
static const float CrestSwellFrequency = 5.0f;
static const float CrestSwellSpeed = 1.6f;
static const float CrestSwellWarpFrequency = 2.3f;
static const float CrestSwellWarpSpeed = 0.7f;
static const float CrestSwellWarpAmount = 1.3f;
// 両端で波を低くし始める位置（幅の割合）。廊下の幅いっぱいに押し寄せ、壁のすぐ手前で床へ下げている
static const float EdgeTaperStart = 0.86f;
// 壁に当たった所で波頭が這い上がる量（両端で、波の高さにこの割合を足す）と、這い上がり始める位置（幅の割合）、
// 右の通路から曲がり角へ入るときに、壁から離れていく距離
static const float WallRideGain = 0.7f;
static const float WallRideStart = 0.75f;
static const float WallContactFade = 12.0f;
static const float CrestFaceCompression = 0.32f;
// 波頭の上の方を前へ倒す量。前の面が切り立ち、のしかかってくる形になる（格子の前後が入れ替わらない大きさにしている）
static const float CrestLean = 1.4f;
static const float WaveFloorOverlap = 0.04f;

static const float InletHalfWidth = 18.0f;
// 曲がり終えたとき、外側の岸が南北の廊下の西の壁（x=-45）に届く幅
static const float OuterBendHalfWidth = 50.0f;
static const float MainHalfWidth = 45.0f;
static const float MainWidthDistance = 33.0f;
static const float BendStart = SIGNAL_LOST_FLOOD_SOURCE_X - SIGNAL_LOST_FLOOD_BEND_START_X;
static const float BendRadius = SIGNAL_LOST_FLOOD_BEND_RADIUS;
static const float BendLength = BendRadius * SIGNAL_LOST_FLOOD_QUARTER_TURN;
static const float BendEnd = BendStart + BendLength;
static const float BendCenterZ = SIGNAL_LOST_FLOOD_BEND_CENTER_Z - BendRadius;
static const float BendEndX = SIGNAL_LOST_FLOOD_BEND_START_X - BendRadius;

float2 GetFlowPosition(float distance, float lateral)
{
    if (distance < BendStart)
    {
        return float2(SIGNAL_LOST_FLOOD_SOURCE_X - distance, SIGNAL_LOST_FLOOD_BEND_CENTER_Z - lateral * InletHalfWidth);
    }
    if (distance < BendEnd)
    {
        // 各列は曲がり角までの距離が違う。後ろの列は先頭が曲がった後も右の通路を流れる。
        const float progress = (distance - BendStart) / BendLength;
        float sine, cosine;
        sincos(progress * SIGNAL_LOST_FLOOD_QUARTER_TURN, sine, cosine);
        const float halfWidth = lateral < 0.0f ? lerp(InletHalfWidth, OuterBendHalfWidth, smoothstep(0.0f, 1.0f, progress)) : InletHalfWidth;
        const float radius = BendRadius - lateral * halfWidth;
        return float2(SIGNAL_LOST_FLOOD_BEND_START_X - radius * sine, BendCenterZ + radius * cosine);
    }
    const float fromBend = distance - BendEnd;
    const float spread = smoothstep(0.0f, MainWidthDistance, fromBend);
    const float halfWidth = lerp(lateral < 0.0f ? OuterBendHalfWidth : InletHalfWidth, MainHalfWidth, spread);
    return float2(lerp(BendEndX, 0.0f, spread) + lateral * halfWidth, BendCenterZ - fromBend);
}

// その位置で、波の両端が壁に接しているか（0〜1）。右の通路と、曲がり終えて廊下いっぱいに広がった所では壁に当たり、
// 曲がっている途中は岸が壁から離れているので、両端を床へ下げる
float GetWallContact(float station)
{
    const float inlet = 1.0f - smoothstep(BendStart - WallContactFade, BendStart, station);
    const float main = smoothstep(0.0f, MainWidthDistance, station - BendEnd);
    return max(inlet, main);
}

float3 WavePosition(float2 uv, out float crest)
{
    const float lateral = uv.x * 2.0f - 1.0f;
    const float time = FloodShape.y;
    // 岸が同時に到達しないよう、先端を幅ごとにずらす。波頭の背後にも低い起伏を通す。
    const float chop = sin(lateral * CrestChopFrequency - time * CrestChopSpeed);
    const float peak = FirstCrestWidth + chop * CrestDrift;
    crest = smoothstep(0.0f, CrestStart, uv.y) * (1.0f - smoothstep(peak, CrestFall, uv.y));
    const float tail = smoothstep(TailStart, TailPeak, uv.y) * (1.0f - smoothstep(TailPeak, TailFade, uv.y));
    const float ridges = 0.5f + 0.5f * sin(uv.y * TailBands + lateral * lateral - time * TailRippleSpeed);

    const float depth = min(FloodShape.w, SIGNAL_LOST_FLOOD_WAVE_DEPTH);
    // 波頭を前に寄せながら、格子の前後の並びを保って折り返しを防ぐ。
    const float fromTail = 1.0f - uv.y;
    const float along = uv.y * depth * (1.0f - CrestFaceCompression * fromTail * fromTail) - CrestLean * crest * crest * FloodShape.z;
    const float station = FloodShape.w - along;

    // 壁に当たっている所では、両端を下げずに、波頭が壁を這い上がるように高くしている
    const float contact = GetWallContact(station);
    const float openShore = 1.0f - smoothstep(EdgeTaperStart, 1.0f, abs(lateral));
    const float shore = lerp(openShore, 1.0f, contact);
    const float rideUp = 1.0f + WallRideGain * smoothstep(WallRideStart, 1.0f, abs(lateral)) * contact;
    const float swell = 1.0f - CrestSwellAmount * (0.5f + 0.5f * sin(lateral * CrestSwellFrequency + time * CrestSwellSpeed +
        CrestSwellWarpAmount * sin(lateral * CrestSwellWarpFrequency - time * CrestSwellWarpSpeed)));
    const float height = (WaveHeight * crest * swell * rideUp + TailHeight * tail * ridges) * FloodShape.z * shore;

    const float2 xz = GetFlowPosition(station, lateral);
    return float3(xz.x, FloodShape.x - WaveFloorOverlap + height, xz.y);
}


#endif
