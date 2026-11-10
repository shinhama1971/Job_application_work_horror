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

// 雨で濡れた後の色・粗さ・濡れ具合（元の材質に、雨の分を足した物）
struct ForestWetSurface
{
    float3 albedo;
    float wetness;
    float rivulet;  // 水の筋（0〜1）。高さを少し盛り上げ、筋の縁で光をきらりと返すのに使う
};

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
    if (rain <= 0.001f)
    {
        return result;
    }

    const float3 normal = normalize(worldNormal);
    // 面の向き：上を向いた面ほど雨を受けてよく濡れる。横の面も伝ってくる水で半分ほど濡れ、真下を向いた面はほとんど乾いている
    const float facing = lerp(0.10f, 1.0f, smoothstep(-0.55f, 0.65f, normal.y));
    // 凹凸の低い所（ひび・溝）には水がたまり、そこだけ先に濡れる
    const float cavity = smoothstep(0.05f, -0.35f, height);
    // 濡れ方のむら（雨の当たり方や、葉の陰になる所の違い）
    const float patch = FastValueNoise(worldPosition.xz * 0.09f + worldPosition.y * 0.05f);
    const float soak = saturate(rain * facing * (0.70f + patch * 0.45f) + cavity * rain * 0.55f);

    // --- 横を向いた面を上から流れ落ちる、細い水の筋
    // 水平の向き（面に沿った横向き）の座標で細い列に分け、列ごとに乱数で筋があるかと流れる速さを決めている。
    // 筋の中には、時間とともに下へ動く水の粒（少し太く明るい所）がある
    const float side = 1.0f - smoothstep(0.55f, 0.85f, abs(normal.y));
    const float2 across2 = normalize(float2(-normal.z, normal.x) + 0.0001f);
    const float across = dot(worldPosition.xz, across2) * grainScale;
    const float column = floor(across);
    const float columnRandom = FastHash21(float2(column, 17.0f));
    const float hasStreak = step(0.62f, columnRandom);
    // 列の中の筋の位置（列の幅に対して細い線）。画素の大きさで線の幅を広げ、広げた分だけ薄めている
    const float footprint = max(fwidth(across), 0.0001f);
    const float lineWidth = 0.06f + footprint;
    const float centerOffset = abs(frac(across) - (0.3f + columnRandom * 0.4f));
    const float streakLine = (1.0f - smoothstep(lineWidth * 0.5f, lineWidth, centerOffset)) * saturate(0.06f / lineWidth + 0.2f);
    // 筋は途中で途切れる（上下に長いノイズで、ところどころだけ水が伝っている）
    const float streakPieces = smoothstep(0.40f, 0.62f, FastValueNoise(float2(column * 3.1f, worldPosition.y * 0.18f)));
    // 下へ流れる水の粒：列ごとに速さと始まりをずらしている
    const float flowSpeed = 0.35f + columnRandom * 0.45f;
    const float flow = frac(worldPosition.y * 0.12f + RainTime * flowSpeed + columnRandom * 7.0f);
    const float bead = smoothstep(0.0f, 0.04f, flow) * (1.0f - smoothstep(0.04f, 0.30f, flow));
    result.rivulet = saturate(hasStreak * streakLine * streakPieces * side * rain * (0.55f + bead * 0.9f));

    // 色：水を吸った分だけ暗くする（石や木の表面のすき間に水が入り、光が中で吸われるため）。筋の所はさらに少し暗い
    result.albedo = albedo * lerp(1.0f, 0.58f, soak) * (1.0f - result.rivulet * 0.18f);
    result.wetness = saturate(max(wetness, soak * 0.95f) + result.rivulet);
    return result;
}

#endif
