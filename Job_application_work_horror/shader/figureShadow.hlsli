// ============================================================================
// 共通の処理: 2面の「見えない影」の異変で、姿の見えない人影が落とす影と、プレイヤー自身の体の影を描いている。litTexturePS と wetFloorPS で使っている。
// 主な技術: 解析的なカプセルの影（シャドウマップを使わず、光源から画素へ向かう線分と、人影の芯の線分の一番近い距離から求める）
// ・人影は物として描いていないため、シャドウマップにも写らない。そこで人の形を、胴（縦長のカプセル）と頭（球）で表し、
//   天井の照明・揺れる裸電球・懐中電灯の光が、その形に遮られるかを画素ごとに調べている。
// ・遮る所から画素までが遠いほど影の縁をぼかし（半影）、電球の下を通ると、床と壁に人の形の影だけが動いて見える。
// ・異変の周回でないとき（UnseenFigure.w = 0）は、計算を丸ごと飛ばしている。
// waterCaustics.hlsli（b12 の UnseenFigure・CausticView）と horrorMist.hlsli（GetMistEyePosition）の後に読み込んでいる。
// 光の筋の Compute Shader（lightShaftsCS.hlsl）では、自分の定数バッファの UnseenFigure と GetMistEyePosition を用意して読み込んでいる。
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
// 遮る所から画素までの距離に合わせて縁をぼかし、近い所ほどくっきり、遠い所ほどぼんやりした影にしている（softness はぼけの広がり方）
float GetCapsuleOcclusion(float3 pixel, float3 light, float3 a, float3 b, float radius, float softness = 0.045f)
{
    float along;
    const float distance = SegmentSegmentDistance(pixel, light, a, b, along);
    const float penumbra = 0.5f + along * length(light - pixel) * softness;
    // 画素のすぐそば（自分の足もとの面）では遮らないようにしている
    return smoothstep(radius + penumbra, radius - penumbra * 0.5f, distance) * smoothstep(0.0f, 0.03f, along);
}

// 見えない影の人影が、lightPosition から worldPosition へ届く光をどれだけ通すか（0〜1）を返している。
// 影で位置を読めるよう、ほぼ真っ黒（97%）まで遮っている（まわりの照り返しは環境光として残る）
float GetUnseenFigureShadow(float3 worldPosition, float3 lightPosition)
{
    // 光の筋の Compute Shader では光源のループの中で何度も呼ぶため、途中で返さず、戻り値を1か所にまとめている
    float visibility = 1.0f;
    [branch]
    if (UnseenFigure.w > 0.001f)
    {
        const float3 foot = UnseenFigure.xyz;
        // 人影を囲む球（中心は胸の高さ、半径27）に、画素から光源への線分が届かない組み合わせは、点と線分の距離だけで先に飛ばしている
        // （ほとんどの画素と光源はここで終わるため、重いカプセルの計算をしない）
        const float3 center = foot + float3(0.0f, 19.0f, 0.0f);
        const float3 toLight = lightPosition - worldPosition;
        const float along = saturate(dot(center - worldPosition, toLight) / max(dot(toLight, toLight), 1e-4f));
        const float3 closest = worldPosition + toLight * along - center;
        [branch]
        if (dot(closest, closest) <= 27.0f * 27.0f)
        {
            const float body = GetCapsuleOcclusion(worldPosition, lightPosition,
                foot + float3(0.0f, 4.0f, 0.0f), foot + float3(0.0f, 29.0f, 0.0f), 5.4f);
            const float head = GetCapsuleOcclusion(worldPosition, lightPosition,
                foot + float3(0.0f, 35.5f, 0.0f), foot + float3(0.0f, 36.5f, 0.0f), 3.8f);
            visibility = 1.0f - max(body, head) * 0.97f * saturate(UnseenFigure.w);
        }
    }
    return visibility;
}

// プレイヤー自身の体が、lightPosition から worldPosition へ届く光をどれだけ通すか（0〜1）を返している。
// 一人称で体は描いていないが、天井の照明や裸電球の下に立つと、床と壁に自分の影が落ちる。
// 体は胴・頭・両脚・両腕のカプセルで表し、歩くと脚と左腕を振る。右腕は懐中電灯を持って前へ出している（FirstPersonHands と同じ持ち方）。
// 懐中電灯の光は目の位置から出ているので、自分の体では遮らない（呼ぶのは点光源だけ）
#if !defined(FIGURE_SHADOW_WITHOUT_PLAYER_BODY)
float GetPlayerBodyShadow(float3 worldPosition, float3 lightPosition)
{
    float visibility = 1.0f;
    [branch]
    if (PlayerBody.w > 0.5f)
    {
        const float3 foot = PlayerBody.xyz;
        // 体を囲む球（中心は腰の高さ、半径24）に、画素から光源への線分が届かないときは、カプセルの計算を飛ばしている
        const float3 center = foot + float3(0.0f, 20.0f, 0.0f);
        const float3 toLight = lightPosition - worldPosition;
        const float along = saturate(dot(center - worldPosition, toLight) / max(dot(toLight, toLight), 1e-4f));
        const float3 closest = worldPosition + toLight * along - center;
        [branch]
        if (dot(closest, closest) <= 24.0f * 24.0f)
        {
            // 体の向き（前と右）と、手足の振り
            const float3 forward = float3(PlayerBodyPose.x, 0.0f, PlayerBodyPose.y);
            const float3 right = float3(forward.z, 0.0f, -forward.x);
            const float swing = PlayerBodyPose.z;
            // 電球や照明は小さな光源なので、体の影は見えない影の人影より縁をくっきりさせている
            const float BodyShadowSoftness = 0.018f;
            #define BODY_POINT(x, y, z) (foot + right * (x) + float3(0.0f, (y), 0.0f) + forward * (z))
            float occlusion = GetCapsuleOcclusion(worldPosition, lightPosition, BODY_POINT(0.0f, 18.5f, 0.0f), BODY_POINT(0.0f, 30.0f, 0.0f), 4.2f, BodyShadowSoftness);
            occlusion = max(occlusion, GetCapsuleOcclusion(worldPosition, lightPosition, BODY_POINT(0.0f, 37.2f, 0.4f), BODY_POINT(0.0f, 37.8f, 0.4f), 2.7f, BodyShadowSoftness));
            occlusion = max(occlusion, GetCapsuleOcclusion(worldPosition, lightPosition, BODY_POINT(1.8f, 17.0f, 0.0f), BODY_POINT(1.9f, 1.2f, swing * 3.5f), 1.8f, BodyShadowSoftness));
            occlusion = max(occlusion, GetCapsuleOcclusion(worldPosition, lightPosition, BODY_POINT(-1.8f, 17.0f, 0.0f), BODY_POINT(-1.9f, 1.2f, -swing * 3.5f), 1.8f, BodyShadowSoftness));
            occlusion = max(occlusion, GetCapsuleOcclusion(worldPosition, lightPosition, BODY_POINT(-4.7f, 30.5f, 0.0f), BODY_POINT(-5.0f, 17.5f, -swing * 3.0f), 1.2f, BodyShadowSoftness));
            occlusion = max(occlusion, GetCapsuleOcclusion(worldPosition, lightPosition, BODY_POINT(4.7f, 30.5f, 0.0f), BODY_POINT(3.6f, 27.5f, 9.5f), 1.2f, BodyShadowSoftness));
            #undef BODY_POINT
            // まわりの照り返しが入るので、影は真っ黒にはしていない
            visibility = 1.0f - occlusion * 0.9f;
        }
    }
    return visibility;
}
#endif

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
