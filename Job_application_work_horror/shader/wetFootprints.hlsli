// ============================================================================
// 共通の処理: 床に残る濡れた足跡（WetFootprints が持つ場所と濡れ具合）の形を描いている。
// 主な技術: 楕円の距離で作る靴底と裸足の形、足の向きへの回転と左右の反転、乾きかけの縁のむら
// 濡れた床（wetFloorPS）が、WetFloorBuffer（b10）の足跡を読んで使っている。fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef WET_FOOTPRINTS_INCLUDED
#define WET_FOOTPRINTS_INCLUDED

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 足跡の輪郭をぼかし、以前と同じ足の長さで描く。
#define FOOTPRINT_ELLIPSE_FEATHER_START 0.80f
#define FOOTPRINT_WORLD_SHAPE_SCALE 1.6f

// 靴底の溝の幅と間隔。溝の有無で湿りの濃さを少し変える。
#define FOOTPRINT_SHOE_TREAD_BASE 0.72f
#define FOOTPRINT_SHOE_TREAD_GAIN 0.28f
#define FOOTPRINT_SHOE_TREAD_THRESHOLD 0.45f
#define FOOTPRINT_SHOE_TREAD_FREQUENCY 2.4f

// 裸足の指は外側ほど小さく、かかと側へ曲がって並ぶ。
#define FOOTPRINT_TOE_COUNT 5
#define FOOTPRINT_TOE_FIRST_LEFT_DISTANCE 0.42f
#define FOOTPRINT_TOE_X_SPACING 0.22f
#define FOOTPRINT_TOE_FIRST_FORWARD 1.45f
#define FOOTPRINT_TOE_FORWARD_STEP 0.09f
#define FOOTPRINT_TOE_FORWARD_CURVATURE 0.015f
#define FOOTPRINT_TOE_FIRST_RADIUS 0.17f
#define FOOTPRINT_TOE_RADIUS_STEP 0.018f
#define FOOTPRINT_TOE_ASPECT 1.15f

// 計算する半径を絞り、乾きかけた足跡の縁を細かいノイズで欠けさせる。
#define FOOTPRINT_MAX_DISTANCE_SQUARED 20.0f
#define FOOTPRINT_DRY_EDGE_FADE_END 0.35f
#define FOOTPRINT_DRY_EDGE_NOISE_SCALE 1.7f
#define FOOTPRINT_DRY_EDGE_NOISE_WEIGHT 0.25f

// 楕円の内側で1、外側で0（縁を少しぼかしている）
float FootprintEllipse(float2 position, float2 center, float2 radius)
{
    return 1.0f - smoothstep(FOOTPRINT_ELLIPSE_FEATHER_START, 1.0f, length((position - center) / radius));
}

// 靴の形：つま先側の楕円（中心・半径）とかかとの楕円（中心・半径）
#define SHOE_FRONT_CENTER float2(0.0f, 0.9f)
#define SHOE_FRONT_RADIUS float2(0.75f, 1.25f)
#define SHOE_HEEL_CENTER float2(0.0f, -1.15f)
#define SHOE_HEEL_RADIUS float2(0.62f, 0.70f)
// 裸足の形：指の付け根のふくらみ、かかと、土踏まずの外側（それぞれ中心・半径）
#define BARE_BALL_CENTER float2(0.05f, 0.45f)
#define BARE_BALL_RADIUS float2(0.62f, 0.78f)
#define BARE_HEEL_CENTER float2(0.0f, -1.10f)
#define BARE_HEEL_RADIUS float2(0.50f, 0.62f)
#define BARE_ARCH_CENTER float2(0.25f, -0.35f)
#define BARE_ARCH_RADIUS float2(0.30f, 0.75f)
// 足跡の並び（置き方・状態の2つ）と、裸足かを見るしきい値
#define FOOTPRINT_STRIDE 2
#define FOOTPRINT_BARE_THRESHOLD 0.5f

// 足跡1つの形（0〜1）。localは足の向きに合わせた座標（x = 右、y = つま先の方向。単位はワールド）。
// 靴は、つま先側の楕円とかかとの楕円に、靴底の溝の横縞。裸足は、くびれた足の裏と5本の指
float FootprintShape(float2 local, bool bare)
{
    // 足の長さ（約6.5）に合わせ、形は長さ4ほどで作ってから広げている
    const float2 p = local / FOOTPRINT_WORLD_SHAPE_SCALE;
    if (!bare)
    {
        const float front = FootprintEllipse(p, SHOE_FRONT_CENTER, SHOE_FRONT_RADIUS);
        const float heel = FootprintEllipse(p, SHOE_HEEL_CENTER, SHOE_HEEL_RADIUS);
        const float tread = FOOTPRINT_SHOE_TREAD_BASE + FOOTPRINT_SHOE_TREAD_GAIN * step(FOOTPRINT_SHOE_TREAD_THRESHOLD, frac(p.y * FOOTPRINT_SHOE_TREAD_FREQUENCY));
        return max(front, heel) * tread;
    }
    const float ball = FootprintEllipse(p, BARE_BALL_CENTER, BARE_BALL_RADIUS);
    const float heel = FootprintEllipse(p, BARE_HEEL_CENTER, BARE_HEEL_RADIUS);
    // 土踏まずは内側（x の負の側）がくびれ、外側だけがつながっている
    const float outerArch = FootprintEllipse(p, BARE_ARCH_CENTER, BARE_ARCH_RADIUS);
    float sole = max(max(ball, heel), outerArch);
    // 指（親指が内側で大きく、外側ほど小さく、少し下がる）
    [unroll]
    for (int toe = 0; toe < FOOTPRINT_TOE_COUNT; ++toe)
    {
        const float t = float(toe);
        const float2 center = float2(-FOOTPRINT_TOE_FIRST_LEFT_DISTANCE + t * FOOTPRINT_TOE_X_SPACING, FOOTPRINT_TOE_FIRST_FORWARD - t * FOOTPRINT_TOE_FORWARD_STEP - t * t * FOOTPRINT_TOE_FORWARD_CURVATURE);
        const float radius = FOOTPRINT_TOE_FIRST_RADIUS - t * FOOTPRINT_TOE_RADIUS_STEP;
        sole = max(sole, FootprintEllipse(p, center, float2(radius, radius * FOOTPRINT_TOE_ASPECT)));
    }
    return sole;
}

// その位置の、濡れた足跡の濃さ（0〜1）を返している
float GetWetFootprints(float2 worldPosition)
{
    float wet = 0.0f;
    const int count = (int)(FootprintInfo.x + 0.5f);
    [loop]
    for (int index = 0; index < count; ++index)
    {
        const float4 placement = Footprints[index * FOOTPRINT_STRIDE];
        const float2 offset = worldPosition - placement.xy;
        // 足跡の大きさ（半分で約4）より遠い所は計算しない
        if (dot(offset, offset) > FOOTPRINT_MAX_DISTANCE_SQUARED)
        {
            continue;
        }
        const float4 state = Footprints[index * FOOTPRINT_STRIDE + 1];
        // 足の向き（前 = (sin, cos)、右 = (cos, -sin)）の座標へ回し、左足は左右を反転している
        const float2 forward = float2(placement.z, placement.w);
        const float2 right = float2(placement.w, -placement.z);
        const float2 local = float2(dot(offset, right) * state.y, dot(offset, forward));
        const float shape = FootprintShape(local, state.z > FOOTPRINT_BARE_THRESHOLD);
        // 乾きかけは縁から乾くので、薄い所ほどノイズで欠けさせている
        const float dryEdge = smoothstep(0.0f, FOOTPRINT_DRY_EDGE_FADE_END, state.x + (FastValueNoise(worldPosition * FOOTPRINT_DRY_EDGE_NOISE_SCALE) - 0.5f) * FOOTPRINT_DRY_EDGE_NOISE_WEIGHT);
        wet = max(wet, shape * state.x * dryEdge);
    }
    return saturate(wet);
}

#endif
