// ============================================================================
// シェーダーの役割: 2面の信号盤の目印を、状態に応じた光る色と、電子パネルのような模様で描いている。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "common.hlsl"

// 頂点シェーダー（litTextureVS）から受け取る値（反射の座標は使わないので省いている）
struct SIGNAL_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
    float4 shadowPos : TEXCOORD6;
};

// 疑似乱数の係数（sin のハッシュ。決まった値）
#define SIGNAL_HASH_DIRECTION float2(41.73f, 289.19f)
#define SIGNAL_HASH_SCALE 43758.5453f

// 2次元の値から0〜1の疑似乱数を作っている
float SignalHash(float2 value)
{
    return frac(sin(dot(value, SIGNAL_HASH_DIRECTION)) * SIGNAL_HASH_SCALE);
}

// パネルの縁の枠（ぼかし始め・終わり）、格子（マスの数・線のぼかし始め・終わり）、
// 2本の配線（太さのぼかし始め・終わり、高さ、傾き）、格子を消すマスの割合
#define SIGNAL_BORDER_INNER 0.025f
#define SIGNAL_BORDER_OUTER 0.085f
#define SIGNAL_GRID_CELLS float2(12.0f, 7.0f)
#define SIGNAL_GRID_LINE_START 0.455f
#define SIGNAL_GRID_LINE_END 0.495f
#define SIGNAL_TRACE_A_INNER 0.018f
#define SIGNAL_TRACE_A_OUTER 0.045f
#define SIGNAL_TRACE_A_HEIGHT 0.28f
#define SIGNAL_TRACE_A_SLOPE 0.18f
#define SIGNAL_TRACE_B_INNER 0.014f
#define SIGNAL_TRACE_B_OUTER 0.038f
#define SIGNAL_TRACE_B_HEIGHT 0.73f
#define SIGNAL_TRACE_B_SLOPE 0.22f
#define SIGNAL_BROKEN_CELL_RATIO 0.22f
// 明かり：環境光に足す量、0で割らないための下限、ランバートの最低と幅
#define SIGNAL_AMBIENT_LIFT 0.12f
#define SIGNAL_MIN_DIVISOR 0.001f
#define SIGNAL_LAMBERT_BASE 0.24f
#define SIGNAL_LAMBERT_RANGE 0.76f
// 模様の重み（枠・格子・配線）、中心の光（ぼかし始め・終わり・横の縮め方）、光る色の強さ（基本・中心・模様）
#define SIGNAL_PATTERN_BORDER 0.82f
#define SIGNAL_PATTERN_GRID 0.20f
#define SIGNAL_PATTERN_CIRCUIT 0.48f
#define SIGNAL_GLOW_INNER 0.12f
#define SIGNAL_GLOW_OUTER 0.70f
#define SIGNAL_GLOW_ASPECT float2(0.72f, 1.0f)
#define SIGNAL_EMISSION_BASE 0.42f
#define SIGNAL_EMISSION_CENTER 0.38f
#define SIGNAL_EMISSION_PATTERN 0.62f
// 横の蛍光体の線（明るさの中心・揺れ幅）と円周率
#define SIGNAL_PHOSPHOR_BASE 0.965f
#define SIGNAL_PHOSPHOR_RANGE 0.035f
#define SIGNAL_PI 3.1415926f
// 古いパネルに見せる：輝度の重み、色の残し方、全体の暗さ
#define SIGNAL_LUMINANCE_WEIGHTS float3(0.2126f, 0.7152f, 0.0722f)
#define SIGNAL_SATURATION 0.70f
#define SIGNAL_DARKEN 0.62f

float4 main(SIGNAL_PS_IN input) : SV_Target
{
    // パネルの縁の明るい枠
    const float2 uv = saturate(input.tex);
    const float2 edgeDistance = min(uv, 1.0f - uv);
    const float border = 1.0f - smoothstep(
        SIGNAL_BORDER_INNER, SIGNAL_BORDER_OUTER, min(edgeDistance.x, edgeDistance.y));

    // 計算で作る格子と配線の模様で、テクスチャを足さずに電子パネルらしい表面にしている。
    const float2 gridCoordinate = abs(frac(uv * SIGNAL_GRID_CELLS) - 0.5f);
    const float grid = 1.0f - smoothstep(
        SIGNAL_GRID_LINE_START, SIGNAL_GRID_LINE_END, max(gridCoordinate.x, gridCoordinate.y));
    const float traceA = 1.0f - smoothstep(
        SIGNAL_TRACE_A_INNER, SIGNAL_TRACE_A_OUTER, abs(uv.y - (SIGNAL_TRACE_A_HEIGHT + uv.x * SIGNAL_TRACE_A_SLOPE)));
    const float traceB = 1.0f - smoothstep(
        SIGNAL_TRACE_B_INNER, SIGNAL_TRACE_B_OUTER, abs(uv.y - (SIGNAL_TRACE_B_HEIGHT - uv.x * SIGNAL_TRACE_B_SLOPE)));
    const float circuit = saturate(traceA + traceB);
    // ところどころの格子を消し、壊れかけた表示にしている
    const float cellNoise = SignalHash(floor(uv * SIGNAL_GRID_CELLS));
    const float brokenCells = step(SIGNAL_BROKEN_CELL_RATIO, cellNoise);

    // 点光源の明かりを足している（タイルベースライティングのリストを使っている）
    float3 lighting = Light.Ambient.rgb + SIGNAL_AMBIENT_LIFT;
    const float3 normal = normalize(input.worldNormal);
    uint pointLightListOffset;
    const uint pointLightCount =
        GetPixelLightCount(input.pos.xy, pointLightListOffset);
    [loop]
    for (uint lightIndex = 0; lightIndex < pointLightCount; ++lightIndex)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight =
            GetPixelLight(pointLightListOffset, lightIndex);
        const float3 toLight =
            pointLight.PositionRange.xyz - input.worldPos;
        const float distanceToLight = length(toLight);
        const float range = max(
            pointLight.PositionRange.w, SIGNAL_MIN_DIVISOR);
        float attenuation = saturate(1.0f - distanceToLight / range);
        attenuation *= attenuation;
        const float lambert = SIGNAL_LAMBERT_BASE + SIGNAL_LAMBERT_RANGE * saturate(dot(
            normal, toLight / max(distanceToLight, SIGNAL_MIN_DIVISOR)));
        lighting += pointLight.ColorIntensity.rgb *
            pointLight.ColorIntensity.a *
            attenuation * lambert;
    }

    // マテリアルの色に明かりを掛け、光る色を中心と模様ほど強く足している
    const float pattern = saturate(
        border * SIGNAL_PATTERN_BORDER + grid * brokenCells * SIGNAL_PATTERN_GRID + circuit * SIGNAL_PATTERN_CIRCUIT);
    const float centerGlow = 1.0f - smoothstep(
        SIGNAL_GLOW_INNER, SIGNAL_GLOW_OUTER, length((uv - 0.5f) * SIGNAL_GLOW_ASPECT));
    float3 color = Material.Diffuse.rgb * lighting;
    color += Material.Emission.rgb *
        (SIGNAL_EMISSION_BASE + centerGlow * SIGNAL_EMISSION_CENTER + pattern * SIGNAL_EMISSION_PATTERN);

    // 細い横の蛍光体の線は、普段の距離から見て目立ちすぎない強さにしている。
    const float phosphor = SIGNAL_PHOSPHOR_BASE +
        sin(input.pos.y * SIGNAL_PI) * SIGNAL_PHOSPHOR_RANGE;
    color *= phosphor;

    // 古く汚れたパネルらしく、全体を暗く・色を少し抜いている（廊下の中でおもちゃのように浮いて見えないように）。
    // 次に押すパネルの脈打つ光（光る色）は残るので、信号盤パズルの手がかりは見える
    const float panelLuminance = dot(color, SIGNAL_LUMINANCE_WEIGHTS);
    color = lerp(panelLuminance.xxx, color, SIGNAL_SATURATION) * SIGNAL_DARKEN;

    // デバッグ表示1：法線を色で表示している
    if (DebugViewMode == DEBUG_VIEW_NORMAL)
    {
        return float4(normal * 0.5f + 0.5f, 1.0f);
    }
    // HDRの値を1.0より高く残し、ブルームの抽出で、直ったパネルを柔らかな光源として見せている。
    return float4(max(color, 0.0f), 1.0f);
}
