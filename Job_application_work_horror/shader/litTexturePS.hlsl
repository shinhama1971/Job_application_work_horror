// ============================================================================
// シェーダーの役割: マテリアル・環境光・懐中電灯・天井灯・影・壁の古さ・霧を合わせて、3Dの面を照らしている（壁・天井・小物など、床以外のほとんどの面）。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "common.hlsl"

// テクスチャ（t0）とサンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"
#include "pointShadow.hlsli"


#include "fastNoise.hlsli"
#include "horrorMarks.hlsli"
#include "flashlightLighting.hlsli"
#include "pbrLighting.hlsli"
#include "waterCaustics.hlsli"
#include "roomOcclusion.hlsli"
#include "echoPulse.hlsli"
#include "screenAmbientOcclusion.hlsli"
#include "surfaceDetail.hlsli"
#if defined(PORTRAIT_SURFACE)
#include "portraitPainting.hlsli"
#endif
#if defined(CLOCK_SURFACE)
#include "clockFace.hlsli"
#endif
#if defined(DOOR_SURFACE)
#include "doorPainting.hlsli"
#endif
#if defined(ROCK_SURFACE)
#include "rockSurface.hlsli"
#endif
#if defined(BARK_SURFACE)
#include "barkSurface.hlsli"
#endif
#if defined(FACADE_SURFACE)
#include "facadeSurface.hlsli"
#endif
#if defined(ROCK_SURFACE) || defined(BARK_SURFACE) || defined(FACADE_SURFACE)
#include "forestWetness.hlsli"
#endif

// --- 2面の「血の手形」の異変の値
// 手形の数（WallHandprints の数）、画素の大きさの下限（0で割らないため）
static const float MIN_PIXEL_FOOTPRINT = 0.0001f;
// 手形の壁の面とみなす範囲：x の離れ具合と、法線の x の大きさ
static const float HANDPRINT_SURFACE_DEPTH = 0.6f;
static const float HANDPRINT_SURFACE_NORMAL = 0.7f;
// 手形ごとの傾き：番号の中心と、1つあたりの角度（ラジアン）。手形の大きさ（座標を割る値）
static const float HANDPRINT_TILT_CENTER = 1.5f;
static const float HANDPRINT_TILT_STEP = 0.18f;
static const float HANDPRINT_SCALE = 1.25f;
// 手のひらの下の引きずった跡：横の幅（消え始め・消えきる）、下へ伸びる長さ、横の縞の細かさ、濃さ
static const float HANDPRINT_DRAG_EDGE_START = 1.4f;
static const float HANDPRINT_DRAG_EDGE_END = 1.9f;
static const float HANDPRINT_DRAG_LENGTH = 5.0f;
static const float HANDPRINT_DRAG_STRIPES = 7.0f;
static const float HANDPRINT_DRAG_STRENGTH = 0.6f;
// にじみ：最低の濃さとむらの幅、むらの細かさ、手形ごとのずらし
static const float HANDPRINT_SMUDGE_BASE = 0.6f;
static const float HANDPRINT_SMUDGE_RANGE = 0.4f;
static const float HANDPRINT_SMUDGE_SCALE = 0.8f;
static const float HANDPRINT_SMUDGE_SEED_STEP = 13.0f;

// 2面の「血の手形」の異変：Stage2Scene が渡した手形（WallHandprints）を、その壁の面に描き、濃さ（0〜1）を返している。
// 形は1面の壁の血の跡と同じ式（HorrorHandprint）。見え方（w）で少しずつ浮かぶ。
float GetAnomalyHandprint(float3 worldPosition, float3 worldNormal)
{
    // 1画素がワールドで何単位か（手形の縁のぼかしに使う。分岐の外で求めている）
    const float footprint = max(length(fwidth(worldPosition.zy)), MIN_PIXEL_FOOTPRINT);
    float mask = 0.0f;
    [unroll]
    for (int index = 0; index < WATER_CAUSTICS_MAX_HANDPRINTS; ++index)
    {
        const float4 print = WallHandprints[index];
        // 浮かんでいない手形と、その手形の壁の面ではない所（x が離れている・壁の向きではない）は描かない
        if (print.w <= 0.0f || abs(worldPosition.x - print.x) > HANDPRINT_SURFACE_DEPTH || abs(worldNormal.x) < HANDPRINT_SURFACE_NORMAL)
        {
            continue;
        }
        // 壁を正面から見たときの右向きを横の軸にし（右の壁は+z、左の壁は-z）、手形ごとに少し傾けている
        const float side = print.x > 0.0f ? 1.0f : -1.0f;
        const float2 local = float2((worldPosition.z - print.z) * side, worldPosition.y - print.y);
        const float angle = (float(index) - HANDPRINT_TILT_CENTER) * HANDPRINT_TILT_STEP;
        const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
        const float2 handPoint = mul(rotation, local) / HANDPRINT_SCALE;
        const float hand = HorrorHandprint(handPoint, ((uint(index) & 1u) == 0u) ? 1.0f : -1.0f, footprint / HANDPRINT_SCALE);
        // 手のひらの下へ、少し引きずった跡
        const float drag = (1.0f - smoothstep(HANDPRINT_DRAG_EDGE_START, HANDPRINT_DRAG_EDGE_END, abs(handPoint.x))) * step(handPoint.y, 0.0f) *
            (1.0f - saturate(-handPoint.y / HANDPRINT_DRAG_LENGTH)) * (0.5f + 0.5f * sin(handPoint.x * HANDPRINT_DRAG_STRIPES)) * HANDPRINT_DRAG_STRENGTH;
        const float smudge = HANDPRINT_SMUDGE_BASE + HANDPRINT_SMUDGE_RANGE * FastValueNoise(local * HANDPRINT_SMUDGE_SCALE + float(index) * HANDPRINT_SMUDGE_SEED_STEP);
        mask = max(mask, max(hand, drag) * smudge * print.w);
    }
    return mask;
}

// 頂点シェーダー（litTextureVS）から受け取る値（反射の座標は使わないので省いている）
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
#if defined(BREATHING_WALL_SURFACE)
    float3 restPosition : TEXCOORD7;
    float3 restNormal : TEXCOORD8;
#endif
};


#include "wallSurface.hlsli"

// ホラー向けの色調と、遠くの闇の色（wetFloorPSと共通。horrorGrade.hlsli）
#include "horrorGrade.hlsli"
#include "horrorMist.hlsli"
#include "figureShadow.hlsli"
#include "toneMapping.hlsli"


// --- 画素の色の計算（main）の値
// 計算で描く絵の、普段のマテリアルの色（C++ の Stage2Layout と同じ値。異変で変わった色は、この色との比で掛ける）
static const float3 PORTRAIT_BASE_DIFFUSE = float3(SIGNAL_LOST_PORTRAIT_BASE_RGB);
static const float3 CLOCK_BASE_DIFFUSE = float3(SIGNAL_LOST_CLOCK_BASE_RGB);
static const float3 DOOR_BASE_DIFFUSE = float3(SIGNAL_LOST_FALSE_DOOR_BASE_RGB);
// 崩れて現れる・消える物：ノイズの細かさ、3つのノイズ（ずらし・細かさ・割合）、しきい値の広げ方、燃える縁の幅
static const float DISSOLVE_NOISE_SCALE = 0.55f;
static const float DISSOLVE_DEPTH_SKEW = 0.37f;
static const float DISSOLVE_WEIGHT_A = 0.55f;
static const float DISSOLVE_SCALE_B = 2.3f;
static const float DISSOLVE_SEED_B = 7.0f;
static const float DISSOLVE_WEIGHT_B = 0.30f;
static const float DISSOLVE_SCALE_C = 5.1f;
static const float DISSOLVE_SEED_C = 3.0f;
static const float DISSOLVE_WEIGHT_C = 0.15f;
static const float DISSOLVE_THRESHOLD_SCALE = 1.08f;
static const float DISSOLVE_THRESHOLD_OFFSET = 0.04f;
static const float DISSOLVE_EDGE_WIDTH = 0.07f;
// 崩れている縁：焦げて黒ずむ量、燃える赤い光の色と強さ
static const float DISSOLVE_CHAR_DARKEN = 0.55f;
static const float3 DISSOLVE_GLOW_COLOR = float3(0.95f, 0.34f, 0.07f);
static const float DISSOLVE_GLOW_STRENGTH = 1.5f;
// 光る色の強さを平均で求める重みと、光らない面とみなす強さ
static const float3 AVERAGE_WEIGHTS = float3(0.3333f, 0.3333f, 0.3333f);
static const float EMISSION_EPSILON = 0.001f;
// 外の森の材質の雨の濡れ：水の筋の列の細かさ（岩・樹皮・外壁）、水の筋を凹凸に足す量、凹凸の強さ
static const float ROCK_RAIN_COLUMN_SCALE = 0.9f;
static const float BARK_RAIN_COLUMN_SCALE = 1.6f;
static const float FACADE_RAIN_COLUMN_SCALE = 0.7f;
static const float RIVULET_HEIGHT = 0.12f;
static const float FACADE_RIVULET_HEIGHT = 0.10f;
static const float ROCK_BUMP_STRENGTH = 0.85f;
static const float BARK_BUMP_STRENGTH = 1.10f;
static const float FACADE_BUMP_STRENGTH = 0.9f;
// 建物の汚れ：暗くする量、くすんだ緑の色と混ぜ方
static const float GRIME_DARKEN = 0.22f;
static const float3 GRIME_TINT = float3(0.72f, 0.80f, 0.69f);
static const float GRIME_TINT_BLEND = 0.20f;
// 壁の古さ：奥の暗さの効き方、湿りの光沢に足す割合。水の筋で暗くする量
static const float AGEING_CAVITY_DARKEN = 0.55f;
static const float AGEING_DAMP_WETNESS = 0.8f;
static const float TRICKLE_DARKEN = 0.30f;
// 水が引いた柱の根元の湿り：横を向いた面とみなす法線の y、柱からの範囲、水位からの高さの上限、
// 水が引いてから湿りが出きる距離、湿りの上端（最低＋むら）とむらの細かさ、帯の下端（出始め・出きる）と上端のぼかし、色と光沢
static const float DRAIN_SIDE_NORMAL_LIMIT = 0.2f;
static const float DRAIN_COLUMN_HALF_WIDTH = 5.2f;
static const float DRAIN_MAX_HEIGHT = 7.5f;
static const float DRAIN_EXPOSE_DISTANCE = 3.0f;
static const float DRAIN_DAMP_TOP = 4.7f;
static const float DRAIN_DAMP_TOP_RANGE = 1.5f;
static const float DRAIN_DAMP_NOISE_SCALE = 0.55f;
static const float DRAIN_BAND_BOTTOM_START = -0.1f;
static const float DRAIN_BAND_BOTTOM_END = 0.5f;
static const float DRAIN_BAND_TOP_BLUR = 0.9f;
static const float3 DRAIN_DAMP_COLOR = float3(0.51f, 0.56f, 0.55f);
static const float DRAIN_DAMP_WETNESS = 0.50f;
// 2面の血の手形の異変：血の色と混ぜ方、濡れた光沢
static const float3 ANOMALY_BLOOD_COLOR = float3(0.85f, 0.05f, 0.04f);
static const float ANOMALY_BLOOD_BLEND = 0.92f;
static const float ANOMALY_BLOOD_WETNESS = 0.9f;
// 凹凸の強さ：建物の壁（古さで強める量）と小物（壁の半分）、金属の錆の凹凸の効き方
static const float BUILDING_BUMP_STRENGTH = 0.72f;
static const float BUILDING_BUMP_WEATHERING = 0.8f;
static const float PROP_BUMP_STRENGTH = 0.36f;
static const float RUST_HEIGHT_SCALE = 0.6f;
// 点光源：範囲と距離の下限（0で割らないため）、なだらかな当たり方（最低＋当たり方の割合）、横へ残す光の量
static const float LIGHT_EPSILON = 0.001f;
static const float SOFT_LAMBERT_BASE = 0.20f;
static const float SOFT_LAMBERT_RANGE = 0.80f;
static const float FIXTURE_SIDE_LIGHT = 0.22f;
// 懐中電灯に掛ける部屋の角の暗がりの割合
static const float FLASHLIGHT_ROOM_OCCLUSION = 0.4f;
// 技術デモで PBR を止める印（PointShadowOptions.y が 0 か 1）を見るしきい値
#define PBR_DISABLED_THRESHOLD 0.5f

// 画素の色を計算している：元の色 → 汚れと古さ → 環境光・天井灯・懐中電灯 → 濡れた光沢 → 自己発光 → 霧 → 色調
#if defined(PRINTED_SURFACE)
[earlydepthstencil]
#endif
float4 main(in LIT_PS_IN input) : SV_Target
{
    float4 color = input.col;

    // テクスチャがあればその色、なければマテリアルの色を使っている
    if (Material.TextureEnable)
    {
        color *= g_Texture.Sample(g_SamplerState, input.tex);
    }
    else
    {
        color *= Material.Diffuse;
    }
    // 2面の肖像画（portraitPS.hlsl）：元の色を、計算で描いた古い油絵の色に差し替えている。
    // マテリアルの色（肖像画の異変で赤黒く変わる）は、普段の色との比として掛けている。
    float paintingRelief = 0.0f;
    float3 paintingEmission = 0.0f;
#if defined(PORTRAIT_SURFACE)
    {
        // 目の状態は、肖像画の面ではマテリアルの Ambuent（使っていない値）で受け取っている（Wall::SetPortraitEyes）
        const PortraitPaint painting = GetPortraitPainting(saturate(input.tex), Material.Ambuent);
        const float3 portraitBaseDiffuse = PORTRAIT_BASE_DIFFUSE;
        color.rgb = painting.albedo * (Material.Diffuse.rgb / portraitBaseDiffuse);
        paintingRelief = painting.relief;
        paintingEmission = painting.emission;
    }
#endif
    // 2面の時計（clockPS.hlsl）：元の色を、計算で描いた文字盤の色に差し替えている。針の角度はマテリアルの Ambuent で受け取り
    // （Wall::SetClockHands）、マテリアルの色（3周目以降に赤黒く変わる）は、普段の色との比として掛けている。
#if defined(CLOCK_SURFACE)
    {
        const ClockPaint clockPaint = GetClockFace(saturate(input.tex), Material.Ambuent);
        const float3 clockBaseDiffuse = CLOCK_BASE_DIFFUSE;
        color.rgb = clockPaint.albedo * (Material.Diffuse.rgb / clockBaseDiffuse);
        paintingRelief = clockPaint.relief;
    }
#endif
    // 2面の偽の扉（doorPS.hlsl）：元の色を、計算で描いた木の扉の色に差し替えている。取っ手の側はマテリアルの Ambuent で受け取っている
#if defined(DOOR_SURFACE)
    {
        const DoorPaint doorPaint = GetDoorPainting(saturate(input.tex), Material.Ambuent);
        const float3 doorBaseDiffuse = DOOR_BASE_DIFFUSE;
        color.rgb = doorPaint.albedo * (Material.Diffuse.rgb / doorBaseDiffuse);
        paintingRelief = doorPaint.relief;
    }
#endif

    // 崩れて現れる・消える物（Wall::DissolveIn / DissolveOut）：ワールドの位置から作ったノイズが崩れ具合より小さい画素を捨て、
    // 残った所の縁を、焦げて燃えているように赤く光らせている（縁の明るさは下の「自ら光る色」の後で足している）。
    float dissolveEdge = 0.0f;
#if !defined(PRINTED_SURFACE)
    [branch]
    if (Material.Dissolve > 0.0f)
    {
        const float3 dissolvePosition = input.worldPos * DISSOLVE_NOISE_SCALE;
        const float dissolveNoise =
            FastValueNoise(dissolvePosition.xy + dissolvePosition.z * DISSOLVE_DEPTH_SKEW) * DISSOLVE_WEIGHT_A +
            FastValueNoise(dissolvePosition.zy * DISSOLVE_SCALE_B + DISSOLVE_SEED_B) * DISSOLVE_WEIGHT_B +
            FastValueNoise(dissolvePosition.xz * DISSOLVE_SCALE_C - DISSOLVE_SEED_C) * DISSOLVE_WEIGHT_C;
        // 崩れ具合0で何も捨てず、1ですべて捨てるよう、しきい値を少し広げている
        const float dissolveThreshold = Material.Dissolve * DISSOLVE_THRESHOLD_SCALE - DISSOLVE_THRESHOLD_OFFSET;
        clip(dissolveNoise - dissolveThreshold);
        dissolveEdge = 1.0f - smoothstep(0.0f, DISSOLVE_EDGE_WIDTH, dissolveNoise - dissolveThreshold);
    }
#endif

    // デバッグのフルブライト表示では、照明を使わずに返している
    if (IsFullbrightDebugView())
    {
        return GetFullbrightColor(color.rgb, input.viewNormal, input.viewPos);
    }

    // 計算で作る汚れは、不透明でテクスチャのない建材だけに使っている。
    // 光るパネルは汚さず、ブルームと天井の光の明るさを保っている。
    const float emissionEnergy = dot(
        abs(Material.Emission.rgb),
        AVERAGE_WEIGHTS);
    float3 detailWorldNormal = normalize(input.worldNormal);
    float grime = 0.0f;
    float wetness = 0.0f;
    // PBRの金属らしさと粗さ（金属の面は、下で錆と擦れ傷の分だけ場所ごとに変える）
    float pbrMetallic = Material.Metallic;
    float pbrRoughness = GetMaterialRoughness();
    float metalRustHeight = 0.0f;
#if !defined(PRINTED_SURFACE)
    if (!Material.TextureEnable && emissionEnergy < EMISSION_EPSILON)
    {
#if defined(ROCK_SURFACE)
        // 外の森の岩（rockPS.hlsl）：壁の汚れと古さの代わりに、岩の色むら・ひび・苔・地衣類・鉱物の粒・地面際の湿りを
        // 色・粗さ・濡れ具合・凹凸として付けている（rockSurface.hlsli）。粗さは配置ファイルの色の rough を元にしている
        const RockSurface rock = GetRockSurface(input.worldPos, input.worldNormal, color.rgb, pbrRoughness);
        // 雨で濡れた分（暗く・光沢・横の面を流れる水の筋）を足している（forestWetness.hlsli）
        const ForestWetSurface rockWet = ApplyForestRain(
            input.worldPos, input.worldNormal, rock.albedo, rock.wetness, rock.height, ROCK_RAIN_COLUMN_SCALE);
        color.rgb = rockWet.albedo;
        pbrRoughness = rock.roughness;
        wetness = rockWet.wetness;
        detailWorldNormal = ApplyHeightToNormal(
            input.worldPos, normalize(input.worldNormal), rock.height + rockWet.rivulet * RIVULET_HEIGHT, ROCK_BUMP_STRENGTH);
#elif defined(BARK_SURFACE)
        // 外の森の枯れ木・倒木・茂み（barkPS.hlsl）：木目に沿った縦の溝・横のひび・繊維・剥がれて出た地の木・苔・地衣類・根元の湿りを
        // 色・粗さ・濡れ具合・凹凸として付けている（barkSurface.hlsli）。木目の向きはマテリアルの環境の色で受け取っている（Wall::UploadMaterial）
        const BarkSurface bark = GetBarkSurface(
            input.worldPos, input.worldNormal, Material.Ambuent.xyz, color.rgb, pbrRoughness);
        // 雨で濡れた分を足している（樹皮は溝が細かいので、水の筋の列も細かくしている）
        const ForestWetSurface barkWet = ApplyForestRain(
            input.worldPos, input.worldNormal, bark.albedo, bark.wetness, bark.height, BARK_RAIN_COLUMN_SCALE);
        color.rgb = barkWet.albedo;
        pbrRoughness = bark.roughness;
        wetness = barkWet.wetness;
        detailWorldNormal = ApplyHeightToNormal(
            input.worldPos, normalize(input.worldNormal), bark.height + barkWet.rivulet * RIVULET_HEIGHT, BARK_BUMP_STRENGTH);
#elif defined(FACADE_SURFACE)
        // 外の森の研究所の外壁（facadePS.hlsl）：型枠の跡・Pコンの穴・雨だれ・白華・根元の泥と苔・ひびを、
        // 色・粗さ・濡れ具合・凹凸として付けている（facadeSurface.hlsli）。雨で濡れた分と、壁を流れる水の筋も重ねている
        const FacadeSurface facade = GetFacadeSurface(input.worldPos, input.worldNormal, color.rgb, pbrRoughness);
        const ForestWetSurface facadeWet = ApplyForestRain(
            input.worldPos, input.worldNormal, facade.albedo, facade.wetness, facade.height, FACADE_RAIN_COLUMN_SCALE);
        color.rgb = facadeWet.albedo;
        pbrRoughness = facade.roughness;
        wetness = facadeWet.wetness;
        detailWorldNormal = ApplyHeightToNormal(
            input.worldPos, normalize(input.worldNormal), facade.height + facadeWet.rivulet * FACADE_RIVULET_HEIGHT, FACADE_BUMP_STRENGTH);
#else
        grime = GetProceduralGrime(
            input.worldPos,
            input.worldNormal) * WallDampStrength;
        color.rgb *= 1.0f - grime * GRIME_DARKEN;
        color.rgb = lerp(
            color.rgb,
            color.rgb * GRIME_TINT,
            grime * GRIME_TINT_BLEND);

        // 壁の古さ（建物の壁と天井だけ）。0のときは前と同じ見た目になる。
        const float weathering = Material.WeatheringSurface ? WallWeathering : 0.0f;
#if defined(BREATHING_WALL_SURFACE)
        // 手形や継ぎ目は、膨らむ前の壁の面で場所を決める。光や影には変形後の位置と法線を使う。
        const WallAgeing ageing = GetWallAgeing(input.restPosition, input.restNormal, weathering);
#else
        const WallAgeing ageing = GetWallAgeing(
            input.worldPos, input.worldNormal, weathering);
#endif
        color.rgb *= ageing.tint;
        color.rgb *= 1.0f - ageing.cavity * AGEING_CAVITY_DARKEN;
        wetness = saturate(grime + ageing.damp * AGEING_DAMP_WETNESS);
        // 壁を伝う水の筋：湿った壁の所々を、細い水の筋が流れ落ちている（濡れて少し暗く、光沢が出る）
        const float trickle = weathering > 0.0f
            ? GetWallTrickle(input.worldPos, input.worldNormal, ageing.damp)
            : 0.0f;
        color.rgb *= 1.0f - trickle * TRICKLE_DARKEN;
        wetness = max(wetness, trickle);

        // 水が引いた柱の根元には、元の水位の少し上まで吸い上げた水が残る。
        const float2 fromDrainColumn = abs(input.worldPos.xz - DrainTrace.xy);
        const float heightAboveWater = input.worldPos.y - CausticWaterY;
        [branch]
        if (DrainTrace.w > 0.0f && Material.WeatheringSurface && abs(input.worldNormal.y) < DRAIN_SIDE_NORMAL_LIMIT &&
            max(fromDrainColumn.x, fromDrainColumn.y) < DRAIN_COLUMN_HALF_WIDTH && heightAboveWater < DRAIN_MAX_HEIGHT)
        {
            const float exposed = smoothstep(0.0f, DRAIN_EXPOSE_DISTANCE, DrainTrace.z - input.worldPos.z);
            const float dampTop = DRAIN_DAMP_TOP + FastValueNoise(input.worldPos.xz * DRAIN_DAMP_NOISE_SCALE) * DRAIN_DAMP_TOP_RANGE;
            const float heightFromFloor = heightAboveWater + DrainTrace.w;
            const float band = exposed * smoothstep(DRAIN_BAND_BOTTOM_START, DRAIN_BAND_BOTTOM_END, heightFromFloor) *
                (1.0f - smoothstep(dampTop - DRAIN_BAND_TOP_BLUR, dampTop, heightFromFloor));
            color.rgb *= lerp(1.0f.xxx, DRAIN_DAMP_COLOR, band);
            wetness = max(wetness, band * DRAIN_DAMP_WETNESS);
        }

        // 2面の「血の手形」の異変（その周回だけ、照らすと次の手形が奥に現れる）。濡れた血として光沢も返す
        const float anomalyHandprint = GetAnomalyHandprint(input.worldPos, normalize(input.worldNormal));
        color.rgb *= lerp(1.0f.xxx, ANOMALY_BLOOD_COLOR, anomalyHandprint * ANOMALY_BLOOD_BLEND);
        wetness = max(wetness, anomalyHandprint * ANOMALY_BLOOD_WETNESS);

        // 金属の錆と擦れ傷（金属らしさのある面だけ。pbrLighting.hlsli）。色・金属らしさ・粗さを場所ごとに変えている
        const MetalWear metalWear = GetMetalWear(
            color.rgb, pbrMetallic, pbrRoughness, input.worldPos, input.worldNormal);
        color.rgb = metalWear.baseColor;
        pbrMetallic = metalWear.metallic;
        pbrRoughness = metalWear.roughness;
        metalRustHeight = metalWear.height;

        // 汚れと古さの凹凸で、法線を傾けている（古さが濃いほど強く）。
        // 扉や家具などの小物は、コンクリートの壁より平らな面なので、凹凸を半分にしている
        // （懐中電灯で正面から照らすと、壁と同じ強さでは木の扉が叩いた金属のように見えるため）
        const float bumpStrength = Material.WeatheringSurface
            ? BUILDING_BUMP_STRENGTH * (1.0f + BUILDING_BUMP_WEATHERING * weathering)
            : PROP_BUMP_STRENGTH;
        detailWorldNormal = GetBumpedWorldNormal(
            input.worldPos,
            input.worldNormal,
            ageing.height + paintingRelief + metalRustHeight * RUST_HEIGHT_SCALE,
            bumpStrength);
#endif
    }
#endif
    const float3 detailViewNormal = normalize(mul(
        float4(detailWorldNormal, 0.0f),
        View).xyz);

    // PBRの材質（金属・粗さの方式。pbrLighting.hlsli）。汚れと古さを付けた後の色を元の色にし、濡れた所は粗さを下げている。
    // 拡散反射（lighting）と鏡面反射（specularLighting）を光ごとに分けて足し、最後に材質の色と合わせている。
    const PbrSurface surface = MakePbrSurface(
        color.rgb, pbrMetallic, pbrRoughness, wetness);
    const float3 viewDirection = normalize(-input.viewPos);
    float3 specularLighting = 0.0f;

    // 上下で色を変えた環境光と、DDGI の間接光（壁や床で跳ね返った光）から始めている
    float3 lighting = GetHemisphereAmbient(detailWorldNormal) + GetScreenIndirectLight(input.pos.xy);
    const float distanceFromCamera = length(input.viewPos);

    // 天井の点光源は、パネルだけでなく、近くの床と壁も照らしている。
    uint pointLightListOffset;
    const uint pointLightCount =
        GetPixelLightCount(input.pos.xy, pointLightListOffset);
    // 天井の照明の接触影（棚・机の足元の細い影。一番強く照らしている点光源の向きで求めてある）
    const float screenContactShadow = GetScreenContactShadow(input.pos.xy);
    [loop]
    for (uint i = 0; i < pointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight =
            GetPixelLight(pointLightListOffset, i);

        const float3 offsetToLight =
            pointLight.PositionRange.xyz - input.worldPos;
        const float lightRange = max(pointLight.PositionRange.w, LIGHT_EPSILON);
        const float distanceSquaredToLight = dot(offsetToLight, offsetToLight);
        // 光が届かない距離の光源は、計算を省いている
        [branch]
        if (distanceSquaredToLight >= lightRange * lightRange)
        {
            continue;
        }
        const float distanceToLight = sqrt(distanceSquaredToLight);
        const float3 directionToPointLight =
            offsetToLight / max(distanceToLight, LIGHT_EPSILON);
        float pointAttenuation = saturate(1.0f - distanceToLight / lightRange);
        pointAttenuation *= pointAttenuation;

        const float pointLambert = saturate(dot(
            detailWorldNormal, directionToPointLight));
        const float softPointLambert = SOFT_LAMBERT_BASE + pointLambert * SOFT_LAMBERT_RANGE;
        // 天井のパネルを裸の電球ではなく、下向きに広がる面の光源として近似している。
        // 床へ光を集めつつ、横の方向にも少しだけ光を残している。
        const float downwardAmount = saturate(directionToPointLight.y);
        const float fixtureDistribution = lerp(
            FIXTURE_SIDE_LIGHT,
            1.0f,
            smoothstep(FIXTURE_BEAM_DOT_START, FIXTURE_BEAM_DOT_END, downwardAmount));

        // 揺れる裸電球の光は、電球との間に物があれば届かない（pointShadow.hlsli）。
        // 物と触れている所の細い影（接触影。SSAOと一緒に求めた画像。screenAmbientOcclusion.hlsli）も掛けている。
        // 光源との間に建物の壁があれば、光は届かない（壁を抜けて隣の部屋を照らさないように。roomOcclusion.hlsli）
        const float3 pointRadiance = pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * pointAttenuation
            * fixtureDistribution
            * screenContactShadow
            * GetRoomLightVisibility(input.worldPos, pointLight.PositionRange.xyz,
                GetPixelLightWallMask(pointLightListOffset, i))
            * GetPointLightShadow(input.worldPos, input.worldNormal, pointLight.PositionRange.xyz)
            * GetUnseenFigureShadow(input.worldPos, pointLight.PositionRange.xyz)
            * GetPlayerBodyShadow(input.worldPos, pointLight.PositionRange.xyz);
        lighting += pointRadiance * softPointLambert;

#if !defined(PRINTED_SURFACE)
        // 鏡面反射は、懐中電灯と同じビュー空間で計算している（光の向きだけをビュー空間へ回している）
        const float3 viewSpaceDirectionToLight = normalize(mul(
            float4(directionToPointLight, 0.0f), View).xyz);
        specularLighting += pointRadiance * EvaluateSpecularGGX(
            surface, detailViewNormal, viewDirection, viewSpaceDirectionToLight);
#endif
    }

    // 部屋の角の暗がり。環境光と天井灯には全部、懐中電灯には一部だけ掛けている（照らせば角も見えるように）。
    // 画面空間の環境遮蔽（SSAO）も重ね、小物の下や物と物の隙間まで暗くしている（懐中電灯には一部だけ）
    const float screenOcclusion = GetScreenAmbientOcclusion(input.pos.xy);
#if defined(PRINTED_SURFACE)
    // 貼り付けた壁までの距離は配置時に分かるので、画素ごとに壁の一覧を走査しない。
    const float roomOcclusion = screenOcclusion * (OcclusionBoxCount > 0
        ? ComputeRoomOcclusion(input.worldPos, detailWorldNormal, Material.Ambuent.x) : 1.0f);
#elif defined(BREATHING_WALL_SURFACE)
    // 膨らんだ所は自分の壁の箱から離れるので、そのままでは「すぐ近くの別の壁（角）」と取り違えて強く暗くなる
    // （膨らみの形に沿った、格子状のギザギザの暗がりになっていた）。部屋の角の暗がりは、膨らむ前の面で求める
    const float roomOcclusion = GetRoomOcclusion(input.restPosition, input.restNormal) * screenOcclusion;
#else
    const float roomOcclusion = GetRoomOcclusion(input.worldPos, normalize(input.worldNormal)) * screenOcclusion;
#endif
    lighting *= roomOcclusion;
    specularLighting *= roomOcclusion;
    const float flashlightOcclusion = lerp(1.0f, roomOcclusion, FLASHLIGHT_ROOM_OCCLUSION);

#if !defined(PRINTED_SURFACE)
    // 環境の映り込み（IBL）：視線を面で反射した向きの景色を、環境マップから粗さに応じたぼやけ具合で読み、
    // 粗さと見る角度に応じた割合で足している。金属は拡散反射を持たないので、この映り込みで金属に見える
    // （非常灯や照明の光だまりが、配管や額縁に赤く・白く映る）。環境マップのない所でも、環境光だけは映している。
    {
        const float nDotV = saturate(dot(detailViewNormal, viewDirection));
        const float3 reflectedView = reflect(-viewDirection, detailViewNormal);
        // ビュー空間の向きをワールドへ戻している（回転の逆 = 転置）
        const float3 reflectedWorld = mul((float3x3)View, reflectedView);
        const float3 environment = max(
            GetHemisphereAmbient(reflectedWorld),
            SampleEnvironment(reflectedWorld, surface.roughness));
        specularLighting += environment
            * EnvironmentBrdfApprox(surface.f0, surface.roughness, nDotV)
            * GetSpecularOcclusion(nDotV, roomOcclusion, surface.roughness);
    }

#endif
    // 懐中電灯の光：円錐の形・距離による弱まり・レンズの模様・面の向き・影を掛け合わせている
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > LIGHT_EPSILON)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
        // 円錐の外では、レンズの汚れのノイズとシャドウマップの参照を丸ごと省いている。
        [branch]
        if (beamProfile > LIGHT_EPSILON)
        {
            const float normalizedDistance = saturate(
                distanceFromCamera / max(Light.Range, LIGHT_EPSILON));
            const float rangeFade = saturate(
                1.0f - normalizedDistance * normalizedDistance);
            const float attenuation = rangeFade * rangeFade;
            const float lensPattern = GetFlashlightLensPattern(pixelDirection);
            const float3 flashlightColor = Light.Diffuse.rgb * GetFlashlightTint(pixelDirection);
            // 実際の光のように距離の2乗で弱まる分を、少しだけ混ぜている
            const float physicalFalloff = rcp(
                1.0f + distanceFromCamera * distanceFromCamera * FLASHLIGHT_PHYSICAL_FALLOFF_SCALE);
            const float naturalAttenuation = attenuation *
                lerp(1.0f, physicalFalloff, FLASHLIGHT_PHYSICAL_FALLOFF_BLEND);

            const float3 normal = detailViewNormal;
            const float3 directionToLight = -pixelDirection;
            const float lambert = saturate(dot(normal, directionToLight));
            // 横から当たる面も真っ暗にならず、正面の面も明るくなりすぎないよう、幅を抑えている（flashlightLighting.hlsli）
            const float softenedLambert = GetFlashlightDiffuseResponse(lambert);

            const float3 flashlightRadiance = flashlightColor
                * Light.Intensity
                * beamProfile
                * naturalAttenuation
                * lensPattern
                * GetFlashlightShadow(input.shadowPos)
                * GetUnseenFigureFlashlightShadow(input.worldPos)
                * flashlightOcclusion;
            lighting += flashlightRadiance * softenedLambert;
#if !defined(PRINTED_SURFACE)
            // 懐中電灯は目のすぐそばにあるので、金属や濡れた面を正面から照らすと、光の芯がはっきり映る
            specularLighting += flashlightRadiance * EvaluateSpecularGGX(
                surface, normal, viewDirection, directionToLight);
#endif
        }
    }

    // 懐中電灯の翼の光：明るい中心の外にも広がる、ぼんやりした弱い光（flashlightLighting.hlsli）
    lighting += GetFlashlightSpillLight(input.viewPos, detailViewNormal) * flashlightOcclusion;

#if !defined(PRINTED_SURFACE)
    // 浸水した床（西棟）や、波の立った水たまりの水面で跳ね返った懐中電灯の光が、壁や天井にゆらゆら映る（waterCaustics.hlsli）
    lighting += Light.Diffuse.rgb * GetWaterCausticLight(input.worldPos, normalize(input.worldNormal));

    // 拡散反射（金属ほど弱い）と鏡面反射を合わせている。湿った壁の冷たい光沢も、濡れて粗さが下がった鏡面反射として出ている。
    // 技術デモでPBRを止めているときは、これまでの拡散反射だけの描き方にしている（金属も色のまま、鏡面反射なし）
    color.rgb = PointShadowOptions.y > PBR_DISABLED_THRESHOLD
        ? color.rgb * lighting
        : surface.diffuseColor * lighting + specularLighting;
#else
    // つや消しの印刷物は、受けた光を紙面の色に掛ける。
    color.rgb *= lighting;
#endif

    // 自ら光る色を足している。0〜1の画面に出したい明るさで決めてあるので、トーンマッピングの後にその明るさで見えるHDRの値に直している（toneMapping.hlsli）
    color.rgb += InverseToneMapEmission(Material.Emission.rgb + paintingEmission);
    // 崩れている縁：焦げて黒ずみ、その外側が燃えるように赤く光る
    color.rgb *= 1.0f - dissolveEdge * DISSOLVE_CHAR_DARKEN;
    color.rgb += DISSOLVE_GLOW_COLOR * (dissolveEdge * dissolveEdge) * DISSOLVE_GLOW_STRENGTH;

    // 高さの違う霧を重ね、近くの見やすさを保ちながら、遠くの輪郭を分けている。
    // 部屋全体を一様に白くせず、床の近くに霧を集めている。
    // 2面の周回が進むほど、霧が近くから濃くなる（LoopDepth。horrorGrade.hlsli）
    const float distanceFog = GetHorrorDistanceFog(distanceFromCamera, LoopDepth);
    const float heightFromFloor = max(input.worldPos.y - SCENE_BASE_FLOOR_Y, 0.0f);
    const float heightDensity = exp(-heightFromFloor * HEIGHT_FOG_DECAY);
    const float nearSuppression =
        smoothstep(HEIGHT_FOG_NEAR_START, HEIGHT_FOG_NEAR_END, distanceFromCamera);
    const float fogVariation = HEIGHT_FOG_BASE_VARIATION +
        FastValueNoise(input.worldPos.xz * HEIGHT_FOG_NOISE_SCALE + HEIGHT_FOG_NOISE_OFFSET) * HEIGHT_FOG_NOISE_WEIGHT;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * HEIGHT_FOG_STRENGTH * GetHorrorHeightFogScale(LoopDepth);
    const float fogFactor = saturate(distanceFog + heightFog);
    // 遠くは灰色のもやではなく、闇に溶けていく
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    color.rgb = lerp(color.rgb, fogColor, fogFactor);
    // 床の近くに溜まって流れる霧（懐中電灯の円錐の中だけ白く浮かぶ。horrorMist.hlsli）
    color.rgb = ApplyHorrorMist(color.rgb, input.worldPos, input.viewPos, lighting);
    // 天井の照明や非常灯から下へ落ちる光の筋（霧に散った光。horrorMist.hlsli）
    color.rgb += GetLightShafts(input.worldPos, pointLightListOffset, pointLightCount, input.pos.xy);
    color.rgb = ApplyFilmicHorrorGrade(color.rgb, LoopDepth);

    // デバッグ表示：1=法線、2=懐中電灯の影、3=照明だけ、6=壁の湿り気、7=タイルごとの光源の数。4・5は床（水たまり・反射）用の表示なので、壁は黒にしている
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
    if (DebugViewMode == DEBUG_VIEW_WALL_DAMP)
    {
        return float4(saturate(grime).xxx, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_LIGHT_TILES)
    {
        return GetLightTileHeatmap(input.pos.xy, pointLightCount);
    }
    // 9 = PBRの材質（赤 = 金属らしさ、緑 = 粗さ、青 = 濡れ具合）
    if (DebugViewMode == DEBUG_VIEW_AMBIENT_OCCLUSION)
    {
        return float4(screenOcclusion.xxx, 1.0f);
    }
    if (DebugViewMode == DEBUG_VIEW_PBR_MATERIAL)
    {
        return float4(surface.metallic, surface.roughness, saturate(wetness), 1.0f);
    }
    if (DebugViewMode >= DEBUG_VIEW_PUDDLE)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // 音で輪郭が浮かぶ部屋：広がる音の輪が通る所と、通り過ぎた後の輪郭を光らせている（輪がないときは何も足さない）
    color.rgb += GetEchoReveal(input.worldPos, normalize(input.worldNormal));
    return color;
}

