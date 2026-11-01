// ============================================================================
// 共通の処理: 2面の「見えない影」の異変で、姿の見えない人影が落とす影だけを描いている。litTexturePS と wetFloorPS で使っている。
// 主な技術: 解析的なカプセルの影（シャドウマップを使わず、光源から画素へ向かう線分と、人影の芯の線分の一番近い距離から求める）
// ・人影は物として描いていないため、シャドウマップにも写らない。そこで人の形を、胴（縦長のカプセル）と頭（球）で表し、
//   天井の照明・揺れる裸電球・懐中電灯の光が、その形に遮られるかを画素ごとに調べている。
// ・遮る所から画素までが遠いほど影の縁をぼかし（半影）、電球の下を通ると、床と壁に人の形の影だけが動いて見える。
// ・異変の周回でないとき（UnseenFigure.w = 0）は、計算を丸ごと飛ばしている。
// waterCaustics.hlsli（b12 の UnseenFigure・CausticView）と horrorMist.hlsli（GetMistEyePosition）の後に読み込んでいる。
// ============================================================================
#ifndef FIGURE_SHADOW_INCLUDED
#define FIGURE_SHADOW_INCLUDED

// 2つの線分（p1-q1 と p2-q2）が一番近づくときの距離を返している。along1 は1つ目の線分の上の位置（0〜1）
float SegmentSegmentDistance(float3 p1, float3 q1, float3 p2, float3 q2, out float along1)
{
    const float3 direction1 = q1 - p1;
    const float3 direction2 = q2 - p2;
    const float3 offset = p1 - p2;
    const float length1 = max(dot(direction1, direction1), 1e-5f);
    const float length2 = max(dot(direction2, direction2), 1e-5f);
    const float cross12 = dot(direction1, direction2);
    const float along1Offset = dot(direction1, offset);
    const float along2Offset = dot(direction2, offset);
    const float denominator = length1 * length2 - cross12 * cross12;
    // 平行に近いときは1つ目の線分の始点から求めている
    float s = denominator > 1e-5f ? saturate((cross12 * along2Offset - along1Offset * length2) / denominator) : 0.0f;
    float t = (cross12 * s + along2Offset) / length2;
    // 2つ目の線分の外へ出たら端に止め、1つ目の線分の位置を求め直している
    if (t < 0.0f)
    {
        t = 0.0f;
        s = saturate(-along1Offset / length1);
    }
    else if (t > 1.0f)
    {
        t = 1.0f;
        s = saturate((cross12 - along1Offset) / length1);
    }
    along1 = s;
    return length(p1 + direction1 * s - (p2 + direction2 * t));
}

// カプセル（芯の線分 a-b、半径 radius）が、画素から光源への線分をどれだけ遮るか（0〜1）を返している。
// 遮る所から画素までの距離に合わせて縁をぼかし、近い所ほどくっきり、遠い所ほどぼんやりした影にしている
float GetCapsuleOcclusion(float3 pixel, float3 light, float3 a, float3 b, float radius)
{
    float along;
    const float distance = SegmentSegmentDistance(pixel, light, a, b, along);
    const float penumbra = 0.8f + along * length(light - pixel) * 0.07f;
    // 画素のすぐそば（自分の足もとの面）では遮らないようにしている
    return smoothstep(radius + penumbra, radius - penumbra * 0.5f, distance) * smoothstep(0.0f, 0.03f, along);
}

// 見えない影の人影が、lightPosition から worldPosition へ届く光をどれだけ通すか（0〜1）を返している。
// 完全な黒にはせず、9割まで遮っている（まわりの照り返しが残るため）
float GetUnseenFigureShadow(float3 worldPosition, float3 lightPosition)
{
    [branch]
    if (UnseenFigure.w <= 0.001f)
    {
        return 1.0f;
    }
    const float3 foot = UnseenFigure.xyz;
    // 人影を囲む球（中心は胸の高さ、半径27）に、画素から光源への線分が届かない組み合わせは、点と線分の距離だけで先に飛ばしている
    // （ほとんどの画素と光源はここで終わるため、重いカプセルの計算をしない）
    const float3 center = foot + float3(0.0f, 19.0f, 0.0f);
    const float3 toLight = lightPosition - worldPosition;
    const float along = saturate(dot(center - worldPosition, toLight) / max(dot(toLight, toLight), 1e-4f));
    const float3 closest = worldPosition + toLight * along - center;
    [branch]
    if (dot(closest, closest) > 27.0f * 27.0f)
    {
        return 1.0f;
    }
    const float body = GetCapsuleOcclusion(worldPosition, lightPosition,
        foot + float3(0.0f, 4.0f, 0.0f), foot + float3(0.0f, 29.0f, 0.0f), 5.4f);
    const float head = GetCapsuleOcclusion(worldPosition, lightPosition,
        foot + float3(0.0f, 35.5f, 0.0f), foot + float3(0.0f, 36.5f, 0.0f), 3.8f);
    return 1.0f - max(body, head) * 0.9f * saturate(UnseenFigure.w);
}

// 懐中電灯（目の位置から照らしている）の光を、見えない影の人影が遮った残りの割合を返している
float GetUnseenFigureFlashlightShadow(float3 worldPosition)
{
    [branch]
    if (UnseenFigure.w <= 0.001f)
    {
        return 1.0f;
    }
    return GetUnseenFigureShadow(worldPosition, GetMistEyePosition());
}

#endif
