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
// 手形の形：輪郭のぼかし、手のひらの楕円（半径）、4本の指（間隔・付け根の高さ・長さ・中の2本に足す長さ・外へ開く量・太さの半分）、
// 親指（付け根・先・太さの半分）
#define HAND_EDGE 0.10f
#define HAND_PALM_RADIUS float2(1.9f, 2.1f)
#define HAND_PALM_DISTANCE_SCALE 1.9f
#define HAND_FINGER_COUNT 4
#define HAND_FINGER_CENTER 1.5f
#define HAND_FINGER_SPACING 0.95f
#define HAND_FINGER_BASE_Y 1.6f
#define HAND_FINGER_LENGTH 2.0f
#define HAND_MIDDLE_FINGER_EXTRA 0.5f
#define HAND_FINGER_SPREAD 0.32f
#define HAND_FINGER_HALF_WIDTH 0.36f
#define HAND_THUMB_BASE float2(1.5f, -0.2f)
#define HAND_THUMB_TIP float2(2.9f, 1.2f)
#define HAND_THUMB_HALF_WIDTH 0.48f

float HorrorHandprint(float2 p, float mirror, float footprint)
{
    p.x *= mirror;
    const float edge = HAND_EDGE + footprint;
    // 手のひら（少し縦長の楕円）
    const float palmDistance = (length(p / HAND_PALM_RADIUS) - 1.0f) * HAND_PALM_DISTANCE_SCALE;
    float mask = 1.0f - smoothstep(-edge, edge, palmDistance);
    // 4本の指（外側に少し開き、中指と薬指を長くしている）
    [unroll]
    for (int finger = 0; finger < HAND_FINGER_COUNT; ++finger)
    {
        const float offset = float(finger) - HAND_FINGER_CENTER;
        const float2 base = float2(offset * HAND_FINGER_SPACING, HAND_FINGER_BASE_Y);
        const float fingerLength = HAND_FINGER_LENGTH + (abs(offset) < 1.0f ? HAND_MIDDLE_FINGER_EXTRA : 0.0f);
        const float2 tip = base + float2(offset * HAND_FINGER_SPREAD, fingerLength);
        const float fingerDistance = HorrorSegmentDistance(p, base, tip) - HAND_FINGER_HALF_WIDTH;
        mask = max(mask, 1.0f - smoothstep(-edge, edge, fingerDistance));
    }
    // 親指（横へ斜めに出ている）
    const float thumbDistance = HorrorSegmentDistance(p, HAND_THUMB_BASE, HAND_THUMB_TIP) - HAND_THUMB_HALF_WIDTH;
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

// --- 壁の跡の置き方
// 描く画素の大きさの上限、マスの幅、壁の面の位置を丸める間隔（4ごと。境目で揺れないよう0.125ずらす）
#define HORROR_MARKS_MAX_FOOTPRINT 0.6f
#define HORROR_MARK_CELL_WIDTH 70.0f
#define HORROR_MARK_PLANE_STEP 0.25f
#define HORROR_MARK_PLANE_OFFSET 0.125f
// 跡の種類の乱数のずらしと、跡を描くマスの割合
#define HORROR_MARK_KIND_SEED 101.3f
#define HORROR_MARK_KIND_CUTOFF 0.36f
// マスごとの3つの乱数のずらし
#define HORROR_MARK_SEED_A 13.7f
#define HORROR_MARK_SEED_B 57.1f
#define HORROR_MARK_SEED_C 88.9f
// マスの中の位置（端から15〜55）、形を計算する範囲（横の半分・上の高さ）
#define HORROR_MARK_MIN_OFFSET 15.0f
#define HORROR_MARK_OFFSET_RANGE 40.0f
#define HORROR_MARK_HALF_REACH 14.0f
#define HORROR_MARK_MAX_HEIGHT 48.0f
// 血のかすれのノイズの細かさと、マスごとのずらし
#define HORROR_MARK_SMUDGE_SCALE 0.8f
#define HORROR_MARK_SMUDGE_CELL_SEED 7.0f
// 手形の大きさ（手のひらの幅が約5）、左右を決める乱数のしきい値、2つ目を押す乱数のしきい値と位置のずれ（横・下）
#define HORROR_MARK_HAND_SCALE 1.25f
#define HORROR_MARK_HAND_MIRROR_THRESHOLD 0.5f
#define HORROR_MARK_SECOND_HAND_THRESHOLD 0.45f
#define HORROR_MARK_SECOND_HAND_OFFSET_X 6.5f
#define HORROR_MARK_SECOND_HAND_DROP 3.0f
// 引きずった跡の幅（ぼかし始め・終わり。手の大きさに掛ける）、指の筋を揺らすノイズ（細かさ・強さ）
#define HORROR_MARK_DRAG_CORE 1.5f
#define HORROR_MARK_DRAG_EDGE 2.0f
#define HORROR_MARK_DRAG_NOISE_SCALE 0.8f
#define HORROR_MARK_DRAG_NOISE_GAIN 2.0f
// 引っかき傷の溝の番号の中心と、溝の芯の割合。溝に血が残る乱数のしきい値
#define HORROR_MARK_SCRATCH_CENTER 1.5f
#define HORROR_MARK_SCRATCH_CORE 0.4f
#define HORROR_MARK_SCRATCH_BLOOD_THRESHOLD 0.5f
// 血の垂れ跡：上の染みの楕円（半径）、縁（ぼかし始め・終わり）とゆがみ（細かさ・強さ）、筋（乱数のずらし・横の間隔・番号の中心・ゆらぎ）、
// 筋の芯の割合、濃さ（最低＋かすれの幅）
#define HORROR_MARK_BLOB_RADIUS float2(2.8f, 1.3f)
#define HORROR_MARK_BLOB_EDGE_START 0.85f
#define HORROR_MARK_BLOB_EDGE_END 1.05f
#define HORROR_MARK_BLOB_WARP_SCALE 1.3f
#define HORROR_MARK_BLOB_WARP 0.5f
#define HORROR_MARK_DRIP_SEED_STEP 3.3f
#define HORROR_MARK_DRIP_SPACING 1.0f
#define HORROR_MARK_DRIP_CENTER 2.0f
#define HORROR_MARK_DRIP_JITTER 0.5f
#define HORROR_MARK_DRIP_STEM_CORE 0.5f
#define HORROR_MARK_DRIP_SHADE_BASE 0.75f
#define HORROR_MARK_DRIP_SHADE_RANGE 0.25f

HorrorMarks ComputeHorrorMarks(float alongWall, float heightFromFloor, float planeCoordinate, float footprint)
{
    HorrorMarks marks;
    marks.blood = 0.0f;
    marks.gouge = 0.0f;
    // 遠くて細部が1画素より小さくなる所では描かない（ちらつきを防ぎ、計算も省く）
    if (footprint > HORROR_MARKS_MAX_FOOTPRINT)
    {
        return marks;
    }

    // 壁に沿って幅70のマス。マスの乱数には、壁の面の位置（4ごとに丸めた値）も混ぜている。
    // 壁の面の位置は整数（壁の中心±厚さの半分）なので、0.125ずらして丸め、境目で画素ごとに値が揺れないようにしている
    const float cellWidth = HORROR_MARK_CELL_WIDTH;
    const float cellIndex = floor(alongWall / cellWidth);
    const float2 cellKey = float2(cellIndex, floor(planeCoordinate * HORROR_MARK_PLANE_STEP + HORROR_MARK_PLANE_OFFSET));
    const float kind = FastHash21(cellKey + HORROR_MARK_KIND_SEED);
    [branch]
    if (kind >= HORROR_MARK_KIND_CUTOFF)
    {
        return marks;
    }
    const float seedA = FastHash21(cellKey + HORROR_MARK_SEED_A);
    const float seedB = FastHash21(cellKey + HORROR_MARK_SEED_B);
    const float seedC = FastHash21(cellKey + HORROR_MARK_SEED_C);
    // マスの中の位置（壁の端に寄りすぎないよう、15〜55の間）
    const float2 local = float2(alongWall - cellIndex * cellWidth - (HORROR_MARK_MIN_OFFSET + seedA * HORROR_MARK_OFFSET_RANGE), heightFromFloor);
    // 2つ目の手形や輪郭のぼけを含めても届かない範囲では、跡の形を計算しない。
    [branch]
    if (abs(local.x) > HORROR_MARK_HALF_REACH || local.y < 0.0f || local.y > HORROR_MARK_MAX_HEIGHT)
    {
        return marks;
    }
    const float fade = saturate(1.0f - footprint / HORROR_MARKS_FINE_DETAIL_FOOTPRINT);
    // 血の付き方のむら（かすれ）
    const float smudge = HORROR_MARK_BLOOD_MIN_COVERAGE + HORROR_MARK_BLOOD_COVERAGE_RANGE * FastValueNoise(local * HORROR_MARK_SMUDGE_SCALE + cellKey * HORROR_MARK_SMUDGE_CELL_SEED);

    [branch]
    if (kind < HORROR_MARK_HANDPRINT_CUTOFF)
    {
        // --- 血の手形。1つか2つ、少し傾けて押し付け、片方は下へ引きずった跡を付けている
        // 手形の大きさ（手のひらの幅が約5。目の高さが床から40なので、人の手と同じくらい）
        const float HandScale = HORROR_MARK_HAND_SCALE;
        const float handHeight = HORROR_MARK_HAND_HEIGHT_MIN + seedB * HORROR_MARK_HAND_HEIGHT_RANGE;
        const float angle = (seedC - 0.5f) * HORROR_MARK_HAND_ROTATION_RANGE;
        const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
        const float2 handPoint = mul(rotation, local - float2(0.0f, handHeight));
        float hand = HorrorHandprint(handPoint / HandScale, seedB < HORROR_MARK_HAND_MIRROR_THRESHOLD ? 1.0f : -1.0f, footprint / HandScale);
        if (seedC > HORROR_MARK_SECOND_HAND_THRESHOLD)
        {
            const float2 secondPoint = mul(rotation, local - float2(HORROR_MARK_SECOND_HAND_OFFSET_X, handHeight - HORROR_MARK_SECOND_HAND_DROP));
            hand = max(hand, HorrorHandprint(secondPoint / HandScale, seedB < HORROR_MARK_HAND_MIRROR_THRESHOLD ? -1.0f : 1.0f,
                footprint / HandScale) * HORROR_MARK_SECOND_HAND_BLEND);
        }
        // 手のひらの下へ、指の筋を残して引きずった跡（下へ行くほど薄くかすれる）
        const float dragLength = HORROR_MARK_HAND_DRAG_MIN_LENGTH + seedA * HORROR_MARK_HAND_DRAG_LENGTH_RANGE;
        const float dragAcross = 1.0f - smoothstep(HORROR_MARK_DRAG_CORE * HandScale, HORROR_MARK_DRAG_EDGE * HandScale + footprint, abs(handPoint.x));
        const float dragAlong = saturate(-handPoint.y / dragLength);
        const float dragStreaks = 0.5f + 0.5f * sin(handPoint.x * HORROR_MARK_HAND_DRAG_STRIPE_RATE + FastValueNoise(handPoint * HORROR_MARK_DRAG_NOISE_SCALE) * HORROR_MARK_DRAG_NOISE_GAIN);
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
            const float offset = (float(scratch) - HORROR_MARK_SCRATCH_CENTER) * HORROR_MARK_SCRATCH_SPACING;
            // 爪が滑ったように、下へ行くほど外へ少し曲がる
            const float curve = p.y * p.y * HORROR_MARK_SCRATCH_CURVATURE * (float(scratch) - HORROR_MARK_SCRATCH_CENTER);
            const float width = HORROR_MARK_SCRATCH_WIDTH * taper + footprint;
            const float lineMask = 1.0f - smoothstep(width * HORROR_MARK_SCRATCH_CORE, width, abs(p.x - offset - curve));
            gouge = max(gouge, lineMask * taper);
        }
        marks.gouge = gouge * step(abs(p.y), halfLength) * fade;
        // 半分ほどの傷には、溝に血が残っている
        marks.blood = marks.gouge * step(HORROR_MARK_SCRATCH_BLOOD_THRESHOLD, seedB) * HORROR_MARK_SCRATCH_BLOOD_GAIN;
    }
    else if (kind < HORROR_MARK_DRIP_CUTOFF)
    {
        // --- 血の垂れ跡。上の方の染みから、長さの違う筋が下へ垂れ、先に雫がたまっている
        const float top = HORROR_MARK_DRIP_MIN_HEIGHT + seedB * HORROR_MARK_DRIP_HEIGHT_RANGE;
        const float2 blobPoint = (local - float2(0.0f, top)) / HORROR_MARK_BLOB_RADIUS;
        float blood = 1.0f - smoothstep(HORROR_MARK_BLOB_EDGE_START, HORROR_MARK_BLOB_EDGE_END, length(blobPoint) +
            (FastValueNoise(local * HORROR_MARK_BLOB_WARP_SCALE + cellKey) - 0.5f) * HORROR_MARK_BLOB_WARP);
        [unroll]
        for (int drip = 0; drip < HORROR_MARK_DRIP_COUNT; ++drip)
        {
            const float dripSeed = FastHash21(cellKey + float(drip) * HORROR_MARK_DRIP_SEED_STEP);
            const float x = (float(drip) - HORROR_MARK_DRIP_CENTER) * HORROR_MARK_DRIP_SPACING + (dripSeed - 0.5f) * HORROR_MARK_DRIP_JITTER;
            const float dripLength = HORROR_MARK_DRIP_MIN_LENGTH + dripSeed * HORROR_MARK_DRIP_LENGTH_RANGE;
            const float width = HORROR_MARK_DRIP_MIN_WIDTH + HORROR_MARK_DRIP_WIDTH_RANGE * FastHash21(cellKey + float(drip) * HORROR_MARK_DRIP_WIDTH_SEED);
            const float below = top - heightFromFloor;
            const float stem = (1.0f - smoothstep(width * HORROR_MARK_DRIP_STEM_CORE, width + footprint, abs(local.x - x))) *
                step(0.0f, below) * step(below, dripLength);
            const float bulb = 1.0f - smoothstep(width * HORROR_MARK_DRIP_BULB_CORE, width * HORROR_MARK_DRIP_BULB_EDGE + footprint,
                length(float2(local.x - x, heightFromFloor - (top - dripLength))));
            blood = max(blood, max(stem, bulb));
        }
        marks.blood = blood * (HORROR_MARK_DRIP_SHADE_BASE + HORROR_MARK_DRIP_SHADE_RANGE * smudge) * fade;
    }
    return marks;
}

#endif
