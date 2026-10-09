// ============================================================================
// シェーダーの役割: マテリアル・環境光・懐中電灯・天井灯・影・壁の古さ・霧を合わせて、3Dの面を照らしている（壁・天井・小物など、床以外のほとんどの面）。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "common.hlsl"

// テクスチャ（t0）とサンプラー（s0）
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"


#include "fastNoise.hlsli"
#include "flashlightLighting.hlsli"
#include "waterCaustics.hlsli"
#include "roomOcclusion.hlsli"
#include "surfaceDetail.hlsli"

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
};


// 壁の汚れの量（0〜1）を計算している：床際の湿り、大きな染み、上から垂れた跡、細かい埃
float GetProceduralGrime(float3 worldPosition, float3 worldNormal)
{
    const float3 normal = abs(normalize(worldNormal));
    const float verticalSurface =
        saturate(1.0f - normal.y * normal.y);

    // 壁の主な向きを選んで、ワールド座標の大きさで模様を作り、UVが大きく引き伸ばされた物でも密度を保っている。
    const float wallCoordinate = normal.x > normal.z
        ? worldPosition.z
        : worldPosition.x;
    const float2 wallUV = float2(wallCoordinate, worldPosition.y);

    const float broadStain = saturate(
        (FastValueNoise(wallUV * float2(0.026f, 0.019f) + 37.2f) - 0.43f)
        * 1.65f);
    // 細かい粒は、補間するノイズではなく、マス目ごとの乱数で十分。
    // 壁が画面の大半を占める場面の、画素ごとの負荷を抑えている。
    const float fineDust = FastHash21(floor(
        wallUV * float2(0.115f, 0.082f) - 11.8f));

    // 主に1次元のマスクから、重力の方向へ伸びる縦長の汚れを作っている。
    const float dripSeed = FastValueNoise(
        float2(wallCoordinate * 0.052f, 8.7f));
    const float dripBreakup = FastValueNoise(
        wallUV * float2(0.017f, 0.033f) + 4.1f);
    const float drip = smoothstep(0.64f, 0.91f, dripSeed) *
        smoothstep(0.30f, 0.78f, dripBreakup);

    // ステージの床はワールドのY=-100あたりにあり、壁の足元に湿気がたまる。
    const float heightFromFloor = max(worldPosition.y + 100.0f, 0.0f);
    const float floorDamp = exp(-heightFromFloor * 0.060f);

    return saturate(verticalSurface *
        (floorDamp * 0.38f + broadStain * 0.25f +
         drip * 0.17f + fineDust * 0.055f));
}

// 壁のざらつき（左官の跡のような凹凸）の高さを計算している
float GetProceduralSurfaceHeight(float3 worldPosition, float3 worldNormal)
{
    const float3 normal = abs(normalize(worldNormal));
    const float wallCoordinate = normal.x > normal.z
        ? worldPosition.z
        : worldPosition.x;
    const float2 wallUV = float2(wallCoordinate, worldPosition.y);

    const float broad = FastValueNoise(wallUV * 0.19f + 3.7f);
    const float plaster = FastValueNoise(wallUV * 0.63f - 12.4f);
    const float fine = FastHash21(floor(wallUV * 1.45f + 27.1f));
    return broad * 0.52f + plaster * 0.33f + fine * 0.15f;
}

// ----------------------------------------------------------------------------
// 壁の古さ（1面）。WallWeathering（面ごと）と Material.WeatheringSurface（建物の壁だけ）で有効になる。
// コンクリートのパネルの継ぎ目・小さな穴・ひびを凹凸として、水の垂れた跡・床際の水位線・カビを色として描いている。
// 画像の素材は使わず、ワールド座標から計算するため、壁の長さが違っても模様の大きさはそろう。
// ----------------------------------------------------------------------------
struct WallAgeing
{
    float height;   // 凹凸に足す高さ（へこみは負）
    float cavity;   // 継ぎ目・穴・ひびの奥の暗さ（0〜1）
    float3 tint;    // 染みの色（元の色に掛けている。1なら変化なし）
    float damp;     // 湿り（0〜1）。浅い角度で見たときの濡れた光沢に使っている
};

// 1面の床の高さ（y=-100）と、壁の上端までの高さ（天井の下面 y≒-48.5）。
static const float WallFloorY = -100.0f;
static const float WallHeight = 51.0f;

// normal はワールドの法線の絶対値、weight は古さの濃さ（0より大きい）。GetWallAgeing から呼んでいる。
WallAgeing ComputeWallAgeing(float3 worldPosition, float3 normal, float weight)
{
    WallAgeing ageing;

    // 壁に沿った横方向と高さの2次元の座標（GetProceduralGrimeと同じ選び方）。
    const float2 uv = float2(
        normal.x > normal.z ? worldPosition.z : worldPosition.x,
        worldPosition.y);
    const float heightFromFloor = worldPosition.y - WallFloorY;
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている。
    const float footprint = max(length(fwidth(uv)), 0.0001f);

    // --- 打ちっぱなしコンクリートの継ぎ目（幅60・高さ17のパネル。壁の文字の高さ y=-70 は横の継ぎ目を避けている）
    // 縦の継ぎ目はくっきり、横の継ぎ目（型枠の跡）は細く浅くして、タイル張りに見えないようにしている。
    const float2 panelSize = float2(60.0f, WallHeight / 3.0f);
    const float2 panelUV = float2(uv.x, heightFromFloor) / panelSize;
    const float2 edgeDistance = (0.5f - abs(frac(panelUV) - 0.5f)) * panelSize;
    const float verticalJoint = 1.0f - smoothstep(0.06f, 0.26f + footprint, edgeDistance.x);
    const float formLine = (1.0f - smoothstep(0.03f, 0.12f + footprint, edgeDistance.y)) * 0.45f;
    const float seam = max(verticalJoint, formLine);
    // パネルごとに、打った日の違いで色をわずかに変えている。
    const float panelShade = FastHash21(floor(panelUV) + 71.0f);

    // 気泡の穴とひびは、遠くて1画素より小さくなる場所では見えないため、計算を省いている。
    float pit = 0.0f;
    float crack = 0.0f;
    [branch]
    if (footprint < 0.5f)
    {
        // --- 小さな気泡の穴（マス目の中で位置と大きさをばらつかせ、格子状に並ばないようにしている）
        const float pitCellSize = 1.6f;
        const float2 pitCell = floor(uv / pitCellSize);
        const float pitSeed = FastHash21(pitCell + 17.3f);
        const float2 pitOffset = float2(
            FastHash21(pitCell + 3.9f), FastHash21(pitCell + 8.6f)) * 0.5f - 0.25f;
        const float pitSize = 0.05f + 0.12f * FastHash21(pitCell + 29.4f);
        const float pitRadius = length(frac(uv / pitCellSize) - 0.5f - pitOffset) * pitCellSize;
        pit = step(0.92f, pitSeed) *
            (1.0f - smoothstep(pitSize * 0.4f, pitSize + footprint, pitRadius)) *
            saturate(1.0f - footprint / 0.5f);

        // --- ひび（ノイズの等高線を細い線として使い、別のノイズで出る場所を絞っている）
        // 等高線だけだと大きな輪のようななめらかな曲線になるため、細かいノイズで線をギザギザに揺らし、
        // さらに別のノイズで途切れ途切れにして、割れたひびらしくしている。
        const float2 crackWarp = float2(
            FastValueNoise(uv * 0.9f + 3.0f), FastValueNoise(uv * 0.9f + 17.0f)) - 0.5f;
        const float crackLine = 1.0f - smoothstep(0.0f, 0.012f + footprint * 0.04f,
            abs(FastValueNoise(uv * 0.085f + 61.0f + crackWarp * 0.22f) - 0.5f));
        const float crackMask = smoothstep(0.62f, 0.78f, FastValueNoise(uv * 0.021f + 9.0f));
        const float crackPieces = smoothstep(0.42f, 0.58f, FastValueNoise(uv * 0.32f + 41.0f));
        crack = crackLine * crackMask * crackPieces * saturate(1.0f - footprint / 0.35f);
    }

    // --- 床際の水位線（水がたまっていた跡）。高さは場所によって6〜12で揺らいでいる
    const float waterLine = 6.0f + 6.0f * FastValueNoise(float2(uv.x * 0.04f, 3.1f));
    const float belowWater = 1.0f - smoothstep(waterLine - 1.5f, waterLine + 0.3f, heightFromFloor);
    const float tideMark = exp(-abs(heightFromFloor - waterLine) * 1.6f);

    // --- 上から垂れた錆と水の跡（幅2.4ごとの縦の帯のうち、2割ほどに出している）
    const float streakWidth = 2.4f;
    const float column = floor(uv.x / streakWidth);
    const float streakSeed = FastHash21(float2(column, 41.7f));
    const float streakX = abs(frac(uv.x / streakWidth) - 0.5f);
    const float streakHalfWidth = 0.10f + 0.22f * FastHash21(float2(column, 7.7f));
    const float streakLength = 14.0f + 26.0f * FastHash21(float2(column, 13.1f));
    const float fromTop = WallHeight - heightFromFloor;
    const float streak = step(0.80f, streakSeed) *
        (1.0f - smoothstep(streakHalfWidth * 0.35f, streakHalfWidth + footprint, streakX)) *
        (1.0f - smoothstep(streakLength * 0.45f, streakLength, fromTop)) *
        (0.55f + 0.45f * FastValueNoise(float2(column * 3.1f, heightFromFloor * 0.35f)));

    // --- カビ（床際に多く、壁の途中にも固まって生えている）。細かい斑点はなめらかなノイズで丸くにじませている
    // 値ノイズは格子に沿った四角い形が出やすいため、座標を回転させ、大きさの違う2つを混ぜて格子を崩している。
    const float moldCluster = smoothstep(0.58f, 0.82f, FastValueNoise(uv * 0.055f + 23.0f));
    const float2 rotatedUV = float2(
        uv.x * 0.866f - uv.y * 0.5f,
        uv.x * 0.5f + uv.y * 0.866f);
    const float moldGrain =
        FastValueNoise(rotatedUV * 1.1f + 5.0f) * 0.6f +
        FastValueNoise(uv * 2.7f - 13.0f) * 0.4f;
    // 斑点は「ある・なし」で切らず、カビの塊の中の濃淡として使っている。
    const float moldSpeckle = lerp(0.45f, 1.0f, smoothstep(0.30f, 0.75f, moldGrain));
    const float mold = moldCluster * lerp(1.0f, moldSpeckle, saturate(1.0f - footprint / 0.6f)) *
        saturate(exp(-heightFromFloor * 0.05f) + 0.30f);

    // 継ぎ目・穴・ひびを、へこみの高さと、奥の暗さにしている
    ageing.height = -(seam * 0.7f + pit * 0.5f + crack * 0.35f) * weight;
    ageing.cavity = saturate(seam * 0.32f + pit * 0.40f + crack * 0.42f) * weight;

    // 色は元の色に掛けている（茶色＝錆、くすんだ緑＝カビ、少し暗い灰緑＝水に浸かっていた部分）。
    const float3 rust = float3(0.66f, 0.50f, 0.38f);
    const float3 moldColor = float3(0.26f, 0.32f, 0.24f);
    const float3 soakedColor = float3(0.72f, 0.76f, 0.70f);
    float3 tint = (0.94f + 0.10f * panelShade).xxx;
    tint = lerp(tint, soakedColor, belowWater * 0.60f);
    tint = lerp(tint, rust * 0.85f, tideMark * 0.50f);
    tint = lerp(tint, rust, streak * 0.65f);
    tint = lerp(tint, moldColor, mold * 0.75f);
    ageing.tint = lerp(1.0f.xxx, tint, weight);
    ageing.damp = saturate(belowWater * 0.6f + streak * 0.4f) * weight;
    return ageing;
}

// 天井の古さ。金属の枠に並んだ天井板（12四方。この世界では約60cm）、外れ落ちた板の穴、雨漏りの染みを描いている。
// 天井の元の色はほぼ黒（0.055）なので、板の部分は色を掛けて暗い灰色まで明るくしている（ホラーらしく、見上げても暗く沈む程度）。
WallAgeing ComputeCeilingAgeing(float2 position, float weight)
{
    WallAgeing ageing;
    const float footprint = max(length(fwidth(position)), 0.0001f);

    // --- 天井板と金属の枠
    const float tileSize = 12.0f;
    const float2 tileUV = position / tileSize;
    const float2 tileId = floor(tileUV);
    const float2 local = frac(tileUV);
    const float2 edgeDistance = (0.5f - abs(local - 0.5f)) * tileSize;
    const float frame = 1.0f - smoothstep(0.15f, 0.35f + footprint, min(edgeDistance.x, edgeDistance.y));
    const float tileSeed = FastHash21(tileId + 5.0f);

    // --- 外れ落ちた板（7%ほど）。枠だけ残り、奥の暗い空間が見えている
    const float missing = step(0.93f, tileSeed) * (1.0f - frame);

    // --- 雨漏りの染み（板の25%ほど）。茶色いにじみと、縁の濃い輪
    const float stainSeed = FastHash21(tileId + 19.0f);
    const float2 stainCenter = 0.3f + 0.4f * float2(
        FastHash21(tileId + 31.0f), FastHash21(tileId + 47.0f));
    const float stainRadius = 2.0f + 3.0f * FastHash21(tileId + 61.0f);
    const float stainDistance = length(local - stainCenter) * tileSize *
        (0.85f + 0.3f * FastValueNoise(position * 0.4f + 3.0f));
    const float stainOn = step(0.75f, stainSeed) * (1.0f - missing) * (1.0f - frame);
    const float stainFill = (1.0f - smoothstep(stainRadius * 0.55f, stainRadius, stainDistance)) * stainOn;
    const float stainRing = exp(-abs(stainDistance - stainRadius) * 3.0f) * stainOn;

    // 枠は板より少し下に出ていて、外れた板の穴は奥へへこんでいる。
    ageing.height = (frame * 0.4f - missing * 1.5f) * weight;
    ageing.cavity = missing * 0.9f * weight;

    // 板・枠・染み・穴の色
    const float3 tileColor = (2.3f + 0.4f * tileSeed).xxx;
    const float3 frameColor = float3(1.6f, 1.6f, 1.55f);
    float3 tint = lerp(tileColor, frameColor, frame);
    tint = lerp(tint, tint * float3(0.78f, 0.62f, 0.44f), stainFill * 0.65f);
    tint = lerp(tint, tint * float3(0.50f, 0.40f, 0.30f), stainRing * 0.55f);
    tint = lerp(tint, 0.20f.xxx, missing);
    ageing.tint = lerp(1.0f.xxx, tint, weight);
    ageing.damp = stainFill * 0.3f * weight;
    return ageing;
}

// その面の古さを返している（壁・天井・それ以外で分けている）
WallAgeing GetWallAgeing(float3 worldPosition, float3 worldNormal, float amount)
{
    WallAgeing ageing;
    ageing.height = 0.0f;
    ageing.cavity = 0.0f;
    ageing.tint = 1.0f.xxx;
    ageing.damp = 0.0f;

    // 古さを描かない面（小物・2面）は何も計算していない。
    // amount は描画の単位で一定（定数バッファとマテリアル）なので、この分岐は画素ごとにばらつかず、安く済む。
    // 建物の面のうち、縦の面は壁の古さ、下を向いた面（天井）は天井の古さを描いている。
    const float3 signedNormal = normalize(worldNormal);
    const float3 normal = abs(signedNormal);
    const float verticalSurface = saturate(1.0f - normal.y * normal.y);
    const float weight = amount * smoothstep(0.5f, 0.9f, verticalSurface);
    const float ceilingWeight = amount * smoothstep(0.7f, 0.95f, -signedNormal.y);
    [branch]
    if (weight > 0.0f)
    {
        ageing = ComputeWallAgeing(worldPosition, normal, weight);
    }
    else if (ceilingWeight > 0.0f)
    {
        ageing = ComputeCeilingAgeing(worldPosition.xz, ceilingWeight);
    }
    return ageing;
}

// extraHeight は GetProceduralSurfaceHeight に足す高さ（壁の古さの継ぎ目など）、strength は凹凸の強さ。
float3 GetBumpedWorldNormal(
    float3 worldPosition,
    float3 worldNormal,
    float extraHeight,
    float strength)
{
    const float3 normal = normalize(worldNormal);
    const float height = GetProceduralSurfaceHeight(
        worldPosition,
        worldNormal) + extraHeight;
    return ApplyHeightToNormal(worldPosition, normal, height, strength);
}

// 映画のような色調にしている（明るい所をなだらかに抑え、影は少し青く、明るい所は少し暖かく）
float3 ApplyFilmicHorrorGrade(float3 color)
{
    color = max(color, 0.0f);

    // 影を潰す強いクリップを避け、控えめなフィルムのような明るい所の圧縮を混ぜている。
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


// 画素の色を計算している：元の色 → 汚れと古さ → 環境光・天井灯・懐中電灯 → 濡れた光沢 → 自己発光 → 霧 → 色調
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

    // デバッグのフルブライト表示では、照明を使わずに返している
    if (DebugViewMode == DEBUG_VIEW_FULLBRIGHT)
    {
        return GetFullbrightColor(color.rgb, input.viewNormal, input.viewPos);
    }

    // 計算で作る汚れは、不透明でテクスチャのない建材だけに使っている。
    // 光るパネルは汚さず、ブルームと天井の光の明るさを保っている。
    const float emissionEnergy = dot(
        abs(Material.Emission.rgb),
        float3(0.3333f, 0.3333f, 0.3333f));
    float3 detailWorldNormal = normalize(input.worldNormal);
    float grime = 0.0f;
    float wetness = 0.0f;
    if (!Material.TextureEnable && emissionEnergy < 0.001f)
    {
        grime = GetProceduralGrime(
            input.worldPos,
            input.worldNormal) * WallDampStrength;
        color.rgb *= 1.0f - grime * 0.22f;
        color.rgb = lerp(
            color.rgb,
            color.rgb * float3(0.72f, 0.80f, 0.69f),
            grime * 0.20f);

        // 壁の古さ（1面の建物の壁だけ）。0のときは前と同じ見た目になる。
        const float weathering = Material.WeatheringSurface ? WallWeathering : 0.0f;
        const WallAgeing ageing = GetWallAgeing(
            input.worldPos, input.worldNormal, weathering);
        color.rgb *= ageing.tint;
        color.rgb *= 1.0f - ageing.cavity * 0.55f;
        wetness = saturate(grime + ageing.damp * 0.8f);

        // 汚れと古さの凹凸で、法線を傾けている（古さが濃いほど強く）
        detailWorldNormal = GetBumpedWorldNormal(
            input.worldPos,
            input.worldNormal,
            ageing.height,
            0.72f * (1.0f + 0.8f * weathering));
    }
    const float3 detailViewNormal = normalize(mul(
        float4(detailWorldNormal, 0.0f),
        View).xyz);

    // 上下で色を変えた環境光から始めている
    float3 lighting = GetHemisphereAmbient(detailWorldNormal);
    const float distanceFromCamera = length(input.viewPos);

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
    }

    // 部屋の角の暗がり。環境光と天井灯には全部、懐中電灯には一部だけ掛けている（照らせば角も見えるように）。
    const float roomOcclusion = GetRoomOcclusion(input.worldPos, normalize(input.worldNormal));
    lighting *= roomOcclusion;
    const float flashlightOcclusion = lerp(1.0f, roomOcclusion, 0.4f);

    // 懐中電灯の光：円錐の形・距離による弱まり・レンズの模様・面の向き・影を掛け合わせている
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > 0.001f)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
        // 円錐の外では、レンズの汚れのノイズとシャドウマップの参照を丸ごと省いている。
        [branch]
        if (beamProfile > 0.001f)
        {
            const float normalizedDistance = saturate(
                distanceFromCamera / max(Light.Range, 0.001f));
            const float rangeFade = saturate(
                1.0f - normalizedDistance * normalizedDistance);
            const float attenuation = rangeFade * rangeFade;
            const float lensPattern = GetFlashlightLensPattern(pixelDirection);
            // 実際の光のように距離の2乗で弱まる分を、少しだけ混ぜている
            const float physicalFalloff = rcp(
                1.0f + distanceFromCamera * distanceFromCamera * 0.000018f);
            const float naturalAttenuation = attenuation *
                lerp(1.0f, physicalFalloff, 0.32f);

            const float3 normal = detailViewNormal;
            const float3 directionToLight = -pixelDirection;
            const float lambert = saturate(dot(normal, directionToLight));
            // 横から当たる面も真っ暗にならないよう、明るさの下限を設けている
            const float softenedLambert = 0.25f + lambert * 0.75f;

            lighting += Light.Diffuse.rgb
                * Light.Intensity
                * beamProfile
                * naturalAttenuation
                * lensPattern
                * softenedLambert
                * GetFlashlightShadow(input.shadowPos)
                * flashlightOcclusion;
        }
    }

    // 浸水した床（西棟）の水面で跳ね返った懐中電灯の光が、壁や天井にゆらゆら映る（waterCaustics.hlsli）
    lighting += Light.Diffuse.rgb * GetWaterCausticLight(input.worldPos, normalize(input.worldNormal));

    color.rgb *= lighting;

    // 湿った漆喰は、浅い角度で見たときに、細く冷たい光沢を返す。
    // 同じ汚れのマスクを、光の吸収と光沢の両方に使い、読み取りを増やさずに見た目をそろえている。
    const float3 viewDirection = normalize(-input.viewPos);
    const float dampFresnel = pow(
        1.0f - saturate(dot(detailViewNormal, viewDirection)),
        4.0f);
    color.rgb += float3(0.055f, 0.070f, 0.076f) * wetness *
        (0.045f + dampFresnel * 0.42f);

    // 自ら光る色を足している
    color.rgb += Material.Emission.rgb;

    // 高さの違う霧を重ね、近くの見やすさを保ちながら、遠くの輪郭を分けている。
    // 部屋全体を一様に白くせず、床の近くに霧を集めている。
    const float distanceFog =
        smoothstep(110.0f, 390.0f, distanceFromCamera) * 0.72f;
    const float heightFromFloor = max(input.worldPos.y + 100.0f, 0.0f);
    const float heightDensity = exp(-heightFromFloor * 0.055f);
    const float nearSuppression =
        smoothstep(35.0f, 150.0f, distanceFromCamera);
    const float fogVariation = 0.78f +
        FastValueNoise(input.worldPos.xz * 0.018f + 21.0f) * 0.22f;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * 0.22f;
    const float fogFactor = saturate(distanceFog + heightFog);
    const float3 fogColor = max(
        Light.Ambient.rgb * 0.38f,
        float3(0.070f, 0.078f, 0.084f));
    color.rgb = lerp(color.rgb, fogColor, fogFactor);
    color.rgb = ApplyFilmicHorrorGrade(color.rgb);

    // デバッグ表示：1=法線、2=懐中電灯の影、3=照明だけ、6=壁の湿り気、7=タイルごとの光源の数。4・5は床（水たまり・反射）用の表示なので、壁は黒にしている
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
    if (DebugViewMode == 6)
    {
        return float4(saturate(grime).xxx, 1.0f);
    }
    if (DebugViewMode == 7)
    {
        return GetLightTileHeatmap(input.pos.xy, pointLightCount);
    }
    if (DebugViewMode >= 4)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return color;
}

