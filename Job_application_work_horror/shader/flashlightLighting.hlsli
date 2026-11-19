// ============================================================================
// 共通の処理: 床と壁が共有する、懐中電灯の光の形（円錐・レンズの模様）と、上下で色を変えた環境光。
// common.hlslとfastNoise.hlsliの後に読み込んでいる。
// ============================================================================
#ifndef FLASHLIGHT_LIGHTING_INCLUDED
#define FLASHLIGHT_LIGHTING_INCLUDED

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 上向きの面と下向きの面で環境光の色と明るさを変える。
#define HEMISPHERE_GROUND_TINT float3(0.84f, 0.82f, 0.78f)
#define HEMISPHERE_SKY_TINT float3(0.92f, 0.98f, 1.06f)
#define HEMISPHERE_GROUND_GAIN 0.90f
#define HEMISPHERE_SKY_GAIN 1.06f

// 光が浅く当たる面を見えるようにし、正面の白飛びも抑える。
#define FLASHLIGHT_DIFFUSE_GRAZING 0.22f
#define FLASHLIGHT_DIFFUSE_FACE_ON 0.45f

// 円錐の中心に向かって強め、軟らかい縁を残す。
#define FLASHLIGHT_CONE_MIN_EXPONENT 0.01f
#define FLASHLIGHT_CONE_EDGE_GAIN 0.82f
#define FLASHLIGHT_CONE_CENTER_GAIN 1.08f
#define FLASHLIGHT_CONE_CENTER_BLEND_START 0.38f

// 中心に集まらなかった弱い光を外側の広い範囲に足す。
#define FLASHLIGHT_SPILL_OUTER_COS 0.41f
#define FLASHLIGHT_SPILL_INNER_COS 0.95f
#define FLASHLIGHT_SPILL_GAIN 0.22f

// 光の中心は青みを残し、外周では暖かい色へ寄せる。
#define FLASHLIGHT_TINT_EDGE_START 0.15f
#define FLASHLIGHT_TINT_CENTER float3(0.96f, 1.0f, 1.07f)
#define FLASHLIGHT_TINT_EDGE float3(1.10f, 0.88f, 0.58f)

// 上を向いた面はわずかに青白く、下を向いた面はわずかに暖かい色にした環境光を返している
// 0で割らないための下限（距離・光の届く範囲）、光の円錐の外側の cos の下限、補助の上向きを横向きに替える縦の向きの上限
#define FLASHLIGHT_MIN_DIVISOR 0.001f
#define FLASHLIGHT_MIN_OUTER_COSINE 0.05f
#define FLASHLIGHT_UP_FALLBACK_LIMIT 0.99f

float3 GetHemisphereAmbient(float3 worldNormal)
{
    const float skyAmount = saturate(normalize(worldNormal).y * 0.5f + 0.5f);
    const float3 tint = lerp(HEMISPHERE_GROUND_TINT,
        HEMISPHERE_SKY_TINT, skyAmount);
    return Light.Ambient.rgb * tint * lerp(HEMISPHERE_GROUND_GAIN, HEMISPHERE_SKY_GAIN, skyAmount);
}

// 懐中電灯の拡散光の強さを、面と光の角度（lambert）から返している。床と壁で共通。
// 横から当たる面も真っ暗にならないよう下限を設け、正面から当たる面も明るくなりすぎないよう上限を抑えている
// （1面の暗さは、面の向きによらず下限の0.25で当たっていたころに合わせて決めたため。凹凸の陰影はこの幅の中で出る）
float GetFlashlightDiffuseResponse(float lambert)
{
    return lerp(FLASHLIGHT_DIFFUSE_GRAZING, FLASHLIGHT_DIFFUSE_FACE_ON, lambert);
}

// 懐中電灯の円錐の中での明るさ（内側の円錐で最大、外側の円錐で0）を返している
float GetFlashlightBeamProfile(float3 pixelDirection)
{
    const float coneDot = dot(pixelDirection, normalize(Light.Direction.xyz));
    const float cone = smoothstep(Light.SpotParams.y, Light.SpotParams.x, coneDot);
    const float shapedCone = pow(saturate(cone), max(Light.SpotParams.z, FLASHLIGHT_CONE_MIN_EXPONENT));
    return shapedCone * lerp(FLASHLIGHT_CONE_EDGE_GAIN, FLASHLIGHT_CONE_CENTER_GAIN, smoothstep(FLASHLIGHT_CONE_CENTER_BLEND_START, 1.0f, cone));
}

// 光の色（下で定義している。翼の光でも使うので、先に宣言している）
float3 GetFlashlightTint(float3 pixelDirection);

// 懐中電灯の「翼の光（スピル）」の強さ（0〜約0.2）。本物の懐中電灯は、明るい中心（ホットスポット）の外にも、
// 中心の1〜2割ほどの弱い光が60度あまりまで広がり、照らした所のまわりがぼんやり見える（反射鏡で集めきれなかった光）。
// 円錐の内側（中心の光）にも少し足され、外側では約66度でなめらかに消える。
float GetFlashlightSpillProfile(float3 pixelDirection)
{
    const float coneDot = dot(pixelDirection, normalize(Light.Direction.xyz));
    // cos(66度) ≒ 0.41、cos(18度) ≒ 0.95
    const float spill = smoothstep(FLASHLIGHT_SPILL_OUTER_COS, FLASHLIGHT_SPILL_INNER_COS, coneDot);
    return spill * spill * FLASHLIGHT_SPILL_GAIN;
}

// 翼の光で面が受ける光。懐中電灯は目のすぐそばにあるので、目に見えている面には懐中電灯からも光が届く（さえぎる物がない）。
// そのため影は付けず、レンズの模様も付けない（中心の光ほどはっきりしない、ぼんやりした光）。
// 壁の文字（flashlightRevealPS）は、これまでどおり中心の光だけで浮かぶ（謎解きを簡単にしないため）
float3 GetFlashlightSpillLight(float3 viewPosition, float3 viewNormal)
{
    const float distanceFromCamera = length(viewPosition);
    if (!(Light.Enable && Light.FlashlightEnabled) || distanceFromCamera < FLASHLIGHT_MIN_DIVISOR)
    {
        return 0.0f;
    }
    const float3 pixelDirection = viewPosition / distanceFromCamera;
    const float spill = GetFlashlightSpillProfile(pixelDirection);
    const float normalizedDistance = saturate(distanceFromCamera / max(Light.Range, FLASHLIGHT_MIN_DIVISOR));
    const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
    const float lambert = saturate(dot(normalize(viewNormal), -pixelDirection));
    return Light.Diffuse.rgb * GetFlashlightTint(pixelDirection) * Light.Intensity * spill *
        rangeFade * rangeFade * GetFlashlightDiffuseResponse(lambert);
}

// 懐中電灯の光の色：中心は少し青みのある白、縁へ行くほど黄色く暖かくにじむ（安い懐中電灯の光。サイレントヒルの懐中電灯を参考）
float3 GetFlashlightTint(float3 pixelDirection)
{
    const float coneDot = dot(pixelDirection, normalize(Light.Direction.xyz));
    const float cone = smoothstep(Light.SpotParams.y, Light.SpotParams.x, coneDot);
    const float edge = smoothstep(FLASHLIGHT_TINT_EDGE_START, 1.0f, 1.0f - cone);
    return lerp(FLASHLIGHT_TINT_CENTER, FLASHLIGHT_TINT_EDGE, edge);
}

// ---- 懐中電灯の光の形（GetFlashlightLensPattern）の値。半径は、外側の円錐の縁を1とした円の中の距離 ----
// 本物の反射鏡の懐中電灯は、中心の芯（ホットスポット）が周りの2倍ほど明るく、芯の縁に明るい輪とすぐ外の暗い隙間があり、
// 外側はなだらかな弱い光になる。以前は中心と外周の差が約1.2倍しかなく、1で切っていたこともあって、照らした所が均一に見えていた
// （審査会の指摘）。HDRになったので、中心を明るくしても白く飛ばずに濃淡が残る
// 芯の外の明るさ（芯・輪を足す前）
static const float BEAM_BASE_LEVEL = 0.70f;
// 芯の大きさ（ガウス分布の広がり）と、芯で足す明るさ（電池が満タン・空のとき。弱るほど芯がぼやけて暗くなる）
static const float BEAM_HOTSPOT_RADIUS = 0.26f;
static const float BEAM_HOTSPOT_GAIN_FULL = 0.95f;
static const float BEAM_HOTSPOT_GAIN_WEAK = 0.35f;
// 芯の縁の明るい輪（反射鏡で光が集まる所）の位置・太さ・明るさ
static const float BEAM_EDGE_RING_RADIUS = 0.36f;
static const float BEAM_EDGE_RING_WIDTH = 0.04f;
static const float BEAM_EDGE_RING_GAIN = 0.12f;
// そのすぐ外の暗い隙間の位置・太さ・暗さ（電池が満タン・空のとき）
static const float BEAM_DARK_GAP_RADIUS = 0.45f;
static const float BEAM_DARK_GAP_WIDTH = 0.05f;
static const float BEAM_DARK_GAP_DEPTH_FULL = 0.10f;
static const float BEAM_DARK_GAP_DEPTH_WEAK = 0.16f;
// 中心のごく小さな暗がり（電球の先の影が映った物）の大きさと暗さ
static const float BEAM_FILAMENT_SHADOW_RADIUS = 0.05f;
static const float BEAM_FILAMENT_SHADOW_DEPTH = 0.10f;
// 円の中の光の合計を、以前の光の形と同じにする倍率（照らせる量・遊びやすさを変えないため。
// 円錐の明るさで重みを付けた面積の平均を数値で求めた：以前 0.957、新しい形 0.843）
static const float BEAM_ENERGY_NORMALIZATION = 1.135f;
// 反射鏡の細かい同心円の輪：輪の細かさ、明暗の幅（電池が満タン・空のとき）、輪を角度でゆがめる量と、ゆがみの模様の細かさ
static const float BEAM_REFLECTOR_RING_FREQUENCY = 31.0f;
static const float BEAM_REFLECTOR_RING_AMPLITUDE_FULL = 0.05f;
static const float BEAM_REFLECTOR_RING_AMPLITUDE_WEAK = 0.11f;
static const float BEAM_REFLECTOR_RING_WARP = 1.6f;
static const float BEAM_REFLECTOR_WARP_SCALE = 2.5f;

// 懐中電灯のレンズの模様：中心の明るい芯、芯の縁の明るい輪と暗い隙間、反射鏡の細かい輪、レンズの汚れのむら、
// 指紋のくもりと傷の影を付けている。電池が弱るほど（Light.SpotParams.w）、芯がぼやけて暗くなり、輪のむらがはっきりする
// レンズのむらを円錐の外周で薄め、大きい埃と小さい埃を別の密度で描く。
#define FLASHLIGHT_LENS_PATTERN_FADE_START 0.38f
#define FLASHLIGHT_LENS_PATTERN_FADE_END 1.02f
#define FLASHLIGHT_LENS_COARSE_DUST_SCALE 6.2f
#define FLASHLIGHT_LENS_COARSE_DUST_SEED 13.7f
#define FLASHLIGHT_LENS_FINE_DUST_SCALE 17.0f
#define FLASHLIGHT_LENS_FINE_DUST_SEED 5.2f
#define FLASHLIGHT_LENS_COARSE_DUST_WEIGHT 0.030f
#define FLASHLIGHT_LENS_FINE_DUST_WEIGHT 0.012f

// 反射鏡の輪は中心から少し外れたところで現れる。
#define FLASHLIGHT_REFLECTOR_WARP_SEED 7.3f
#define FLASHLIGHT_REFLECTOR_RING_START 0.15f

// レンズの曇りは偏った位置に置き、細い傷とは別の強さで暗くする。
#define FLASHLIGHT_LENS_SMUDGE_CENTER float2(0.35f, 0.25f)
#define FLASHLIGHT_LENS_SMUDGE_RADIUS float2(0.35f, 0.22f)
#define FLASHLIGHT_LENS_SMUDGE_DEPTH 0.06f
#define FLASHLIGHT_LENS_SCRATCH_WIDTH 0.012f
#define FLASHLIGHT_LENS_SCRATCH_SLOPE 0.4f
#define FLASHLIGHT_LENS_SCRATCH_OFFSET 0.18f
#define FLASHLIGHT_LENS_SCRATCH_DEPTH 0.07f
#define FLASHLIGHT_LENS_SCRATCH_HALF_LENGTH 0.6f

float GetFlashlightLensPattern(float3 pixelDirection)
{
    const float outerCosine = max(Light.SpotParams.y, FLASHLIGHT_MIN_OUTER_COSINE);
    const float outerTangent =
        sqrt(saturate(1.0f - outerCosine * outerCosine)) / outerCosine;
    // 揺れるライトの向きを基準にし、明るい中心と外周を一致させている。
    const float3 forward = normalize(Light.Direction.xyz);
    const float3 referenceUp = abs(forward.y) < FLASHLIGHT_UP_FALLBACK_LIMIT
        ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 right = normalize(cross(referenceUp, forward));
    const float3 up = cross(forward, right);
    // 画素の方向を、ライトの円錐の中の2次元の座標（外周が半径1）にしている
    const float2 lensUV = float2(dot(pixelDirection, right), dot(pixelDirection, up)) /
        max(dot(pixelDirection, forward) * outerTangent, FLASHLIGHT_MIN_DIVISOR);
    const float radius = length(lensUV);

    const float patternFade =
        1.0f - smoothstep(FLASHLIGHT_LENS_PATTERN_FADE_START, FLASHLIGHT_LENS_PATTERN_FADE_END, radius);
    const float largeDust = FastValueNoise(lensUV * FLASHLIGHT_LENS_COARSE_DUST_SCALE + FLASHLIGHT_LENS_COARSE_DUST_SEED);
    const float fineDust = FastValueNoise(lensUV * FLASHLIGHT_LENS_FINE_DUST_SCALE - FLASHLIGHT_LENS_FINE_DUST_SEED);
    const float lensDirt =
        ((largeDust - 0.5f) * FLASHLIGHT_LENS_COARSE_DUST_WEIGHT +
         (fineDust - 0.5f) * FLASHLIGHT_LENS_FINE_DUST_WEIGHT) * patternFade;

    // 電池の弱り具合（0〜1。Player が SpotParams.w で渡している）。弱るほど、光のむらがはっきりする
    const float weakness = saturate(Light.SpotParams.w);
    // 光の形：芯の外の明るさに、中心の芯・芯の縁の明るい輪・すぐ外の暗い隙間・中心の電球の影を足している
    const float hotspotOffset = radius / BEAM_HOTSPOT_RADIUS;
    const float hotspot = exp(-hotspotOffset * hotspotOffset) *
        lerp(BEAM_HOTSPOT_GAIN_FULL, BEAM_HOTSPOT_GAIN_WEAK, weakness);
    const float edgeRingOffset = (radius - BEAM_EDGE_RING_RADIUS) / BEAM_EDGE_RING_WIDTH;
    const float edgeRing = exp(-edgeRingOffset * edgeRingOffset) * BEAM_EDGE_RING_GAIN;
    const float darkGapOffset = (radius - BEAM_DARK_GAP_RADIUS) / BEAM_DARK_GAP_WIDTH;
    const float darkGap = -exp(-darkGapOffset * darkGapOffset) *
        lerp(BEAM_DARK_GAP_DEPTH_FULL, BEAM_DARK_GAP_DEPTH_WEAK, weakness);
    const float filamentOffset = radius / BEAM_FILAMENT_SHADOW_RADIUS;
    const float filamentShadow = -exp(-filamentOffset * filamentOffset) * BEAM_FILAMENT_SHADOW_DEPTH;
    const float beamShape = BEAM_BASE_LEVEL + hotspot + edgeRing + darkGap + filamentShadow;

    // 反射鏡の細かい同心円の輪。本物の反射鏡はきれいな円ではないので、向きによって輪の位置を少しずらしている
    // （向きの単位ベクトルからノイズを作るので、角度の境目で模様が切れない）
    const float2 ringDirection = lensUV / max(radius, FLASHLIGHT_MIN_DIVISOR);
    const float ringWarp = FastValueNoise(ringDirection * BEAM_REFLECTOR_WARP_SCALE + FLASHLIGHT_REFLECTOR_WARP_SEED) - 0.5f;
    const float rings = sin(radius * BEAM_REFLECTOR_RING_FREQUENCY + ringWarp * BEAM_REFLECTOR_RING_WARP) * 0.5f + 0.5f;
    const float ringPattern =
        (rings - 0.5f) *
        lerp(BEAM_REFLECTOR_RING_AMPLITUDE_FULL, BEAM_REFLECTOR_RING_AMPLITUDE_WEAK, weakness) *
        smoothstep(FLASHLIGHT_REFLECTOR_RING_START, 0.5f, radius) * patternFade;
    // レンズに残った指紋のようなくもり（右上が少し暗い）と、1本の細い傷の影
    const float smudge = -(1.0f - smoothstep(0.0f, 1.0f,
        length((lensUV - FLASHLIGHT_LENS_SMUDGE_CENTER) / FLASHLIGHT_LENS_SMUDGE_RADIUS))) * FLASHLIGHT_LENS_SMUDGE_DEPTH;
    const float scratch = -(1.0f - smoothstep(0.0f, FLASHLIGHT_LENS_SCRATCH_WIDTH, abs(lensUV.y - lensUV.x * FLASHLIGHT_LENS_SCRATCH_SLOPE + FLASHLIGHT_LENS_SCRATCH_OFFSET))) *
        FLASHLIGHT_LENS_SCRATCH_DEPTH * step(abs(lensUV.x), FLASHLIGHT_LENS_SCRATCH_HALF_LENGTH);

    // 1を超える明るさも残している（HDR。中心の芯は周りの2倍ほど明るい）
    return max(
        (beamShape + lensDirt * (1.0f + weakness) + ringPattern + smudge + scratch) * BEAM_ENERGY_NORMALIZATION,
        0.0f);
}

// 壁と床で、距離の二乗で弱まる照明を同じ割合だけ混ぜる。
static const float FLASHLIGHT_PHYSICAL_FALLOFF_SCALE = 0.000018f;
static const float FLASHLIGHT_PHYSICAL_FALLOFF_BLEND = 0.32f;

#endif
