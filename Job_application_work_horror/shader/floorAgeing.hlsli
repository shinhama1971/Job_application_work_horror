// ============================================================================
// シェーダーの部品: 床の古さ（1面と2面のコンクリートの床版の目地・ひび・染み。wetFloorPS.hlsl が使う）
// ============================================================================

#ifndef SIGNAL_LOST_FLOORAGEING_HLSLI
#define SIGNAL_LOST_FLOORAGEING_HLSLI

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
// 床材の係数はプリプロセッサで展開する。FXC が型付き定数で生じさせる丸め差を避けるため。
// 床版の大きさと継ぎ目の幅。大きな床版にしてタイルに見えないようにする。
#define FLOOR_SLAB_SIZE (56.0f)
#define FLOOR_JOINT_CORE (0.05f)
#define FLOOR_JOINT_EDGE (0.20f)
#define FLOOR_SLAB_TINT_SEED (11.0f)

// ひびの太さ、現れる領域、遠くで薄める境界を別々に調整する。
#define FLOOR_CRACK_LINE_WIDTH (0.014f)
#define FLOOR_CRACK_LINE_FILTER (0.04f)
#define FLOOR_CRACK_NOISE_SCALE (0.06f)
#define FLOOR_CRACK_NOISE_SEED (17.0f)
#define FLOOR_CRACK_MASK_START (0.60f)
#define FLOOR_CRACK_MASK_FULL (0.78f)
#define FLOOR_CRACK_MASK_SCALE (0.015f)
#define FLOOR_CRACK_MASK_SEED (4.0f)
#define FLOOR_CRACK_DISTANCE_FILTER (0.35f)

// 引きずった跡は一方向に伸ばし、遠くでは薄めてちらつきを抑える。
#define FLOOR_SCUFF_LINE_START (0.72f)
#define FLOOR_SCUFF_LINE_FULL (0.90f)
#define FLOOR_SCUFF_NOISE_X (0.04f)
#define FLOOR_SCUFF_NOISE_Z (0.9f)
#define FLOOR_SCUFF_NOISE_SEED (53.0f)
#define FLOOR_SCUFF_MASK_FULL (0.70f)
#define FLOOR_SCUFF_MASK_SCALE (0.012f)
#define FLOOR_SCUFF_MASK_SEED (2.0f)
#define FLOOR_SCUFF_DISTANCE_FILTER (0.6f)

// 壁際の埃は壁までの距離が増えるほど薄くする。
#define FLOOR_WALL_DUST_FALLOFF_DISTANCE (4.5f)

// 床の古さの値は #define にしている（型付きの定数にすると FXC の最適化が変わり、浮動小数点の丸めが変わるため）。
// 画素の大きさの下限（0で割らないため）と、ひびを描く画素の大きさの上限
#define FLOOR_MIN_FOOTPRINT (0.0001f)
#define FLOOR_CRACK_FOOTPRINT_LIMIT (0.5f)
// 油や水の染み：出る場所（出始め・濃くなる）とノイズの細かさ・ずらし、濃さのむら（最低＋幅、細かさ・ずらし）
#define FLOOR_STAIN_START (0.50f)
#define FLOOR_STAIN_FULL (0.82f)
#define FLOOR_STAIN_SCALE (0.03f)
#define FLOOR_STAIN_SEED (31.0f)
#define FLOOR_STAIN_SHADE_BASE (0.6f)
#define FLOOR_STAIN_SHADE_RANGE (0.4f)
#define FLOOR_STAIN_SHADE_SCALE (0.2f)
#define FLOOR_STAIN_SHADE_SEED (7.0f)
// 引きずった跡を出す場所の出始め
#define FLOOR_SCUFF_MASK_START (0.50f)
// へこみの高さ（目地・ひび）
#define FLOOR_JOINT_DEPTH (0.5f)
#define FLOOR_CRACK_DEPTH (0.4f)
// 色：床版の明るさ（最低＋むらの幅）、目地とひびの暗さ、染みの色と混ぜ方、引きずった跡の明るさと混ぜ方、埃の色と混ぜ方
#define FLOOR_SLAB_TINT_BASE (0.93f)
#define FLOOR_SLAB_TINT_RANGE (0.12f)
#define FLOOR_JOINT_DARKEN (0.22f)
#define FLOOR_CRACK_DARKEN (0.50f)
#define FLOOR_STAIN_COLOR (float3(0.55f, 0.52f, 0.48f))
#define FLOOR_STAIN_BLEND (0.75f)
#define FLOOR_SCUFF_BRIGHTEN (1.18f)
#define FLOOR_SCUFF_BLEND (0.50f)
#define FLOOR_DUST_COLOR (float3(1.30f, 1.25f, 1.15f))
#define FLOOR_DUST_BLEND (0.55f)

FloorAgeing ComputeFloorAgeing(float2 position, float amount)
{
    FloorAgeing ageing;
    // 1画素がワールドで何単位か。遠くで細かい模様がちらつかないよう、これで薄めている。
    const float footprint = max(length(fwidth(position)), FLOOR_MIN_FOOTPRINT);

    // --- 床版の目地（56四方。細く浅くして、タイル張りに見えないようにしている）。床版ごとに色をわずかに変えている
    const float slabSize = FLOOR_SLAB_SIZE;
    const float2 slabUV = position / slabSize;
    const float2 edgeDistance = (0.5f - abs(frac(slabUV) - 0.5f)) * slabSize;
    const float joint = 1.0f - smoothstep(FLOOR_JOINT_CORE, FLOOR_JOINT_EDGE + footprint, min(edgeDistance.x, edgeDistance.y));
    const float slabShade = FastHash21(floor(slabUV) + FLOOR_SLAB_TINT_SEED);

    // --- ひび（遠くでは1画素より細くなるため、計算を省いている）
    float crack = 0.0f;
    [branch]
    if (footprint < FLOOR_CRACK_FOOTPRINT_LIMIT)
    {
        const float crackLine = 1.0f - smoothstep(0.0f, FLOOR_CRACK_LINE_WIDTH + footprint * FLOOR_CRACK_LINE_FILTER,
            abs(FastValueNoise(position * FLOOR_CRACK_NOISE_SCALE + FLOOR_CRACK_NOISE_SEED) - 0.5f));
        crack = crackLine * smoothstep(FLOOR_CRACK_MASK_START, FLOOR_CRACK_MASK_FULL, FastValueNoise(position * FLOOR_CRACK_MASK_SCALE + FLOOR_CRACK_MASK_SEED)) *
            saturate(1.0f - footprint / FLOOR_CRACK_DISTANCE_FILTER);
    }

    // --- 油や水の染み（大きな黒ずみ）
    const float stain = smoothstep(FLOOR_STAIN_START, FLOOR_STAIN_FULL, FastValueNoise(position * FLOOR_STAIN_SCALE + FLOOR_STAIN_SEED)) *
        (FLOOR_STAIN_SHADE_BASE + FLOOR_STAIN_SHADE_RANGE * FastValueNoise(position * FLOOR_STAIN_SHADE_SCALE + FLOOR_STAIN_SHADE_SEED));

    // --- 物を引きずった跡（一方向に伸びた細い筋。出る場所を別のノイズで絞っている）
    const float scuffLine = smoothstep(FLOOR_SCUFF_LINE_START, FLOOR_SCUFF_LINE_FULL,
        FastValueNoise(float2(position.x * FLOOR_SCUFF_NOISE_X, position.y * FLOOR_SCUFF_NOISE_Z) + FLOOR_SCUFF_NOISE_SEED));
    const float scuff = scuffLine * smoothstep(FLOOR_SCUFF_MASK_START, FLOOR_SCUFF_MASK_FULL, FastValueNoise(position * FLOOR_SCUFF_MASK_SCALE + FLOOR_SCUFF_MASK_SEED)) *
        saturate(1.0f - footprint / FLOOR_SCUFF_DISTANCE_FILTER);

    // --- 壁際にたまった埃（部屋の角の暗がりと同じ壁の形から、壁までの距離を求めている）
    const float dust = exp(-GetNearestWallDistance(position) / FLOOR_WALL_DUST_FALLOFF_DISTANCE);

    // 目地とひびを、へこみの高さにしている
    ageing.height = -(joint * FLOOR_JOINT_DEPTH + crack * FLOOR_CRACK_DEPTH) * amount;

    // 床版ごとの色、目地・ひびの暗さ、染み、引きずった跡、埃を順に重ねている
    float3 tint = (FLOOR_SLAB_TINT_BASE + FLOOR_SLAB_TINT_RANGE * slabShade).xxx;
    tint *= 1.0f - joint * FLOOR_JOINT_DARKEN;
    tint *= 1.0f - crack * FLOOR_CRACK_DARKEN;
    tint = lerp(tint, tint * FLOOR_STAIN_COLOR, stain * FLOOR_STAIN_BLEND);
    tint = lerp(tint, tint * FLOOR_SCUFF_BRIGHTEN, scuff * FLOOR_SCUFF_BLEND);
    tint = lerp(tint, FLOOR_DUST_COLOR, dust * FLOOR_DUST_BLEND);
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

#endif
