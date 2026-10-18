// ============================================================================
// 共通の処理: 1面の建物の壁に残った（2面では異変と紛れないよう描かない）、ホラーらしい跡（血の手形と引きずった跡、引っかき傷、血の垂れ跡）。
// 主な技術: 距離関数（線分・楕円）で形を作る、壁に沿ったマス目ごとの乱数で置く場所と種類を決める、遠くでは細部を省く
// ・画像の素材は使わず、壁に沿った横方向の座標と床からの高さで計算している（壁の古さと同じ座標）。
// ・壁に沿って幅70ごとにマスを区切り、4割弱のマスにだけ1つ置いている（全部の壁が汚れて、怖さが薄まらないように）。
//   マスの乱数には壁の面の位置も混ぜ、平行な別の壁に同じ跡が並ばないようにしている。
// ・暗い施設では懐中電灯で照らした所だけが見えるため、照らすと赤黒い跡が浮かぶ。血は濡れているので、浅い角度で光を返す。
// fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef HORROR_MARKS_INCLUDED
#define HORROR_MARKS_INCLUDED

struct HorrorMarks
{
    float blood;    // 血の濃さ（0〜1）。色を赤黒くし、濡れた光沢を足すのに使っている
    float gouge;    // 引っかき傷の溝（0〜1）。へこみと、削れて明るくなった色に使っている
};

// 点pから線分abまでの距離
float HorrorSegmentDistance(float2 p, float2 a, float2 b)
{
    const float2 pa = p - a;
    const float2 ba = b - a;
    const float along = saturate(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * along);
}

// 手形（手のひらの中心が原点、指先が+y。大きさは手のひらの幅が約4）。mirrorが-1なら左手にしている
float HorrorHandprint(float2 p, float mirror, float footprint)
{
    p.x *= mirror;
    const float edge = 0.10f + footprint;
    // 手のひら（少し縦長の楕円）
    const float palmDistance = (length(p / float2(1.9f, 2.1f)) - 1.0f) * 1.9f;
    float mask = 1.0f - smoothstep(-edge, edge, palmDistance);
    // 4本の指（外側に少し開き、中指と薬指を長くしている）
    [unroll]
    for (int finger = 0; finger < 4; ++finger)
    {
        const float offset = float(finger) - 1.5f;
        const float2 base = float2(offset * 0.95f, 1.6f);
        const float fingerLength = 2.0f + (abs(offset) < 1.0f ? 0.5f : 0.0f);
        const float2 tip = base + float2(offset * 0.32f, fingerLength);
        const float fingerDistance = HorrorSegmentDistance(p, base, tip) - 0.36f;
        mask = max(mask, 1.0f - smoothstep(-edge, edge, fingerDistance));
    }
    // 親指（横へ斜めに出ている）
    const float thumbDistance = HorrorSegmentDistance(p, float2(1.5f, -0.2f), float2(2.9f, 1.2f)) - 0.48f;
    mask = max(mask, 1.0f - smoothstep(-edge, edge, thumbDistance));
    return mask;
}

// 壁に沿った横の座標 alongWall、床からの高さ heightFromFloor、壁の面の位置 planeCoordinate から、その点の跡を返している
HorrorMarks ComputeHorrorMarks(float alongWall, float heightFromFloor, float planeCoordinate, float footprint)
{
    HorrorMarks marks;
    marks.blood = 0.0f;
    marks.gouge = 0.0f;
    // 遠くて細部が1画素より小さくなる所では描かない（ちらつきを防ぎ、計算も省く）
    if (footprint > 0.6f)
    {
        return marks;
    }

    // 壁に沿って幅70のマス。マスの乱数には、壁の面の位置（4ごとに丸めた値）も混ぜている。
    // 壁の面の位置は整数（壁の中心±厚さの半分）なので、0.125ずらして丸め、境目で画素ごとに値が揺れないようにしている
    const float cellWidth = 70.0f;
    const float cellIndex = floor(alongWall / cellWidth);
    const float2 cellKey = float2(cellIndex, floor(planeCoordinate * 0.25f + 0.125f));
    const float kind = FastHash21(cellKey + 101.3f);
    const float seedA = FastHash21(cellKey + 13.7f);
    const float seedB = FastHash21(cellKey + 57.1f);
    const float seedC = FastHash21(cellKey + 88.9f);
    // マスの中の位置（壁の端に寄りすぎないよう、15〜55の間）
    const float2 local = float2(alongWall - cellIndex * cellWidth - (15.0f + seedA * 40.0f), heightFromFloor);
    const float fade = saturate(1.0f - footprint / 0.6f);
    // 血の付き方のむら（かすれ）
    const float smudge = 0.55f + 0.45f * FastValueNoise(local * 0.8f + cellKey * 7.0f);

    [branch]
    if (kind < 0.12f)
    {
        // --- 血の手形。1つか2つ、少し傾けて押し付け、片方は下へ引きずった跡を付けている
        // 手形の大きさ（手のひらの幅が約5。目の高さが床から40なので、人の手と同じくらい）
        const float HandScale = 1.25f;
        const float handHeight = 20.0f + seedB * 12.0f;
        const float angle = (seedC - 0.5f) * 0.6f;
        const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
        const float2 handPoint = mul(rotation, local - float2(0.0f, handHeight));
        float hand = HorrorHandprint(handPoint / HandScale, seedB < 0.5f ? 1.0f : -1.0f, footprint / HandScale);
        if (seedC > 0.45f)
        {
            const float2 secondPoint = mul(rotation, local - float2(6.5f, handHeight - 3.0f));
            hand = max(hand, HorrorHandprint(secondPoint / HandScale, seedB < 0.5f ? -1.0f : 1.0f, footprint / HandScale) * 0.8f);
        }
        // 手のひらの下へ、指の筋を残して引きずった跡（下へ行くほど薄くかすれる）
        const float dragLength = 6.0f + seedA * 10.0f;
        const float dragAcross = 1.0f - smoothstep(1.5f * HandScale, 2.0f * HandScale + footprint, abs(handPoint.x));
        const float dragAlong = saturate(-handPoint.y / dragLength);
        const float dragStreaks = 0.5f + 0.5f * sin(handPoint.x * 7.0f + FastValueNoise(handPoint * 0.8f) * 2.0f);
        const float drag = dragAcross * step(0.0f, -handPoint.y) * (1.0f - dragAlong) * dragStreaks * 0.75f;
        marks.blood = max(hand, drag) * smudge * fade;
    }
    else if (kind < 0.24f)
    {
        // --- 引っかき傷。4本の細い溝が、斜めに少し曲がりながら並んでいる
        const float scratchCenter = 14.0f + seedB * 16.0f;
        const float angle = (seedC - 0.5f) * 1.2f;
        const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
        const float2 p = mul(rotation, local - float2(0.0f, scratchCenter));
        const float halfLength = 5.0f + seedA * 4.0f;
        const float along = saturate(abs(p.y) / halfLength);
        // 端ほど細く浅くなる
        const float taper = 1.0f - along * along;
        float gouge = 0.0f;
        [unroll]
        for (int scratch = 0; scratch < 4; ++scratch)
        {
            const float offset = (float(scratch) - 1.5f) * 0.75f;
            // 爪が滑ったように、下へ行くほど外へ少し曲がる
            const float curve = p.y * p.y * 0.012f * (float(scratch) - 1.5f);
            const float width = 0.10f * taper + footprint;
            const float lineMask = 1.0f - smoothstep(width * 0.4f, width, abs(p.x - offset - curve));
            gouge = max(gouge, lineMask * taper);
        }
        marks.gouge = gouge * step(abs(p.y), halfLength) * fade;
        // 半分ほどの傷には、溝に血が残っている
        marks.blood = marks.gouge * step(0.5f, seedB) * 0.7f;
    }
    else if (kind < 0.36f)
    {
        // --- 血の垂れ跡。上の方の染みから、長さの違う筋が下へ垂れ、先に雫がたまっている
        const float top = 30.0f + seedB * 14.0f;
        const float2 blobPoint = (local - float2(0.0f, top)) / float2(2.8f, 1.3f);
        float blood = 1.0f - smoothstep(0.85f, 1.05f, length(blobPoint) +
            (FastValueNoise(local * 1.3f + cellKey) - 0.5f) * 0.5f);
        [unroll]
        for (int drip = 0; drip < 5; ++drip)
        {
            const float dripSeed = FastHash21(cellKey + float(drip) * 3.3f);
            const float x = (float(drip) - 2.0f) * 1.0f + (dripSeed - 0.5f) * 0.5f;
            const float dripLength = 4.0f + dripSeed * 16.0f;
            const float width = 0.16f + 0.14f * FastHash21(cellKey + float(drip) * 7.9f);
            const float below = top - heightFromFloor;
            const float stem = (1.0f - smoothstep(width * 0.5f, width + footprint, abs(local.x - x))) *
                step(0.0f, below) * step(below, dripLength);
            const float bulb = 1.0f - smoothstep(width * 1.2f, width * 1.8f + footprint,
                length(float2(local.x - x, heightFromFloor - (top - dripLength))));
            blood = max(blood, max(stem, bulb));
        }
        marks.blood = blood * (0.75f + 0.25f * smudge) * fade;
    }
    return marks;
}

#endif
