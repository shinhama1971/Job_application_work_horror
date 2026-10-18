// ============================================================================
// 共通の処理: 2面の時計の文字盤（真鍮の縁・黄ばんだ文字盤・目盛り・ローマ数字・針・ひびの入ったガラス）を計算で描いている。
// 主な技術: 極座標の目盛り、線分の距離で描くローマ数字（I・V・X の組み合わせを表で持つ）、角度から向きを決める針、
//           一番近い数字だけを計算して画素ごとの負荷を抑える工夫
// litTexturePS.hlsl を CLOCK_SURFACE を付けて読み込んだとき（clockPS.hlsl）だけ使っている。fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef CLOCK_FACE_INCLUDED
#define CLOCK_FACE_INCLUDED

// 文字盤の色と、凹凸（目盛り・数字の刻み）
struct ClockPaint
{
    float3 albedo;
    float relief;
};

// 点pから線分abまでの距離
float ClockSegmentDistance(float2 p, float2 a, float2 b)
{
    const float2 ab = b - a;
    const float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 0.0001f));
    return length(p - (a + ab * t));
}

// ローマ数字の1文字（1 = I、2 = V、3 = X）までの距離。localは文字の中心からの位置（高さ1.3、幅0.6）
float ClockGlyphDistance(float2 local, uint glyph)
{
    const float halfHeight = 0.65f;
    const float halfWidth = 0.30f;
    if (glyph == 1u)
    {
        return ClockSegmentDistance(local, float2(0.0f, -halfHeight), float2(0.0f, halfHeight));
    }
    if (glyph == 2u)
    {
        return min(
            ClockSegmentDistance(local, float2(-halfWidth, halfHeight), float2(0.0f, -halfHeight)),
            ClockSegmentDistance(local, float2(halfWidth, halfHeight), float2(0.0f, -halfHeight)));
    }
    return min(
        ClockSegmentDistance(local, float2(-halfWidth, halfHeight), float2(halfWidth, -halfHeight)),
        ClockSegmentDistance(local, float2(halfWidth, halfHeight), float2(-halfWidth, -halfHeight)));
}

// 時計の数字（12, 1, 2 … 11）を、1文字2ビット（下の桁から左の文字の順）で表にしている。古い時計に多い「IIII」で4を書いている
static const uint ClockNumerals[12] =
{
    3u | (1u << 2) | (1u << 4),              // XII
    1u,                                      // I
    1u | (1u << 2),                          // II
    1u | (1u << 2) | (1u << 4),              // III
    1u | (1u << 2) | (1u << 4) | (1u << 6),  // IIII
    2u,                                      // V
    2u | (1u << 2),                          // VI
    2u | (1u << 2) | (1u << 4),              // VII
    2u | (1u << 2) | (1u << 4) | (1u << 6),  // VIII
    1u | (3u << 2),                          // IX
    3u,                                      // X
    3u | (1u << 2)                           // XI
};

// 針（中心から angle の向きへ handLength の長さ、根元の太さ width、先が細い。反対側に tail だけ出る）までの近さ（0〜1）
float ClockHand(float2 p, float angle, float handLength, float width, float tail, float footprint)
{
    const float2 direction = float2(sin(angle), cos(angle));
    const float along = dot(p, direction);
    const float across = abs(dot(p, float2(direction.y, -direction.x)));
    const float taper = width * lerp(1.0f, 0.25f, saturate(along / handLength));
    const float inside = step(-tail, along) * step(along, handLength);
    return inside * (1.0f - smoothstep(taper, taper + footprint * 1.5f, across));
}

// 文字盤のUV（u は見る人から見て右から左へ増える。v は上から下）と、針の角度（x = 時針、y = 分針。12時が0、右回りが+）から、
// その点の色と凹凸を求めている
ClockPaint GetClockFace(float2 uv, float4 hands)
{
    // 文字盤の単位（幅・高さ20、中心が0、上が+、右が+）。見る人から見て右が+になるよう、uを反転している
    const float2 p = float2((0.5f - uv.x) * 20.0f, (0.5f - uv.y) * 20.0f);
    const float footprint = max(length(fwidth(p)), 0.0001f);
    const float radius = length(p);

    // 外側：黒ずんだ木の箱（木目のノイズ）
    const float grain = FastValueNoise(float2(p.x * 0.35f, p.y * 4.0f) + 3.0f);
    float3 color = float3(0.075f, 0.050f, 0.032f) * (0.80f + grain * 0.35f);
    float relief = grain * 0.2f;

    // 真鍮の縁（くすんで、ところどころ黒ずんでいる）
    const float bezel = smoothstep(9.3f + footprint, 9.2f, radius);
    const float tarnish = FastValueNoise(p * 0.8f + 17.0f);
    color = lerp(color, float3(0.32f, 0.23f, 0.10f) * lerp(0.55f, 1.0f, tarnish), bezel);
    relief += bezel * 0.4f;

    // 文字盤：黄ばんだ象牙色の紙。縁ほど暗く、茶色い染みが散っている
    const float dial = smoothstep(8.65f + footprint, 8.55f, radius);
    const float stain = smoothstep(0.62f, 0.85f, FastValueNoise(p * 0.45f + 5.0f));
    const float foxing = smoothstep(0.86f, 0.92f, FastValueNoise(p * 2.3f - 9.0f));
    float3 paper = float3(0.46f, 0.42f, 0.33f) * lerp(1.0f, 0.70f, smoothstep(5.0f, 8.6f, radius));
    paper = lerp(paper, float3(0.30f, 0.22f, 0.13f), stain * 0.45f + foxing * 0.55f);
    color = lerp(color, paper, dial);
    relief -= dial * 0.4f;

    const float3 ink = float3(0.045f, 0.038f, 0.035f);
    float inkAmount = 0.0f;

    // 目盛り：分の細い目盛り（60本）と、時の太い目盛り（12本）。一番近い目盛りだけを計算している
    const float angle = atan2(p.x, p.y);
    const float minuteIndex = round(angle / (3.14159265f / 30.0f));
    const float minuteAngle = minuteIndex * (3.14159265f / 30.0f);
    const float2 minuteDirection = float2(sin(minuteAngle), cos(minuteAngle));
    const float isHourTick = step(abs(fmod(abs(minuteIndex), 5.0f)), 0.5f);
    const float tickInner = lerp(7.75f, 7.10f, isHourTick);
    const float tickWidth = lerp(0.05f, 0.14f, isHourTick);
    const float tickDistance = ClockSegmentDistance(p, minuteDirection * tickInner, minuteDirection * 8.3f);
    inkAmount = max(inkAmount, 1.0f - smoothstep(tickWidth, tickWidth + footprint * 1.5f, tickDistance));

    // ローマ数字：一番近い時の位置の数字だけを計算している（文字は傾けず、立てたまま並べている）
    const float hourIndex = round(angle / (3.14159265f / 6.0f));
    const uint hour = ((uint)((int)hourIndex + 12)) % 12u;
    const float hourAngle = hourIndex * (3.14159265f / 6.0f);
    const float2 numeralCenter = float2(sin(hourAngle), cos(hourAngle)) * 5.9f;
    const uint numeral = ClockNumerals[hour];
    uint glyphCount = 0u;
    [unroll]
    for (uint countIndex = 0u; countIndex < 4u; ++countIndex)
    {
        glyphCount += ((numeral >> (countIndex * 2u)) & 3u) != 0u ? 1u : 0u;
    }
    const float glyphSpacing = 0.68f;
    float glyphDistance = 10.0f;
    [unroll]
    for (uint glyphIndex = 0u; glyphIndex < 4u; ++glyphIndex)
    {
        const uint glyph = (numeral >> (glyphIndex * 2u)) & 3u;
        if (glyph != 0u)
        {
            const float offset = (float(glyphIndex) - (float(glyphCount) - 1.0f) * 0.5f) * glyphSpacing;
            glyphDistance = min(glyphDistance, ClockGlyphDistance(p - numeralCenter - float2(offset, 0.0f), glyph));
        }
    }
    // 文字の上下の横線（セリフ）を、文字の幅いっぱいに引いている
    const float serifHalf = float(glyphCount) * glyphSpacing * 0.5f;
    const float2 numeralLocal = p - numeralCenter;
    const float serif = min(
        ClockSegmentDistance(numeralLocal, float2(-serifHalf, 0.66f), float2(serifHalf, 0.66f)),
        ClockSegmentDistance(numeralLocal, float2(-serifHalf, -0.66f), float2(serifHalf, -0.66f)));
    inkAmount = max(inkAmount, 1.0f - smoothstep(0.10f, 0.10f + footprint * 1.5f, min(glyphDistance, serif + 0.03f)));

    // 色あせたインク：染みの所は薄く
    color = lerp(color, ink, inkAmount * dial * lerp(0.92f, 0.6f, stain));
    relief -= inkAmount * dial * 0.3f;

    // 針（青く焼けた鋼）と、真鍮の中心の軸
    const float hourHand = ClockHand(p, hands.x, 4.6f, 0.34f, 0.9f, footprint);
    const float minuteHand = ClockHand(p, hands.y, 7.3f, 0.24f, 1.3f, footprint);
    const float3 steel = float3(0.035f, 0.035f, 0.055f);
    color = lerp(color, steel, saturate(hourHand + minuteHand));
    relief += saturate(hourHand + minuteHand) * 0.6f;
    const float cap = smoothstep(0.50f + footprint, 0.45f, radius);
    color = lerp(color, float3(0.30f, 0.22f, 0.10f), cap);

    // ガラス：くもりと、斜めの淡い映り込み、それに右側から走る1本のひび
    const float sheen = smoothstep(1.2f, 0.0f, abs(p.x * 0.7f + p.y * 0.7f - 3.0f)) * 0.06f;
    const float haze = FastValueNoise(p * 0.6f + 23.0f) * 0.05f;
    const float crackPath = abs(p.y - (p.x * 0.55f + 2.0f + sin(p.x * 2.3f) * 0.35f));
    const float crack = (1.0f - smoothstep(0.02f, 0.06f + footprint, crackPath)) * step(2.0f, p.x) * dial;
    color += (sheen + haze) * dial;
    color = lerp(color, float3(0.55f, 0.55f, 0.50f), crack * 0.6f);

    ClockPaint paint;
    paint.albedo = color;
    paint.relief = relief;
    return paint;
}

#endif
