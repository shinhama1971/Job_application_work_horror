// ============================================================================
// 共通の処理: 2面の肖像画（古い油絵の人物）を、テクスチャを使わずに計算で描いている。
// 主な技術: 楕円と曲線の距離で描く形（顔・髪・服・閉じた目・口）、筆の跡のノイズ、
//           ずらした格子の点による絵の具のひび割れ（クラクリュール）、ニスの黄ばみ
// litTexturePS.hlsl を PORTRAIT_SURFACE を付けて読み込んだとき（portraitPS.hlsl）だけ使っている。
// fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef PORTRAIT_PAINTING_INCLUDED
#define PORTRAIT_PAINTING_INCLUDED

// 絵の色と、筆の跡・ひびの凹凸（法線を傾けるのに使う高さ）
struct PortraitPaint
{
    float3 albedo;
    float relief;
};

// 楕円の内側で1、外側で0を返している（縁をwidthの割合でぼかしている）
float PortraitEllipse(float2 position, float2 center, float2 radius, float width)
{
    const float distance = length((position - center) / radius) - 1.0f;
    return 1.0f - smoothstep(-width, width, distance);
}

// 曲線 y = curve からの距離が太さの半分より近い所で1を返している（xの範囲を halfLength で区切る）
float PortraitStroke(float2 position, float curve, float halfThickness, float halfLength, float footprint)
{
    const float across = 1.0f - smoothstep(halfThickness, halfThickness + footprint * 1.5f, abs(position.y - curve));
    const float along = 1.0f - smoothstep(halfLength * 0.8f, halfLength, abs(position.x));
    return across * along;
}

// 絵の具のひび割れ：ずらした格子の点のうち、一番近い点と2番目に近い点までの距離がほぼ同じ所（境目）を線にしている
float PortraitCracks(float2 position)
{
    const float2 cell = floor(position);
    float nearest = 8.0f;
    float second = 8.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 neighbor = cell + float2(x, y);
            const float2 jitter = float2(FastHash21(neighbor), FastHash21(neighbor + 17.3f));
            const float distance = length(neighbor + jitter - position);
            if (distance < nearest)
            {
                second = nearest;
                nearest = distance;
            }
            else if (distance < second)
            {
                second = distance;
            }
        }
    }
    return 1.0f - smoothstep(0.0f, 0.07f, second - nearest);
}

// キャンバスのUV（u = 左から右、v = 上から下）から、その点の絵の色と凹凸を求めている
PortraitPaint GetPortraitPainting(float2 uv)
{
    // キャンバスの単位（幅16・高さ22、中心が0、上が+）。閉じた目は (±3.5, 2.5) にあり、
    // 肖像画の異変で現れる赤い目（Stage2Layout の Stage2PortraitEyeLeft/Right）と同じ位置にしている
    const float2 p = float2((uv.x - 0.5f) * 16.0f, (0.5f - uv.y) * 22.0f);
    // 1画素がキャンバスで何単位か（細い線のぼかしと、遠くでひびを薄めるのに使っている）
    const float footprint = max(length(fwidth(p)), 0.0001f);

    // 筆の跡：斜めに引き伸ばしたノイズで、筆でなでたようなむらにしている
    const float stroke = FastValueNoise(float2(p.x * 0.9f + p.y * 0.35f, p.y * 3.2f));
    const float strokeFine = FastValueNoise(p * float2(2.5f, 7.0f) + 11.0f);

    // 背景：暗い焦げ茶。頭の後ろだけ、ぼんやり明るい（古い肖像画の光の当て方）
    const float halo = saturate(1.0f - length((p - float2(-0.6f, 2.5f)) / float2(9.0f, 10.5f)));
    float3 color = lerp(float3(0.045f, 0.034f, 0.025f), float3(0.20f, 0.155f, 0.10f), halo * halo);

    // 黒い服と、白っぽい襟
    const float body = PortraitEllipse(p, float2(0.0f, -14.8f), float2(9.6f, 7.8f), 0.04f);
    color = lerp(color, float3(0.055f, 0.048f, 0.050f) * lerp(0.7f, 1.2f, saturate(0.5f - p.x * 0.06f)), body);
    const float collar = PortraitEllipse(p, float2(0.0f, -7.4f), float2(2.8f, 1.3f), 0.20f) * body;
    color = lerp(color, float3(0.28f, 0.26f, 0.22f), collar * 0.75f);

    // 首と顔。左上から光が当たり、右側と下側が影になる
    const float faceLight = saturate(0.62f - p.x * 0.075f + p.y * 0.03f);
    const float3 skin = float3(0.36f, 0.29f, 0.22f) * lerp(0.42f, 1.05f, faceLight);
    const float neck =
        (1.0f - smoothstep(1.9f, 2.2f, abs(p.x))) *
        smoothstep(-7.9f, -7.3f, p.y) *
        (1.0f - smoothstep(-4.2f, -3.6f, p.y));
    color = lerp(color, skin * 0.70f, neck);
    const float face = PortraitEllipse(p, float2(0.0f, 1.6f), float2(5.4f, 6.9f), 0.04f);
    color = lerp(color, skin, face);

    // 髪：頭の上と横を覆う暗い色（額の生え際は、ノイズで少し乱している）
    const float hairline = 4.6f + abs(p.x) * -0.35f + (FastValueNoise(float2(p.x * 1.4f, 3.0f)) - 0.5f) * 0.6f;
    const float hair =
        PortraitEllipse(p, float2(0.0f, 2.6f), float2(6.1f, 7.5f), 0.05f) *
        saturate(smoothstep(hairline - 0.3f, hairline + 0.3f, p.y) + smoothstep(4.8f, 5.6f, abs(p.x)));
    color = lerp(color, float3(0.034f, 0.027f, 0.021f), hair);

    // 閉じた目（くぼみの影と、下向きの弧のまぶたの線）と眉。目が開く異変では、ここに赤い目が浮かぶ
    [unroll]
    for (int side = -1; side <= 1; side += 2)
    {
        const float2 eye = p - float2(3.5f * side, 2.5f);
        const float socket = PortraitEllipse(eye, float2(0.0f, 0.0f), float2(1.8f, 1.1f), 0.35f);
        color *= 1.0f - socket * 0.42f * face;
        const float lid = PortraitStroke(eye, 0.22f * eye.x * eye.x - 0.15f, 0.07f, 1.35f, footprint);
        const float brow = PortraitStroke(eye, 1.55f - 0.10f * eye.x * eye.x + eye.x * side * 0.10f, 0.13f, 1.7f, footprint);
        color = lerp(color, float3(0.055f, 0.040f, 0.030f), saturate(lid + brow * 0.85f) * face);
    }

    // 鼻の右側の細い影と、端の下がった真一文字の口（無表情で不気味にしている）
    const float noseShadow =
        (1.0f - smoothstep(0.15f, 0.45f, abs(p.x - 0.55f))) *
        smoothstep(-1.4f, -0.8f, p.y) * (1.0f - smoothstep(1.0f, 1.8f, p.y));
    color *= 1.0f - noseShadow * 0.30f * face;
    const float mouth = PortraitStroke(p - float2(0.0f, -2.7f), -0.10f * p.x * p.x, 0.07f, 1.5f, footprint);
    color = lerp(color, float3(0.07f, 0.035f, 0.030f), mouth * face);

    // 縁ほど暗く（額の内側の影）、全体にニスが黄ばんでいる
    const float edge = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    color *= lerp(0.55f, 1.0f, smoothstep(0.0f, 0.12f, edge));
    color *= float3(1.0f, 0.93f, 0.78f);

    // 筆の跡のむら
    color *= 0.86f + stroke * 0.20f + strokeFine * 0.06f;

    // 絵の具のひび割れ（遠くで1画素より細くなったら薄めている）と、ところどころ剥がれて見えるキャンバスの地
    const float cracks = PortraitCracks(p * 2.6f) * saturate(1.0f - footprint * 8.0f);
    color *= 1.0f - cracks * 0.32f;
    const float flake = smoothstep(0.84f, 0.90f, FastValueNoise(p * 1.3f + 41.0f));
    color = lerp(color, float3(0.11f, 0.092f, 0.065f), flake * 0.40f);

    PortraitPaint paint;
    paint.albedo = color;
    paint.relief = stroke * 0.25f - cracks * 0.35f - flake * 0.15f;
    return paint;
}

#endif
