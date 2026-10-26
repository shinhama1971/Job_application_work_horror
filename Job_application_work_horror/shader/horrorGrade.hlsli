// ============================================================================
// 共通の処理: 面の色に掛けるホラー向けの色調（壁・天井・小物の litTexturePS と、床の wetFloorPS で共通）。
// 主な技術: 明るい所をなだらかに抑えるフィルム風の曲線、明るさに応じた彩度の落とし方、黒を沈める足元のカーブ、
//           影と明るい所で色を分ける（スプリットトーン）
//
// ・清潔なオフィスに見えないよう、色を抜き、暗い所を深く沈めて、光の当たった所とその間の闇の差をはっきりさせている。
// ・影は病的な緑がかった暗い青に、明るい所は黄ばんだ古い蛍光灯の色に寄せている。
// ・霧より後、ブルームより前に掛けているので、光の筋やブルームのにじみは残る。
// ============================================================================
#ifndef HORROR_GRADE_INCLUDED
#define HORROR_GRADE_INCLUDED

float3 ApplyFilmicHorrorGrade(float3 color)
{
    color = max(color, 0.0f);

    // 明るい所だけをフィルムのようになだらかに抑えている（白く飛ばしすぎない）
    const float3 acesColor = saturate(
        (color * (2.51f * color + 0.03f)) /
        (color * (2.43f * color + 0.59f) + 0.14f));
    color = lerp(color, acesColor, 0.28f);

    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));

    // 彩度：暗い所ほど色を抜き（55%）、照らされた所に少しだけ色を残している（80%）
    const float saturation = lerp(0.55f, 0.80f, smoothstep(0.05f, 0.50f, luminance));
    color = lerp(luminance.xxx, color, saturation);

    // 黒を沈める：とても暗い所ほど、さらに暗くしている（光の届かない所が、灰色ではなく闇に見える）
    const float toe = smoothstep(0.0f, 0.14f, luminance);
    color *= lerp(0.50f, 1.0f, toe);

    // 影は病的な緑がかった暗い青、明るい所は黄ばんだ古い蛍光灯の色に寄せている
    const float shadowWeight = 1.0f - smoothstep(0.06f, 0.40f, luminance);
    const float highlightWeight = smoothstep(0.35f, 0.90f, luminance);
    color *= lerp(1.0f.xxx, float3(0.84f, 0.98f, 0.95f), shadowWeight * 0.75f);
    color *= lerp(1.0f.xxx, float3(1.06f, 1.00f, 0.82f), highlightWeight * 0.50f);
    return saturate(color);
}

// 遠くの霧の色：灰色のもやではなく、ほとんど黒い闇に溶けていく色（環境光が強い所だけ、わずかに明るい）
float3 GetHorrorFogColor(float3 ambient)
{
    return max(ambient * 0.06f, float3(0.010f, 0.012f, 0.013f));
}

#endif
