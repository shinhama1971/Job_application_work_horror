// ============================================================================
// 共通の処理: 2面の偽の扉の板（木の扉）を計算で描いている。本物の出口の扉と見分けがつかないほど「普通の扉」に見せている。
// 主な技術: 角を丸めた四角の距離で描く6枚の羽目板と、その段差の凹凸、縦に流れる木目のノイズ、
//           ノイズのしきい値で描く剥げた塗装（縁を暗く）、7本の線で描く部屋番号の数字、鍵穴の金具
// litTexturePS.hlsl を DOOR_SURFACE を付けて読み込んだとき（doorPS.hlsl）だけ使っている。fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef DOOR_PAINTING_INCLUDED
#define DOOR_PAINTING_INCLUDED

// 扉の色と、凹凸（羽目板の段差・剥げた塗装の縁）
struct DoorPaint
{
    float3 albedo;
    float relief;
};

// 角を丸めた四角（中心center、半分の大きさhalfSize、角の丸みcorner）までの距離（内側が負）
float DoorRoundedBox(float2 p, float2 center, float2 halfSize, float corner)
{
    const float2 q = abs(p - center) - halfSize + corner;
    return length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - corner;
}

// 点pから線分abまでの距離
float DoorSegment(float2 p, float2 a, float2 b)
{
    const float2 ab = b - a;
    const float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 0.0001f));
    return length(p - (a + ab * t));
}

// 7本の線（上・右上・右下・下・左下・左上・中）で描く数字1文字までの距離。localは文字の中心から（高さ1.6、幅0.8）
float DoorDigit(float2 local, uint digit)
{
    // 数字ごとに、使う線をビットで持っている（下の桁から 上・右上・右下・下・左下・左上・中 の順）
    static const uint segments[10] = { 0x3Fu, 0x06u, 0x5Bu, 0x4Fu, 0x66u, 0x6Du, 0x7Du, 0x07u, 0x7Fu, 0x6Fu };
    const uint mask = segments[digit % 10u];
    const float w = 0.4f;
    const float h = 0.8f;
    const float2 ends[7][2] =
    {
        { float2(-w, h), float2(w, h) },
        { float2(w, h), float2(w, 0.0f) },
        { float2(w, 0.0f), float2(w, -h) },
        { float2(-w, -h), float2(w, -h) },
        { float2(-w, 0.0f), float2(-w, -h) },
        { float2(-w, h), float2(-w, 0.0f) },
        { float2(-w, 0.0f), float2(w, 0.0f) }
    };
    float distance = 10.0f;
    [unroll]
    for (uint segment = 0u; segment < 7u; ++segment)
    {
        if ((mask >> segment) & 1u)
        {
            distance = min(distance, DoorSegment(local, ends[segment][0], ends[segment][1]));
        }
    }
    return distance;
}

// 扉のUV（u は見る人から見て右から左へ増える。v は上から下）と、取っ手の側（params.x：-1 = 見て左、+1 = 見て右）から、
// その点の色と凹凸を求めている
DoorPaint GetDoorPainting(float2 uv, float4 params)
{
    // 扉の単位（幅22・高さ40、中心が0、上が+、見る人から見て右が+）
    const float2 p = float2((0.5f - uv.x) * 22.0f, (0.5f - uv.y) * 40.0f);
    const float footprint = max(length(fwidth(p)), 0.0001f);
    const float handleSide = params.x < 0.0f ? -1.0f : 1.0f;

    // 木目：縦に流れる筋と、ところどころの節
    const float grain =
        FastValueNoise(float2(p.x * 2.6f, p.y * 0.18f)) * 0.6f +
        FastValueNoise(float2(p.x * 9.0f, p.y * 0.5f) + 7.0f) * 0.4f;
    const float knot = smoothstep(0.82f, 0.95f, FastValueNoise(p * 0.35f + 31.0f));

    // 塗装：くすんだ茶色のペンキ。木目がうっすら透け、下の方ほど汚れて暗い
    float3 paint = float3(0.30f, 0.16f, 0.10f) * (0.85f + grain * 0.25f);
    paint *= lerp(0.70f, 1.0f, smoothstep(-20.0f, -8.0f, p.y));

    // 6枚の羽目板（2列3段）：枠より一段へこみ、縁に斜めの面がある
    float relief = 0.0f;
    float panelShade = 0.0f;
    [unroll]
    for (int column = 0; column < 2; ++column)
    {
        [unroll]
        for (int row = 0; row < 3; ++row)
        {
            const float2 center = float2(column == 0 ? -5.0f : 5.0f, row == 0 ? 12.5f : (row == 1 ? 1.0f : -12.0f));
            const float2 halfSize = float2(3.6f, row == 1 ? 3.2f : 6.0f);
            const float distance = DoorRoundedBox(p, center, halfSize, 0.4f);
            // 縁の斜めの面（幅0.7）と、へこんだ平らな面
            const float bevel = smoothstep(0.0f, -0.7f, distance);
            const float inside = smoothstep(-0.7f, -0.8f - footprint, distance);
            relief -= bevel * 0.5f;
            // 斜めの面は、上と左が明るく、下と右が暗い（上から光が当たる）
            const float2 toCenter = normalize(center - p + 0.0001f);
            panelShade += (bevel - inside) * dot(toCenter, float2(0.35f, -0.55f));
        }
    }
    paint *= 1.0f + panelShade * 0.35f;

    // 剥げた塗装：ノイズのしきい値より上の所が剥げ、下の白っぽい木地が見えている。縁は塗装が浮いて暗い
    const float peelNoise =
        FastValueNoise(p * float2(0.45f, 0.30f) + 11.0f) * 0.7f +
        FastValueNoise(p * 1.8f - 4.0f) * 0.3f;
    const float peel = smoothstep(0.66f, 0.68f + footprint * 0.05f, peelNoise);
    const float peelEdge = smoothstep(0.60f, 0.66f, peelNoise) * (1.0f - peel);
    const float3 bareWood = float3(0.42f, 0.33f, 0.22f) * (0.8f + grain * 0.35f);
    float3 color = lerp(paint, bareWood, peel);
    color *= 1.0f - peelEdge * 0.45f;
    color = lerp(color, color * 0.55f, knot * 0.6f);
    relief -= peel * 0.15f;

    // 部屋番号の札（くすんだ真鍮）と、黒い数字「204」
    const float plate = smoothstep(0.05f + footprint, 0.0f, DoorRoundedBox(p, float2(0.0f, 7.0f), float2(2.3f, 1.25f), 0.3f));
    color = lerp(color, float3(0.30f, 0.22f, 0.10f) * (0.75f + grain * 0.2f), plate);
    relief += plate * 0.3f;
    float digits = 10.0f;
    digits = min(digits, DoorDigit((p - float2(-1.25f, 7.0f)) * float2(1.0f, 1.0f) / 0.62f, 2u));
    digits = min(digits, DoorDigit((p - float2(0.0f, 7.0f)) / 0.62f, 0u));
    digits = min(digits, DoorDigit((p - float2(1.25f, 7.0f)) / 0.62f, 4u));
    const float digitInk = (1.0f - smoothstep(0.10f, 0.10f + footprint * 2.0f, digits)) * plate;
    color = lerp(color, float3(0.03f, 0.025f, 0.02f), digitInk);

    // 取っ手の下の鍵穴の金具と、取っ手の周りの手垢・引っかき傷
    const float2 keyholeCenter = float2(handleSide * 8.0f, -3.0f);
    const float escutcheon = smoothstep(0.05f + footprint, 0.0f, DoorRoundedBox(p, keyholeCenter, float2(0.55f, 1.0f), 0.5f));
    color = lerp(color, float3(0.28f, 0.20f, 0.09f), escutcheon);
    const float keyhole = smoothstep(0.03f + footprint, 0.0f,
        min(length(p - keyholeCenter - float2(0.0f, 0.25f)) - 0.18f,
            DoorRoundedBox(p, keyholeCenter - float2(0.0f, 0.2f), float2(0.07f, 0.3f), 0.05f)));
    color = lerp(color, float3(0.01f, 0.01f, 0.01f), keyhole);
    const float handWear = smoothstep(4.0f, 0.0f, length((p - float2(handleSide * 8.0f, 0.0f)) / float2(1.0f, 1.6f)));
    color *= 1.0f - handWear * 0.25f;
    relief += escutcheon * 0.3f;

    DoorPaint door;
    door.albedo = color;
    door.relief = relief;
    return door;
}

#endif
