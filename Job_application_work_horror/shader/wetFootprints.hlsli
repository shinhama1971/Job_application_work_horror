// ============================================================================
// 共通の処理: 床に残る濡れた足跡（WetFootprints が持つ場所と濡れ具合）の形を描いている。
// 主な技術: 楕円の距離で作る靴底と裸足の形、足の向きへの回転と左右の反転、乾きかけの縁のむら
// 濡れた床（wetFloorPS）が、WetFloorBuffer（b10）の足跡を読んで使っている。fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef WET_FOOTPRINTS_INCLUDED
#define WET_FOOTPRINTS_INCLUDED

// 楕円の内側で1、外側で0（縁を少しぼかしている）
float FootprintEllipse(float2 position, float2 center, float2 radius)
{
    return 1.0f - smoothstep(0.80f, 1.0f, length((position - center) / radius));
}

// 足跡1つの形（0〜1）。localは足の向きに合わせた座標（x = 右、y = つま先の方向。単位はワールド）。
// 靴は、つま先側の楕円とかかとの楕円に、靴底の溝の横縞。裸足は、くびれた足の裏と5本の指
float FootprintShape(float2 local, bool bare)
{
    // 足の長さ（約6.5）に合わせ、形は長さ4ほどで作ってから広げている
    const float2 p = local / 1.6f;
    if (!bare)
    {
        const float front = FootprintEllipse(p, float2(0.0f, 0.9f), float2(0.75f, 1.25f));
        const float heel = FootprintEllipse(p, float2(0.0f, -1.15f), float2(0.62f, 0.70f));
        const float tread = 0.72f + 0.28f * step(0.45f, frac(p.y * 2.4f));
        return max(front, heel) * tread;
    }
    const float ball = FootprintEllipse(p, float2(0.05f, 0.45f), float2(0.62f, 0.78f));
    const float heel = FootprintEllipse(p, float2(0.0f, -1.10f), float2(0.50f, 0.62f));
    // 土踏まずは内側（x の負の側）がくびれ、外側だけがつながっている
    const float outerArch = FootprintEllipse(p, float2(0.25f, -0.35f), float2(0.30f, 0.75f));
    float sole = max(max(ball, heel), outerArch);
    // 指（親指が内側で大きく、外側ほど小さく、少し下がる）
    [unroll]
    for (int toe = 0; toe < 5; ++toe)
    {
        const float t = float(toe);
        const float2 center = float2(-0.42f + t * 0.22f, 1.45f - t * 0.09f - t * t * 0.015f);
        const float radius = 0.17f - t * 0.018f;
        sole = max(sole, FootprintEllipse(p, center, float2(radius, radius * 1.15f)));
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
        const float4 placement = Footprints[index * 2];
        const float2 offset = worldPosition - placement.xy;
        // 足跡の大きさ（半分で約4）より遠い所は計算しない
        if (dot(offset, offset) > 20.0f)
        {
            continue;
        }
        const float4 state = Footprints[index * 2 + 1];
        // 足の向き（前 = (sin, cos)、右 = (cos, -sin)）の座標へ回し、左足は左右を反転している
        const float2 forward = float2(placement.z, placement.w);
        const float2 right = float2(placement.w, -placement.z);
        const float2 local = float2(dot(offset, right) * state.y, dot(offset, forward));
        const float shape = FootprintShape(local, state.z > 0.5f);
        // 乾きかけは縁から乾くので、薄い所ほどノイズで欠けさせている
        const float dryEdge = smoothstep(0.0f, 0.35f, state.x + (FastValueNoise(worldPosition * 1.7f) - 0.5f) * 0.25f);
        wet = max(wet, shape * state.x * dryEdge);
    }
    return saturate(wet);
}

#endif
