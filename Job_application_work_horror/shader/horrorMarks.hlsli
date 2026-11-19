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

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

struct HorrorMarks
{
    float blood;    // 血の濃さ（0〜1）。色を赤黒くし、濡れた光沢を足すのに使っている
    float gouge;    // 引っかき傷の溝（0〜1）。へこみと、削れて明るくなった色に使っている
};

// 点pから線分abまでの距離
// 遠くにある細い手形や傷を薄め、画素より細い線のちらつきを抑える。
static const float HORROR_MARKS_FINE_DETAIL_FOOTPRINT = 0.6f;

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
// 血の付き方のむらと、手形・引っかき傷・垂れ跡の出る乱数の範囲。
#define HORROR_MARK_BLOOD_MIN_COVERAGE 0.55f
#define HORROR_MARK_BLOOD_COVERAGE_RANGE 0.45f
#define HORROR_MARK_HANDPRINT_CUTOFF 0.12f
#define HORROR_MARK_SCRATCH_CUTOFF 0.24f
#define HORROR_MARK_DRIP_CUTOFF 0.36f

// 手を押し付ける高さと、床側へ引きずる長さをばらつかせる。
#define HORROR_MARK_HAND_HEIGHT_MIN 20.0f
#define HORROR_MARK_HAND_HEIGHT_RANGE 12.0f
#define HORROR_MARK_HAND_ROTATION_RANGE 0.6f
#define HORROR_MARK_SECOND_HAND_BLEND 0.8f
#define HORROR_MARK_HAND_DRAG_MIN_LENGTH 6.0f
#define HORROR_MARK_HAND_DRAG_LENGTH_RANGE 10.0f
#define HORROR_MARK_HAND_DRAG_STRIPE_RATE 7.0f
#define HORROR_MARK_HAND_DRAG_ALPHA 0.75f

// 爪の溝は端ほど細くなり、血が入る溝も一部に限る。
#define HORROR_MARK_SCRATCH_MIN_HEIGHT 14.0f
#define HORROR_MARK_SCRATCH_HEIGHT_RANGE 16.0f
#define HORROR_MARK_SCRATCH_ROTATION_RANGE 1.2f
#define HORROR_MARK_SCRATCH_MIN_HALF_LENGTH 5.0f
#define HORROR_MARK_SCRATCH_HALF_LENGTH_RANGE 4.0f
#define HORROR_MARK_SCRATCH_COUNT 4
#define HORROR_MARK_SCRATCH_SPACING 0.75f
#define HORROR_MARK_SCRATCH_CURVATURE 0.012f
#define HORROR_MARK_SCRATCH_WIDTH 0.10f
#define HORROR_MARK_SCRATCH_BLOOD_GAIN 0.7f

// 垂れる筋ごとに長さと幅を変え、下端に光の当たる小さな雫を作る。
#define HORROR_MARK_DRIP_MIN_HEIGHT 30.0f
#define HORROR_MARK_DRIP_HEIGHT_RANGE 14.0f
#define HORROR_MARK_DRIP_COUNT 5
#define HORROR_MARK_DRIP_MIN_LENGTH 4.0f
#define HORROR_MARK_DRIP_LENGTH_RANGE 16.0f
#define HORROR_MARK_DRIP_MIN_WIDTH 0.16f
#define HORROR_MARK_DRIP_WIDTH_RANGE 0.14f
#define HORROR_MARK_DRIP_WIDTH_SEED 7.9f
#define HORROR_MARK_DRIP_BULB_CORE 1.2f
#define HORROR_MARK_DRIP_BULB_EDGE 1.8f

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
    [branch]
    if (kind >= 0.36f)
    {
        return marks;
    }
    const float seedA = FastHash21(cellKey + 13.7f);
    const float seedB = FastHash21(cellKey + 57.1f);
    const float seedC = FastHash21(cellKey + 88.9f);
    // マスの中の位置（壁の端に寄りすぎないよう、15〜55の間）
    const float2 local = float2(alongWall - cellIndex * cellWidth - (15.0f + seedA * 40.0f), heightFromFloor);
    // 2つ目の手形や輪郭のぼけを含めても届かない範囲では、跡の形を計算しない。
    [branch]
    if (abs(local.x) > 14.0f || local.y < 0.0f || local.y > 48.0f)
    {
        return marks;
    }
    const float fade = saturate(1.0f - footprint / HORROR_MARKS_FINE_DETAIL_FOOTPRINT);
    // 血の付き方のむら（かすれ）
    const float smudge = HORROR_MARK_BLOOD_MIN_COVERAGE + HORROR_MARK_BLOOD_COVERAGE_RANGE * FastValueNoise(local * 0.8f + cellKey * 7.0f);

    [branch]
    if (kind < HORROR_MARK_HANDPRINT_CUTOFF)
    {
        // --- 血の手形。1つか2つ、少し傾けて押し付け、片方は下へ引きずった跡を付けている
        // 手形の大きさ（手のひらの幅が約5。目の高さが床から40なので、人の手と同じくらい）
        const float HandScale = 1.25f;
        const float handHeight = HORROR_MARK_HAND_HEIGHT_MIN + seedB * HORROR_MARK_HAND_HEIGHT_RANGE;
        const float angle = (seedC - 0.5f) * HORROR_MARK_HAND_ROTATION_RANGE;
        const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
        const float2 handPoint = mul(rotation, local - float2(0.0f, handHeight));
        float hand = HorrorHandprint(handPoint / HandScale, seedB < 0.5f ? 1.0f : -1.0f, footprint / HandScale);
        if (seedC > 0.45f)
        {
            const float2 secondPoint = mul(rotation, local - float2(6.5f, handHeight - 3.0f));
            hand = max(hand, HorrorHandprint(secondPoint / HandScale, seedB < 0.5f ? -1.0f : 1.0f, footprint / HandScale) * HORROR_MARK_SECOND_HAND_BLEND);
        }
        // 手のひらの下へ、指の筋を残して引きずった跡（下へ行くほど薄くかすれる）
        const float dragLength = HORROR_MARK_HAND_DRAG_MIN_LENGTH + seedA * HORROR_MARK_HAND_DRAG_LENGTH_RANGE;
        const float dragAcross = 1.0f - smoothstep(1.5f * HandScale, 2.0f * HandScale + footprint, abs(handPoint.x));
        const float dragAlong = saturate(-handPoint.y / dragLength);
        const float dragStreaks = 0.5f + 0.5f * sin(handPoint.x * HORROR_MARK_HAND_DRAG_STRIPE_RATE + FastValueNoise(handPoint * 0.8f) * 2.0f);
        const float drag = dragAcross * step(0.0f, -handPoint.y) * (1.0f - dragAlong) * dragStreaks * HORROR_MARK_HAND_DRAG_ALPHA;
        marks.blood = max(hand, drag) * smudge * fade;
    }
    else if (kind < HORROR_MARK_SCRATCH_CUTOFF)
    {
        // --- 引っかき傷。4本の細い溝が、斜めに少し曲がりながら並んでいる
        const float scratchCenter = HORROR_MARK_SCRATCH_MIN_HEIGHT + seedB * HORROR_MARK_SCRATCH_HEIGHT_RANGE;
        const float angle = (seedC - 0.5f) * HORROR_MARK_SCRATCH_ROTATION_RANGE;
        const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
        const float2 p = mul(rotation, local - float2(0.0f, scratchCenter));
        const float halfLength = HORROR_MARK_SCRATCH_MIN_HALF_LENGTH + seedA * HORROR_MARK_SCRATCH_HALF_LENGTH_RANGE;
        const float along = saturate(abs(p.y) / halfLength);
        // 端ほど細く浅くなる
        const float taper = 1.0f - along * along;
        float gouge = 0.0f;
        [unroll]
        for (int scratch = 0; scratch < HORROR_MARK_SCRATCH_COUNT; ++scratch)
        {
            const float offset = (float(scratch) - 1.5f) * HORROR_MARK_SCRATCH_SPACING;
            // 爪が滑ったように、下へ行くほど外へ少し曲がる
            const float curve = p.y * p.y * HORROR_MARK_SCRATCH_CURVATURE * (float(scratch) - 1.5f);
            const float width = HORROR_MARK_SCRATCH_WIDTH * taper + footprint;
            const float lineMask = 1.0f - smoothstep(width * 0.4f, width, abs(p.x - offset - curve));
            gouge = max(gouge, lineMask * taper);
        }
        marks.gouge = gouge * step(abs(p.y), halfLength) * fade;
        // 半分ほどの傷には、溝に血が残っている
        marks.blood = marks.gouge * step(0.5f, seedB) * HORROR_MARK_SCRATCH_BLOOD_GAIN;
    }
    else if (kind < HORROR_MARK_DRIP_CUTOFF)
    {
        // --- 血の垂れ跡。上の方の染みから、長さの違う筋が下へ垂れ、先に雫がたまっている
        const float top = HORROR_MARK_DRIP_MIN_HEIGHT + seedB * HORROR_MARK_DRIP_HEIGHT_RANGE;
        const float2 blobPoint = (local - float2(0.0f, top)) / float2(2.8f, 1.3f);
        float blood = 1.0f - smoothstep(0.85f, 1.05f, length(blobPoint) +
            (FastValueNoise(local * 1.3f + cellKey) - 0.5f) * 0.5f);
        [unroll]
        for (int drip = 0; drip < HORROR_MARK_DRIP_COUNT; ++drip)
        {
            const float dripSeed = FastHash21(cellKey + float(drip) * 3.3f);
            const float x = (float(drip) - 2.0f) * 1.0f + (dripSeed - 0.5f) * 0.5f;
            const float dripLength = HORROR_MARK_DRIP_MIN_LENGTH + dripSeed * HORROR_MARK_DRIP_LENGTH_RANGE;
            const float width = HORROR_MARK_DRIP_MIN_WIDTH + HORROR_MARK_DRIP_WIDTH_RANGE * FastHash21(cellKey + float(drip) * HORROR_MARK_DRIP_WIDTH_SEED);
            const float below = top - heightFromFloor;
            const float stem = (1.0f - smoothstep(width * 0.5f, width + footprint, abs(local.x - x))) *
                step(0.0f, below) * step(below, dripLength);
            const float bulb = 1.0f - smoothstep(width * HORROR_MARK_DRIP_BULB_CORE, width * HORROR_MARK_DRIP_BULB_EDGE + footprint,
                length(float2(local.x - x, heightFromFloor - (top - dripLength))));
            blood = max(blood, max(stem, bulb));
        }
        marks.blood = blood * (0.75f + 0.25f * smudge) * fade;
    }
    return marks;
}

#endif
