// ============================================================================
// シェーダーの役割: 濡れた床を描いている（床の古さ、水たまりと浸水した範囲、波紋、水面の反射、フレネル、懐中電灯と天井灯の照明、霧）。
// ============================================================================

#include "common.hlsl"

// 床のテクスチャ（t0）とサンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"

// 水面の反射の画像（PlanarReflectionが描いたもの、t6）
Texture2D g_PlanarReflection : register(t6);

// Ground.cppのWetFloorBufferと同じ並び（b10）。経過時間、波紋の強さ、反射の強さ、詰め物
cbuffer WetFloorBuffer : register(b10)
{
    float WetTime;
    float RippleStrength;
    float ReflectionStrength;
    float WetPadding;
    // 床一面が水に浸かった範囲（xy = x・zの最小、zw = x・zの最大）。範囲がないときは、最小が最大より大きい値にしている。
    float4 FloodRect;
}

// マス目ごとの水たまりの表（b13）。WaterEffectSystem::Init がCPUで決めて渡している。
// x = 水たまりがあれば1、yz = 中心のずれと回転に使う乱数。マス目(PUDDLE_CELL_MIN, PUDDLE_CELL_MIN)から16x16マス。
// sin を使った乱数をシェーダーで計算すると、GPUの種類で sin の精度が違い、水たまりの場所が変わるため、表にしている。
#define PUDDLE_CELL_MIN (-8)
#define PUDDLE_CELL_COUNT 16
cbuffer PuddleCellBuffer : register(b13)
{
    float4 PuddleCells[PUDDLE_CELL_COUNT * PUDDLE_CELL_COUNT];
}

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
    float4 reflectionPos : TEXCOORD7;
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
#include "flashlightLighting.hlsli"
#include "roomOcclusion.hlsli"
#include "surfaceDetail.hlsli"


// ----------------------------------------------------------------------------
// 床の古さ（1面）。壁の古さと同じ WallWeathering で有効になり、2面（0）では何もしない。
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

// 映画のような色調にしている（litTexturePSと同じ処理）
float3 ApplyFilmicHorrorGrade(float3 color)
{
    color = max(color, 0.0f);
    const float3 acesColor = saturate(
        (color * (2.51f * color + 0.03f)) /
        (color * (2.43f * color + 0.59f) + 0.14f));
    color = lerp(color, acesColor, 0.28f);

    const float luminance = dot(
        color,
        float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luminance.xxx, color, 0.86f);
    const float shadowWeight =
        1.0f - smoothstep(0.08f, 0.42f, luminance);
    const float highlightWeight =
        smoothstep(0.38f, 0.90f, luminance);
    color *= lerp(
        1.0f.xxx,
        float3(0.91f, 0.96f, 1.035f),
        shadowWeight * 0.48f);
    color *= lerp(
        1.0f.xxx,
        float3(1.025f, 0.995f, 0.955f),
        highlightWeight * 0.36f);
    return saturate(color);
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
    const float phase = frac(WetTime * 0.18f + dropSeed);
    const float radius = phase * 0.48f;
    const float distanceToDrop = length(localPosition - dropPosition);
    const float ring = 1.0f - smoothstep(
        0.012f,
        0.035f,
        abs(distanceToDrop - radius));
    return ring * activeDrop * (1.0f - phase);
}

// その位置の水たまりの濃さ（0〜1）を返し、岸の線と波の模様も返している
float GetPuddleMask(
    float2 worldPosition,
    out float shore,
    out float ripplePattern)
{
    shore = 0.0f;
    ripplePattern = 0.0f;
    // 大きなワールド空間のマス目ごとに、不規則に回した水たまりを1つ置いている。
    // 半径をマス目の境目より小さくし、床全体を一様に濡らさず、独立した水たまりにしている。
    const float cellSize = 82.0f;
    const float2 gridPosition = worldPosition / cellSize;
    const float2 cell = floor(gridPosition);
    float2 localPosition = frac(gridPosition) - 0.5f;

    // このマス目の値を表から読んでいる（表の外の床は乾いている扱い）
    const int2 tableIndex = int2(cell) - PUDDLE_CELL_MIN;
    float4 cellValue = 0.0f;
    if (all(tableIndex >= 0) && all(tableIndex < PUDDLE_CELL_COUNT))
    {
        cellValue = PuddleCells[tableIndex.y * PUDDLE_CELL_COUNT + tableIndex.x];
    }
    const float hasPuddle = cellValue.x;
    // 大半の床のマス目は乾いているため、対象外のマス目では、回転・輪郭のノイズ・波の計算を省いている。
    // （この分岐はワールド空間の大きなマス目の単位でそろうので、画素ごとにばらつかない）
    if (hasPuddle < 0.5f)
    {
        shore = 0.0f;
        ripplePattern = 0.0f;
        return 0.0f;
    }

    // 水たまりの中心をずらし、ランダムに回した楕円にしている
    const float2 randomValue = cellValue.yz;
    localPosition -= (randomValue - 0.5f) * 0.22f;

    const float angle = randomValue.x * 6.2831853f;
    const float cosine = cos(angle);
    const float sine = sin(angle);
    const float2 rotated = float2(
        localPosition.x * cosine - localPosition.y * sine,
        localPosition.x * sine + localPosition.y * cosine);
    const float2 axes = float2(
        0.72f + randomValue.x * 0.25f,
        0.43f + randomValue.y * 0.20f);

    // 縁をノイズと波形でゆがませ、自然な形の水たまりにしている
    const float radialDistance = length(rotated / axes);
    const float polarAngle = atan2(rotated.y, rotated.x);
    const float organicLobes =
        sin(polarAngle * 5.0f + randomValue.x * 9.0f) * 0.045f +
        sin(polarAngle * 9.0f + randomValue.y * 13.0f) * 0.022f;
    const float edgeWarp =
        (FastValueNoise(worldPosition * 0.052f + cell * 1.73f) - 0.5f) * 0.22f +
        organicLobes;
    const float irregularDistance = radialDistance + edgeWarp;

    // 水たまりの濃さ、岸の細い線、ゆっくり広がる波と交差する波の模様
    const float puddle =
        (1.0f - smoothstep(0.34f, 0.40f, irregularDistance)) * hasPuddle;
    shore = (1.0f - smoothstep(
        0.012f,
        0.070f,
        abs(irregularDistance - 0.37f))) * hasPuddle;
    const float slowWave = sin(
        radialDistance * 48.0f + edgeWarp * 18.0f - WetTime * 1.65f);
    const float crossingWave = sin(
        rotated.x * 76.0f - rotated.y * 41.0f + WetTime * 1.10f);
    ripplePattern =
        (slowWave * 0.72f + crossingWave * 0.28f) * puddle;
    return puddle;
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

        // 床の古さ（1面だけ）。色を掛け、目地とひびの凹凸で法線を作っている。
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

    // 水たまりの濃さ・岸の線・波の模様を求めている
    float shore = 0.0f;
    float ripplePattern = 0.0f;
    float puddle = GetPuddleMask(
        input.worldPos.xz,
        shore,
        ripplePattern);
    // 床一面が水に浸かった範囲（1面の西棟）。範囲の内側は全面を水たまりと同じに扱い、ゆるい波を立てている。
    {
        const float2 fromMinimum = input.worldPos.xz - FloodRect.xy;
        const float2 toMaximum = FloodRect.zw - input.worldPos.xz;
        const float insideDistance = min(
            min(fromMinimum.x, fromMinimum.y), min(toMaximum.x, toMaximum.y));
        const float flood = smoothstep(-0.5f, 1.5f, insideDistance);
        ripplePattern += sin(input.worldPos.x * 0.21f + input.worldPos.z * 0.13f + WetTime * 0.9f) *
            0.35f * flood * (1.0f - puddle);
        puddle = max(puddle, flood);
    }
    // 上を向いた面（床）だけを水たまりにしている
    puddle *= smoothstep(0.55f, 0.92f, saturate(input.worldNormal.y));
    shore *= smoothstep(0.55f, 0.92f, saturate(input.worldNormal.y));

    float dripRing = 0.0f;
    float3 detailWorldNormal = agedWorldNormal;
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
    const float roomOcclusion = GetRoomOcclusion(input.worldPos, normalize(input.worldNormal));
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

            const float shadow = GetFlashlightShadow(input.shadowPos);
            const float flashlightAmount = Light.Intensity
                * beamProfile * naturalAttenuation * lensPattern * shadow * flashlightOcclusion;

            lighting += Light.Diffuse.rgb
                * flashlightAmount
                * softenedLambert;

            // 濡れた面の鏡面反射（水たまりほど鋭く強い）
            const float3 halfVector = normalize(directionToLight + viewDirection);
            const float wetSpecular = pow(
                saturate(dot(normal, halfVector)),
                lerp(18.0f, 92.0f, puddle));
            specularLighting += Light.Diffuse.rgb
                * flashlightAmount
                * wetSpecular
                * lerp(0.025f, 1.15f, puddle);

            // 完全な鏡面反射の方向がカメラを外れても、浅い水は懐中電灯を弱く広く反射する。
            // 水面が黒く沈むのを防いでいる。
            specularLighting += Light.Diffuse.rgb
                * flashlightAmount
                * puddle
                * (0.045f + fresnel * 0.12f);
        }
    }

    color.rgb *= lighting;
    // 水面の空の映り込み・波の光・水滴の輪・岸の光を足している
    specularLighting += float3(0.19f, 0.25f, 0.28f)
        * puddle * (0.15f + fresnel * 0.76f);
    const float rippleHighlight = pow(
        saturate(ripplePattern * 0.5f + 0.5f),
        10.0f) * puddle;
    specularLighting += float3(0.12f, 0.16f, 0.17f) * rippleHighlight * 0.22f;
    specularLighting += float3(0.28f, 0.34f, 0.35f)
        * dripRing * 0.20f * RippleStrength;
    specularLighting += float3(0.10f, 0.13f, 0.125f)
        * shore * (0.24f + fresnel * 0.46f);
    color.rgb += specularLighting;
    color.rgb += Material.Emission.rgb;

    // ワールド座標を反転したカメラの画像へ投影し、平面反射の参照する位置を求めている。
    // （反射は実際の景色を映し、法線は水の小さな揺らぎだけを足している）
    const float reflectionW = max(input.reflectionPos.w, 0.0001f);
    const float3 reflectionNdc =
        input.reflectionPos.xyz / reflectionW;
    float2 reflectionUV = float2(
        reflectionNdc.x * 0.5f + 0.5f,
        -reflectionNdc.y * 0.5f + 0.5f);
    const float reflectionInside =
        step(0.0f, reflectionUV.x) *
        step(reflectionUV.x, 1.0f) *
        step(0.0f, reflectionUV.y) *
        step(reflectionUV.y, 1.0f) *
        step(0.0f, reflectionNdc.z) *
        step(reflectionNdc.z, 1.0f) *
        step(0.0001f, input.reflectionPos.w);

    // 波の法線で、反射を参照する位置を少しずらしている
    const float2 waterDistortion =
        detailWorldNormal.xz *
        (0.0045f + abs(ripplePattern) * 0.0022f) *
        puddle * RippleStrength;
    reflectionUV = saturate(reflectionUV + waterDistortion);

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
    const float reflectionStrength = saturate(
        puddle * reflectionInside *
        lerp(0.24f, 0.98f, viewAngleReflection) * ReflectionStrength *
        lerp(0.60f, 1.0f, smoothstep(0.25f, 0.75f, grime)));
    float3 reflectedScene = 0.0f;
    [branch]
    if (reflectionStrength > 0.001f)
    {
        // 濡れた床に映る照明は、見る人の方へ縦に伸びて見える。荒いほど、反射の画像を縦長の範囲で読んで平均している。
        const float2 streak = float2(0.0012f, 0.011f) * waterRoughness;
        reflectedScene =
            g_PlanarReflection.Sample(g_SamplerState, reflectionUV).rgb * 0.28f +
            g_PlanarReflection.Sample(g_SamplerState, saturate(reflectionUV + streak * 0.45f)).rgb * 0.20f +
            g_PlanarReflection.Sample(g_SamplerState, saturate(reflectionUV - streak * 0.45f)).rgb * 0.20f +
            g_PlanarReflection.Sample(g_SamplerState, saturate(reflectionUV + streak)).rgb * 0.16f +
            g_PlanarReflection.Sample(g_SamplerState, saturate(reflectionUV - streak)).rgb * 0.16f;
    }
    const float reflectionGain = 1.10f + rippleHighlight * 0.08f;
    color.rgb = lerp(
        color.rgb,
        reflectedScene * reflectionGain + float3(0.023f, 0.032f, 0.037f),
        reflectionStrength);

    // 高さの違う霧を重ね、近くの見やすさを保ちながら、遠くの輪郭を分けている。
    // 部屋全体を一様に白くせず、床の近くに霧を集めている。
    const float distanceFog =
        smoothstep(110.0f, 390.0f, distanceFromCamera) * 0.72f;
    const float heightFromFloor = max(input.worldPos.y + 100.0f, 0.0f);
    const float heightDensity = exp(-heightFromFloor * 0.055f);
    const float nearSuppression =
        smoothstep(35.0f, 150.0f, distanceFromCamera);
    const float fogVariation = 0.78f +
        saturate(FastValueNoise(input.worldPos.xz * 0.018f + 21.0f)) * 0.22f;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * 0.22f;
    const float fogFactor = saturate(distanceFog + heightFog);
    const float3 fogColor = max(
        Light.Ambient.rgb * 0.38f,
        float3(0.070f, 0.078f, 0.084f));
    color.rgb = lerp(color.rgb, fogColor, fogFactor);
    color.rgb = ApplyFilmicHorrorGrade(color.rgb);

    // デバッグ表示：1=法線、2=懐中電灯の影、3=照明だけ、4=水たまりの濃さ、5=反射の画像、7=タイルごとの光源の数（6は壁用なので黒）
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
        return float4(puddle.xxx, 1.0f);
    }
    if (DebugViewMode == 5)
    {
        return float4(reflectedScene, 1.0f);
    }
    if (DebugViewMode == 7)
    {
        return GetLightTileHeatmap(input.pos.xy, pointLightCount);
    }
    if (DebugViewMode >= 6)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return color;
}

