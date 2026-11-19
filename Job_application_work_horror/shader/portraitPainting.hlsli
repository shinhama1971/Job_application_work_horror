// ============================================================================
// 共通の処理: 2面の肖像画（古い油絵の人物）を、テクスチャを使わずに計算で描いている。
// 主な技術: 楕円と曲線の距離で描く形（顔・髪・服・開閉する目と見る人を追う瞳・口）、筆の跡のノイズ、
//           ずらした格子の点による絵の具のひび割れ（クラクリュール）、ニスの黄ばみ
// litTexturePS.hlsl を PORTRAIT_SURFACE を付けて読み込んだとき（portraitPS.hlsl）だけ使っている。
// fastNoise.hlsli の後に読み込んでいる。
// ============================================================================
#ifndef PORTRAIT_PAINTING_INCLUDED
#define PORTRAIT_PAINTING_INCLUDED

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 絵の色と、筆の跡・ひびの凹凸（法線を傾けるのに使う高さ）
struct PortraitPaint
{
    float3 albedo;
    float relief;
    // 自ら光る色（目が開いたときの、瞳の赤い光）
    float3 emission;
};

// 楕円の内側で1、外側で0を返している（縁をwidthの割合でぼかしている）
// 額縁の内側を暗くし、ニスの黄ばみと異変で光る目の色を別々に調整する。
static const float PORTRAIT_FRAME_EDGE_BRIGHTNESS = 0.55f;
static const float PORTRAIT_FRAME_SHADOW_WIDTH = 0.12f;
static const float3 PORTRAIT_VARNISH_TINT = float3(1.0f, 0.93f, 0.78f);

// 赤い目は絵具の色を変える処理と分けて発光に足す。
static const float3 PORTRAIT_EYE_EMISSION = float3(0.55f, 0.02f, 0.01f);

float PortraitEllipse(float2 position, float2 center, float2 radius, float width)
{
    const float distance = length((position - center) / radius) - 1.0f;
    return 1.0f - smoothstep(-width, width, distance);
}

// 曲線 y = curve からの距離が太さの半分より近い所で1を返している（xの範囲を halfLength で区切る）
// 線の縁のぼかし（画素の大きさに掛ける）と、線の端を細くし始める長さの割合
#define PORTRAIT_STROKE_EDGE_FILTER 1.5f
#define PORTRAIT_STROKE_TAPER_START 0.8f

float PortraitStroke(float2 position, float curve, float halfThickness, float halfLength, float footprint)
{
    const float across = 1.0f - smoothstep(halfThickness, halfThickness + footprint * PORTRAIT_STROKE_EDGE_FILTER, abs(position.y - curve));
    const float along = 1.0f - smoothstep(halfLength * PORTRAIT_STROKE_TAPER_START, halfLength, abs(position.x));
    return across * along;
}

// 絵の具のひび割れ：ずらした格子の点のうち、一番近い点と2番目に近い点までの距離がほぼ同じ所（境目）を線にしている
// 一番近い点を探し始める距離（どの点より遠い値）、点のずらしの乱数のずらし、境目の線の太さ
#define PORTRAIT_CRACK_FAR_DISTANCE 8.0f
#define PORTRAIT_CRACK_JITTER_SEED 17.3f
#define PORTRAIT_CRACK_LINE_WIDTH 0.07f

float PortraitCracks(float2 position)
{
    const float2 cell = floor(position);
    float nearest = PORTRAIT_CRACK_FAR_DISTANCE;
    float second = PORTRAIT_CRACK_FAR_DISTANCE;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 neighbor = cell + float2(x, y);
            const float2 jitter = float2(FastHash21(neighbor), FastHash21(neighbor + PORTRAIT_CRACK_JITTER_SEED));
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
    return 1.0f - smoothstep(0.0f, PORTRAIT_CRACK_LINE_WIDTH, second - nearest);
}

// キャンバスのUV（u = 左から右、v = 上から下）と目の状態（開き具合・瞳の向き・赤い光）から、その点の絵の色と凹凸を求めている
// 塗りで左上からの光を表し、顔と首で影の濃さを変える。
#define PORTRAIT_FACE_LIGHT_BASE 0.62f
#define PORTRAIT_FACE_LIGHT_X_GRADIENT 0.075f
#define PORTRAIT_FACE_LIGHT_Y_GRADIENT 0.03f
#define PORTRAIT_SKIN_PAINT float3(0.36f, 0.29f, 0.22f)
// 襟の色（くすんだ白）と、体と顔の輪郭のぼかし
#define PORTRAIT_COLLAR_COLOR float3(0.28f, 0.26f, 0.22f)
#define PORTRAIT_FIGURE_EDGE_SOFTNESS 0.04f
#define PORTRAIT_SKIN_SHADOW_GAIN 0.42f
#define PORTRAIT_SKIN_LIGHT_GAIN 1.05f

// まぶたが開くと、目のくぼみの陰影を少し強くする。
#define PORTRAIT_EYE_SOCKET_BASE_SHADOW 0.42f
#define PORTRAIT_EYE_SOCKET_OPEN_SHADOW 0.18f

// 開く量は上下のまぶたで変え、輪郭は画素幅に応じてなじませる。
#define PORTRAIT_EYE_HALF_WIDTH 1.35f
#define PORTRAIT_EYE_CLOSED_CURVATURE 0.22f
#define PORTRAIT_EYE_CLOSED_Y_OFFSET 0.15f
#define PORTRAIT_EYE_UPPER_OPEN_TRAVEL 0.80f
#define PORTRAIT_EYE_LOWER_OPEN_TRAVEL 0.28f
#define PORTRAIT_EYE_EDGE_SOFTNESS 0.04f
#define PORTRAIT_EYE_EDGE_CORE 1.25f
#define PORTRAIT_EYE_MIN_OPENNESS 0.001f

// 黒目は視線で動かし、白目・虹彩・瞳の色を分ける。
#define PORTRAIT_GAZE_X_TRAVEL 0.62f
#define PORTRAIT_GAZE_Y_TRAVEL 0.30f
#define PORTRAIT_IRIS_CORE 0.40f
#define PORTRAIT_IRIS_EDGE 0.44f
#define PORTRAIT_PUPIL_CORE 0.16f
#define PORTRAIT_PUPIL_EDGE 0.20f
#define PORTRAIT_SCLERA_COLOR float3(0.50f, 0.45f, 0.36f)
#define PORTRAIT_SCLERA_EDGE_GAIN 0.55f
#define PORTRAIT_IRIS_COLOR float3(0.10f, 0.055f, 0.035f)
#define PORTRAIT_PUPIL_COLOR float3(0.010f, 0.008f, 0.008f)

// 顔の上の筆跡、離れると薄くなるひび、絵の具が剥がれた面の色と凹凸。
#define PORTRAIT_STROKE_COLOR_BASE 0.86f
#define PORTRAIT_STROKE_COLOR_GAIN 0.20f
#define PORTRAIT_FINE_STROKE_COLOR_GAIN 0.06f
#define PORTRAIT_CRACK_PATTERN_SCALE 2.6f
#define PORTRAIT_CRACK_DISTANCE_FADE 8.0f
#define PORTRAIT_CRACK_DARKENING 0.32f
#define PORTRAIT_FLAKE_START 0.84f
#define PORTRAIT_FLAKE_FULL 0.90f
#define PORTRAIT_FLAKE_NOISE_SCALE 1.3f
#define PORTRAIT_FLAKE_NOISE_SEED 41.0f
#define PORTRAIT_CANVAS_COLOR float3(0.11f, 0.092f, 0.065f)
#define PORTRAIT_CANVAS_BLEND 0.40f
#define PORTRAIT_STROKE_RELIEF 0.25f
#define PORTRAIT_CRACK_RECESS 0.35f
#define PORTRAIT_FLAKE_RECESS 0.15f

// --- 絵の形（キャンバスの単位。幅16・高さ22、中心が0、上が+）
#define PORTRAIT_CANVAS_WIDTH 16.0f
#define PORTRAIT_CANVAS_HEIGHT 22.0f
#define PORTRAIT_MIN_FOOTPRINT 0.0001f
// 筆の跡：斜めに引き伸ばしたノイズ（x の細かさ・y の傾き・y の細かさ）と、細かい筆の跡（細かさ・ずらし）
#define PORTRAIT_STROKE_SCALE_X 0.9f
#define PORTRAIT_STROKE_SLANT 0.35f
#define PORTRAIT_STROKE_SCALE_Y 3.2f
#define PORTRAIT_FINE_STROKE_SCALE float2(2.5f, 7.0f)
#define PORTRAIT_FINE_STROKE_SEED 11.0f
// 背景：頭の後ろの明るい所（中心・半径）、暗い焦げ茶と明るい所の色
#define PORTRAIT_HALO_CENTER float2(-0.6f, 2.5f)
#define PORTRAIT_HALO_RADIUS float2(9.0f, 10.5f)
#define PORTRAIT_BACKGROUND_DARK float3(0.045f, 0.034f, 0.025f)
#define PORTRAIT_BACKGROUND_LIGHT float3(0.20f, 0.155f, 0.10f)
// 黒い服：楕円（中心・半径）、色、光の当たり方（影・光・傾き）
#define PORTRAIT_BODY_CENTER float2(0.0f, -14.8f)
#define PORTRAIT_BODY_RADIUS float2(9.6f, 7.8f)
#define PORTRAIT_BODY_COLOR float3(0.055f, 0.048f, 0.050f)
#define PORTRAIT_BODY_SHADE 0.7f
#define PORTRAIT_BODY_LIGHT 1.2f
#define PORTRAIT_BODY_LIGHT_GRADIENT 0.06f
// 襟：楕円（中心・半径・ぼかし）と濃さ
#define PORTRAIT_COLLAR_CENTER float2(0.0f, -7.4f)
#define PORTRAIT_COLLAR_RADIUS float2(2.8f, 1.3f)
#define PORTRAIT_COLLAR_SOFTNESS 0.20f
#define PORTRAIT_COLLAR_BLEND 0.75f
// 首：横の幅（縁のぼかし始め・終わり）、下端（ぼかし始め・終わり）、上端（ぼかし始め・終わり）、影の暗さ
#define PORTRAIT_NECK_EDGE_START 1.9f
#define PORTRAIT_NECK_EDGE_END 2.2f
#define PORTRAIT_NECK_BOTTOM_START -7.9f
#define PORTRAIT_NECK_BOTTOM_END -7.3f
#define PORTRAIT_NECK_TOP_START -4.2f
#define PORTRAIT_NECK_TOP_END -3.6f
#define PORTRAIT_NECK_SHADE 0.70f
// 顔の楕円（中心・半径）
#define PORTRAIT_FACE_CENTER float2(0.0f, 1.6f)
#define PORTRAIT_FACE_RADIUS float2(5.4f, 6.9f)
// 髪：生え際（高さ・横へ下がる傾き・乱れのノイズの細かさと縦の座標・乱れの幅）、頭の楕円（中心・半径・ぼかし）、
// 生え際のぼかし、横の髪（ぼかし始め・終わり）、髪の色
#define PORTRAIT_HAIRLINE_HEIGHT 4.6f
#define PORTRAIT_HAIRLINE_SLOPE -0.35f
#define PORTRAIT_HAIRLINE_NOISE_SCALE 1.4f
#define PORTRAIT_HAIRLINE_NOISE_ROW 3.0f
#define PORTRAIT_HAIRLINE_JITTER 0.6f
#define PORTRAIT_HAIR_CENTER float2(0.0f, 2.6f)
#define PORTRAIT_HAIR_RADIUS float2(6.1f, 7.5f)
#define PORTRAIT_HAIR_SOFTNESS 0.05f
#define PORTRAIT_HAIRLINE_BLUR 0.3f
#define PORTRAIT_SIDE_HAIR_START 4.8f
#define PORTRAIT_SIDE_HAIR_END 5.6f
#define PORTRAIT_HAIR_COLOR float3(0.034f, 0.027f, 0.021f)
// 目の位置（中心から左右へ・高さ。Stage2Scene の目の向きの計算と合わせている）と、くぼみの楕円（半径・ぼかし）
#define PORTRAIT_EYE_OFFSET_X 3.5f
#define PORTRAIT_EYE_Y 2.5f
#define PORTRAIT_EYE_SOCKET_RADIUS float2(1.8f, 1.1f)
#define PORTRAIT_EYE_SOCKET_SOFTNESS 0.35f
// 瞳に映る小さな光：大きさ（芯・縁）、虹彩の中心からのずれ、明るさ、瞳の上で弱める割合
#define PORTRAIT_GLINT_CORE 0.04f
#define PORTRAIT_GLINT_EDGE 0.08f
#define PORTRAIT_GLINT_OFFSET 0.12f
#define PORTRAIT_GLINT_BRIGHTNESS 0.25f
#define PORTRAIT_GLINT_PUPIL_DIM 0.5f
// まぶたと眉の線：閉じた線（太さの半分・長さの半分）、上のまぶた（太さ・開くと太くなる量・長さ）、
// 眉（高さ・開くと上がる量・曲がり・左右の傾き・太さ・長さ）、線の色と眉の濃さ
#define PORTRAIT_LID_HALF_THICKNESS 0.07f
#define PORTRAIT_LID_HALF_LENGTH 1.35f
#define PORTRAIT_UPPER_LID_THICKNESS 0.06f
#define PORTRAIT_UPPER_LID_OPEN_THICKNESS 0.05f
#define PORTRAIT_UPPER_LID_HALF_LENGTH 1.4f
#define PORTRAIT_BROW_HEIGHT 1.55f
#define PORTRAIT_BROW_RAISE 0.20f
#define PORTRAIT_BROW_CURVE 0.10f
#define PORTRAIT_BROW_TILT 0.10f
#define PORTRAIT_BROW_HALF_THICKNESS 0.13f
#define PORTRAIT_BROW_HALF_LENGTH 1.7f
#define PORTRAIT_LINE_COLOR float3(0.055f, 0.040f, 0.030f)
#define PORTRAIT_BROW_DARKNESS 0.85f
// 鼻の右側の影：横の位置と幅（ぼかし始め・終わり）、縦の範囲（下端のぼかし・上端のぼかし）、暗さ
#define PORTRAIT_NOSE_X 0.55f
#define PORTRAIT_NOSE_EDGE_START 0.15f
#define PORTRAIT_NOSE_EDGE_END 0.45f
#define PORTRAIT_NOSE_BOTTOM_START -1.4f
#define PORTRAIT_NOSE_BOTTOM_END -0.8f
#define PORTRAIT_NOSE_TOP_START 1.0f
#define PORTRAIT_NOSE_TOP_END 1.8f
#define PORTRAIT_NOSE_SHADOW 0.30f
// 口：位置、端の下がり方、太さの半分・長さの半分、色
#define PORTRAIT_MOUTH_CENTER float2(0.0f, -2.7f)
#define PORTRAIT_MOUTH_DROOP -0.10f
#define PORTRAIT_MOUTH_HALF_THICKNESS 0.07f
#define PORTRAIT_MOUTH_HALF_LENGTH 1.5f
#define PORTRAIT_MOUTH_COLOR float3(0.07f, 0.035f, 0.030f)

PortraitPaint GetPortraitPainting(float2 uv, float4 eyeState)
{
    // キャンバスの単位（幅16・高さ22、中心が0、上が+）。目は (±3.5, 2.5) にある（Stage2Scene の目の向きの計算と合わせている）
    const float2 p = float2((uv.x - 0.5f) * PORTRAIT_CANVAS_WIDTH, (0.5f - uv.y) * PORTRAIT_CANVAS_HEIGHT);
    // 1画素がキャンバスで何単位か（細い線のぼかしと、遠くでひびを薄めるのに使っている）
    const float footprint = max(length(fwidth(p)), PORTRAIT_MIN_FOOTPRINT);

    // 筆の跡：斜めに引き伸ばしたノイズで、筆でなでたようなむらにしている
    const float stroke = FastValueNoise(float2(p.x * PORTRAIT_STROKE_SCALE_X + p.y * PORTRAIT_STROKE_SLANT, p.y * PORTRAIT_STROKE_SCALE_Y));
    const float strokeFine = FastValueNoise(p * PORTRAIT_FINE_STROKE_SCALE + PORTRAIT_FINE_STROKE_SEED);

    // 背景：暗い焦げ茶。頭の後ろだけ、ぼんやり明るい（古い肖像画の光の当て方）
    const float halo = saturate(1.0f - length((p - PORTRAIT_HALO_CENTER) / PORTRAIT_HALO_RADIUS));
    float3 color = lerp(PORTRAIT_BACKGROUND_DARK, PORTRAIT_BACKGROUND_LIGHT, halo * halo);

    // 黒い服と、白っぽい襟
    const float body = PortraitEllipse(p, PORTRAIT_BODY_CENTER, PORTRAIT_BODY_RADIUS, PORTRAIT_FIGURE_EDGE_SOFTNESS);
    color = lerp(color, PORTRAIT_BODY_COLOR * lerp(PORTRAIT_BODY_SHADE, PORTRAIT_BODY_LIGHT, saturate(0.5f - p.x * PORTRAIT_BODY_LIGHT_GRADIENT)), body);
    const float collar = PortraitEllipse(p, PORTRAIT_COLLAR_CENTER, PORTRAIT_COLLAR_RADIUS, PORTRAIT_COLLAR_SOFTNESS) * body;
    color = lerp(color, PORTRAIT_COLLAR_COLOR, collar * PORTRAIT_COLLAR_BLEND);

    // 首と顔。左上から光が当たり、右側と下側が影になる
    const float faceLight = saturate(PORTRAIT_FACE_LIGHT_BASE - p.x * PORTRAIT_FACE_LIGHT_X_GRADIENT + p.y * PORTRAIT_FACE_LIGHT_Y_GRADIENT);
    const float3 skin = PORTRAIT_SKIN_PAINT * lerp(PORTRAIT_SKIN_SHADOW_GAIN, PORTRAIT_SKIN_LIGHT_GAIN, faceLight);
    const float neck =
        (1.0f - smoothstep(PORTRAIT_NECK_EDGE_START, PORTRAIT_NECK_EDGE_END, abs(p.x))) *
        smoothstep(PORTRAIT_NECK_BOTTOM_START, PORTRAIT_NECK_BOTTOM_END, p.y) *
        (1.0f - smoothstep(PORTRAIT_NECK_TOP_START, PORTRAIT_NECK_TOP_END, p.y));
    color = lerp(color, skin * PORTRAIT_NECK_SHADE, neck);
    const float face = PortraitEllipse(p, PORTRAIT_FACE_CENTER, PORTRAIT_FACE_RADIUS, PORTRAIT_FIGURE_EDGE_SOFTNESS);
    color = lerp(color, skin, face);

    // 髪：頭の上と横を覆う暗い色（額の生え際は、ノイズで少し乱している）
    const float hairline = PORTRAIT_HAIRLINE_HEIGHT + abs(p.x) * PORTRAIT_HAIRLINE_SLOPE +
        (FastValueNoise(float2(p.x * PORTRAIT_HAIRLINE_NOISE_SCALE, PORTRAIT_HAIRLINE_NOISE_ROW)) - 0.5f) * PORTRAIT_HAIRLINE_JITTER;
    const float hair =
        PortraitEllipse(p, PORTRAIT_HAIR_CENTER, PORTRAIT_HAIR_RADIUS, PORTRAIT_HAIR_SOFTNESS) *
        saturate(smoothstep(hairline - PORTRAIT_HAIRLINE_BLUR, hairline + PORTRAIT_HAIRLINE_BLUR, p.y) +
            smoothstep(PORTRAIT_SIDE_HAIR_START, PORTRAIT_SIDE_HAIR_END, abs(p.x)));
    color = lerp(color, PORTRAIT_HAIR_COLOR, hair);

    // 目（くぼみの影・まぶた・眉）。eyeState.x が開き具合（0 = 閉じた線、1 = 大きく見開いた目）、
    // yz が瞳の向き（-1〜1。見ている人の方へ動く）、w が瞳の赤い光（肖像画の異変で目が開いたとき）
    const float openness = saturate(eyeState.x);
    const float2 gaze = clamp(eyeState.yz, -1.0f.xx, 1.0f.xx);
    float eyeGlow = 0.0f;
    [unroll]
    for (int side = -1; side <= 1; side += 2)
    {
        const float2 eye = p - float2(PORTRAIT_EYE_OFFSET_X * side, PORTRAIT_EYE_Y);
        const float socket = PortraitEllipse(eye, float2(0.0f, 0.0f), PORTRAIT_EYE_SOCKET_RADIUS, PORTRAIT_EYE_SOCKET_SOFTNESS);
        color *= 1.0f - socket * (PORTRAIT_EYE_SOCKET_BASE_SHADOW + openness * PORTRAIT_EYE_SOCKET_OPEN_SHADOW) * face;

        // 開いた目の形（アーモンド形）：閉じた線から、上のまぶたが大きく、下のまぶたが少し開く
        const float across = saturate(1.0f - (eye.x / PORTRAIT_EYE_HALF_WIDTH) * (eye.x / PORTRAIT_EYE_HALF_WIDTH));
        const float closedLine = PORTRAIT_EYE_CLOSED_CURVATURE * eye.x * eye.x - PORTRAIT_EYE_CLOSED_Y_OFFSET;
        const float upperLid = closedLine + PORTRAIT_EYE_UPPER_OPEN_TRAVEL * openness * across;
        const float lowerLid = closedLine - PORTRAIT_EYE_LOWER_OPEN_TRAVEL * openness * across;
        const float edgeSoftness = PORTRAIT_EYE_EDGE_SOFTNESS + footprint;
        const float opening =
            smoothstep(lowerLid - edgeSoftness, lowerLid + edgeSoftness, eye.y) *
            (1.0f - smoothstep(upperLid - edgeSoftness, upperLid + edgeSoftness, eye.y)) *
            (1.0f - smoothstep(PORTRAIT_EYE_EDGE_CORE, PORTRAIT_EYE_HALF_WIDTH, abs(eye.x))) *
            step(PORTRAIT_EYE_MIN_OPENNESS, openness);
        // 白目は黄ばんで、縁ほど暗い。黒目と瞳は、見ている人の方へ寄せている
        const float2 irisCenter = float2(gaze.x * PORTRAIT_GAZE_X_TRAVEL, gaze.y * PORTRAIT_GAZE_Y_TRAVEL * openness + (upperLid + lowerLid - 2.0f * closedLine) * 0.5f);
        const float irisDistance = length(eye - float2(irisCenter.x, closedLine + irisCenter.y));
        const float iris = 1.0f - smoothstep(PORTRAIT_IRIS_CORE, PORTRAIT_IRIS_EDGE + footprint, irisDistance);
        const float pupil = 1.0f - smoothstep(PORTRAIT_PUPIL_CORE, PORTRAIT_PUPIL_EDGE + footprint, irisDistance);
        float3 eyeColor = PORTRAIT_SCLERA_COLOR * lerp(PORTRAIT_SCLERA_EDGE_GAIN, 1.0f, across);
        eyeColor = lerp(eyeColor, PORTRAIT_IRIS_COLOR, iris);
        eyeColor = lerp(eyeColor, PORTRAIT_PUPIL_COLOR, pupil);
        // 瞳に映る小さな光
        const float glint = 1.0f - smoothstep(PORTRAIT_GLINT_CORE, PORTRAIT_GLINT_EDGE + footprint,
            length(eye - float2(irisCenter.x - PORTRAIT_GLINT_OFFSET, closedLine + irisCenter.y + PORTRAIT_GLINT_OFFSET)));
        eyeColor += glint * PORTRAIT_GLINT_BRIGHTNESS * (1.0f - pupil * PORTRAIT_GLINT_PUPIL_DIM);
        color = lerp(color, eyeColor, opening * face);
        eyeGlow = max(eyeGlow, iris * opening * face);

        // 閉じているときは線だけ、開くほど上のまぶたの線が太く暗くなる
        const float lid = PortraitStroke(eye, closedLine, PORTRAIT_LID_HALF_THICKNESS, PORTRAIT_LID_HALF_LENGTH, footprint) * (1.0f - openness);
        const float upperLine = PortraitStroke(eye, upperLid, PORTRAIT_UPPER_LID_THICKNESS + openness * PORTRAIT_UPPER_LID_OPEN_THICKNESS,
            PORTRAIT_UPPER_LID_HALF_LENGTH, footprint) * openness;
        const float brow = PortraitStroke(eye, PORTRAIT_BROW_HEIGHT + openness * PORTRAIT_BROW_RAISE - PORTRAIT_BROW_CURVE * eye.x * eye.x +
            eye.x * side * PORTRAIT_BROW_TILT, PORTRAIT_BROW_HALF_THICKNESS, PORTRAIT_BROW_HALF_LENGTH, footprint);
        color = lerp(color, PORTRAIT_LINE_COLOR, saturate(lid + upperLine + brow * PORTRAIT_BROW_DARKNESS) * face);
    }

    // 鼻の右側の細い影と、端の下がった真一文字の口（無表情で不気味にしている）
    const float noseShadow =
        (1.0f - smoothstep(PORTRAIT_NOSE_EDGE_START, PORTRAIT_NOSE_EDGE_END, abs(p.x - PORTRAIT_NOSE_X))) *
        smoothstep(PORTRAIT_NOSE_BOTTOM_START, PORTRAIT_NOSE_BOTTOM_END, p.y) * (1.0f - smoothstep(PORTRAIT_NOSE_TOP_START, PORTRAIT_NOSE_TOP_END, p.y));
    color *= 1.0f - noseShadow * PORTRAIT_NOSE_SHADOW * face;
    const float mouth = PortraitStroke(p - PORTRAIT_MOUTH_CENTER, PORTRAIT_MOUTH_DROOP * p.x * p.x,
        PORTRAIT_MOUTH_HALF_THICKNESS, PORTRAIT_MOUTH_HALF_LENGTH, footprint);
    color = lerp(color, PORTRAIT_MOUTH_COLOR, mouth * face);

    // 縁ほど暗く（額の内側の影）、全体にニスが黄ばんでいる
    const float edge = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    color *= lerp(PORTRAIT_FRAME_EDGE_BRIGHTNESS, 1.0f, smoothstep(0.0f, PORTRAIT_FRAME_SHADOW_WIDTH, edge));
    color *= PORTRAIT_VARNISH_TINT;

    // 筆の跡のむら
    color *= PORTRAIT_STROKE_COLOR_BASE + stroke * PORTRAIT_STROKE_COLOR_GAIN + strokeFine * PORTRAIT_FINE_STROKE_COLOR_GAIN;

    // 絵の具のひび割れ（遠くで1画素より細くなったら薄めている）と、ところどころ剥がれて見えるキャンバスの地
    const float cracks = PortraitCracks(p * PORTRAIT_CRACK_PATTERN_SCALE) * saturate(1.0f - footprint * PORTRAIT_CRACK_DISTANCE_FADE);
    color *= 1.0f - cracks * PORTRAIT_CRACK_DARKENING;
    const float flake = smoothstep(PORTRAIT_FLAKE_START, PORTRAIT_FLAKE_FULL, FastValueNoise(p * PORTRAIT_FLAKE_NOISE_SCALE + PORTRAIT_FLAKE_NOISE_SEED));
    color = lerp(color, PORTRAIT_CANVAS_COLOR, flake * PORTRAIT_CANVAS_BLEND);

    PortraitPaint paint;
    paint.albedo = color;
    paint.relief = stroke * PORTRAIT_STROKE_RELIEF - cracks * PORTRAIT_CRACK_RECESS - flake * PORTRAIT_FLAKE_RECESS;
    paint.emission = PORTRAIT_EYE_EMISSION * eyeGlow * saturate(eyeState.w);
    return paint;
}

#endif
