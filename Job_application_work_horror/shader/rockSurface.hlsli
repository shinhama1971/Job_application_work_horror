// ============================================================================
// 共通の処理: 外の森の岩の面（rockPS.hlsl）。色・粗さ・濡れ具合・凹凸の高さを、ワールドの位置から計算している（PBRの材質）。
// 主な技術: 3方向から投影したノイズ（トライプラナー）、尾根の形のノイズで作るひび、面の向きで決める苔、
//           マス目の中で位置をばらつかせた鉱物の粒（粗さが低く、懐中電灯の光の芯を点々と返す）、
//           画素の大きさ（footprint）で細かい模様を薄めるちらつき対策
//
// 岩のモデル（forest_rock）は、潰れた球を重ねた形で、UVを持たない。そこで、模様は面の向きに合わせて
// x・y・z の3方向から投影したノイズを混ぜて作り、どの向きの面でも模様が伸びないようにしている。
// 目の高さは地面から約40。岩の大きさは幅9〜19。
// ============================================================================
#ifndef ROCK_SURFACE_INCLUDED
#define ROCK_SURFACE_INCLUDED

// 岩の1画素分の材質
struct RockSurface
{
    float3 albedo;      // 色（PBRの元の色）
    float roughness;    // 粗さ（0.045〜1）
    float wetness;      // 濡れ具合（0〜1。地面に近いほど湿っている）
    float height;       // 凹凸の高さ（へこみは負。ApplyHeightToNormal で法線を傾ける）
};

// 森の地面の高さ（配置ファイルの地面と同じ y=-100）
static const float RockGroundY = -100.0f;

// 面の向きに合わせて、3方向から投影したノイズを混ぜている（weights は3方向の重みで、合計1）
float RockTriplanarNoise(float3 position, float3 weights, float scale, float offset)
{
    const float3 p = position * scale + offset;
    return FastValueNoise(p.yz) * weights.x +
        FastValueNoise(p.xz) * weights.y +
        FastValueNoise(p.xy) * weights.z;
}

// 尾根の形のノイズ（ノイズが0.5を横切る所だけ1に近い）。ひびの線に使っている
float RockRidge(float3 position, float3 weights, float scale, float offset)
{
    return 1.0f - abs(RockTriplanarNoise(position, weights, scale, offset) * 2.0f - 1.0f);
}

// 鉱物の粒（0〜1）。3方向のうち一番近い面へ投影し、マス目ごとに乱数で粒があるか・マス目の中の位置を決めている
// （位置をばらつかせないと、粒がマス目どおりに並んで水玉模様に見えるため）。radius は粒の半径（ワールドの単位）
float RockGrainSpark(float3 position, float3 weights, float scale, float radius, float footprint)
{
    const float3 p = position * scale;
    const float2 uv = weights.y > max(weights.x, weights.z)
        ? p.xz
        : (weights.x > weights.z ? p.yz : p.xy);
    const float2 cell = floor(uv);
    const float present = step(0.78f, FastHash21(cell + 37.0f));
    const float2 center = 0.15f + 0.70f * float2(FastHash21(cell + 13.0f), FastHash21(cell + 71.0f));
    const float distanceToGrain = length(frac(uv) - center) / scale;
    return present * (1.0f - smoothstep(radius * 0.4f, radius + footprint, distanceToGrain));
}

// baseColor は配置ファイルの色（頂点の環境遮蔽を掛けた後）、baseRoughness はその色の粗さ（rough。rockColor は0.85）
RockSurface GetRockSurface(float3 worldPosition, float3 worldNormal, float3 baseColor, float baseRoughness)
{
    RockSurface rock;
    const float3 normal = normalize(worldNormal);
    // 投影の重み：面の向きに近い軸ほど強くし（4乗）、斜めの面で模様がぼやけて混ざる幅を狭めている
    float3 weights = pow(abs(normal), 4.0f);
    weights /= max(weights.x + weights.y + weights.z, 0.0001f);
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている
    const float footprint = max(length(fwidth(worldPosition)), 0.0001f);
    const float fineFade = saturate(1.0f - footprint / 0.8f);

    // --- 大きな色むら（岩ごと・場所ごとの色の違い）と、うっすらした地層の縞
    const float tone = RockTriplanarNoise(worldPosition, weights, 0.07f, 3.1f) * 0.65f +
        RockTriplanarNoise(worldPosition, weights, 0.21f, 17.4f) * 0.35f;
    const float bandWarp = RockTriplanarNoise(worldPosition, weights, 0.15f, 41.0f);
    const float band = 0.5f + 0.5f * sin(worldPosition.y * 1.35f + bandWarp * 5.0f);

    // --- ひび（尾根のノイズの細い線を、別のノイズで途切れさせている）
    // 等高線のままだと、丸い起伏をなぞった輪の形になるため、座標を細かいノイズで揺らして線をギザギザに折り、
    // 大きさの違う2本の線を重ねて、輪に見えないようにしている
    const float3 crackWarp = float3(
        RockTriplanarNoise(worldPosition, weights, 1.3f, 3.0f),
        RockTriplanarNoise(worldPosition, weights, 1.3f, 19.0f),
        RockTriplanarNoise(worldPosition, weights, 1.3f, 37.0f)) - 0.5f;
    const float3 crackPosition = worldPosition + crackWarp * 1.6f;
    const float crackLine = max(
        smoothstep(0.93f, 0.99f, RockRidge(crackPosition, weights, 0.27f, 7.7f)),
        smoothstep(0.95f, 0.995f, RockRidge(crackPosition, weights, 0.61f, 53.0f)) * 0.7f);
    const float crackPieces = smoothstep(0.45f, 0.66f, RockTriplanarNoise(worldPosition, weights, 0.23f, 29.0f));
    const float crack = crackLine * crackPieces;

    // --- 細かいざらつき（遠くでは薄める）
    const float grain = RockTriplanarNoise(worldPosition, weights, 2.1f, 5.5f);
    const float grainFine = RockTriplanarNoise(worldPosition, weights, 5.3f, 11.0f);

    // --- 鉱物の粒（石英や雲母）：ところどころの小さな粒だけ、なめらかで明るい。懐中電灯で照らすと点々と光る
    const float glint = RockGrainSpark(worldPosition, weights, 2.6f, 0.045f, footprint) * fineFade;

    // --- 苔：上を向いた面の、大きなまだらの中に生えている。暗い緑で、光をほとんど返さない
    const float upward = smoothstep(0.20f, 0.75f, normal.y);
    const float mossPatch = smoothstep(0.42f, 0.66f,
        RockTriplanarNoise(worldPosition, weights, 0.11f, 61.0f) + grain * 0.18f);
    const float moss = upward * mossPatch;

    // --- 地衣類：横を向いた面にも付く、白っぽい灰緑の小さな丸い斑点
    const float lichenSpots = smoothstep(0.62f, 0.70f, RockTriplanarNoise(worldPosition, weights, 0.85f, 83.0f));
    const float lichenArea = smoothstep(0.50f, 0.70f, RockTriplanarNoise(worldPosition, weights, 0.09f, 97.0f));
    const float lichen = lichenSpots * lichenArea * (1.0f - moss) * fineFade;

    // --- 地面との境目の湿り（地面の水を吸い上げて黒ずみ、少し光る）
    const float heightFromGround = worldPosition.y - RockGroundY;
    const float groundDamp = 1.0f - smoothstep(0.5f, 3.5f + grain * 1.5f, heightFromGround);

    // 色：元の色に、色むら（暖かい灰色〜冷たい灰色）・縞・ざらつきを掛け、ひびの奥を暗く、苔・地衣類・粒の色を混ぜている
    const float3 warmGray = baseColor * float3(1.08f, 1.00f, 0.92f);
    const float3 coolGray = baseColor * float3(0.94f, 0.98f, 1.04f);
    float3 albedo = lerp(coolGray, warmGray, saturate(tone * 1.4f - 0.2f));
    albedo *= 0.84f + tone * 0.40f;
    albedo *= 0.94f + band * 0.10f;
    albedo *= lerp(1.0f, 0.86f + grain * 0.22f + grainFine * 0.08f, fineFade);
    albedo *= 1.0f - crack * 0.60f;
    albedo = lerp(albedo, float3(0.075f, 0.095f, 0.045f), moss * 0.85f);
    albedo = lerp(albedo, float3(0.30f, 0.32f, 0.27f), lichen * 0.70f);
    albedo = lerp(albedo, albedo * 1.25f + 0.01f, glint);
    albedo *= 1.0f - groundDamp * 0.30f;
    rock.albedo = albedo;

    // 粗さ：元の粗さを色むらで少し揺らし、苔はさらに粗く、鉱物の粒はなめらかにしている（濡れた所は MakePbrSurface が下げる）
    float roughness = baseRoughness + (tone - 0.5f) * 0.12f + (grain - 0.5f) * 0.06f * fineFade;
    roughness = lerp(roughness, 0.97f, moss);
    roughness = lerp(roughness, 0.92f, crack);
    roughness = lerp(roughness, 0.18f, glint);
    rock.roughness = clamp(roughness, 0.045f, 1.0f);

    // 濡れ具合：地面との境目と、ひびの奥にたまった水
    rock.wetness = saturate(groundDamp * 0.55f + crack * 0.25f);

    // 高さ：ゆるい起伏・細かいざらつき・ひびのへこみ・苔の盛り上がり
    const float lumps = RockTriplanarNoise(worldPosition, weights, 0.45f, 23.0f);
    rock.height = lumps * 0.55f +
        (grain * 0.22f + grainFine * 0.08f) * fineFade -
        crack * 0.55f +
        moss * 0.18f * grainFine;
    return rock;
}

#endif
