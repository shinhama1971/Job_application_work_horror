// ============================================================================
// 共通の処理: 外の森の枯れ木・倒木・枯れた茂みの樹皮（barkPS.hlsl）。色・粗さ・濡れ具合・凹凸の高さを、ワールドの位置から計算している（PBRの材質）。
// 主な技術: 木目の向きに合わせた座標（木目の向きを y にした向きへ回してから、3方向に投影するノイズ）、
//           まわりの向きに並べて木目に沿って揺らした縦の溝と、溝の間の板の横のひび、剥がれて地の木が出た所、苔と地衣類、
//           格子の形を消すための、回した2つの格子のノイズの平均、
//           画素の大きさ（footprint）で細かい模様を薄めるちらつき対策
//
// 木のモデルは「先の細い円柱」を重ねた形で、樹皮の筋は幹や枝に沿って走る。シェーダーには、モデルの木目の向き
// （木と茂みは上、倒木は長い向き。Wall::UploadMaterial がマテリアルの使っていない環境の色で渡す）だけを渡し、
// その向きを y にした座標で、筋の向き（y）にはゆっくり、まわり（x・z）には細かく変わるノイズを使っている。
// 斜めの枝では筋が幹ほど正確に枝に沿わないが、細く遠いので目立たない。目の高さは地面から約40、幹の太さは半径2〜4。
// ============================================================================
#ifndef BARK_SURFACE_INCLUDED
#define BARK_SURFACE_INCLUDED

// 樹皮の1画素分の材質
struct BarkSurface
{
    float3 albedo;      // 色（PBRの元の色）
    float roughness;    // 粗さ（0.045〜1）
    float wetness;      // 濡れ具合（0〜1。地面に近いほど湿っている）
    float height;       // 凹凸の高さ（へこみは負。ApplyHeightToNormal で法線を傾ける）
};

// 森の地面の高さ（配置ファイルの地面と同じ y=-100）
static const float BarkGroundY = -100.0f;

// 格子の形が出にくい2次元のノイズ（0〜1）。値ノイズは格子の縁で傾きが0になるため、細長く伸ばして等高線を線に使うと、
// 線が格子に沿って階段のように折れ、斑点も四角くなる。そこで、60度回した格子のノイズと平均して格子の向きを消し、
// 平均で狭まった値の幅を広げ直している
float BarkSmoothNoise(float2 position)
{
    const float2 rotated = float2(
        position.x * 0.5f - position.y * 0.866f,
        position.x * 0.866f + position.y * 0.5f);
    const float average = (FastValueNoise(position) + FastValueNoise(rotated + 17.3f)) * 0.5f;
    return saturate(0.5f + (average - 0.5f) * 1.7f);
}

// 木目の向きを y にした座標 grain で、面の向きの重み weights に合わせて3方向から投影したノイズを混ぜている。
// scale.x はまわりの向きの細かさ、scale.y は木目の向きの細かさ（小さいほど筋が長く伸びる）。
// 切り口の向きの面（木目の向きを向いた面）は、まわりの細かさで両方向をそろえている
float BarkNoise(float3 grain, float3 weights, float2 scale, float offset)
{
    return BarkSmoothNoise(float2(grain.z * scale.x, grain.y * scale.y) + offset) * weights.x +
        BarkSmoothNoise(float2(grain.x, grain.z) * scale.x + offset) * weights.y +
        BarkSmoothNoise(float2(grain.x * scale.x, grain.y * scale.y) + offset) * weights.z;
}

// 樹皮の溝と板（1つの投影面の中）。uv.x はまわりの向き、uv.y は木目の向きの座標。
// まわりの向きに一定の間隔で並ぶ溝を、木目に沿ってゆっくり左右に揺らし（溝が曲がり、寄ったり離れたりする）、
// 所々で途切れさせて隣の板とつなげている。溝と溝の間の板には、板ごとの乱数で短い横のひびを入れている
struct BarkPlates
{
    float furrow;       // 縦の溝（0〜1）
    float crossCrack;   // 板の横のひび（0〜1）
    float plateTone;    // 板ごとの色の違い（0〜1）
};

BarkPlates GetBarkPlates(float2 uv, float footprint)
{
    BarkPlates plates;
    const float spacing = 0.85f;
    const float sway = (BarkSmoothNoise(float2(uv.x * 0.25f, uv.y * 0.045f) + 3.0f) - 0.5f) * 2.6f +
        (BarkSmoothNoise(float2(uv.x * 1.3f, uv.y * 0.22f) + 9.0f) - 0.5f) * 0.5f;
    const float phase = uv.x * spacing + sway;
    const float plateId = floor(phase);
    const float distanceToFurrow = abs(frac(phase) - 0.5f);
    // 溝の幅は板ごとに少し変え、遠くでは画素の大きさの分だけ広げて、ちらつきを抑えている
    const float width = lerp(0.07f, 0.15f, FastHash21(float2(plateId, 5.0f)));
    const float furrowLine = 1.0f - smoothstep(width, width + 0.05f + footprint * spacing, distanceToFurrow);
    // 溝が途切れて、隣の板とつながる所
    const float bridge = smoothstep(0.30f, 0.50f, BarkSmoothNoise(float2(uv.x * 0.9f, uv.y * 0.11f) + 31.0f));
    plates.furrow = furrowLine * bridge;
    // 板の横のひび：板ごとに間隔とずれを変え、4割ほどの板にだけ入れている。ひびの線は少し波打たせ、
    // 板の端（溝の近く）へ向かって細くして消している（端で急に切れると、黒い四角に見えるため）
    const float crackWobble = (BarkSmoothNoise(uv * float2(2.2f, 0.6f) + 71.0f) - 0.5f) * 0.12f;
    const float crackRow = frac(uv.y * lerp(0.10f, 0.22f, FastHash21(float2(plateId, 13.0f))) +
        FastHash21(float2(plateId, 29.0f)) * 7.0f + crackWobble);
    const float plateInside = smoothstep(width + 0.04f, width + 0.22f, distanceToFurrow);
    plates.crossCrack = (1.0f - smoothstep(0.0f, (0.030f + footprint * 0.1f) * plateInside, abs(crackRow - 0.5f))) *
        step(0.6f, FastHash21(float2(plateId, 47.0f))) * plateInside;
    plates.plateTone = FastHash21(float2(plateId, 61.0f));
    return plates;
}

// grainAxis は木目の向き（ワールド）、baseColor は配置ファイルの色（頂点の環境遮蔽を掛けた後）、
// baseRoughness はその色の粗さ（rough。bark は0.92）
BarkSurface GetBarkSurface(float3 worldPosition, float3 worldNormal, float3 grainAxis, float3 baseColor, float baseRoughness)
{
    BarkSurface bark;
    // 木目の向きを y、それと直交する2つの向きを x・z にした座標へ回している（向きが渡っていなければ上向き）
    const float3 axis = dot(grainAxis, grainAxis) > 0.01f ? normalize(grainAxis) : float3(0.0f, 1.0f, 0.0f);
    const float3 reference = abs(axis.y) < 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 side = normalize(cross(reference, axis));
    const float3 front = cross(axis, side);
    const float3 grain = float3(dot(worldPosition, side), dot(worldPosition, axis), dot(worldPosition, front));
    const float3 normal = normalize(worldNormal);
    const float3 grainNormal = float3(dot(normal, side), dot(normal, axis), dot(normal, front));
    // 投影の重み：面の向きに近い軸ほど強くし（4乗）、斜めの面で模様が混ざる幅を狭めている
    float3 weights = pow(abs(grainNormal), 4.0f);
    weights /= max(weights.x + weights.y + weights.z, 0.0001f);
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている
    const float footprint = max(length(fwidth(worldPosition)), 0.0001f);
    const float fineFade = saturate(1.0f - footprint / 0.6f);

    // --- 縦の溝と、溝の間の板・板の横のひび（幹の横を向いた2つの投影面で求め、面の向きで混ぜている。
    //     切り口の向きの面には溝を付けない）
    const BarkPlates platesX = GetBarkPlates(float2(grain.z, grain.y), footprint);
    const BarkPlates platesZ = GetBarkPlates(float2(grain.x, grain.y) + float2(37.0f, 11.0f), footprint);
    const float sideWeight = weights.x + weights.z;
    const float furrow = platesX.furrow * weights.x + platesZ.furrow * weights.z;
    const float crossCrack = (platesX.crossCrack * weights.x + platesZ.crossCrack * weights.z) * 0.8f;
    const float plateTone = (platesX.plateTone * weights.x + platesZ.plateTone * weights.z) + 0.5f * weights.y;
    // --- 細かい繊維（木目の向きに長い、細かな筋）
    const float fibre = BarkNoise(grain, weights, float2(5.5f, 0.45f), 11.0f);

    // --- 剥がれ：樹皮が落ちて、灰色に枯れた地の木が出ている所（枯れ木らしさ）。木目に沿って細長く、縁はぎざぎざで、
    //     縁の樹皮は少し盛り上がって残っている
    const float peelNoise = BarkNoise(grain, weights, float2(0.22f, 0.06f), 41.0f) +
        (BarkNoise(grain, weights, float2(1.6f, 0.4f), 43.0f) - 0.5f) * 0.16f;
    const float peel = smoothstep(0.76f, 0.78f, peelNoise) * sideWeight;
    const float peelRim = smoothstep(0.71f, 0.76f, peelNoise) * (1.0f - peel) * sideWeight;

    // --- 苔：上を向いた面（倒木の上・太い枝の上）と、根元の湿った所に、まだらに生えている
    const float heightFromGround = worldPosition.y - BarkGroundY;
    const float mossPatch = smoothstep(0.45f, 0.70f,
        BarkNoise(grain, weights, float2(0.22f, 0.10f), 61.0f) + fibre * 0.15f);
    const float mossPlace = max(smoothstep(0.30f, 0.80f, normal.y),
        (1.0f - smoothstep(2.0f, 14.0f, heightFromGround)) * 0.85f);
    const float moss = mossPlace * mossPatch * (1.0f - peel);

    // --- 地衣類：樹皮の上の、白っぽい灰緑の小さな斑点
    const float lichenSpots = smoothstep(0.66f, 0.74f, BarkNoise(grain, weights, float2(1.2f, 1.0f), 83.0f));
    const float lichenArea = smoothstep(0.52f, 0.72f, BarkNoise(grain, weights, float2(0.12f, 0.08f), 97.0f));
    const float lichen = lichenSpots * lichenArea * (1.0f - moss) * (1.0f - peel) * fineFade;

    // --- 地面との境目の湿り（根元と倒木の下側が、地面の水を吸って黒ずんでいる）
    const float groundDamp = 1.0f - smoothstep(0.5f, 4.0f + fibre * 2.0f, heightFromGround);

    // 色：樹皮の色に板ごとの濃淡と繊維を掛け、溝の奥とひびを暗くし、剥がれ・苔・地衣類の色を混ぜている
    // 板の表は雨と日に晒されて、樹皮の色より少し灰色に抜けている（溝の奥は元の暗い色のまま）
    float3 albedo = lerp(baseColor, baseColor * 1.6f + 0.02f, 0.35f + plateTone * 0.4f);
    albedo *= lerp(1.0f, 0.82f + fibre * 0.32f, fineFade);
    albedo *= 1.0f - furrow * 0.78f;
    albedo *= 1.0f - crossCrack * 0.45f;
    // 剥がれた所の地の木は、雨と日に晒されて灰色に抜けている（樹皮より明るく、木目の筋が見える）
    const float3 bareWood = float3(0.17f, 0.16f, 0.145f) * (0.80f + fibre * 0.40f);
    albedo = lerp(albedo, bareWood, peel);
    albedo *= 1.0f + peelRim * 0.25f;
    albedo = lerp(albedo, float3(0.055f, 0.08f, 0.032f), moss * 0.85f);
    albedo = lerp(albedo, float3(0.26f, 0.28f, 0.23f), lichen * 0.65f);
    albedo *= 1.0f - groundDamp * 0.30f;
    bark.albedo = albedo;

    // 粗さ：樹皮はざらざら、溝の奥と苔はさらに粗く、剥がれた地の木は少しなめらか（濡れた所は MakePbrSurface が下げる）
    float roughness = baseRoughness + (plateTone - 0.5f) * 0.08f;
    roughness = lerp(roughness, 0.98f, furrow * 0.7f);
    roughness = lerp(roughness, 0.62f, peel);
    roughness = lerp(roughness, 0.97f, moss);
    bark.roughness = clamp(roughness, 0.045f, 1.0f);

    // 濡れ具合：地面との境目と、溝の奥にたまった水
    bark.wetness = saturate(groundDamp * 0.55f + furrow * 0.12f);

    // 高さ：溝のへこみ・横のひび・繊維・剥がれ（樹皮の厚みの分だけ低い）とその縁・苔の盛り上がり
    bark.height = -furrow * 0.85f -
        crossCrack * 0.40f +
        (fibre - 0.5f) * 0.20f * fineFade -
        peel * 0.45f +
        peelRim * 0.15f +
        moss * 0.12f;
    return bark;
}

#endif
