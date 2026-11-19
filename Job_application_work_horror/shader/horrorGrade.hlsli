// ============================================================================
// 共通の処理: 面の色に掛けるホラー向けの色調（壁・天井・小物の litTexturePS と、床の wetFloorPS で共通）。
// 主な技術: 明るさに応じた彩度の落とし方、黒を沈める足元のカーブ、
//           影と明るい所で色を分ける（スプリットトーン）
//
// ・清潔なオフィスに見えないよう、色を抜き、暗い所を深く沈めて、光の当たった所とその間の闇の差をはっきりさせている。
// ・影は病的な緑がかった暗い青に、明るい所は黄ばんだ古い蛍光灯の色に寄せている。
// ・霧より後、ブルームより前に掛けているので、光の筋やブルームのにじみは残る。
// ・周回の深さ（loopDepth。0〜1、2面のループが進むほど大きい）を渡すと、P.T. の廊下のように、
//   色が少しずつ赤黒く沈み、霧が近くから濃く、赤黒い闇の色になっていく。1面と2面の最初の周回は0で、これまでと同じ色。
// ============================================================================
#ifndef HORROR_GRADE_INCLUDED
#define HORROR_GRADE_INCLUDED

// 2面の周回が一番深いとき、とても暗い所をさらに沈める割合
static const float LOOP_DEPTH_EXTRA_BLACK_CRUSH = 0.12f;

// 暗いところの彩度を落とし、非常灯や照明が当たった色は残す。
static const float HORROR_DARK_SATURATION = 0.70f;
static const float HORROR_LIT_SATURATION = 0.82f;
static const float HORROR_SATURATION_START_LUMA = 0.05f;
static const float HORROR_SATURATION_FULL_LUMA = 0.50f;

// 黒つぶしは暗部に限り、照らされた面の輪郭を残す。
static const float HORROR_BLACK_CRUSH_END_LUMA = 0.14f;

// 影を青緑に、明るい所を黄ばんだ色に寄せる明るさの境界と混合率。
static const float HORROR_SHADOW_TINT_START_LUMA = 0.06f;
static const float HORROR_SHADOW_TINT_END_LUMA = 0.40f;
static const float HORROR_HIGHLIGHT_TINT_START_LUMA = 0.35f;
static const float HORROR_HIGHLIGHT_TINT_FULL_LUMA = 0.90f;
static const float3 HORROR_SHADOW_TINT = float3(0.84f, 0.98f, 0.95f);
static const float3 HORROR_HIGHLIGHT_TINT = float3(1.06f, 1.00f, 0.82f);
static const float HORROR_SHADOW_TINT_BLEND = 0.75f;
static const float HORROR_HIGHLIGHT_TINT_BLEND = 0.50f;

// 深い周回ほど、影の赤みと明部の橙を増やし、全体の光量を下げる。
static const float3 HORROR_LOOP_SHADOW_TINT = float3(1.02f, 0.70f, 0.66f);
static const float3 HORROR_LOOP_HIGHLIGHT_TINT = float3(1.04f, 0.88f, 0.78f);
static const float HORROR_LOOP_TINT_BASE_BLEND = 0.45f;
static const float HORROR_LOOP_SHADOW_TINT_GAIN = 0.55f;
static const float HORROR_LOOP_HIGHLIGHT_TINT_GAIN = 0.60f;
static const float HORROR_LOOP_DARKEN_GAIN = 0.14f;

float3 ApplyFilmicHorrorGrade(float3 color, float loopDepth)
{
    const float depth = saturate(loopDepth);
    color = max(color, 0.0f);

    // 明るい所を丸める曲線（ACES）は、画面効果の最後のトーンマッピング（toneMapPS.hlsl）で画面全体に1回だけ掛けている
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));

    // 彩度：暗い所ほど色を抜き（70%）、照らされた所に少しだけ色を残している（82%）。
    // 暗い所でも、非常灯の赤や電球の橙の光だまりの色は残るようにしている
    const float saturation = lerp(HORROR_DARK_SATURATION, HORROR_LIT_SATURATION, smoothstep(HORROR_SATURATION_START_LUMA, HORROR_SATURATION_FULL_LUMA, luminance));
    color = lerp(luminance.xxx, color, saturation);

    // 黒を沈める：光の届かない所が灰色ではなく闇に見えるよう、とても暗い所ほど暗くする。
    // 普段の分は、トーンマッピングの ACES の曲線の足元（暗い所ほど強く沈む）に任せ、ここでは2面の周回が深い分だけを足している
    const float toe = smoothstep(0.0f, HORROR_BLACK_CRUSH_END_LUMA, luminance);
    color *= lerp(1.0f - depth * LOOP_DEPTH_EXTRA_BLACK_CRUSH, 1.0f, toe);

    // 影は病的な緑がかった暗い青、明るい所は黄ばんだ古い蛍光灯の色に寄せている
    const float shadowWeight = 1.0f - smoothstep(HORROR_SHADOW_TINT_START_LUMA, HORROR_SHADOW_TINT_END_LUMA, luminance);
    const float highlightWeight = smoothstep(HORROR_HIGHLIGHT_TINT_START_LUMA, HORROR_HIGHLIGHT_TINT_FULL_LUMA, luminance);
    color *= lerp(1.0f.xxx, HORROR_SHADOW_TINT, shadowWeight * HORROR_SHADOW_TINT_BLEND);
    color *= lerp(1.0f.xxx, HORROR_HIGHLIGHT_TINT, highlightWeight * HORROR_HIGHLIGHT_TINT_BLEND);

    // 周回の深さ：暗い所ほど古い血のような赤黒に、明るい所はくすんだ橙に寄せ、全体を少しずつ沈めている
    color *= lerp(1.0f.xxx, HORROR_LOOP_SHADOW_TINT, depth * (HORROR_LOOP_TINT_BASE_BLEND + shadowWeight * HORROR_LOOP_SHADOW_TINT_GAIN));
    color *= lerp(1.0f.xxx, HORROR_LOOP_HIGHLIGHT_TINT, depth * highlightWeight * HORROR_LOOP_HIGHLIGHT_TINT_GAIN);
    color *= 1.0f - depth * HORROR_LOOP_DARKEN_GAIN;
    // 1を超える明るさは切らずに残している（HDR。トーンマッピングで丸める）
    return max(color, 0.0f);
}

// 周回に応じて距離の霧を手前へ寄せ、密度を上げる。
static const float DISTANCE_FOG_START = 110.0f;
static const float DISTANCE_FOG_FULL = 390.0f;
static const float DISTANCE_FOG_LOOP_START_SHIFT = 45.0f;
static const float DISTANCE_FOG_LOOP_FULL_SHIFT = 140.0f;
static const float DISTANCE_FOG_BASE_STRENGTH = 0.72f;
static const float DISTANCE_FOG_LOOP_STRENGTH_GAIN = 0.16f;

// 深い周回では足元の霧も濃くし、遠景の霧と重ねて廊下の見通しを狭める。
static const float HEIGHT_FOG_LOOP_STRENGTH_GAIN = 0.80f;

// 遠景を環境光より暗く保ち、周回につれて赤みを増す。
static const float FOG_AMBIENT_BLEND = 0.06f;
static const float3 FOG_MINIMUM_TINT = float3(0.010f, 0.012f, 0.013f);
static const float3 FOG_DEEP_LOOP_TINT = float3(0.026f, 0.006f, 0.005f);

// 遠くの霧の色：灰色のもやではなく、ほとんど黒い闇に溶けていく色（環境光が強い所だけ、わずかに明るい）。
// 周回が深いほど、赤黒い闇の色に寄せている
float3 GetHorrorFogColor(float3 ambient, float loopDepth)
{
    const float3 darkness = max(ambient * FOG_AMBIENT_BLEND, FOG_MINIMUM_TINT);
    return lerp(darkness, FOG_DEEP_LOOP_TINT, saturate(loopDepth));
}

// 距離の霧の濃さ（0〜1）：110〜390の間で濃くなる。周回が深いほど、近くから始まり、濃くなっている
float GetHorrorDistanceFog(float distanceFromCamera, float loopDepth)
{
    const float depth = saturate(loopDepth);
    return smoothstep(DISTANCE_FOG_START - depth * DISTANCE_FOG_LOOP_START_SHIFT, DISTANCE_FOG_FULL - depth * DISTANCE_FOG_LOOP_FULL_SHIFT, distanceFromCamera) *
        (DISTANCE_FOG_BASE_STRENGTH + depth * DISTANCE_FOG_LOOP_STRENGTH_GAIN);
}

// 床の近くの霧の濃さに掛ける倍率（周回が深いほど、足元に霧が溜まる）
float GetHorrorHeightFogScale(float loopDepth)
{
    return 1.0f + saturate(loopDepth) * HEIGHT_FOG_LOOP_STRENGTH_GAIN;
}

// 壁と床で同じ高さ・距離に霧を置く。濃度の揺れと霧全体の強さは個別に調整できる。
static const float HEIGHT_FOG_DECAY = 0.055f;
static const float HEIGHT_FOG_NEAR_START = 35.0f;
static const float HEIGHT_FOG_NEAR_END = 150.0f;
static const float HEIGHT_FOG_BASE_VARIATION = 0.78f;
static const float HEIGHT_FOG_NOISE_SCALE = 0.018f;
static const float HEIGHT_FOG_NOISE_OFFSET = 21.0f;
static const float HEIGHT_FOG_NOISE_WEIGHT = 0.22f;
static const float HEIGHT_FOG_STRENGTH = 0.22f;

#endif
