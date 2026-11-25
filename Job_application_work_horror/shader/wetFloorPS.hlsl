// ============================================================================
// シェーダーの役割: 濡れた床を描いている（床の古さ、水たまりと浸水した範囲、波紋、足音と水滴の波（波のシミュレーション）、水面の反射、フレネル、懐中電灯と天井灯の照明、霧）。
// ============================================================================

#include "common.hlsl"
#include "renderingConfig.hlsli"

// 床のテクスチャ（t0）とサンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"
#include "pointShadow.hlsli"

// 反射の方式の比較で使うキューブマップ（t7、ReflectionProbe）。SSRの画面と深度（t4・t8）は rayReflection.hlsli で読んでいる
TextureCube g_ReflectionProbe : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_REFLECTION_PROBE_TEXTURE_SLOT));

// Ground.cppのWetFloorBufferと同じ並び（b10）。経過時間、波紋の強さ、反射の強さ、水面の細かさ
// 濡れた足跡の最大数（WetFootprints::MaxCount と同じ）
#define FOOTPRINT_COUNT SIGNAL_LOST_FOOTPRINT_COUNT
// 天井の雨漏りの最大数（WaterEffectSystem::MaxLeakCount と同じ）
#define LEAK_COUNT SIGNAL_LOST_MAX_WATER_LEAKS
cbuffer WetFloorBuffer : register(SIGNAL_LOST_CBUFFER_REGISTER(SIGNAL_LOST_WET_FLOOR_BUFFER_SLOT))
{
    float WetTime;
    float RippleStrength;
    float ReflectionStrength;
    // 今描いている面の、水面の細かさ（Ground::DrawWaterSurfaces）。
    // 0 = 床の本体（乾いた床。水たまり・浸水・雨漏りは計算しない）、1〜3 = 水たまりのメッシュ（カメラから遠いほど小さい。WATER_LEVEL_*）
    float WaterSurfaceLevel;
    // 床一面が水に浸かった範囲（xy = x・zの最小、zw = x・zの最大）。範囲がないときは、最小が最大より大きい値にしている。
    float4 FloodRect;
    float4 FloodInletRect;
    // 濡れた足跡（WetFootprints::Fill）。x = 数。1つにつき2つ（xy = 位置、z = sin(向き)、w = cos(向き)/ x = 濡れ具合、y = 左右、z = 裸足か）
    float4 FootprintInfo;
    float4 Footprints[FOOTPRINT_COUNT * SIGNAL_LOST_FOOTPRINT_STRIDE];
    // x = 雨漏りの数、y = 反射の距離による簡略化、z = 主な浸水範囲、w = 右から流入する範囲の水の赤さ（0〜1）。
    // Leaks は xy = 中心のx・z、z = 半径。
    float4 LeakInfo;
    float4 Leaks[LEAK_COUNT];
}

// 水たまりのメッシュの細かさ（Ground::WaterLevel と同じ値）。メッシュ1つの中では一定なので、分岐は画素ごとにばらつかない
#define WATER_LEVEL_DRY_FLOOR SIGNAL_LOST_WATER_LEVEL_DRY_FLOOR
#define WATER_LEVEL_MIDDLE SIGNAL_LOST_WATER_LEVEL_MIDDLE
#define WATER_LEVEL_NEAR SIGNAL_LOST_WATER_LEVEL_NEAR
// 水たまりの表示（デバッグ）：メッシュの細かさの色（近い = 緑、中くらい = 黄、遠い = 赤）と、濃さの塗り方（最低＋幅）
#define DEBUG_PUDDLE_NEAR_COLOR float3(0.25f, 1.0f, 0.30f)
#define DEBUG_PUDDLE_MIDDLE_COLOR float3(1.0f, 0.85f, 0.20f)
#define DEBUG_PUDDLE_FAR_COLOR float3(1.0f, 0.30f, 0.25f)
#define DEBUG_PUDDLE_BASE 0.10f
#define DEBUG_PUDDLE_RANGE 0.90f
// 床の画像を読むときの Mip のずらし（草のような細かい模様を弱める）
#define FLOOR_TEXTURE_MIP_BIAS 1.15f
// 0で割らないための下限、懐中電灯が届いているとみなす下限、部屋の遮りを懐中電灯に効かせる割合
#define FLOOR_MIN_DIVISOR 0.001f
#define FLOOR_MIN_BEAM 0.001f
#define FLOOR_FLASHLIGHT_ROOM_OCCLUSION 0.4f
// 遠くの波の輪の光（波の傾きに掛ける強さ）と、水たまりで直射光の光沢を抑える割合
#define PUDDLE_WAVE_GLINT_SLOPE_GAIN 3.0f
#define PUDDLE_SPECULAR_SCALE 0.58f
// 定数の印（0 か 1）を見るしきい値（雨漏りの部屋の反射を使うか、SSR を使うか）
#define FLOOR_FLAG_THRESHOLD 0.5f

// 頂点シェーダー（litTextureVS）から受け取る値（影と反射の座標も使っている）
struct LIT_PS_IN
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


// 2次元の値から0〜1の疑似乱数を作っている（水滴の輪とノイズに使う。水たまりの位置は PuddleCells の表で決めている）
// GroundWaterEffects.cpp の Hash21FrequencyX/Y と HashAmplitude に対応する係数。
static const float2 WATER_HASH21_FREQUENCY = float2(127.1f, 311.7f);
static const float WATER_HASH21_AMPLITUDE = 43758.5453f;

float Hash21(float2 value)
{
    return frac(sin(dot(value, WATER_HASH21_FREQUENCY)) * WATER_HASH21_AMPLITUDE);
}

// 格子の4隅の乱数を、なめらかに補間した値のノイズ
float ValueNoise(float2 value)
{
    const float2 cell = floor(value);
    float2 blend = frac(value);
    blend = blend * blend * (3.0f - 2.0f * blend);

    const float a = Hash21(cell);
    const float b = Hash21(cell + float2(1.0f, 0.0f));
    const float c = Hash21(cell + float2(0.0f, 1.0f));
    const float d = Hash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);
}

#include "fastNoise.hlsli"
#include "puddleMask.hlsli"
#include "flashlightLighting.hlsli"
#include "waterCaustics.hlsli"
#include "roomOcclusion.hlsli"
#include "echoPulse.hlsli"
#include "screenAmbientOcclusion.hlsli"
#include "surfaceDetail.hlsli"
#include "wetFootprints.hlsli"

// ----------------------------------------------------------------------------
// 水面の反射の方式（ReflectionParams.x：0 反射なし、1 キューブマップ、2 SSR、3 レイ反射）。ゲームの普段の方式はレイ反射。
// SSRとレイ反射の本体（TraceScreenSpaceReflection・TraceRoomReflection）は、壁の鏡と共通の rayReflection.hlsli にある。
// ----------------------------------------------------------------------------
// キューブマップ：反射の向きで、カメラの位置から撮った6方向の画像を読む。
// 撮った位置からの景色なので軽いが、撮った位置から離れた所や近くの物では、映る位置がずれる。
float3 SampleProbeReflection(float3 worldPosition, float3 worldNormal)
{
    const float3 eye = -mul((float3x3)CausticView, CausticView[3].xyz);
    const float3 reflected = reflect(normalize(worldPosition - eye), worldNormal);
    // キューブマップの面は左手系、このゲームの座標は右手系なので、z を反転して読んでいる（ReflectionProbe.cpp）
    return g_ReflectionProbe.SampleLevel(g_SamplerState, float3(reflected.x, reflected.y, -reflected.z), 0.0f).rgb;
}

#include "floorAgeing.hlsli"

// ホラー向けの色調と、遠くの闇の色（litTexturePSと共通。horrorGrade.hlsli）
#include "horrorGrade.hlsli"
#include "horrorMist.hlsli"
#include "figureShadow.hlsli"
#include "rayReflection.hlsli"
#include "figureVolume.hlsli"

#include "puddleReflection.hlsli"

#include "waterDrips.hlsli"

#include "wetFloorMaterialSettings.hlsli"
#include "wetFloorStages.hlsli"

float4 main(in LIT_PS_IN input) : SV_Target
{
    float4 color = input.col;
    // 床の古さ（目地・ひび）の凹凸を入れた法線。古さのない面では元の法線のまま。
    const float3 agedWorldNormal = ApplyFloorMaterial(input, color);

    if (IsFullbrightDebugView() && DebugViewMode != DEBUG_VIEW_FULLBRIGHT_REFLECTION)
    {
        return GetFullbrightColor(color.rgb, input.viewNormal, input.viewPos);
    }

    // 水たまりの濃さ・岸の線・波の模様を求めている。
    // 床の本体（WaterSurfaceLevel が0）は乾いた床として描き、水たまり・浸水・雨漏りは、その形を覆うメッシュ（Ground::DrawWaterSurfaces）が描き重ねる。
    // メッシュは床より少し上にあり、深度プリパスにも描くので、メッシュの下の床の画素はこの計算まで来ない
    const int waterLevel = (int)(WaterSurfaceLevel + 0.5f);
    const FloorWater water = GetFloorWater(input, waterLevel);
    float shore = water.shore;
    float ripplePattern = water.ripplePattern;
    float puddle = water.puddle;
    const float floodAmount = water.floodAmount;
    const float surfaceFloodRedness = water.surfaceFloodRedness;
    // 柱の脇に残る薄い水と、その周りの水を吸ったコンクリート。水が通り過ぎた画素だけに出す。
    float drainedDamp = 0.0f;
    float remainingWater = 0.0f;
    const float2 fromDrainColumn = input.worldPos.xz - DrainTrace.xy;
    [branch]
    if (DrainTrace.w > 0.0f && DrainTrace.z > input.worldPos.z &&
        abs(fromDrainColumn.x) < DRAIN_AREA_HALF_X && abs(fromDrainColumn.y) < DRAIN_AREA_HALF_Z)
    {
        const float exposed = smoothstep(0.0f, DRAIN_WATER_EDGE_BLEND_DISTANCE, DrainTrace.z - input.worldPos.z);
        const float edgeNoise = (FastValueNoise(input.worldPos.xz * DRAIN_EDGE_NOISE_SCALE) - 0.5f) * DRAIN_EDGE_NOISE_STRENGTH;
        const float poolDistance = length((fromDrainColumn - DRAIN_POOL_CENTER) / DRAIN_POOL_RADIUS) + edgeNoise;
        remainingWater = (1.0f - smoothstep(DRAIN_POOL_EDGE_START, 1.0f, poolDistance)) * exposed;
        puddle = max(puddle, remainingWater * DRAIN_POOL_PUDDLE);
        const float dampDistance = length((fromDrainColumn - DRAIN_DAMP_CENTER) / DRAIN_DAMP_RADIUS) + edgeNoise;
        drainedDamp = (1.0f - smoothstep(DRAIN_DAMP_EDGE_START, 1.0f, dampDistance)) * exposed * (1.0f - remainingWater);
    }
    // 上を向いた面（床）だけを水たまりにしている
    const float upwardMask = smoothstep(WET_FLOOR_UPWARD_START, WET_FLOOR_UPWARD_FULL,
        saturate(input.worldNormal.y));
    puddle *= upwardMask;
    shore *= upwardMask;
    // 濡れた足跡（水たまりの外の乾いた床だけ。WetFootprints が場所と濡れ具合を渡している）
    const float footprintWet = GetWetFootprints(input.worldPos.xz) *
        upwardMask * (1.0f - saturate(puddle * FOOTPRINT_PUDDLE_EXCLUDE));

    // 足音や水滴でできた波（WaterRippleSimulation）の高さ（x）と傾き（yz）、水滴の輪、波で傾けた法線
    float dripRing;
    float3 rippleWave;
    float3 detailWorldNormal = GetFloorDetailNormal(input, agedWorldNormal, puddle, shore, ripplePattern, dripRing, rippleWave);

    const float baseLuminance = dot(
        color.rgb,
        LUMINANCE_WEIGHTS);
    // 浅い室内の水は、上から見たときに床を透かして見せる。
    // 暗くしすぎて、黒いシールのように見えるのを防いでいる。
    const float3 wetColor = lerp(
        color.rgb * WET_SHALLOW_SHADE,
        color.rgb * WET_DEEP_SHADE +
            WET_WATER_TINT + baseLuminance.xxx * WET_LUMINANCE_LIFT,
        WET_DEPTH_BLEND);
    color.rgb = lerp(color.rgb, wetColor, puddle * WET_PUDDLE_BLEND);
    color.rgb *= 1.0f - shore * SHORE_DARKEN - drainedDamp * DRAINED_DAMP_DARKEN - remainingWater * REMAINING_WATER_DARKEN;
    // 濡れた足跡は、水を吸って暗くなっている
    color.rgb = lerp(color.rgb, color.rgb * FOOTPRINT_SHADE + FOOTPRINT_WATER_TINT, footprintWet * FOOTPRINT_BLEND);

    // 上下で色を変えた環境光と DDGI の間接光（壁や床で跳ね返った光）と、浅い角度ほど強い反射（フレネル）
    float3 lighting = GetHemisphereAmbient(detailWorldNormal) + GetScreenIndirectLight(input.pos.xy);
    float3 specularLighting = 0.0f;
    const float distanceFromCamera = length(input.viewPos);
    const float3 viewDirection = distanceFromCamera > FLOOR_MIN_DIVISOR
        ? -input.viewPos / distanceFromCamera
        : float3(0.0f, 1.0f, 0.0f);
    // 離れるほど波の輪が細く見えるので、距離40から240にかけて輪の光り方を最大2.5倍まで強め、遠くの水たまりでも雨の輪が分かるようにしている
    const float farRippleBoost = 1.0f + WATER_RIPPLE_FAR_BOOST * smoothstep(WATER_RIPPLE_BOOST_START_DISTANCE, WATER_RIPPLE_BOOST_FULL_DISTANCE, distanceFromCamera);
    const float fresnel = pow(
        1.0f - saturate(dot(normalize(input.viewNormal), viewDirection)),
        PUDDLE_FRESNEL_EXPONENT);

    // 天井の点光源は、パネルだけでなく、近くの床と壁も照らしている。
    uint pointLightListOffset;
    const uint pointLightCount =
        GetPixelLightCount(input.pos.xy, pointLightListOffset);
    AddFloorPointLights(input, pointLightListOffset, pointLightCount, detailWorldNormal, puddle, lighting, specularLighting);

    // 部屋の角の暗がり。環境光と天井灯には全部、懐中電灯には一部だけ掛けている（照らせば角も見えるように）。
    // 画面空間の環境遮蔽（SSAO）も重ね、柱や棚の根元の床まで暗くしている（懐中電灯には一部だけ）
    const float screenOcclusion = GetScreenAmbientOcclusion(input.pos.xy);
    const float roomOcclusion = GetRoomOcclusion(input.worldPos, normalize(input.worldNormal)) * screenOcclusion;
    lighting *= roomOcclusion;
    const float flashlightOcclusion = lerp(1.0f, roomOcclusion, FLOOR_FLASHLIGHT_ROOM_OCCLUSION);

    AddFloorFlashlight(input, distanceFromCamera, viewDirection, detailWorldNormal, puddle, drainedDamp, footprintWet,
        fresnel, rippleWave, farRippleBoost, flashlightOcclusion, lighting, specularLighting);
    // 懐中電灯の翼の光：明るい中心の外にも広がる、ぼんやりした弱い光（flashlightLighting.hlsli）
    lighting += GetFlashlightSpillLight(input.viewPos, normalize(input.viewNormal)) * flashlightOcclusion;

    const float3 floorBaseColor = color.rgb;
    color.rgb *= lighting;
    // 水面の空の映り込み・波の光・水滴の輪・岸の光を足している
    specularLighting += PUDDLE_SKY_GLINT_TINT
        * puddle * (PUDDLE_SKY_GLINT_BASE + fresnel * PUDDLE_SKY_GLINT_FRESNEL);
    specularLighting += WET_FOOTPRINT_GLINT_TINT * footprintWet * (WET_FOOTPRINT_GLINT_BASE + fresnel * WET_FOOTPRINT_GLINT_FRESNEL);
    const float rippleHighlight = pow(
        saturate(ripplePattern * 0.5f + 0.5f),
        PUDDLE_PATTERN_GLINT_EXPONENT) * puddle;
    specularLighting += PUDDLE_PATTERN_GLINT_TINT * rippleHighlight * PUDDLE_PATTERN_GLINT_GAIN;
    // 足音や水滴の波の、傾いた斜面が光を返して、輪が細く光る
    // （懐中電灯が当たっていない所でも、暗い中でうっすら輪が分かる程度に光らせている）
    specularLighting += PUDDLE_WAVE_GLINT_TINT *
        saturate(length(rippleWave.yz) * PUDDLE_WAVE_GLINT_SLOPE_GAIN * farRippleBoost) * (PUDDLE_WAVE_GLINT_BASE + fresnel * PUDDLE_WAVE_GLINT_FRESNEL) * farRippleBoost;
    specularLighting += PUDDLE_DRIP_GLINT_TINT
        * dripRing * PUDDLE_DRIP_GLINT_GAIN * RippleStrength;
    specularLighting += PUDDLE_SHORE_GLINT_TINT
        * shore * (PUDDLE_SHORE_GLINT_BASE + fresnel * PUDDLE_SHORE_GLINT_FRESNEL);
    // 床が透けるようになった分、直射光の光沢で白い金属板のように見えない強さに抑える。
    color.rgb += specularLighting * lerp(1.0f, PUDDLE_SPECULAR_SCALE, puddle);
    color.rgb += Material.Emission.rgb;

    // 水面のむら：浮いた埃や油膜を、ゆっくり流れる2つのノイズで表している。
    // filmは水面の荒さ（反射のぼけ）に、grimeは映り込みの強さに使い、場所ごとに映り方が変わるようにしている。
    const float2 filmFlow = float2(WetTime * PUDDLE_FILM_FLOW_X, -WetTime * PUDDLE_FILM_FLOW_Z);
    const float film = FastValueNoise(input.worldPos.xz * PUDDLE_FILM_NOISE_SCALE + filmFlow);
    const float grime = FastValueNoise(input.worldPos.xz * PUDDLE_GRIME_NOISE_SCALE - filmFlow * 0.5f + PUDDLE_GRIME_NOISE_SEED);
    // 荒さ（0=鏡、1=ぼやけた反射）：浅い縁（水たまりの濃さが低い所）と、波が強い所ほど荒くしている
    const float waterRoughness = saturate(
        PUDDLE_BASE_ROUGHNESS + film * PUDDLE_FILM_ROUGHNESS_WEIGHT +
        abs(ripplePattern) * PUDDLE_WAVE_ROUGHNESS_WEIGHT +
        (1.0f - saturate(puddle * PUDDLE_SHALLOW_MASK_GAIN)) * PUDDLE_SHALLOW_ROUGHNESS_WEIGHT);

    // 水は正面に近いほど床が透け、浅い角度で反射が強くなる。床を見下ろしても鏡のように光らない幅にする。
    const float grazing = 1.0f - saturate(dot(normalize(input.viewNormal), viewDirection));
    const float viewAngleReflection = fresnel * grazing;
    // 反射の方式の比較：キューブマップ・SSR・レイ反射の色と映る度合いを、ここで求めている（反射なしのときは映さない）
    const int reflectionMode = int(ReflectionParams.x + 0.5f);
    float4 alternateReflection = 0.0f;
    // 反射像は実際に広がる波の傾きで少しだけ揺らす。床の凹凸と細かいノイズは光沢と波の輪に使う。
    // 遠くの水たまりは、映った景色の揺れも少し大きくして、輪が映り込みを乱す様子が見えるようにしている
    const float2 reflectionSlope = clamp(-rippleWave.yz * PUDDLE_REFLECTION_SLOPE_SCALE * farRippleBoost,
        -PUDDLE_REFLECTION_MAX_SLOPE * farRippleBoost, PUDDLE_REFLECTION_MAX_SLOPE * farRippleBoost);
    const float3 reflectionNormal = normalize(float3(reflectionSlope.x, 1.0f, reflectionSlope.y));
    // 「水たまり」の異変の人影が、反射の光線を遮った度合い（レイ反射のときだけ求めている）
    float puddleFigure = 0.0f;
    float nearVertical = 0.0f;
    [branch]
    if (reflectionMode != SIGNAL_LOST_REFLECTION_OFF && puddle > WATER_EPSILON)
    {
        if (reflectionMode == SIGNAL_LOST_REFLECTION_CUBE_MAP)
        {
            alternateReflection = float4(SampleProbeReflection(input.worldPos, reflectionNormal), 1.0f);
        }
        else if (reflectionMode == SIGNAL_LOST_REFLECTION_SCREEN_SPACE)
        {
            if (ReflectionParams.y > FLOOR_FLAG_THRESHOLD)
            {
                alternateReflection = TraceScreenSpaceReflection(input.worldPos, reflectionNormal,
                    WATER_SSR_STEP_COUNT, WATER_SSR_STEP_GROWTH, 1.0f / WATER_SSR_DISTANCE_PER_MIP,
                    WATER_SSR_BASE_BLUR + waterRoughness, WATER_SSR_MIN_HIT_HEIGHT);
            }
        }
        else
        {
            alternateReflection = float4(
                TraceRayReflection(input.worldPos, reflectionNormal, waterRoughness, distanceFromCamera, puddleFigure, nearVertical),
                1.0f);
        }
    }
    // 映り込みの強さ：水たまりの濃さ・光線が当たった度合い・見る角度（フレネル）・反射の強さの設定・水面の汚れを掛け合わせている。
    // 濡れた床と波紋を残しつつ、近くの壁や扉は斜めからでも輪郭が分かる強さで映す。
    const float faceReflection = lerp(PUDDLE_REFLECTION_FACE, PUDDLE_REFLECTION_NEAR_WALL, nearVertical * smoothstep(PUDDLE_REFLECTION_GRAZING_START, PUDDLE_REFLECTION_GRAZING_END, grazing));
    const float reflectionStrength = saturate(
        puddle * alternateReflection.w *
        lerp(faceReflection, PUDDLE_REFLECTION_GRAZING, viewAngleReflection) * ReflectionStrength *
        lerp(PUDDLE_REFLECTION_GRIME_MIN, 1.0f, smoothstep(PUDDLE_REFLECTION_GRIME_START, PUDDLE_REFLECTION_GRIME_END, grime)));
    if (DebugViewMode == DEBUG_VIEW_FULLBRIGHT_REFLECTION)
    {
        // 床の材質を明るくし、通常と同じ水面マスクと反射像を重ねる。
        const float3 floor = GetFullbrightColor(floorBaseColor, input.viewNormal, input.viewPos).rgb;
        const float weight = saturate(max(reflectionStrength,
            puddle * alternateReflection.w * ReflectionStrength * ALTERNATE_REFLECTION_WEIGHT));
        return float4(lerp(floor, alternateReflection.rgb, weight), 1.0f);
    }
    float3 reflectedScene = 0.0f;
    [branch]
    if (reflectionStrength > WATER_EPSILON)
    {
        reflectedScene = alternateReflection.rgb;
    }
    color.rgb = lerp(
        color.rgb,
        reflectedScene,
        reflectionStrength);
    // 真上から見ると反射は弱い（フレネル）ため、人影が映る所は床の色ごと沈め、水面の黒い人の形として見えるようにしている
    color.rgb *= 1.0f - puddleFigure * saturate(puddle * FIGURE_PUDDLE_GAIN) * FIGURE_PUDDLE_DARKEN;
    // 1面のループ廊下に押し寄せる水は、映った景色ごと赤黒くしている（西棟の水は赤さ0なので、元のまま）
    const float floodRedness = floodAmount * surfaceFloodRedness;
    color.rgb = lerp(color.rgb, color.rgb * FLOOD_WAVE_FILTER + FLOOD_WAVE_TINT, floodRedness);

    // 高さの違う霧を重ね、近くの見やすさを保ちながら、遠くの輪郭を分けている。
    // 部屋全体を一様に白くせず、床の近くに霧を集めている。
    // 2面の周回が進むほど、霧が近くから濃くなる（LoopDepth。horrorGrade.hlsli）
    const float distanceFog = GetHorrorDistanceFog(distanceFromCamera, LoopDepth);
    const float heightFromFloor = max(input.worldPos.y - SCENE_BASE_FLOOR_Y, 0.0f);
    const float heightDensity = exp(-heightFromFloor * HEIGHT_FOG_DECAY);
    const float nearSuppression =
        smoothstep(HEIGHT_FOG_NEAR_START, HEIGHT_FOG_NEAR_END, distanceFromCamera);
    const float fogVariation = HEIGHT_FOG_BASE_VARIATION +
        saturate(FastValueNoise(input.worldPos.xz * HEIGHT_FOG_NOISE_SCALE + HEIGHT_FOG_NOISE_OFFSET)) * HEIGHT_FOG_NOISE_WEIGHT;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * HEIGHT_FOG_STRENGTH * GetHorrorHeightFogScale(LoopDepth);
    const float fogFactor = saturate(distanceFog + heightFog);
    // 遠くは灰色のもやではなく、闇に溶けていく
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    color.rgb = lerp(color.rgb, fogColor, fogFactor);
    // 床の近くに溜まって流れる霧（懐中電灯の円錐の中だけ白く浮かぶ。horrorMist.hlsli）。
    // 映り込みの強い所だけ霧を少し薄める。
    const float3 beforeMist = color.rgb;
    color.rgb = ApplyHorrorMist(color.rgb, input.worldPos, input.viewPos, lighting);
    color.rgb = lerp(color.rgb, beforeMist, reflectionStrength * PUDDLE_REFLECTION_MIST_REDUCTION);
    // 天井の照明や非常灯から下へ落ちる光の筋（霧に散った光。horrorMist.hlsli）
    color.rgb += GetLightShafts(input.worldPos, pointLightListOffset, pointLightCount, input.pos.xy);
    // 赤い濁流の上では、霧と光の筋が足した白い光も水に吸わせ、水面を白っぽいピンクにせず暗い赤に保っている
    color.rgb = beforeMist + (color.rgb - beforeMist) * lerp(1.0f.xxx, FLOOD_MIST_FILTER, floodRedness);
    color.rgb = ApplyFilmicHorrorGrade(color.rgb, LoopDepth);

    // デバッグ表示：1=法線、2=懐中電灯の影、3=照明だけ、4=水たまりの濃さとメッシュの細かさ、5=反射の画像、7=タイルごとの光源の数（6は壁用なので黒）
    if (DebugViewMode == DEBUG_VIEW_NORMAL)
    {
        return float4(detailWorldNormal * 0.5f + 0.5f, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_FLASHLIGHT_SHADOW)
    {
        const float shadowVisibility =
            GetFlashlightShadow(input.shadowPos);
        return float4(shadowVisibility.xxx, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_LIGHTING)
    {
        return float4(saturate(lighting * DEBUG_VIEW_LIGHTING_SCALE), 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_PUDDLE)
    {
        // 水たまりのメッシュを細かさの色（近い=緑、中くらい=黄、遠い=赤）で薄く塗り、水たまりの濃さをその色で重ねている。床の本体は黒
        const float3 levelColor = waterLevel >= WATER_LEVEL_NEAR ? DEBUG_PUDDLE_NEAR_COLOR
            : waterLevel == WATER_LEVEL_MIDDLE ? DEBUG_PUDDLE_MIDDLE_COLOR
            : DEBUG_PUDDLE_FAR_COLOR;
        return waterLevel == WATER_LEVEL_DRY_FLOOR
            ? float4(0.0f, 0.0f, 0.0f, 1.0f)
            : float4(levelColor * (DEBUG_PUDDLE_BASE + puddle * DEBUG_PUDDLE_RANGE), 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_REFLECTION)
    {
        return float4(reflectedScene, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_LIGHT_TILES)
    {
        return GetLightTileHeatmap(input.pos.xy, pointLightCount);
    }
    if (DebugViewMode == DEBUG_VIEW_AMBIENT_OCCLUSION)
    {
        return float4(screenOcclusion.xxx, 1.0f);
    }
    if (DebugViewMode >= DEBUG_VIEW_WALL_DAMP)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // 音で輪郭が浮かぶ部屋：広がる音の輪が床を通る所を光らせている（輪がないときは何も足さない）
    color.rgb += GetEchoReveal(input.worldPos, normalize(input.worldNormal));
    return color;
}
