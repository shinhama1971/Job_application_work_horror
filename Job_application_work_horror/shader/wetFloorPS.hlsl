// ============================================================================
// シェーダーの役割: 濡れた床を描いている（床の古さ、水たまりと浸水した範囲、波紋、足音と水滴の波（波のシミュレーション）、水面の反射、フレネル、懐中電灯と天井灯の照明、霧）。
// ============================================================================

#include "common.hlsl"

// 床のテクスチャ（t0）とサンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"
#include "pointShadow.hlsli"

// 反射の方式の比較で使うキューブマップ（t7、ReflectionProbe）。SSRの画面と深度（t4・t8）は rayReflection.hlsli で読んでいる
TextureCube g_ReflectionProbe : register(t7);

// Ground.cppのWetFloorBufferと同じ並び（b10）。経過時間、波紋の強さ、反射の強さ、水面の細かさ
// 濡れた足跡の最大数（WetFootprints::MaxCount と同じ）
#define FOOTPRINT_COUNT 32
// 天井の雨漏りの最大数（WaterEffectSystem::MaxLeakCount と同じ）
#define LEAK_COUNT 8
cbuffer WetFloorBuffer : register(b10)
{
    float WetTime;
    float RippleStrength;
    float ReflectionStrength;
    // 今描いている面の、水面の細かさ（Ground::DrawWaterSurfaces）。
    // 0 = 床の本体（乾いた床。水たまり・浸水・雨漏りは計算しない）、1〜3 = 水たまりのメッシュ（カメラから遠いほど小さい。WATER_LEVEL_*）
    float WaterSurfaceLevel;
    // 床一面が水に浸かった範囲（xy = x・zの最小、zw = x・zの最大）。範囲がないときは、最小が最大より大きい値にしている。
    float4 FloodRect;
    // 濡れた足跡（WetFootprints::Fill）。x = 数。1つにつき2つ（xy = 位置、z = sin(向き)、w = cos(向き)/ x = 濡れ具合、y = 左右、z = 裸足か）
    float4 FootprintInfo;
    float4 Footprints[FOOTPRINT_COUNT * 2];
    // 天井の雨漏りの真下の水たまり（Ground）。x = 数。1つにつき xy = 中心のx・z、z = 半径
    float4 LeakInfo;
    float4 Leaks[LEAK_COUNT];
}

// 水たまりのメッシュの細かさ（Ground::WaterLevel と同じ値）。メッシュ1つの中では一定なので、分岐は画素ごとにばらつかない
#define WATER_LEVEL_DRY_FLOOR 0
#define WATER_LEVEL_FAR 1      // 遠い：反射の光線を飛ばさず、霧の色を映す
#define WATER_LEVEL_MIDDLE 2   // 中くらい：部屋の形との交差だけ（画面の中をたどるSSRは省く）
#define WATER_LEVEL_NEAR 3     // 近い：SSRと部屋の形の交差の両方（これまでと同じ）

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
float Hash21(float2 value)
{
    return frac(sin(dot(value, float2(127.1f, 311.7f))) * 43758.5453f);
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

// ----------------------------------------------------------------------------
// 床の古さ（1面と2面）。壁の古さと同じ WallWeathering で有効になり、0の面では何もしない。
// コンクリートの床版の目地・ひびを凹凸として、油や水の染み・物を引きずった跡・壁際の埃を色として描いている。
// ----------------------------------------------------------------------------
struct FloorAgeing
{
    float height;   // 凹凸に足す高さ（へこみは負）
    float3 tint;    // 元の色に掛ける色（1なら変化なし）
};

// 床の古さを計算している（amountは古さの濃さ）
FloorAgeing ComputeFloorAgeing(float2 position, float amount)
{
    FloorAgeing ageing;
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている。
    const float footprint = max(length(fwidth(position)), 0.0001f);

    // --- 床版の目地（56四方。細く浅くして、タイル張りに見えないようにしている）。床版ごとに色をわずかに変えている
    const float slabSize = 56.0f;
    const float2 slabUV = position / slabSize;
    const float2 edgeDistance = (0.5f - abs(frac(slabUV) - 0.5f)) * slabSize;
    const float joint = 1.0f - smoothstep(0.05f, 0.20f + footprint, min(edgeDistance.x, edgeDistance.y));
    const float slabShade = FastHash21(floor(slabUV) + 11.0f);

    // --- ひび（遠くでは1画素より細くなるため、計算を省いている）
    float crack = 0.0f;
    [branch]
    if (footprint < 0.5f)
    {
        const float crackLine = 1.0f - smoothstep(0.0f, 0.014f + footprint * 0.04f,
            abs(FastValueNoise(position * 0.06f + 17.0f) - 0.5f));
        crack = crackLine * smoothstep(0.60f, 0.78f, FastValueNoise(position * 0.015f + 4.0f)) *
            saturate(1.0f - footprint / 0.35f);
    }

    // --- 油や水の染み（大きな黒ずみ）
    const float stain = smoothstep(0.50f, 0.82f, FastValueNoise(position * 0.03f + 31.0f)) *
        (0.6f + 0.4f * FastValueNoise(position * 0.2f + 7.0f));

    // --- 物を引きずった跡（一方向に伸びた細い筋。出る場所を別のノイズで絞っている）
    const float scuffLine = smoothstep(0.72f, 0.90f,
        FastValueNoise(float2(position.x * 0.04f, position.y * 0.9f) + 53.0f));
    const float scuff = scuffLine * smoothstep(0.50f, 0.70f, FastValueNoise(position * 0.012f + 2.0f)) *
        saturate(1.0f - footprint / 0.6f);

    // --- 壁際にたまった埃（部屋の角の暗がりと同じ壁の形から、壁までの距離を求めている）
    const float dust = exp(-GetNearestWallDistance(position) / 4.5f);

    // 目地とひびを、へこみの高さにしている
    ageing.height = -(joint * 0.5f + crack * 0.4f) * amount;

    // 床版ごとの色、目地・ひびの暗さ、染み、引きずった跡、埃を順に重ねている
    float3 tint = (0.93f + 0.12f * slabShade).xxx;
    tint *= 1.0f - joint * 0.22f;
    tint *= 1.0f - crack * 0.50f;
    tint = lerp(tint, tint * float3(0.55f, 0.52f, 0.48f), stain * 0.75f);
    tint = lerp(tint, tint * 1.18f, scuff * 0.50f);
    tint = lerp(tint, float3(1.30f, 1.25f, 1.15f), dust * 0.55f);
    ageing.tint = lerp(1.0f.xxx, tint, amount);
    return ageing;
}

// 床の古さを返している（古さが0なら何も計算しない）
FloorAgeing GetFloorAgeing(float3 worldPosition, float amount)
{
    FloorAgeing ageing;
    ageing.height = 0.0f;
    ageing.tint = 1.0f.xxx;
    // amount は面ごとに一定なので、この分岐は画素ごとにばらつかない。
    [branch]
    if (amount > 0.0f)
    {
        ageing = ComputeFloorAgeing(worldPosition.xz, amount);
    }
    return ageing;
}

// ホラー向けの色調と、遠くの闇の色（litTexturePSと共通。horrorGrade.hlsli）
#include "horrorGrade.hlsli"
#include "horrorMist.hlsli"
#include "figureShadow.hlsli"
#include "rayReflection.hlsli"
#include "figureVolume.hlsli"

// 2面の「水たまり」の異変の人影（PuddleFigure。xyz = 足もと、w = 見え方）が、反射の光線をどれだけ遮るか（0〜1）を返している。
// 人影のモデルと同じ形の距離場に光線を通し（figureVolume.hlsli）、形に一番近づいた距離から、輪郭のぼけた影の濃さを求めている。
// 人影は目の方を向き、頭が足もとから36の高さになる大きさにしている（PuddleReflectionAnomaly::FigureHeadHeight と合わせている）。
// 物として置いていないので、画面にも鏡にも映らず、レイ反射の水面にだけ映る（PuddleReflectionAnomaly.h）
// PuddleFigure.w が負のときは、1面の西棟の天井に逆さまに張り付いた人影（足もとが天井で、頭が下）として、同じ形を上下逆に描いている
float TracePuddleFigure(float3 origin, float3 direction)
{
    const float visibility = abs(PuddleFigure.w);
    [branch]
    if (visibility <= 0.001f)
    {
        return 0.0f;
    }
    const float3 foot = PuddleFigure.xyz;
    // 足もとから頭への向き（床に立つ人影は上、天井に張り付いた人影は下）
    const float upSign = PuddleFigure.w < 0.0f ? -1.0f : 1.0f;
    // 人影を囲む球（中心は胸の高さ、半径28）のそばを通らない光線は、点と光線の距離だけで先に飛ばしている
    const float3 toCenter = foot + float3(0.0f, upSign * 19.0f, 0.0f) - origin;
    const float alongCenter = max(dot(toCenter, direction), 0.0f);
    const float3 missCenter = toCenter - direction * alongCenter;
    [branch]
    if (dot(missCenter, missCenter) > 28.0f * 28.0f)
    {
        return 0.0f;
    }
    // 人影の空間（前 = 目の方、右、上）へ光線を移し、大きさで割っている。天井の人影は上下を反転している
    const float scale = 36.0f / FIGURE_VOLUME_HEAD_HEIGHT;
    const float2 toEye = GetMistEyePosition().xz - foot.xz;
    const float3 forward = normalize(float3(toEye.x, 0.0f, toEye.y) + float3(1e-4f, 0.0f, 0.0f));
    const float3 right = float3(forward.z, 0.0f, -forward.x);
    const float3 fromFoot = (origin - foot) / scale;
    const float3 localOrigin = float3(dot(fromFoot, right), fromFoot.y * upSign, dot(fromFoot, forward));
    const float3 localDirection = float3(dot(direction, right), direction.y * upSign, dot(direction, forward));
    float depth;
    return TraceFigureVolume(localOrigin, localDirection, 1.6f, depth) * saturate(visibility);
}

// レイ反射の本体：水面で反射した光線を、まず画面の中（SSR）でたどり、外れた所を部屋の形との交差で埋めている。
// SSRは深度プリパスの深度と前のフレームの画面を使うため、それが用意されているとき（ReflectionParams.y）だけ使っている。
// figure は「水たまり」の異変の人影が光線を遮った度合い（0〜1）で、床の色そのものも沈めるのに使っている。
// level は水たまりのメッシュの細かさ（WATER_LEVEL_*）。遠い水たまりほど、映る物が小さく霧に沈むので、計算を省いている
float3 TraceRayReflection(float3 worldPosition, float3 worldNormal, float roughness, int level, out float figure)
{
    figure = 0.0f;
    // 遠い：光線を飛ばさず、遠くの闇（霧）の色を映している
    [branch]
    if (level <= WATER_LEVEL_FAR)
    {
        return GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    }
    float3 direction = reflect(normalize(worldPosition - GetMistEyePosition()), normalize(worldNormal));
    // 波の法線で光線が床へ潜らないよう、少しだけ上へ向けている
    direction = normalize(float3(direction.x, max(direction.y, 0.02f), direction.z));
    float roomDistance;
    // 画面の外の物は、部屋の形に当たった位置から、環境マップ（本物の景色）の色を読んでいる（rayReflection.hlsli）
    float3 color = TraceRoomReflection(worldPosition + float3(0.0f, 0.05f, 0.0f), direction, roughness, roomDistance, true);
    // 画面の中をたどるSSRは、近い水たまりだけにしている（中くらいの距離では、部屋の形との交差だけ）
    [branch]
    if (level >= WATER_LEVEL_NEAR && ReflectionParams.y > 0.5f)
    {
        // 近い水たまりは、画面の中を細かい歩幅で多めにたどっている（32歩、1歩ごとに歩幅を1.14倍）。
        // 歩幅が粗いと光線が物をすり抜けて外れ、部屋の形の簡単な色で埋めることになり、映り込みが濁るため。
        // 映る物が水面から離れているほど、前のフレームの画面の縮小画像のぼやけた段を読んでいる（距離60で1段、最大4段）。
        // 粗い水面（浅い縁や波の強い所）は、最初から少しぼかしている
        // 水面より1.5以上高い物に当たったときだけ映し、床そのものへの誤った当たりは捨てている（かくかくした映り込みを防ぐ）
        const float4 screen = TraceScreenSpaceReflection(worldPosition, worldNormal, 32, 1.14f,
            1.0f / 60.0f, roughness * 1.2f, 1.5f);
        color = lerp(color, screen.rgb, screen.w);
    }
    // 人影は画面に描いていないため、SSRで拾った物より手前に立っているものとして、最後に重ねている（ほぼ黒い影）。
    // worldNormal は呼ぶ側で、すでに水面の揺れを3割に抑えた向きにしている（人の形が崩れないよう、人影も同じ向きで映す）
    const float3 calmNormal = normalize(worldNormal);
    float3 figureDirection = reflect(normalize(worldPosition - GetMistEyePosition()), calmNormal);
    figureDirection = normalize(float3(figureDirection.x, max(figureDirection.y, 0.02f), figureDirection.z));
    figure = TracePuddleFigure(worldPosition + float3(0.0f, 0.05f, 0.0f), figureDirection);
    color = lerp(color, GetHorrorFogColor(Light.Ambient.rgb, LoopDepth) * 0.12f, figure);
    return color;
}

// 2次元の値から、0〜1の乱数を2つ作っている
float2 Hash22(float2 value)
{
    const float first = Hash21(value + float2(17.3f, 41.7f));
    const float second = Hash21(value + float2(93.1f, 11.8f));
    return float2(first, second);
}

// 水たまりに落ちる水滴の、広がる輪を計算している（0〜1）
float GetDripRing(float2 worldPosition)
{
    // 水滴はまばらに起こし、雨には見せずに、水たまりへ小さな動きを加えている。
    const float cellSize = 15.0f;
    const float2 cell = floor(worldPosition / cellSize);
    const float2 localPosition = frac(worldPosition / cellSize);
    const float2 dropPosition = 0.18f + Hash22(cell) * 0.64f;
    const float dropSeed = Hash21(cell + float2(31.7f, 19.3f));
    const float activeDrop = step(0.76f, dropSeed);
    const float phase = frac(WetTime * 0.11f + dropSeed);
    const float radius = phase * 0.48f;
    const float distanceToDrop = length(localPosition - dropPosition);
    const float ring = 1.0f - smoothstep(
        0.012f,
        0.035f,
        abs(distanceToDrop - radius));
    return ring * activeDrop * (1.0f - phase);
}

// 床の画素の色を計算している：床の色と古さ → 水たまり → 照明 → 光沢 → 反射 → 霧 → 色調
float4 main(in LIT_PS_IN input) : SV_Target
{
    float4 color = input.col;
    // 床の古さ（目地・ひび）の凹凸を入れた法線。古さのない面では元の法線のまま。
    float3 agedWorldNormal = normalize(input.worldNormal);

    if (Material.TextureEnable)
    {
        // 元の画像の草のような細かい模様を、正のMipバイアスで弱めようとしている。
        // 色の幅を抑えて、湿った汚いコンクリートに見せ、素材を足さずに済ませている。
        const float3 sampledFloor = g_Texture.SampleBias(
            g_SamplerState,
            input.tex,
            1.15f).rgb;
        // 1面（床の古さあり）は、素材の明るさを平均（field.jpgで約0.40）へ寄せて砂利のようなざらつきを抑え、
        // 床の模様は目地・染み・引きずった跡（GetFloorAgeing）で作っている。
        // 素材にMipマップがないため、Mipバイアスではぼかせない。
        const float floorLuminance = lerp(
            dot(sampledFloor, float3(0.2126f, 0.7152f, 0.0722f)),
            0.40f,
            0.65f * WallWeathering);
        float3 concreteFloor = lerp(
            floorLuminance.xxx,
            sampledFloor,
            0.10f);
        // 広い範囲の明るさのむらを付け、少し緑がかった灰色にしている
        const float broadVariation = saturate(FastValueNoise(
            input.worldPos.xz * 0.020f + 6.4f));
        concreteFloor *= float3(0.72f, 0.75f, 0.73f) *
            lerp(0.88f, 1.04f, broadVariation);
        color.rgb *= concreteFloor;

        // 床の古さ（WallWeathering が0より大きい面だけ）。色を掛け、目地とひびの凹凸で法線を作っている。
        // TextureEnable は描画の単位で一定なので、この中で画面上の偏微分を使っても問題ない。
        const FloorAgeing floorAgeing = GetFloorAgeing(input.worldPos, WallWeathering);
        color.rgb *= floorAgeing.tint;
        agedWorldNormal = ApplyHeightToNormal(
            input.worldPos, agedWorldNormal, floorAgeing.height, 0.9f);
    }
    else
    {
        color *= Material.Diffuse;
    }

    if (DebugViewMode == DEBUG_VIEW_FULLBRIGHT)
    {
        return GetFullbrightColor(color.rgb, input.viewNormal, input.viewPos);
    }

    // 水たまりの濃さ・岸の線・波の模様を求めている。
    // 床の本体（WaterSurfaceLevel が0）は乾いた床として描き、水たまり・浸水・雨漏りは、その形を覆うメッシュ（Ground::DrawWaterSurfaces）が描き重ねる。
    // メッシュは床より少し上にあり、深度プリパスにも描くので、メッシュの下の床の画素はこの計算まで来ない
    const int waterLevel = (int)(WaterSurfaceLevel + 0.5f);
    float shore = 0.0f;
    float ripplePattern = 0.0f;
    float puddle = 0.0f;
    [branch]
    if (waterLevel > WATER_LEVEL_DRY_FLOOR)
    {
        puddle = GetPuddleMask(
            input.worldPos.xz,
            WetTime,
            shore,
            ripplePattern);
        // 床一面が水に浸かった範囲（1面の西棟）。範囲の内側は全面を水たまりと同じに扱い、ゆるい波を立てている。
        {
            const float2 fromMinimum = input.worldPos.xz - FloodRect.xy;
            const float2 toMaximum = FloodRect.zw - input.worldPos.xz;
            const float insideDistance = min(
                min(fromMinimum.x, fromMinimum.y), min(toMaximum.x, toMaximum.y));
            const float flood = smoothstep(-0.5f, 1.5f, insideDistance);
            ripplePattern += sin(input.worldPos.x * 0.21f + input.worldPos.z * 0.13f + WetTime * 0.5f) *
                0.35f * flood * (1.0f - puddle);
            puddle = max(puddle, flood);
        }
        // 天井の雨漏りの真下にできた小さな水たまり（少しずつ広がる。縁はノイズでゆがめ、落ちてくる水滴の輪が広がる）
        {
            const int leakCount = (int)(LeakInfo.x + 0.5f);
            [loop]
            for (int leakIndex = 0; leakIndex < leakCount; ++leakIndex)
            {
                const float4 leak = Leaks[leakIndex];
                const float2 leakOffset = input.worldPos.xz - leak.xy;
                const float reach = leak.z + 3.0f;
                if (dot(leakOffset, leakOffset) > reach * reach)
                {
                    continue;
                }
                const float leakDistance = length(leakOffset);
                const float edgeNoise = (FastValueNoise(input.worldPos.xz * 0.35f + leak.xy) - 0.5f) * 0.35f;
                const float normalizedDistance = leakDistance / max(leak.z, 0.01f) + edgeNoise;
                const float leakPuddle = 1.0f - smoothstep(0.80f, 1.0f, normalizedDistance);
                shore = max(shore, (1.0f - smoothstep(0.0f, 0.08f, abs(normalizedDistance - 0.97f))) * 0.6f);
                ripplePattern += sin(leakDistance * 2.2f - WetTime * 2.6f) * 0.30f * leakPuddle * exp(-leakDistance * 0.25f);
                puddle = max(puddle, leakPuddle);
            }
        }
    }
    // 上を向いた面（床）だけを水たまりにしている
    puddle *= smoothstep(0.55f, 0.92f, saturate(input.worldNormal.y));
    shore *= smoothstep(0.55f, 0.92f, saturate(input.worldNormal.y));
    // 濡れた足跡（水たまりの外の乾いた床だけ。WetFootprints が場所と濡れ具合を渡している）
    const float footprintWet = GetWetFootprints(input.worldPos.xz) *
        smoothstep(0.55f, 0.92f, saturate(input.worldNormal.y)) * (1.0f - saturate(puddle * 2.0f));

    float dripRing = 0.0f;
    float3 detailWorldNormal = agedWorldNormal;
    // 足音や水滴でできた波（WaterRippleSimulation）の高さ（x）と傾き（yz）
    float3 rippleWave = 0.0f;
    // 動く波の法線を作り直す処理は、水たまりと細い岸だけにしている。
    // 乾いた床では、3回のノイズの計算を省いている。
    [branch]
    if (puddle > 0.001f || shore > 0.001f)
    {
        dripRing = GetDripRing(input.worldPos.xz) * puddle;
        ripplePattern += dripRing * 1.35f;
        const float2 animatedNoiseOffset = float2(
            WetTime * 0.018f,
            -WetTime * 0.013f);
        // 少しずつ流れるノイズの傾きから、波の凹凸の法線を作っている
        const float ripple = FastValueNoise(
            input.worldPos.xz * 0.095f + animatedNoiseOffset);
        const float rippleX = FastValueNoise(
            input.worldPos.xz * 0.095f + animatedNoiseOffset +
            float2(0.035f, 0.0f));
        const float rippleZ = FastValueNoise(
            input.worldPos.xz * 0.095f + animatedNoiseOffset +
            float2(0.0f, 0.035f));
        // 水たまりの中は、目地やひびが水で埋まるため、平らな面から波の凹凸を作っている。
        const float3 puddleBaseNormal = normalize(lerp(
            agedWorldNormal, normalize(input.worldNormal), saturate(puddle * 1.5f)));
        detailWorldNormal = normalize(
            puddleBaseNormal +
            float3(ripple - rippleX, 0.0f, ripple - rippleZ) *
            (0.72f + abs(ripplePattern) * 0.55f) * puddle * RippleStrength);

        // 足音や水滴でできた本物の波。広がって重なり、岸で跳ね返る輪の傾きで法線を傾け、
        // 波の山を光らせている（波の模様に足すと、下の光沢の計算で輪が光る）。
        rippleWave = GetRippleHeightAndSlope(input.worldPos.xz) * puddle;
        ripplePattern += rippleWave.x * 1.4f;
        detailWorldNormal = normalize(
            detailWorldNormal + float3(-rippleWave.y, 0.0f, -rippleWave.z) * 1.6f);
    }

    const float baseLuminance = dot(
        color.rgb,
        float3(0.2126f, 0.7152f, 0.0722f));
    // 浅い室内の水は、上から見たときに床を透かして見せる。
    // 暗くしすぎて、黒いシールのように見えるのを防いでいる。
    const float3 wetColor = lerp(
        color.rgb * 0.76f,
        color.rgb * 0.58f +
            float3(0.018f, 0.030f, 0.034f) + baseLuminance.xxx * 0.035f,
        0.38f);
    color.rgb = lerp(color.rgb, wetColor, puddle * 0.84f);
    color.rgb *= 1.0f - shore * 0.035f;
    // 濡れた足跡は、水を吸って暗くなっている
    color.rgb = lerp(color.rgb, color.rgb * 0.38f + float3(0.004f, 0.007f, 0.008f), footprintWet * 0.90f);

    // 上下で色を変えた環境光と、浅い角度ほど強い反射（フレネル）
    float3 lighting = GetHemisphereAmbient(detailWorldNormal);
    float3 specularLighting = 0.0f;
    const float distanceFromCamera = length(input.viewPos);
    const float3 viewDirection = distanceFromCamera > 0.001f
        ? -input.viewPos / distanceFromCamera
        : float3(0.0f, 1.0f, 0.0f);
    const float fresnel = pow(
        1.0f - saturate(dot(normalize(input.viewNormal), viewDirection)),
        4.0f);

    // 天井の点光源は、パネルだけでなく、近くの床と壁も照らしている。
    uint pointLightListOffset;
    const uint pointLightCount =
        GetPixelLightCount(input.pos.xy, pointLightListOffset);
    [loop]
    for (uint i = 0; i < pointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight =
            GetPixelLight(pointLightListOffset, i);

        const float3 offsetToLight =
            pointLight.PositionRange.xyz - input.worldPos;
        const float lightRange = max(pointLight.PositionRange.w, 0.001f);
        const float distanceSquaredToLight = dot(offsetToLight, offsetToLight);
        // 光が届かない距離の光源は、計算を省いている
        [branch]
        if (distanceSquaredToLight >= lightRange * lightRange)
        {
            continue;
        }
        const float distanceToLight = sqrt(distanceSquaredToLight);
        const float3 directionToPointLight =
            offsetToLight / max(distanceToLight, 0.001f);
        float pointAttenuation = saturate(1.0f - distanceToLight / lightRange);
        pointAttenuation *= pointAttenuation;
        // 揺れる裸電球の光は、電球との間に物があれば届かない（床に落ちる、揺れる影。pointShadow.hlsli）
        pointAttenuation *= GetPointLightShadow(input.worldPos, input.worldNormal, pointLight.PositionRange.xyz);
        // 見えない影の異変では、姿の見えない人影が、この光源の光を遮って床に影だけを落とす（figureShadow.hlsli）
        pointAttenuation *= GetUnseenFigureShadow(input.worldPos, pointLight.PositionRange.xyz) *
            GetPlayerBodyShadow(input.worldPos, pointLight.PositionRange.xyz);

        const float pointLambert = saturate(dot(
            detailWorldNormal, directionToPointLight));
        const float softPointLambert = 0.20f + pointLambert * 0.80f;
        // 天井のパネルを裸の電球ではなく、下向きに広がる面の光源として近似している。
        // 床へ光を集めつつ、横の方向にも少しだけ光を残している。
        const float downwardAmount = saturate(directionToPointLight.y);
        const float fixtureDistribution = lerp(
            0.22f,
            1.0f,
            smoothstep(0.04f, 0.72f, downwardAmount));

        lighting += pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * pointAttenuation
            * softPointLambert
            * fixtureDistribution;

        // 水たまりでは、点光源の鋭い光沢を足している
        specularLighting += pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * pointAttenuation
            * pow(pointLambert, 12.0f)
            * puddle * 0.58f;

        // 柔らかい光の範囲を作り、天井の照明が水面へ映っていることを分かりやすくしている。
        const float horizontalDistance = length(offsetToLight.xz);
        const float reflectedFixture = pow(saturate(
            1.0f - horizontalDistance / max(lightRange * 0.46f, 0.001f)),
            4.5f);
        specularLighting += pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * reflectedFixture * puddle * 0.38f;
    }

    // 部屋の角の暗がり。環境光と天井灯には全部、懐中電灯には一部だけ掛けている（照らせば角も見えるように）。
    // 画面空間の環境遮蔽（SSAO）も重ね、柱や棚の根元の床まで暗くしている（懐中電灯には一部だけ）
    const float screenOcclusion = GetScreenAmbientOcclusion(input.pos.xy);
    const float roomOcclusion = GetRoomOcclusion(input.worldPos, normalize(input.worldNormal)) * screenOcclusion;
    lighting *= roomOcclusion;
    const float flashlightOcclusion = lerp(1.0f, roomOcclusion, 0.4f);

    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > 0.001f)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
        // 円錐の外の床では、レンズの汚れ・影・鏡面反射の計算を行っていない。
        [branch]
        if (beamProfile > 0.001f)
        {
            const float normalizedDistance = saturate(
                distanceFromCamera / max(Light.Range, 0.001f));
            const float rangeFade = saturate(
                1.0f - normalizedDistance * normalizedDistance);
            const float attenuation = rangeFade * rangeFade;
            const float lensPattern = GetFlashlightLensPattern(pixelDirection);
            const float3 flashlightColor = Light.Diffuse.rgb * GetFlashlightTint(pixelDirection);
            const float physicalFalloff = rcp(
                1.0f + distanceFromCamera * distanceFromCamera * 0.000018f);
            const float naturalAttenuation = attenuation *
                lerp(1.0f, physicalFalloff, 0.32f);

            // 1面は目地・ひび・水の波の凹凸を懐中電灯にも反映している（2面は前と同じく平らな面で計算）。
            const float3 normal = WallWeathering > 0.0f
                ? normalize(mul(float4(detailWorldNormal, 0.0f), View).xyz)
                : normalize(input.viewNormal);
            const float3 directionToLight = -pixelDirection;
            const float lambert = saturate(dot(normal, directionToLight));
            const float softenedLambert = 0.25f + lambert * 0.75f;

            const float shadow = GetFlashlightShadow(input.shadowPos) * GetUnseenFigureFlashlightShadow(input.worldPos);
            const float flashlightAmount = Light.Intensity
                * beamProfile * naturalAttenuation * lensPattern * shadow * flashlightOcclusion;

            lighting += flashlightColor
                * flashlightAmount
                * softenedLambert;

            // 濡れた面の鏡面反射（水たまりほど鋭く強い）
            const float3 halfVector = normalize(directionToLight + viewDirection);
            const float wetSpecular = pow(
                saturate(dot(normal, halfVector)),
                lerp(18.0f, 92.0f, puddle));
            specularLighting += flashlightColor
                * flashlightAmount
                * wetSpecular
                * lerp(0.025f, 1.15f, puddle);
            // 濡れた足跡も、懐中電灯の光を細く返す
            specularLighting += flashlightColor
                * flashlightAmount
                * pow(saturate(dot(normal, halfVector)), 48.0f)
                * 0.55f * footprintWet;

            // 完全な鏡面反射の方向がカメラを外れても、浅い水は懐中電灯を弱く広く反射する。
            // 水面が黒く沈むのを防いでいる。
            specularLighting += flashlightColor
                * flashlightAmount
                * puddle
                * (0.045f + fresnel * 0.12f);

            // 足音や水滴の波：傾いた斜面が懐中電灯の光をきらっと返し、照らした所に光の輪が広がって見える
            specularLighting += flashlightColor
                * flashlightAmount
                * saturate(length(rippleWave.yz) * 3.0f)
                * 0.40f;
        }
    }
    // 懐中電灯の翼の光：明るい中心の外にも広がる、ぼんやりした弱い光（flashlightLighting.hlsli）
    lighting += GetFlashlightSpillLight(input.viewPos, normalize(input.viewNormal)) * flashlightOcclusion;

    color.rgb *= lighting;
    // 水面の空の映り込み・波の光・水滴の輪・岸の光を足している
    specularLighting += float3(0.19f, 0.25f, 0.28f)
        * puddle * (0.15f + fresnel * 0.76f);
    specularLighting += float3(0.08f, 0.10f, 0.11f) * footprintWet * (0.06f + fresnel * 0.55f);
    const float rippleHighlight = pow(
        saturate(ripplePattern * 0.5f + 0.5f),
        10.0f) * puddle;
    specularLighting += float3(0.12f, 0.16f, 0.17f) * rippleHighlight * 0.22f;
    // 足音や水滴の波の、傾いた斜面が光を返して、輪が細く光る
    // （懐中電灯が当たっていない所でも、暗い中でうっすら輪が分かる程度に光らせている）
    specularLighting += float3(0.07f, 0.09f, 0.10f) *
        saturate(length(rippleWave.yz) * 3.0f) * (0.30f + fresnel * 0.70f);
    specularLighting += float3(0.28f, 0.34f, 0.35f)
        * dripRing * 0.20f * RippleStrength;
    specularLighting += float3(0.10f, 0.13f, 0.125f)
        * shore * (0.24f + fresnel * 0.46f);
    color.rgb += specularLighting;
    color.rgb += Material.Emission.rgb;

    // 水面のむら：浮いた埃や油膜を、ゆっくり流れる2つのノイズで表している。
    // filmは水面の荒さ（反射のぼけ）に、grimeは映り込みの強さに使い、場所ごとに映り方が変わるようにしている。
    const float2 filmFlow = float2(WetTime * 0.006f, -WetTime * 0.004f);
    const float film = FastValueNoise(input.worldPos.xz * 0.045f + filmFlow);
    const float grime = FastValueNoise(input.worldPos.xz * 0.021f - filmFlow * 0.5f + 37.0f);
    // 荒さ（0=鏡、1=ぼやけた反射）：浅い縁（水たまりの濃さが低い所）と、波が強い所ほど荒くしている
    const float waterRoughness = saturate(
        0.18f + film * 0.50f +
        abs(ripplePattern) * 0.22f +
        (1.0f - saturate(puddle * 1.6f)) * 0.35f);

    // フレネル効果により、真上からは主に水面の下の床を見せている。
    // 浅い角度から見ると、鏡に映った部屋と照明がはっきり見える。
    // 平方根で変換して、中くらいの角度の反射を見やすくしている。
    // 最小値を低く保ち、水たまりが黒い板に戻るのを防いでいる。
    // 汚れた所（grimeが低い所）は、映り込みを最大で4割弱めている。
    const float viewAngleReflection = sqrt(saturate(fresnel));
    // 反射の方式の比較：キューブマップ・SSR・レイ反射の色と映る度合いを、ここで求めている（反射なしのときは映さない）
    const int reflectionMode = int(ReflectionParams.x + 0.5f);
    float4 alternateReflection = 0.0f;
    // 反射の光線を飛ばす向きは、穏やかな水面の向き（真上）に、波の傾きを3割だけ足したものにしている。
    // 細かい波の模様・足音の波・床の凹凸をそのまま使うと、映る景色が細かく乱れて濁って見えるため。
    // 平らな水面で映した景色を、波の分だけ少し揺らす形になり、映る物の形が崩れずに読める。
    // 光沢と、波の山が光る輪には、元の波の向き（detailWorldNormal）をそのまま使っている
    const float3 reflectionNormal = normalize(lerp(float3(0.0f, 1.0f, 0.0f), detailWorldNormal, 0.30f));
    // 「水たまり」の異変の人影が、反射の光線を遮った度合い（レイ反射のときだけ求めている）
    float puddleFigure = 0.0f;
    [branch]
    if (reflectionMode != 0 && puddle > 0.001f)
    {
        if (reflectionMode == 1)
        {
            alternateReflection = float4(SampleProbeReflection(input.worldPos, reflectionNormal), 1.0f);
        }
        else if (reflectionMode == 2)
        {
            alternateReflection = TraceScreenSpaceReflection(input.worldPos, reflectionNormal);
        }
        else
        {
            alternateReflection = float4(
                TraceRayReflection(input.worldPos, reflectionNormal, waterRoughness, waterLevel, puddleFigure), 1.0f);
        }
    }
    // 映り込みの強さ：水たまりの濃さ・光線が当たった度合い・見る角度（フレネル）・反射の強さの設定・水面の汚れを掛け合わせている。
    // 見る角度は、浅い角度でほぼ鏡（98%）、真上から見ても4割は映して、水たまりが床と同じに見えないようにしている。
    // 浮いた埃や油膜のむら（grime）が多い所は、映り込みを最大2割弱めている
    const float reflectionStrength = saturate(
        puddle * alternateReflection.w *
        lerp(0.40f, 0.98f, viewAngleReflection) * ReflectionStrength *
        lerp(0.80f, 1.0f, smoothstep(0.25f, 0.75f, grime)));
    float3 reflectedScene = 0.0f;
    [branch]
    if (reflectionStrength > 0.001f)
    {
        reflectedScene = alternateReflection.rgb;
    }
    const float reflectionGain = 1.10f + rippleHighlight * 0.08f;
    color.rgb = lerp(
        color.rgb,
        reflectedScene * reflectionGain + float3(0.023f, 0.032f, 0.037f),
        reflectionStrength);
    // 真上から見ると反射は弱い（フレネル）ため、人影が映る所は床の色ごと沈め、水面の黒い人の形として見えるようにしている
    color.rgb *= 1.0f - puddleFigure * saturate(puddle * 1.4f) * 0.82f;

    // 高さの違う霧を重ね、近くの見やすさを保ちながら、遠くの輪郭を分けている。
    // 部屋全体を一様に白くせず、床の近くに霧を集めている。
    // 2面の周回が進むほど、霧が近くから濃くなる（LoopDepth。horrorGrade.hlsli）
    const float distanceFog = GetHorrorDistanceFog(distanceFromCamera, LoopDepth);
    const float heightFromFloor = max(input.worldPos.y + 100.0f, 0.0f);
    const float heightDensity = exp(-heightFromFloor * 0.055f);
    const float nearSuppression =
        smoothstep(35.0f, 150.0f, distanceFromCamera);
    const float fogVariation = 0.78f +
        saturate(FastValueNoise(input.worldPos.xz * 0.018f + 21.0f)) * 0.22f;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * 0.22f * GetHorrorHeightFogScale(LoopDepth);
    const float fogFactor = saturate(distanceFog + heightFog);
    // 遠くは灰色のもやではなく、闇に溶けていく
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    color.rgb = lerp(color.rgb, fogColor, fogFactor);
    // 床の近くに溜まって流れる霧（懐中電灯の円錐の中だけ白く浮かぶ。horrorMist.hlsli）。
    // 水たまりのよく映る所は、霧を4割弱めている（霧を反射の上から全部かけると、ライトの中で映り込みが白く濁って見えなくなるため）
    const float3 beforeMist = color.rgb;
    color.rgb = ApplyHorrorMist(color.rgb, input.worldPos, input.viewPos, lighting);
    color.rgb = lerp(color.rgb, beforeMist, reflectionStrength * 0.40f);
    // 天井の照明や非常灯から下へ落ちる光の筋（霧に散った光。horrorMist.hlsli）
    color.rgb += GetLightShafts(input.worldPos, pointLightListOffset, pointLightCount, input.pos.xy);
    color.rgb = ApplyFilmicHorrorGrade(color.rgb, LoopDepth);

    // デバッグ表示：1=法線、2=懐中電灯の影、3=照明だけ、4=水たまりの濃さとメッシュの細かさ、5=反射の画像、7=タイルごとの光源の数（6は壁用なので黒）
    if (DebugViewMode == 1)
    {
        return float4(detailWorldNormal * 0.5f + 0.5f, 1.0f);
    }
    if (DebugViewMode == 2)
    {
        const float shadowVisibility =
            GetFlashlightShadow(input.shadowPos);
        return float4(shadowVisibility.xxx, 1.0f);
    }
    if (DebugViewMode == 3)
    {
        return float4(saturate(lighting * 0.5f), 1.0f);
    }
    if (DebugViewMode == 4)
    {
        // 水たまりのメッシュを細かさの色（近い=緑、中くらい=黄、遠い=赤）で薄く塗り、水たまりの濃さをその色で重ねている。床の本体は黒
        const float3 levelColor = waterLevel >= WATER_LEVEL_NEAR ? float3(0.25f, 1.0f, 0.30f)
            : waterLevel == WATER_LEVEL_MIDDLE ? float3(1.0f, 0.85f, 0.20f)
            : float3(1.0f, 0.30f, 0.25f);
        return waterLevel == WATER_LEVEL_DRY_FLOOR
            ? float4(0.0f, 0.0f, 0.0f, 1.0f)
            : float4(levelColor * (0.10f + puddle * 0.90f), 1.0f);
    }
    if (DebugViewMode == 5)
    {
        return float4(reflectedScene, 1.0f);
    }
    if (DebugViewMode == 7)
    {
        return GetLightTileHeatmap(input.pos.xy, pointLightCount);
    }
    if (DebugViewMode == DEBUG_VIEW_AMBIENT_OCCLUSION)
    {
        return float4(screenOcclusion.xxx, 1.0f);
    }
    if (DebugViewMode >= 6)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return color;
}

