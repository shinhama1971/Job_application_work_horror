// ============================================================================
// 共通の処理: 面の色に掛けるホラー向けの色調（壁・天井・小物の litTexturePS と、床の wetFloorPS で共通）。
// 主な技術: 明るい所をなだらかに抑えるフィルム風の曲線、明るさに応じた彩度の落とし方、黒を沈める足元のカーブ、
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

float3 ApplyFilmicHorrorGrade(float3 color, float loopDepth)
{
    const float depth = saturate(loopDepth);
    color = max(color, 0.0f);

    // 明るい所だけをフィルムのようになだらかに抑えている（白く飛ばしすぎない）
    const float3 acesColor = saturate(
        (color * (2.51f * color + 0.03f)) /
        (color * (2.43f * color + 0.59f) + 0.14f));
    color = lerp(color, acesColor, 0.28f);

    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));

    // 彩度：暗い所ほど色を抜き（70%）、照らされた所に少しだけ色を残している（82%）。
    // 暗い所でも、非常灯の赤や電球の橙の光だまりの色は残るようにしている
    const float saturation = lerp(0.70f, 0.82f, smoothstep(0.05f, 0.50f, luminance));
    color = lerp(luminance.xxx, color, saturation);

    // 黒を沈める：とても暗い所ほど、さらに暗くしている（光の届かない所が、灰色ではなく闇に見える）
    const float toe = smoothstep(0.0f, 0.14f, luminance);
    color *= lerp(0.50f - depth * 0.12f, 1.0f, toe);

    // 影は病的な緑がかった暗い青、明るい所は黄ばんだ古い蛍光灯の色に寄せている
    const float shadowWeight = 1.0f - smoothstep(0.06f, 0.40f, luminance);
    const float highlightWeight = smoothstep(0.35f, 0.90f, luminance);
    color *= lerp(1.0f.xxx, float3(0.84f, 0.98f, 0.95f), shadowWeight * 0.75f);
    color *= lerp(1.0f.xxx, float3(1.06f, 1.00f, 0.82f), highlightWeight * 0.50f);

    // 周回の深さ：暗い所ほど古い血のような赤黒に、明るい所はくすんだ橙に寄せ、全体を少しずつ沈めている
    color *= lerp(1.0f.xxx, float3(1.02f, 0.70f, 0.66f), depth * (0.45f + shadowWeight * 0.55f));
    color *= lerp(1.0f.xxx, float3(1.04f, 0.88f, 0.78f), depth * highlightWeight * 0.60f);
    color *= 1.0f - depth * 0.14f;
    return saturate(color);
}

// 遠くの霧の色：灰色のもやではなく、ほとんど黒い闇に溶けていく色（環境光が強い所だけ、わずかに明るい）。
// 周回が深いほど、赤黒い闇の色に寄せている
float3 GetHorrorFogColor(float3 ambient, float loopDepth)
{
    const float3 darkness = max(ambient * 0.06f, float3(0.010f, 0.012f, 0.013f));
    return lerp(darkness, float3(0.026f, 0.006f, 0.005f), saturate(loopDepth));
}

// 距離の霧の濃さ（0〜1）：110〜390の間で濃くなる。周回が深いほど、近くから始まり、濃くなっている
float GetHorrorDistanceFog(float distanceFromCamera, float loopDepth)
{
    const float depth = saturate(loopDepth);
    return smoothstep(110.0f - depth * 45.0f, 390.0f - depth * 140.0f, distanceFromCamera) *
        (0.72f + depth * 0.16f);
}

// 床の近くの霧の濃さに掛ける倍率（周回が深いほど、足元に霧が溜まる）
float GetHorrorHeightFogScale(float loopDepth)
{
    return 1.0f + saturate(loopDepth) * 0.80f;
}

#endif
