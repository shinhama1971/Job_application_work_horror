// ============================================================================
// シェーダーの役割: 外の森で、稲光が光った一瞬だけ木の間に浮かぶ黒い人影（ForestLightningFigure）を描いている。
// 主な技術: 背にした稲光による輪郭の光（リムライト）、光が消えるにつれてノイズで崩れる表現（ディゾルブ）
// ・体はほとんど黒のまま、稲光の方へ傾いた輪郭だけを、稲光の色で細く光らせている（逆光で人の形だけが浮かぶ）。
// ・見え方（Visibility）が下がると、ワールドに固定したマス目のノイズで穴が増え、崩れるように消える（崩れる縁も稲光の色）。
// ・遠い人影が貼り付けたように見えないよう、距離の霧に少しだけ沈めている。
// ・この人影を描く間は b7 を借りているので、common.hlsl の b7 の値（デバッグ表示の番号・周回の深さ）は読まない。
// ============================================================================

#include "common.hlsl"
#include "horrorGrade.hlsli"
#include "fastNoise.hlsli"

// ForestLightningFigure.h の LightningFigureBuffer と同じ並び（b7 を一時的に借りている）
cbuffer LightningFigureBuffer : register(b7)
{
    float FigureTime;
    float FigureVisibility;
    float FigureBacklight;      // 稲光の強さ（0〜1）
    float FigurePadding;
    float4 FigureLightningPosition;
    float4 FigureLightningColor;
};

// 疑似乱数の係数（Dave Hoskins の hash13。決まった値）
// 崩れる模様：マスの細かさ、動く模様（マスのずらし・切り替わる速さ）、2つの模様の重み、崩れる縁の幅
#define FIGURE_CELL_SCALE 0.42f
#define FIGURE_MOVING_CELL_SCALE 1.73f
#define FIGURE_MOVING_RATE 14.0f
#define FIGURE_COARSE_WEIGHT 0.72f
#define FIGURE_MOVING_WEIGHT 0.28f
#define FIGURE_EDGE_WIDTH 0.06f
// 体の色（ほとんど黒）
#define FIGURE_SILHOUETTE_COLOR float3(0.004f, 0.005f, 0.006f)
// 輪郭の光：視線に対して横を向いた面ほど光る鋭さ、稲光の方を向いていない輪郭にも残す割合と、向いた輪郭に足す割合、明るさ
#define FIGURE_RIM_EXPONENT 3.0f
#define FIGURE_RIM_AWAY 0.25f
#define FIGURE_RIM_TOWARD 0.75f
#define FIGURE_RIM_GAIN 1.6f
// 崩れる縁の明るさ（稲光の色に掛ける）
#define FIGURE_EDGE_GAIN 0.9f
// 距離の霧に沈める割合（霧の濃さに掛ける。1 だと周りと同じだけ沈んで見えなくなる）
#define FIGURE_FOG_KEEP 0.55f
// 0で割らないための下限
#define FIGURE_MIN_DIVISOR 0.001f

// 3次元の値から0〜1の疑似乱数を作っている
float4 main(in PS_IN input) : SV_Target
{
    // ワールドのマス目に固定したノイズで、見え方より大きい画素を捨てている（見え方が下がるほど穴が増える）
    const float3 cell = floor(input.worldPos * FIGURE_CELL_SCALE);
    const float noise = FastHash31(cell) * FIGURE_COARSE_WEIGHT +
        FastHash31(cell * FIGURE_MOVING_CELL_SCALE + floor(FigureTime * FIGURE_MOVING_RATE)) * FIGURE_MOVING_WEIGHT;
    const float signedEdge = FigureVisibility - noise;
    clip(signedEdge);
    const float edge = 1.0f - smoothstep(0.0f, FIGURE_EDGE_WIDTH, signedEdge);

    // 輪郭の光：視線と面の向きが直角に近い所（輪郭）ほど強く、稲光の方を向いた輪郭はさらに強くしている
    const float distanceFromCamera = length(input.viewPos);
    const float3 toView = -input.viewPos / max(distanceFromCamera, FIGURE_MIN_DIVISOR);
    const float facing = saturate(dot(normalize(input.viewNormal), toView));
    const float rim = pow(1.0f - facing, FIGURE_RIM_EXPONENT);
    const float3 toLightning = normalize(FigureLightningPosition.xyz - input.worldPos);
    const float towardLightning = saturate(dot(normalize(input.worldNormal), toLightning));
    const float3 lightning = FigureLightningColor.rgb * FigureBacklight;
    float3 color = FIGURE_SILHOUETTE_COLOR +
        lightning * rim * (FIGURE_RIM_AWAY + FIGURE_RIM_TOWARD * towardLightning) * FIGURE_RIM_GAIN +
        lightning * edge * FIGURE_EDGE_GAIN;

    // 距離の霧に少しだけ沈めている（b7 を借りているので、周回の深さは 0 として計算する）
    const float fog = GetHorrorDistanceFog(distanceFromCamera, 0.0f) * FIGURE_FOG_KEEP;
    color = lerp(color, GetHorrorFogColor(Light.Ambient.rgb, 0.0f), fog);
    return float4(color, 1.0f);
}
