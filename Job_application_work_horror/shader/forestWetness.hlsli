// ============================================================================
// 共通の処理: 外の森の雨で濡れた岩・樹皮・研究所の外壁（rockPS.hlsl・barkPS.hlsl・facadePS.hlsl）。
// 雨の強さ（RainWetness。common.hlsl の b7。ForestRain が毎フレーム設定する）に合わせて、
// 色を暗くし（水を吸った石や木やコンクリートは暗くなる）、濡れ具合を上げて光沢を出し（MakePbrSurface が粗さを下げる）、
// 横を向いた面には、上から流れ落ちる細い水の筋を付けている。
// 雨が降っていないとき（RainWetness が0）は、元の材質をそのまま返し、計算を丸ごと省いている。
// 主な技術: 面の向き（上を向くほど雨を受ける）による濡れ方の違い、凹凸の低い所（ひび・溝）にたまる水、
//           水平の座標で選んだ細い列ごとに、時間でずらして下へ流れる水の筋、画素の大きさで細い筋を薄めるちらつき対策
// ============================================================================
#ifndef FOREST_WETNESS_INCLUDED
#define FOREST_WETNESS_INCLUDED

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 雨で濡れた後の色・粗さ・濡れ具合（元の材質に、雨の分を足した物）
struct ForestWetSurface
{
    float3 albedo;
    float wetness;
    float rivulet;  // 水の筋（0〜1）。高さを少し盛り上げ、筋の縁で光をきらりと返すのに使う
};

// 雨がほとんどないときは濡れの模様を計算しない。
#define FOREST_RAIN_MIN_AMOUNT 0.001f

// 上向きの面と凹みを濡れやすくし、雨の当たり方をむらにする。
#define FOREST_RAIN_FACING_MIN 0.10f
#define FOREST_RAIN_FACING_FADE_START -0.55f
#define FOREST_RAIN_FACING_FADE_END 0.65f
#define FOREST_RAIN_CAVITY_SHALLOW 0.05f
#define FOREST_RAIN_CAVITY_DEEP (-0.35f)
#define FOREST_RAIN_PATCH_SCALE_XZ 0.09f
#define FOREST_RAIN_PATCH_SCALE_Y 0.05f
#define FOREST_RAIN_SOAK_BASE 0.70f
#define FOREST_RAIN_SOAK_PATCH_GAIN 0.45f
#define FOREST_RAIN_CAVITY_GAIN 0.55f

// 水の筋が残る向きと列の出やすさ。画素が粗いときは線を広げて薄める。
#define FOREST_RAIN_SIDE_FADE_START 0.55f
#define FOREST_RAIN_SIDE_FADE_END 0.85f
#define FOREST_RAIN_COLUMN_SEED 17.0f
#define FOREST_RAIN_COLUMN_THRESHOLD 0.62f
#define FOREST_RAIN_STREAK_WIDTH 0.06f
// 横向きが0にならないよう足すわずかなずれと、画素の大きさの下限
#define FOREST_RAIN_DIRECTION_NUDGE 0.0001f
#define FOREST_RAIN_MIN_FOOTPRINT 0.0001f
#define FOREST_RAIN_STREAK_CENTER_START 0.3f
#define FOREST_RAIN_STREAK_CENTER_SPAN 0.4f
#define FOREST_RAIN_STREAK_FILTER_FLOOR 0.2f

// 筋を高さ方向に途切れさせ、列によって流れる粒の速さと位相を変える。
#define FOREST_RAIN_STREAK_BREAK_START 0.40f
#define FOREST_RAIN_STREAK_BREAK_END 0.62f
#define FOREST_RAIN_STREAK_BREAK_COLUMN_SCALE 3.1f
#define FOREST_RAIN_STREAK_BREAK_HEIGHT_SCALE 0.18f
#define FOREST_RAIN_FLOW_MIN_SPEED 0.35f
#define FOREST_RAIN_FLOW_SPEED_RANGE 0.45f
#define FOREST_RAIN_BEAD_SPATIAL_RATE 0.12f
#define FOREST_RAIN_BEAD_PHASE_RANGE 7.0f
#define FOREST_RAIN_BEAD_PEAK 0.04f
#define FOREST_RAIN_BEAD_END 0.30f
#define FOREST_RAIN_RIVULET_BACKGROUND 0.55f
#define FOREST_RAIN_RIVULET_BEAD_GAIN 0.9f

// 吸水した面と水の筋で暗さを変え、元の材質の濡れも残す。
#define FOREST_RAIN_SOAK_TINT_GAIN 0.58f
#define FOREST_RAIN_RIVULET_DARKENING 0.18f
#define FOREST_RAIN_SOAK_WETNESS_GAIN 0.95f

// albedo・wetness は岩や樹皮の材質、height はその凹凸の高さ（へこみは負）。
// grainScale は水の筋の列の細かさ（岩は粗く、樹皮は溝に沿って細かく）
ForestWetSurface ApplyForestRain(
    float3 worldPosition, float3 worldNormal, float3 albedo, float wetness, float height, float grainScale)
{
    ForestWetSurface result;
    result.albedo = albedo;
    result.wetness = wetness;
    result.rivulet = 0.0f;
    const float rain = saturate(RainWetness);
    [branch]
    if (rain <= FOREST_RAIN_MIN_AMOUNT)
    {
        return result;
    }

    const float3 normal = normalize(worldNormal);
    // 面の向き：上を向いた面ほど雨を受けてよく濡れる。横の面も伝ってくる水で半分ほど濡れ、真下を向いた面はほとんど乾いている
    const float facing = lerp(FOREST_RAIN_FACING_MIN, 1.0f, smoothstep(FOREST_RAIN_FACING_FADE_START, FOREST_RAIN_FACING_FADE_END, normal.y));
    // 凹凸の低い所（ひび・溝）には水がたまり、そこだけ先に濡れる
    const float cavity = smoothstep(FOREST_RAIN_CAVITY_SHALLOW, FOREST_RAIN_CAVITY_DEEP, height);
    // 濡れ方のむら（雨の当たり方や、葉の陰になる所の違い）
    const float patch = FastValueNoise(worldPosition.xz * FOREST_RAIN_PATCH_SCALE_XZ + worldPosition.y * FOREST_RAIN_PATCH_SCALE_Y);
    const float soak = saturate(rain * facing * (FOREST_RAIN_SOAK_BASE + patch * FOREST_RAIN_SOAK_PATCH_GAIN) + cavity * rain * FOREST_RAIN_CAVITY_GAIN);

    // --- 横を向いた面を上から流れ落ちる、細い水の筋
    // 水平の向き（面に沿った横向き）の座標で細い列に分け、列ごとに乱数で筋があるかと流れる速さを決めている。
    // 筋の中には、時間とともに下へ動く水の粒（少し太く明るい所）がある
    const float side = 1.0f - smoothstep(FOREST_RAIN_SIDE_FADE_START, FOREST_RAIN_SIDE_FADE_END, abs(normal.y));
    const float2 across2 = normalize(float2(-normal.z, normal.x) + FOREST_RAIN_DIRECTION_NUDGE);
    const float across = dot(worldPosition.xz, across2) * grainScale;
    const float column = floor(across);
    const float columnRandom = FastHash21(float2(column, FOREST_RAIN_COLUMN_SEED));
    const float hasStreak = step(FOREST_RAIN_COLUMN_THRESHOLD, columnRandom);
    // 列の中の筋の位置（列の幅に対して細い線）。画素の大きさで線の幅を広げ、広げた分だけ薄めている
    const float footprint = max(fwidth(across), FOREST_RAIN_MIN_FOOTPRINT);
    const float lineWidth = FOREST_RAIN_STREAK_WIDTH + footprint;
    const float centerOffset = abs(frac(across) - (FOREST_RAIN_STREAK_CENTER_START + columnRandom * FOREST_RAIN_STREAK_CENTER_SPAN));
    const float streakLine = (1.0f - smoothstep(lineWidth * 0.5f, lineWidth, centerOffset)) * saturate(FOREST_RAIN_STREAK_WIDTH / lineWidth + FOREST_RAIN_STREAK_FILTER_FLOOR);
    // 筋は途中で途切れる（上下に長いノイズで、ところどころだけ水が伝っている）
    const float streakPieces = smoothstep(FOREST_RAIN_STREAK_BREAK_START, FOREST_RAIN_STREAK_BREAK_END, FastValueNoise(float2(column * FOREST_RAIN_STREAK_BREAK_COLUMN_SCALE, worldPosition.y * FOREST_RAIN_STREAK_BREAK_HEIGHT_SCALE)));
    // 下へ流れる水の粒：列ごとに速さと始まりをずらしている
    const float flowSpeed = FOREST_RAIN_FLOW_MIN_SPEED + columnRandom * FOREST_RAIN_FLOW_SPEED_RANGE;
    const float flow = frac(worldPosition.y * FOREST_RAIN_BEAD_SPATIAL_RATE + RainTime * flowSpeed + columnRandom * FOREST_RAIN_BEAD_PHASE_RANGE);
    const float bead = smoothstep(0.0f, FOREST_RAIN_BEAD_PEAK, flow) * (1.0f - smoothstep(FOREST_RAIN_BEAD_PEAK, FOREST_RAIN_BEAD_END, flow));
    result.rivulet = saturate(hasStreak * streakLine * streakPieces * side * rain * (FOREST_RAIN_RIVULET_BACKGROUND + bead * FOREST_RAIN_RIVULET_BEAD_GAIN));

    // 色：水を吸った分だけ暗くする（石や木の表面のすき間に水が入り、光が中で吸われるため）。筋の所はさらに少し暗い
    result.albedo = albedo * lerp(1.0f, FOREST_RAIN_SOAK_TINT_GAIN, soak) * (1.0f - result.rivulet * FOREST_RAIN_RIVULET_DARKENING);
    result.wetness = saturate(max(wetness, soak * FOREST_RAIN_SOAK_WETNESS_GAIN) + result.rivulet);
    return result;
}

#endif
