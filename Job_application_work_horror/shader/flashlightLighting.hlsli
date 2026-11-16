// ============================================================================
// 共通の処理: 床と壁が共有する、懐中電灯の光の形（円錐・レンズの模様）と、上下で色を変えた環境光。
// common.hlslとfastNoise.hlsliの後に読み込んでいる。
// ============================================================================
#ifndef FLASHLIGHT_LIGHTING_INCLUDED
#define FLASHLIGHT_LIGHTING_INCLUDED

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// 上を向いた面はわずかに青白く、下を向いた面はわずかに暖かい色にした環境光を返している
float3 GetHemisphereAmbient(float3 worldNormal)
{
    const float skyAmount = saturate(normalize(worldNormal).y * 0.5f + 0.5f);
    const float3 tint = lerp(float3(0.84f, 0.82f, 0.78f),
        float3(0.92f, 0.98f, 1.06f), skyAmount);
    return Light.Ambient.rgb * tint * lerp(0.90f, 1.06f, skyAmount);
}

// 懐中電灯の拡散光の強さを、面と光の角度（lambert）から返している。床と壁で共通。
// 横から当たる面も真っ暗にならないよう下限を設け、正面から当たる面も明るくなりすぎないよう上限を抑えている
// （1面の暗さは、面の向きによらず下限の0.25で当たっていたころに合わせて決めたため。凹凸の陰影はこの幅の中で出る）
float GetFlashlightDiffuseResponse(float lambert)
{
    return lerp(0.22f, 0.45f, lambert);
}

// 懐中電灯の円錐の中での明るさ（内側の円錐で最大、外側の円錐で0）を返している
float GetFlashlightBeamProfile(float3 pixelDirection)
{
    const float coneDot = dot(pixelDirection, normalize(Light.Direction.xyz));
    const float cone = smoothstep(Light.SpotParams.y, Light.SpotParams.x, coneDot);
    const float shapedCone = pow(saturate(cone), max(Light.SpotParams.z, 0.01f));
    return shapedCone * lerp(0.82f, 1.08f, smoothstep(0.38f, 1.0f, cone));
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
    const float spill = smoothstep(0.41f, 0.95f, coneDot);
    return spill * spill * 0.22f;
}

// 翼の光で面が受ける光。懐中電灯は目のすぐそばにあるので、目に見えている面には懐中電灯からも光が届く（さえぎる物がない）。
// そのため影は付けず、レンズの模様も付けない（中心の光ほどはっきりしない、ぼんやりした光）。
// 壁の文字（flashlightRevealPS）は、これまでどおり中心の光だけで浮かぶ（謎解きを簡単にしないため）
float3 GetFlashlightSpillLight(float3 viewPosition, float3 viewNormal)
{
    const float distanceFromCamera = length(viewPosition);
    if (!(Light.Enable && Light.FlashlightEnabled) || distanceFromCamera < 0.001f)
    {
        return 0.0f;
    }
    const float3 pixelDirection = viewPosition / distanceFromCamera;
    const float spill = GetFlashlightSpillProfile(pixelDirection);
    const float normalizedDistance = saturate(distanceFromCamera / max(Light.Range, 0.001f));
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
    const float edge = smoothstep(0.15f, 1.0f, 1.0f - cone);
    return lerp(float3(0.96f, 1.0f, 1.07f), float3(1.10f, 0.88f, 0.58f), edge);
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
float GetFlashlightLensPattern(float3 pixelDirection)
{
    const float outerCosine = max(Light.SpotParams.y, 0.05f);
    const float outerTangent =
        sqrt(saturate(1.0f - outerCosine * outerCosine)) / outerCosine;
    // 揺れるライトの向きを基準にし、明るい中心と外周を一致させている。
    const float3 forward = normalize(Light.Direction.xyz);
    const float3 referenceUp = abs(forward.y) < 0.99f
        ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 right = normalize(cross(referenceUp, forward));
    const float3 up = cross(forward, right);
    // 画素の方向を、ライトの円錐の中の2次元の座標（外周が半径1）にしている
    const float2 lensUV = float2(dot(pixelDirection, right), dot(pixelDirection, up)) /
        max(dot(pixelDirection, forward) * outerTangent, 0.001f);
    const float radius = length(lensUV);

    const float patternFade =
        1.0f - smoothstep(0.38f, 1.02f, radius);
    const float largeDust = FastValueNoise(lensUV * 6.2f + 13.7f);
    const float fineDust = FastValueNoise(lensUV * 17.0f - 5.2f);
    const float lensDirt =
        ((largeDust - 0.5f) * 0.030f +
         (fineDust - 0.5f) * 0.012f) * patternFade;

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
    const float2 ringDirection = lensUV / max(radius, 0.001f);
    const float ringWarp = FastValueNoise(ringDirection * BEAM_REFLECTOR_WARP_SCALE + 7.3f) - 0.5f;
    const float rings = sin(radius * BEAM_REFLECTOR_RING_FREQUENCY + ringWarp * BEAM_REFLECTOR_RING_WARP) * 0.5f + 0.5f;
    const float ringPattern =
        (rings - 0.5f) *
        lerp(BEAM_REFLECTOR_RING_AMPLITUDE_FULL, BEAM_REFLECTOR_RING_AMPLITUDE_WEAK, weakness) *
        smoothstep(0.15f, 0.5f, radius) * patternFade;
    // レンズに残った指紋のようなくもり（右上が少し暗い）と、1本の細い傷の影
    const float smudge = -(1.0f - smoothstep(0.0f, 1.0f,
        length((lensUV - float2(0.35f, 0.25f)) / float2(0.35f, 0.22f)))) * 0.06f;
    const float scratch = -(1.0f - smoothstep(0.0f, 0.012f, abs(lensUV.y - lensUV.x * 0.4f + 0.18f))) *
        0.07f * step(abs(lensUV.x), 0.6f);

    // 1を超える明るさも残している（HDR。中心の芯は周りの2倍ほど明るい）
    return max(
        (beamShape + lensDirt * (1.0f + weakness) + ringPattern + smudge + scratch) * BEAM_ENERGY_NORMALIZATION,
        0.0f);
}

#endif
