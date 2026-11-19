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

// キャンバスのUV（u = 左から右、v = 上から下）と目の状態（開き具合・瞳の向き・赤い光）から、その点の絵の色と凹凸を求めている
// 塗りで左上からの光を表し、顔と首で影の濃さを変える。
#define PORTRAIT_FACE_LIGHT_BASE 0.62f
#define PORTRAIT_FACE_LIGHT_X_GRADIENT 0.075f
#define PORTRAIT_FACE_LIGHT_Y_GRADIENT 0.03f
#define PORTRAIT_SKIN_PAINT float3(0.36f, 0.29f, 0.22f)
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

PortraitPaint GetPortraitPainting(float2 uv, float4 eyeState)
{
    // キャンバスの単位（幅16・高さ22、中心が0、上が+）。目は (±3.5, 2.5) にある（Stage2Scene の目の向きの計算と合わせている）
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
    const float body = PortraitEllipse(p, float2(0.0f, -14.8f), float2(9.6f, 7.8f), PORTRAIT_EYE_EDGE_SOFTNESS);
    color = lerp(color, float3(0.055f, 0.048f, 0.050f) * lerp(0.7f, 1.2f, saturate(0.5f - p.x * 0.06f)), body);
    const float collar = PortraitEllipse(p, float2(0.0f, -7.4f), float2(2.8f, 1.3f), 0.20f) * body;
    color = lerp(color, float3(PORTRAIT_EYE_LOWER_OPEN_TRAVEL, 0.26f, PORTRAIT_EYE_CLOSED_CURVATURE), collar * 0.75f);

    // 首と顔。左上から光が当たり、右側と下側が影になる
    const float faceLight = saturate(PORTRAIT_FACE_LIGHT_BASE - p.x * PORTRAIT_FACE_LIGHT_X_GRADIENT + p.y * PORTRAIT_FACE_LIGHT_Y_GRADIENT);
    const float3 skin = PORTRAIT_SKIN_PAINT * lerp(PORTRAIT_SKIN_SHADOW_GAIN, PORTRAIT_SKIN_LIGHT_GAIN, faceLight);
    const float neck =
        (1.0f - smoothstep(1.9f, 2.2f, abs(p.x))) *
        smoothstep(-7.9f, -7.3f, p.y) *
        (1.0f - smoothstep(-4.2f, -3.6f, p.y));
    color = lerp(color, skin * 0.70f, neck);
    const float face = PortraitEllipse(p, float2(0.0f, 1.6f), float2(5.4f, 6.9f), PORTRAIT_EYE_EDGE_SOFTNESS);
    color = lerp(color, skin, face);

    // 髪：頭の上と横を覆う暗い色（額の生え際は、ノイズで少し乱している）
    const float hairline = 4.6f + abs(p.x) * -0.35f + (FastValueNoise(float2(p.x * 1.4f, 3.0f)) - 0.5f) * 0.6f;
    const float hair =
        PortraitEllipse(p, float2(0.0f, 2.6f), float2(6.1f, 7.5f), 0.05f) *
        saturate(smoothstep(hairline - 0.3f, hairline + 0.3f, p.y) + smoothstep(4.8f, 5.6f, abs(p.x)));
    color = lerp(color, float3(0.034f, 0.027f, 0.021f), hair);

    // 目（くぼみの影・まぶた・眉）。eyeState.x が開き具合（0 = 閉じた線、1 = 大きく見開いた目）、
    // yz が瞳の向き（-1〜1。見ている人の方へ動く）、w が瞳の赤い光（肖像画の異変で目が開いたとき）
    const float openness = saturate(eyeState.x);
    const float2 gaze = clamp(eyeState.yz, -1.0f.xx, 1.0f.xx);
    float eyeGlow = 0.0f;
    [unroll]
    for (int side = -1; side <= 1; side += 2)
    {
        const float2 eye = p - float2(3.5f * side, 2.5f);
        const float socket = PortraitEllipse(eye, float2(0.0f, 0.0f), float2(1.8f, 1.1f), 0.35f);
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
        const float glint = 1.0f - smoothstep(0.04f, 0.08f + footprint,
            length(eye - float2(irisCenter.x - 0.12f, closedLine + irisCenter.y + 0.12f)));
        eyeColor += glint * 0.25f * (1.0f - pupil * 0.5f);
        color = lerp(color, eyeColor, opening * face);
        eyeGlow = max(eyeGlow, iris * opening * face);

        // 閉じているときは線だけ、開くほど上のまぶたの線が太く暗くなる
        const float lid = PortraitStroke(eye, closedLine, 0.07f, 1.35f, footprint) * (1.0f - openness);
        const float upperLine = PortraitStroke(eye, upperLid, 0.06f + openness * 0.05f, 1.4f, footprint) * openness;
        const float brow = PortraitStroke(eye, 1.55f + openness * 0.20f - 0.10f * eye.x * eye.x + eye.x * side * 0.10f, 0.13f, 1.7f, footprint);
        color = lerp(color, float3(0.055f, 0.040f, 0.030f), saturate(lid + upperLine + brow * 0.85f) * face);
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
