// ============================================================================
// シェーダーの部品: 建物の壁と天井の表面（汚れ・塗りのむら・古さ・凹凸の法線・壁を伝う水の筋。litTexturePS.hlsl が使う）
// ============================================================================

#ifndef SIGNAL_LOST_WALLSURFACE_HLSLI
#define SIGNAL_LOST_WALLSURFACE_HLSLI

// 壁の汚れの量（0〜1）を計算している：床際の湿り、大きな染み、上から垂れた跡、細かい埃
// 床に近い湿りと染み・雨だれ・埃を別々に混ぜ、壁の汚れに高さの差を付ける。
static const float WALL_DAMP_HEIGHT_DECAY = 0.060f;
static const float WALL_GRIME_DAMP_WEIGHT = 0.38f;
static const float WALL_GRIME_STAIN_WEIGHT = 0.25f;
static const float WALL_GRIME_DRIP_WEIGHT = 0.17f;
static const float WALL_GRIME_DUST_WEIGHT = 0.055f;

// 染みは広いまだらに、埃は小さいマスごとの粒にする。
static const float2 WALL_STAIN_NOISE_SCALE = float2(0.026f, 0.019f);
static const float WALL_STAIN_NOISE_SEED = 37.2f;
static const float WALL_STAIN_THRESHOLD = 0.43f;
static const float WALL_STAIN_CONTRAST = 1.65f;
static const float2 WALL_DUST_NOISE_SCALE = float2(0.115f, 0.082f);
static const float WALL_DUST_NOISE_SEED = 11.8f;

// 雨だれは横方向で位置を決め、細かいノイズで筋をところどころ途切れさせる。
static const float WALL_DRIP_COLUMN_NOISE_SCALE = 0.052f;
static const float WALL_DRIP_COLUMN_NOISE_SEED = 8.7f;
static const float2 WALL_DRIP_BREAKUP_NOISE_SCALE = float2(0.017f, 0.033f);
static const float WALL_DRIP_BREAKUP_NOISE_SEED = 4.1f;
static const float WALL_DRIP_START = 0.64f;
static const float WALL_DRIP_FULL = 0.91f;
static const float WALL_DRIP_BREAKUP_START = 0.30f;
static const float WALL_DRIP_BREAKUP_FULL = 0.78f;

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
        (FastValueNoise(wallUV * WALL_STAIN_NOISE_SCALE + WALL_STAIN_NOISE_SEED) - WALL_STAIN_THRESHOLD)
        * WALL_STAIN_CONTRAST);
    // 細かい粒は、補間するノイズではなく、マス目ごとの乱数で十分。
    // 壁が画面の大半を占める場面の、画素ごとの負荷を抑えている。
    const float fineDust = FastHash21(floor(
        wallUV * WALL_DUST_NOISE_SCALE - WALL_DUST_NOISE_SEED));

    // 主に1次元のマスクから、重力の方向へ伸びる縦長の汚れを作っている。
    const float dripSeed = FastValueNoise(
        float2(wallCoordinate * WALL_DRIP_COLUMN_NOISE_SCALE, WALL_DRIP_COLUMN_NOISE_SEED));
    const float dripBreakup = FastValueNoise(
        wallUV * WALL_DRIP_BREAKUP_NOISE_SCALE + WALL_DRIP_BREAKUP_NOISE_SEED);
    const float drip = smoothstep(WALL_DRIP_START, WALL_DRIP_FULL, dripSeed) *
        smoothstep(WALL_DRIP_BREAKUP_START, WALL_DRIP_BREAKUP_FULL, dripBreakup);

    // ステージの床はワールドのY=-100あたりにあり、壁の足元に湿気がたまる。
    const float heightFromFloor = max(worldPosition.y - SCENE_BASE_FLOOR_Y, 0.0f);
    const float floorDamp = exp(-heightFromFloor * WALL_DAMP_HEIGHT_DECAY);

    return saturate(verticalSurface *
        (floorDamp * WALL_GRIME_DAMP_WEIGHT + broadStain * WALL_GRIME_STAIN_WEIGHT +
         drip * WALL_GRIME_DRIP_WEIGHT + fineDust * WALL_GRIME_DUST_WEIGHT));
}

// 壁の凹凸は、三つの大きさのノイズを組み合わせる。
// 打ちっぱなしのコンクリートは、面そのものは平らで、型枠のわずかなゆがみと細かいざらつきがあるだけなので、
// 大きなうねりは弱くし、細かいざらつきを主にしている（大きなうねりが強いと、懐中電灯の光で壁が肉や漆喰のように波打って見える）。
static const float WALL_PLASTER_BROAD_SCALE = 0.19f;
static const float WALL_PLASTER_BROAD_SEED = 3.7f;
static const float WALL_PLASTER_MID_SCALE = 0.9f;
static const float WALL_PLASTER_MID_SEED = 12.4f;
static const float WALL_PLASTER_FINE_SCALE = 3.4f;
static const float WALL_PLASTER_FINE_SEED = 27.1f;
static const float WALL_PLASTER_BROAD_WEIGHT = 0.12f;
static const float WALL_PLASTER_MID_WEIGHT = 0.10f;
static const float WALL_PLASTER_FINE_WEIGHT = 0.07f;

// 壁のざらつき（左官の跡のような凹凸）の高さを計算している
float GetProceduralSurfaceHeight(float3 worldPosition, float3 worldNormal)
{
    const float3 normal = abs(normalize(worldNormal));
    const float wallCoordinate = normal.x > normal.z
        ? worldPosition.z
        : worldPosition.x;
    const float2 wallUV = float2(wallCoordinate, worldPosition.y);

    const float broad = FastValueNoise(wallUV * WALL_PLASTER_BROAD_SCALE + WALL_PLASTER_BROAD_SEED);
    const float plaster = FastValueNoise(wallUV * WALL_PLASTER_MID_SCALE - WALL_PLASTER_MID_SEED);
    // 細かいざらつきは、マス目ごとの乱数（段差のある値）ではなく、なめらかなノイズにしている。
    // 段差があると、画面の微分で求める法線がマス目の縁で折れ、明るく照らした壁にレンガのような格子が見えるため。
    const float fine = FastValueNoise(wallUV * WALL_PLASTER_FINE_SCALE + WALL_PLASTER_FINE_SEED);
    return broad * WALL_PLASTER_BROAD_WEIGHT + plaster * WALL_PLASTER_MID_WEIGHT + fine * WALL_PLASTER_FINE_WEIGHT;
}

// ----------------------------------------------------------------------------
// 壁の古さ（1面と2面）。WallWeathering（面ごと）と Material.WeatheringSurface（建物の壁と天井だけ）で有効になる。
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
// コンクリートの縦目地と型枠の横線の幅。横線は目地より浅く見せる。
static const float WALL_VERTICAL_JOINT_CORE = 0.06f;
static const float WALL_VERTICAL_JOINT_EDGE = 0.26f;
static const float WALL_FORM_LINE_CORE = 0.03f;
static const float WALL_FORM_LINE_EDGE = 0.12f;
static const float WALL_FORM_LINE_WEIGHT = 0.45f;

// 画面で見分けられる大きさのひびだけを描き、ノイズで出る場所と途切れる場所を決める。
static const float WALL_CRACK_MASK_START = 0.62f;
static const float WALL_CRACK_MASK_FULL = 0.78f;
static const float WALL_CRACK_MASK_SCALE = 0.021f;
static const float WALL_CRACK_FOOTPRINT_LIMIT = 0.35f;
static const float WALL_CRACK_LINE_WIDTH = 0.012f;
static const float WALL_CRACK_LINE_FILTER = 0.04f;
static const float WALL_CRACK_WARP_WEIGHT = 0.22f;
static const float WALL_CRACK_BREAK_START = 0.42f;
static const float WALL_CRACK_BREAK_END = 0.58f;

// 水が溜まっていた高さの上下で色をなじませ、境目を線として残す。
static const float WALL_TIDE_MARK_BOTTOM_BLEND = 1.5f;
static const float WALL_TIDE_MARK_TOP_BLEND = 0.3f;
static const float WALL_TIDE_MARK_LINE_FALLOFF = 1.6f;

static const float WallFloorY = -100.0f;
static const float WallHeight = 51.0f;

// --- 壁の古さの値（ComputeWallAgeing）
// 打ちっぱなしコンクリートのパネルの幅（高さは壁の3分の1）と、パネルごとの色の乱数のずらし
static const float WALL_PANEL_WIDTH = 60.0f;
static const float WALL_PANEL_ROWS = 3.0f;
static const float WALL_PANEL_SHADE_SEED = 71.0f;
// 気泡の穴とひびを描く画素の大きさの上限（これより遠いと1画素より小さい）
static const float WALL_DETAIL_FOOTPRINT_LIMIT = 0.5f;
// 気泡の穴：マス目の大きさ、乱数のずらし（出るか・横・縦・大きさ）、出る割合のしきい値、位置のずらしの幅、
// 大きさ（最小＋乱数の幅）、穴の芯の割合
static const float WALL_PIT_CELL_SIZE = 1.6f;
static const float WALL_PIT_SEED = 17.3f;
static const float WALL_PIT_OFFSET_SEED_X = 3.9f;
static const float WALL_PIT_OFFSET_SEED_Y = 8.6f;
static const float WALL_PIT_SIZE_SEED = 29.4f;
static const float WALL_PIT_THRESHOLD = 0.92f;
static const float WALL_PIT_OFFSET_RANGE = 0.5f;
static const float WALL_PIT_OFFSET_CENTER = 0.25f;
static const float WALL_PIT_MIN_SIZE = 0.05f;
static const float WALL_PIT_SIZE_RANGE = 0.12f;
static const float WALL_PIT_CORE = 0.4f;
// ひび：出る場所のノイズのずらし、線を揺らすノイズ（細かさとずらし）、等高線のノイズ（細かさとずらし）、途切れさせるノイズ（細かさとずらし）
static const float WALL_CRACK_MASK_SEED = 9.0f;
static const float WALL_CRACK_WARP_SCALE = 0.9f;
static const float WALL_CRACK_WARP_SEED_X = 3.0f;
static const float WALL_CRACK_WARP_SEED_Y = 17.0f;
static const float WALL_CRACK_LINE_SCALE = 0.085f;
static const float WALL_CRACK_LINE_SEED = 61.0f;
static const float WALL_CRACK_BREAK_SCALE = 0.32f;
static const float WALL_CRACK_BREAK_SEED = 41.0f;
// 床際の水位線：高さ（最低＋揺らぎの幅）、揺らぎの細かさ、ノイズの縦の座標
static const float WALL_WATER_LINE_BASE = 6.0f;
static const float WALL_WATER_LINE_RANGE = 6.0f;
static const float WALL_WATER_LINE_SCALE = 0.04f;
static const float WALL_WATER_LINE_SEED = 3.1f;
// 上から垂れた錆と水の跡：帯の幅、出るかの乱数のずらしとしきい値（2割ほど）、半分の幅（最小＋乱数の幅）とその乱数のずらし、
// 長さ（最小＋乱数の幅）とその乱数のずらし、芯の割合、消え始める長さの割合、濃さのむら（最低＋幅、細かさ）
static const float WALL_STREAK_WIDTH = 2.4f;
static const float WALL_STREAK_SEED = 41.7f;
static const float WALL_STREAK_THRESHOLD = 0.80f;
static const float WALL_STREAK_MIN_HALF_WIDTH = 0.10f;
static const float WALL_STREAK_HALF_WIDTH_RANGE = 0.22f;
static const float WALL_STREAK_WIDTH_SEED = 7.7f;
static const float WALL_STREAK_MIN_LENGTH = 14.0f;
static const float WALL_STREAK_LENGTH_RANGE = 26.0f;
static const float WALL_STREAK_LENGTH_SEED = 13.1f;
static const float WALL_STREAK_CORE = 0.35f;
static const float WALL_STREAK_FADE_START = 0.45f;
static const float WALL_STREAK_SHADE_BASE = 0.55f;
static const float WALL_STREAK_SHADE_RANGE = 0.45f;
static const float WALL_STREAK_SHADE_COLUMN_SCALE = 3.1f;
static const float WALL_STREAK_SHADE_HEIGHT_SCALE = 0.35f;
// カビ：塊のしきい値（出始め・濃くなる）とノイズの細かさ・ずらし、床からの減り方と壁の途中に残す量、
// 斑点を描く画素の大きさの上限、回転（30度の cos と sin）、斑点のノイズ2つ（細かさ・ずらし・割合）、斑点の濃淡（最低・しきい値）
static const float WALL_MOLD_START = 0.58f;
static const float WALL_MOLD_FULL = 0.82f;
static const float WALL_MOLD_SCALE = 0.055f;
static const float WALL_MOLD_SEED = 23.0f;
static const float WALL_MOLD_HEIGHT_FALLOFF = 0.05f;
static const float WALL_MOLD_HEIGHT_FLOOR = 0.30f;
static const float WALL_MOLD_FOOTPRINT_LIMIT = 0.6f;
static const float WALL_MOLD_ROTATE_COS = 0.866f;
static const float WALL_MOLD_ROTATE_SIN = 0.5f;
static const float WALL_MOLD_GRAIN_SCALE_A = 1.1f;
static const float WALL_MOLD_GRAIN_SEED_A = 5.0f;
static const float WALL_MOLD_GRAIN_WEIGHT_A = 0.6f;
static const float WALL_MOLD_GRAIN_SCALE_B = 2.7f;
static const float WALL_MOLD_GRAIN_SEED_B = 13.0f;
static const float WALL_MOLD_GRAIN_WEIGHT_B = 0.4f;
static const float WALL_MOLD_SPECKLE_MIN = 0.45f;
static const float WALL_MOLD_SPECKLE_START = 0.30f;
static const float WALL_MOLD_SPECKLE_END = 0.75f;
// へこみの高さ（継ぎ目・穴・ひび・引っかき傷）と、奥の暗さ（同じ順）
static const float WALL_SEAM_DEPTH = 0.7f;
static const float WALL_PIT_DEPTH = 0.5f;
static const float WALL_CRACK_DEPTH = 0.35f;
static const float WALL_GOUGE_DEPTH = 0.9f;
static const float WALL_SEAM_CAVITY = 0.32f;
static const float WALL_PIT_CAVITY = 0.40f;
static const float WALL_CRACK_CAVITY = 0.42f;
static const float WALL_GOUGE_CAVITY = 0.25f;
// 色（元の色に掛ける）：錆、カビ、水に浸かっていた部分、パネルの明るさ（最低＋むらの幅）、それぞれの混ぜ方、
// 引っかき傷で明るくする量と混ぜ方、血の色と混ぜ方
static const float3 WALL_RUST_COLOR = float3(0.66f, 0.50f, 0.38f);
static const float3 WALL_MOLD_COLOR = float3(0.26f, 0.32f, 0.24f);
static const float3 WALL_SOAKED_COLOR = float3(0.72f, 0.76f, 0.70f);
static const float WALL_PANEL_TINT_BASE = 0.94f;
static const float WALL_PANEL_TINT_RANGE = 0.10f;
static const float WALL_SOAKED_BLEND = 0.60f;
static const float WALL_TIDE_RUST_DARKEN = 0.85f;
static const float WALL_TIDE_BLEND = 0.50f;
static const float WALL_STREAK_BLEND = 0.65f;
static const float WALL_MOLD_BLEND = 0.75f;
static const float WALL_GOUGE_BRIGHTEN = 1.35f;
static const float WALL_GOUGE_BLEND = 0.8f;
static const float3 WALL_BLOOD_COLOR = float3(0.46f, 0.035f, 0.030f);
static const float WALL_BLOOD_BLEND = 0.92f;
// 湿りの光沢（水に浸かっていた部分・垂れ跡・血）
static const float WALL_SOAKED_DAMP = 0.6f;
static const float WALL_STREAK_DAMP = 0.4f;
static const float WALL_BLOOD_DAMP = 0.9f;
// 1面の部屋の腐り具合（RoomDecay。周回ごとに進む）で、跡を広げる量：垂れ跡が出る列を増やす量、垂れ跡のうち血に変わる列の割合と
// その乱数のずらし、カビの塊のしきい値を下げる量と壁の途中に残す量を増やす量、床際の水位線を上げる高さ、決まった血の跡を濃くする量
static const float WALL_DECAY_STREAK_SPREAD = 0.28f;
static const float WALL_DECAY_BLOOD_SHARE = 0.55f;
static const float WALL_DECAY_BLOOD_SEED = 57.3f;
static const float WALL_DECAY_MOLD_SPREAD = 0.18f;
static const float WALL_DECAY_MOLD_HEIGHT = 0.30f;
static const float WALL_DECAY_WATER_RISE = 10.0f;
static const float WALL_DECAY_MARKS_GAIN = 1.4f;

// --- 天井の古さの値（ComputeCeilingAgeing）
// 天井板の大きさ（12四方）、枠の幅（芯・ぼかし）、板ごとの乱数のずらし、外れ落ちた板の割合のしきい値（7%ほど）
static const float CEILING_TILE_SIZE = 12.0f;
static const float CEILING_FRAME_CORE = 0.15f;
static const float CEILING_FRAME_EDGE = 0.35f;
static const float CEILING_TILE_SEED = 5.0f;
static const float CEILING_MISSING_THRESHOLD = 0.93f;
// 雨漏りの染み：出るかの乱数のずらしとしきい値（25%ほど）、中心（最小＋乱数の幅）とその乱数のずらし、半径（最小＋乱数の幅）とその乱数のずらし、
// 縁のゆがみ（最低＋ノイズの幅、細かさ・ずらし）、にじみの芯の割合、輪の鋭さ
static const float CEILING_STAIN_SEED = 19.0f;
static const float CEILING_STAIN_THRESHOLD = 0.75f;
static const float CEILING_STAIN_CENTER_MIN = 0.3f;
static const float CEILING_STAIN_CENTER_RANGE = 0.4f;
static const float CEILING_STAIN_CENTER_SEED_X = 31.0f;
static const float CEILING_STAIN_CENTER_SEED_Y = 47.0f;
static const float CEILING_STAIN_MIN_RADIUS = 2.0f;
static const float CEILING_STAIN_RADIUS_RANGE = 3.0f;
static const float CEILING_STAIN_RADIUS_SEED = 61.0f;
static const float CEILING_STAIN_WARP_BASE = 0.85f;
static const float CEILING_STAIN_WARP_RANGE = 0.3f;
static const float CEILING_STAIN_WARP_SCALE = 0.4f;
static const float CEILING_STAIN_WARP_SEED = 3.0f;
static const float CEILING_STAIN_CORE = 0.55f;
static const float CEILING_STAIN_RING_SHARPNESS = 3.0f;
// 高さ（枠は少し下に出る、外れた板の穴はへこむ）と穴の暗さ
static const float CEILING_FRAME_HEIGHT = 0.4f;
static const float CEILING_MISSING_DEPTH = 1.5f;
static const float CEILING_MISSING_CAVITY = 0.9f;
// 色：板の明るさ（最低＋乱数の幅）、枠、染みのにじみと輪（色と混ぜ方）、穴の暗さ、染みの湿り
static const float CEILING_TILE_BRIGHTNESS = 2.3f;
static const float CEILING_TILE_BRIGHTNESS_RANGE = 0.4f;
static const float3 CEILING_FRAME_COLOR = float3(1.6f, 1.6f, 1.55f);
static const float3 CEILING_STAIN_COLOR = float3(0.78f, 0.62f, 0.44f);
static const float CEILING_STAIN_BLEND = 0.65f;
static const float3 CEILING_STAIN_RING_COLOR = float3(0.50f, 0.40f, 0.30f);
static const float CEILING_STAIN_RING_BLEND = 0.55f;
static const float CEILING_MISSING_SHADE = 0.20f;
static const float CEILING_STAIN_DAMP = 0.3f;

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
    const float footprint = max(length(fwidth(uv)), MIN_PIXEL_FOOTPRINT);

    // --- 打ちっぱなしコンクリートの継ぎ目（幅60・高さ17のパネル。壁の文字の高さ y=-70 は横の継ぎ目を避けている）
    // 縦の継ぎ目はくっきり、横の継ぎ目（型枠の跡）は細く浅くして、タイル張りに見えないようにしている。
    const float2 panelSize = float2(WALL_PANEL_WIDTH, WallHeight / WALL_PANEL_ROWS);
    const float2 panelUV = float2(uv.x, heightFromFloor) / panelSize;
    const float2 edgeDistance = (0.5f - abs(frac(panelUV) - 0.5f)) * panelSize;
    const float verticalJoint = 1.0f - smoothstep(WALL_VERTICAL_JOINT_CORE, WALL_VERTICAL_JOINT_EDGE + footprint, edgeDistance.x);
    const float formLine = (1.0f - smoothstep(WALL_FORM_LINE_CORE, WALL_FORM_LINE_EDGE + footprint, edgeDistance.y)) * WALL_FORM_LINE_WEIGHT;
    const float seam = max(verticalJoint, formLine);
    // パネルごとに、打った日の違いで色をわずかに変えている。
    const float panelShade = FastHash21(floor(panelUV) + WALL_PANEL_SHADE_SEED);

    // 気泡の穴とひびは、遠くて1画素より小さくなる場所では見えないため、計算を省いている。
    float pit = 0.0f;
    float crack = 0.0f;
    [branch]
    if (footprint < WALL_DETAIL_FOOTPRINT_LIMIT)
    {
        // --- 小さな気泡の穴（マス目の中で位置と大きさをばらつかせ、格子状に並ばないようにしている）
        const float pitCellSize = WALL_PIT_CELL_SIZE;
        const float2 pitCell = floor(uv / pitCellSize);
        const float pitSeed = FastHash21(pitCell + WALL_PIT_SEED);
        [branch]
        if (pitSeed >= WALL_PIT_THRESHOLD)
        {
            const float2 pitOffset = float2(
                FastHash21(pitCell + WALL_PIT_OFFSET_SEED_X), FastHash21(pitCell + WALL_PIT_OFFSET_SEED_Y)) * WALL_PIT_OFFSET_RANGE - WALL_PIT_OFFSET_CENTER;
            const float pitSize = WALL_PIT_MIN_SIZE + WALL_PIT_SIZE_RANGE * FastHash21(pitCell + WALL_PIT_SIZE_SEED);
            const float pitRadius = length(frac(uv / pitCellSize) - 0.5f - pitOffset) * pitCellSize;
            pit = (1.0f - smoothstep(pitSize * WALL_PIT_CORE, pitSize + footprint, pitRadius)) *
                saturate(1.0f - footprint / WALL_DETAIL_FOOTPRINT_LIMIT);
        }

        // --- ひび（ノイズの等高線を細い線として使い、別のノイズで出る場所を絞っている）
        // 等高線だけだと大きな輪のようななめらかな曲線になるため、細かいノイズで線をギザギザに揺らし、
        // さらに別のノイズで途切れ途切れにして、割れたひびらしくしている。
        const float crackMask = smoothstep(WALL_CRACK_MASK_START, WALL_CRACK_MASK_FULL, FastValueNoise(uv * WALL_CRACK_MASK_SCALE + WALL_CRACK_MASK_SEED));
        [branch]
        if (crackMask > 0.0f && footprint < WALL_CRACK_FOOTPRINT_LIMIT)
        {
            const float2 crackWarp = float2(
                FastValueNoise(uv * WALL_CRACK_WARP_SCALE + WALL_CRACK_WARP_SEED_X), FastValueNoise(uv * WALL_CRACK_WARP_SCALE + WALL_CRACK_WARP_SEED_Y)) - 0.5f;
            const float crackLine = 1.0f - smoothstep(0.0f, WALL_CRACK_LINE_WIDTH + footprint * WALL_CRACK_LINE_FILTER,
                abs(FastValueNoise(uv * WALL_CRACK_LINE_SCALE + WALL_CRACK_LINE_SEED + crackWarp * WALL_CRACK_WARP_WEIGHT) - 0.5f));
            const float crackPieces = smoothstep(WALL_CRACK_BREAK_START, WALL_CRACK_BREAK_END, FastValueNoise(uv * WALL_CRACK_BREAK_SCALE + WALL_CRACK_BREAK_SEED));
            crack = crackLine * crackMask * crackPieces * saturate(1.0f - footprint / WALL_CRACK_FOOTPRINT_LIMIT);
        }
    }

    // --- 床際の水位線（水がたまっていた跡）。高さは場所によって6〜12で揺らいでいる
    // 部屋が腐っていくほど（RoomDecay）、水の跡は高くまで上がる
    const float waterLine = WALL_WATER_LINE_BASE + RoomDecay * WALL_DECAY_WATER_RISE + WALL_WATER_LINE_RANGE * FastValueNoise(float2(uv.x * WALL_WATER_LINE_SCALE, WALL_WATER_LINE_SEED));
    const float belowWater = 1.0f - smoothstep(waterLine - WALL_TIDE_MARK_BOTTOM_BLEND, waterLine + WALL_TIDE_MARK_TOP_BLEND, heightFromFloor);
    const float tideMark = exp(-abs(heightFromFloor - waterLine) * WALL_TIDE_MARK_LINE_FALLOFF);

    // --- 上から垂れた錆と水の跡（幅2.4ごとの縦の帯のうち、2割ほどに出している）
    const float streakWidth = WALL_STREAK_WIDTH;
    const float column = floor(uv.x / streakWidth);
    const float streakSeed = FastHash21(float2(column, WALL_STREAK_SEED));
    float streak = 0.0f;
    // 部屋が腐っていくほど垂れ跡の列が増え、その一部は錆ではなく血の垂れ跡になる
    const float bloodStreak = step(FastHash21(float2(column, WALL_DECAY_BLOOD_SEED)), RoomDecay * WALL_DECAY_BLOOD_SHARE);
    [branch]
    if (streakSeed >= WALL_STREAK_THRESHOLD - RoomDecay * WALL_DECAY_STREAK_SPREAD)
    {
        const float streakX = abs(frac(uv.x / streakWidth) - 0.5f);
        const float streakHalfWidth = WALL_STREAK_MIN_HALF_WIDTH + WALL_STREAK_HALF_WIDTH_RANGE * FastHash21(float2(column, WALL_STREAK_WIDTH_SEED));
        const float streakLength = WALL_STREAK_MIN_LENGTH + WALL_STREAK_LENGTH_RANGE * FastHash21(float2(column, WALL_STREAK_LENGTH_SEED));
        const float fromTop = WallHeight - heightFromFloor;
        streak = (1.0f - smoothstep(streakHalfWidth * WALL_STREAK_CORE, streakHalfWidth + footprint, streakX)) *
            (1.0f - smoothstep(streakLength * WALL_STREAK_FADE_START, streakLength, fromTop)) *
            (WALL_STREAK_SHADE_BASE + WALL_STREAK_SHADE_RANGE * FastValueNoise(float2(column * WALL_STREAK_SHADE_COLUMN_SCALE, heightFromFloor * WALL_STREAK_SHADE_HEIGHT_SCALE)));
    }

    // --- カビ（床際に多く、壁の途中にも固まって生えている）。細かい斑点はなめらかなノイズで丸くにじませている
    // 値ノイズは格子に沿った四角い形が出やすいため、座標を回転させ、大きさの違う2つを混ぜて格子を崩している。
    // 部屋が腐っていくほど（RoomDecay）、カビの塊は広がり、壁の高い所まで生える
    const float moldSpread = RoomDecay * WALL_DECAY_MOLD_SPREAD;
    const float moldCluster = smoothstep(WALL_MOLD_START - moldSpread, WALL_MOLD_FULL - moldSpread, FastValueNoise(uv * WALL_MOLD_SCALE + WALL_MOLD_SEED));
    float mold = moldCluster * saturate(exp(-heightFromFloor * WALL_MOLD_HEIGHT_FALLOFF) + WALL_MOLD_HEIGHT_FLOOR + RoomDecay * WALL_DECAY_MOLD_HEIGHT);
    [branch]
    if (moldCluster > 0.0f && footprint < WALL_MOLD_FOOTPRINT_LIMIT)
    {
        const float2 rotatedUV = float2(
            uv.x * WALL_MOLD_ROTATE_COS - uv.y * WALL_MOLD_ROTATE_SIN,
            uv.x * WALL_MOLD_ROTATE_SIN + uv.y * WALL_MOLD_ROTATE_COS);
        const float moldGrain =
            FastValueNoise(rotatedUV * WALL_MOLD_GRAIN_SCALE_A + WALL_MOLD_GRAIN_SEED_A) * WALL_MOLD_GRAIN_WEIGHT_A +
            FastValueNoise(uv * WALL_MOLD_GRAIN_SCALE_B - WALL_MOLD_GRAIN_SEED_B) * WALL_MOLD_GRAIN_WEIGHT_B;
        // 斑点はカビの塊の中の濃淡として使う。
        const float moldSpeckle = lerp(WALL_MOLD_SPECKLE_MIN, 1.0f, smoothstep(WALL_MOLD_SPECKLE_START, WALL_MOLD_SPECKLE_END, moldGrain));
        mold *= lerp(1.0f, moldSpeckle, saturate(1.0f - footprint / WALL_MOLD_FOOTPRINT_LIMIT));
    }

    // --- ホラーらしい跡（血の手形と引きずった跡、引っかき傷、血の垂れ跡。horrorMarks.hlsli）
    // 壁の面の位置（法線の向きの座標）も渡し、平行な別の壁に同じ跡が並ばないようにしている。
    const float planeCoordinate = normal.x > normal.z ? worldPosition.x : worldPosition.z;
    // 2面（HorrorMarksStrength = 0）は、手形や傷を異変として出すため、決まった跡を描かない（計算も省いている）
    HorrorMarks marks;
    marks.blood = 0.0f;
    marks.gouge = 0.0f;
    [branch]
    if (HorrorMarksStrength > 0.0f)
    {
        marks = ComputeHorrorMarks(uv.x, heightFromFloor, planeCoordinate, footprint);
        marks.blood = saturate(marks.blood * HorrorMarksStrength * (1.0f + RoomDecay * WALL_DECAY_MARKS_GAIN));
        marks.gouge *= HorrorMarksStrength;
    }

    // 継ぎ目・穴・ひび・引っかき傷を、へこみの高さと、奥の暗さにしている
    ageing.height = -(seam * WALL_SEAM_DEPTH + pit * WALL_PIT_DEPTH + crack * WALL_CRACK_DEPTH + marks.gouge * WALL_GOUGE_DEPTH) * weight;
    ageing.cavity = saturate(seam * WALL_SEAM_CAVITY + pit * WALL_PIT_CAVITY + crack * WALL_CRACK_CAVITY + marks.gouge * WALL_GOUGE_CAVITY) * weight;

    // 色は元の色に掛けている（茶色＝錆、くすんだ緑＝カビ、少し暗い灰緑＝水に浸かっていた部分）。
    float3 tint = (WALL_PANEL_TINT_BASE + WALL_PANEL_TINT_RANGE * panelShade).xxx;
    tint = lerp(tint, WALL_SOAKED_COLOR, belowWater * WALL_SOAKED_BLEND);
    tint = lerp(tint, WALL_RUST_COLOR * WALL_TIDE_RUST_DARKEN, tideMark * WALL_TIDE_BLEND);
    tint = lerp(tint, lerp(WALL_RUST_COLOR, WALL_BLOOD_COLOR, bloodStreak), streak * WALL_STREAK_BLEND);
    tint = lerp(tint, WALL_MOLD_COLOR, mold * WALL_MOLD_BLEND);
    // 引っかき傷は、汚れた表面が削れて下の明るいコンクリートが見えている。血は赤黒く、青と緑を強く吸う
    tint = lerp(tint, tint * WALL_GOUGE_BRIGHTEN, marks.gouge * WALL_GOUGE_BLEND);
    tint = lerp(tint, WALL_BLOOD_COLOR, marks.blood * WALL_BLOOD_BLEND);
    ageing.tint = lerp(1.0f.xxx, tint, weight);
    // 血は乾ききっておらず、浅い角度で懐中電灯の光を返す（湿りの光沢を使っている）
    ageing.damp = saturate(belowWater * WALL_SOAKED_DAMP + streak * WALL_STREAK_DAMP + marks.blood * WALL_BLOOD_DAMP) * weight;
    return ageing;
}

// 天井の古さ。金属の枠に並んだ天井板（12四方。この世界では約60cm）、外れ落ちた板の穴、雨漏りの染みを描いている。
// 天井の元の色はほぼ黒（0.055）なので、板の部分は色を掛けて暗い灰色まで明るくしている（ホラーらしく、見上げても暗く沈む程度）。
WallAgeing ComputeCeilingAgeing(float2 position, float weight)
{
    WallAgeing ageing;
    const float footprint = max(length(fwidth(position)), MIN_PIXEL_FOOTPRINT);

    // --- 天井板と金属の枠
    const float tileSize = CEILING_TILE_SIZE;
    const float2 tileUV = position / tileSize;
    const float2 tileId = floor(tileUV);
    const float2 local = frac(tileUV);
    const float2 edgeDistance = (0.5f - abs(local - 0.5f)) * tileSize;
    const float frame = 1.0f - smoothstep(CEILING_FRAME_CORE, CEILING_FRAME_EDGE + footprint, min(edgeDistance.x, edgeDistance.y));
    const float tileSeed = FastHash21(tileId + CEILING_TILE_SEED);

    // --- 外れ落ちた板（7%ほど）。枠だけ残り、奥の暗い空間が見えている
    const float missing = step(CEILING_MISSING_THRESHOLD, tileSeed) * (1.0f - frame);

    // --- 雨漏りの染み（板の25%ほど）。茶色いにじみと、縁の濃い輪
    const float stainSeed = FastHash21(tileId + CEILING_STAIN_SEED);
    const float2 stainCenter = CEILING_STAIN_CENTER_MIN + CEILING_STAIN_CENTER_RANGE * float2(
        FastHash21(tileId + CEILING_STAIN_CENTER_SEED_X), FastHash21(tileId + CEILING_STAIN_CENTER_SEED_Y));
    const float stainRadius = CEILING_STAIN_MIN_RADIUS + CEILING_STAIN_RADIUS_RANGE * FastHash21(tileId + CEILING_STAIN_RADIUS_SEED);
    const float stainDistance = length(local - stainCenter) * tileSize *
        (CEILING_STAIN_WARP_BASE + CEILING_STAIN_WARP_RANGE * FastValueNoise(position * CEILING_STAIN_WARP_SCALE + CEILING_STAIN_WARP_SEED));
    const float stainOn = step(CEILING_STAIN_THRESHOLD, stainSeed) * (1.0f - missing) * (1.0f - frame);
    const float stainFill = (1.0f - smoothstep(stainRadius * CEILING_STAIN_CORE, stainRadius, stainDistance)) * stainOn;
    const float stainRing = exp(-abs(stainDistance - stainRadius) * CEILING_STAIN_RING_SHARPNESS) * stainOn;

    // 枠は板より少し下に出ていて、外れた板の穴は奥へへこんでいる。
    ageing.height = (frame * CEILING_FRAME_HEIGHT - missing * CEILING_MISSING_DEPTH) * weight;
    ageing.cavity = missing * CEILING_MISSING_CAVITY * weight;

    // 板・枠・染み・穴の色
    const float3 tileColor = (CEILING_TILE_BRIGHTNESS + CEILING_TILE_BRIGHTNESS_RANGE * tileSeed).xxx;
    float3 tint = lerp(tileColor, CEILING_FRAME_COLOR, frame);
    tint = lerp(tint, tint * CEILING_STAIN_COLOR, stainFill * CEILING_STAIN_BLEND);
    tint = lerp(tint, tint * CEILING_STAIN_RING_COLOR, stainRing * CEILING_STAIN_RING_BLEND);
    tint = lerp(tint, CEILING_MISSING_SHADE.xxx, missing);
    ageing.tint = lerp(1.0f.xxx, tint, weight);
    ageing.damp = stainFill * CEILING_STAIN_DAMP * weight;
    return ageing;
}

// その面の古さを返している（壁・天井・それ以外で分けている）
// 縦の面とみなす範囲（壁の古さが出始める・出きる）と、下を向いた面とみなす範囲（天井の古さ）
static const float WALL_AGEING_VERTICAL_START = 0.5f;
static const float WALL_AGEING_VERTICAL_FULL = 0.9f;
static const float CEILING_AGEING_DOWN_START = 0.7f;
static const float CEILING_AGEING_DOWN_FULL = 0.95f;

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
    const float weight = amount * smoothstep(WALL_AGEING_VERTICAL_START, WALL_AGEING_VERTICAL_FULL, verticalSurface);
    const float ceilingWeight = amount * smoothstep(CEILING_AGEING_DOWN_START, CEILING_AGEING_DOWN_FULL, -signedNormal.y);
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

// 壁を伝う水の筋（建物の壁の、湿った所だけ）。0〜1の濡れ具合を返している。
// 壁の横方向を細い列に分け、列ごとに乱数で筋があるか・太さ・流れる速さを決め、少しくねらせた細い線にしている。
// 線の中では、薄い水の膜の上を、水の粒が上から下へゆっくり流れ落ちていく（時間で縦方向の濃淡をずらしている）。
// 濡れた所は粗さが下がるので、懐中電灯で照らすと筋だけが細く光る（pbrLighting.hlsli）。
// 垂直に近い面だけで、水が流れる列を決める。
static const float WALL_TRICKLE_TILT_FADE = 4.0f;
static const float WALL_TRICKLE_COLUMN_WIDTH = 3.6f;
static const float WALL_TRICKLE_COLUMN_SEED = 3.7f;
static const float WALL_TRICKLE_COLUMN_THRESHOLD = 0.80f;

// 列に沿って線をくねらせ、太さも列ごとに変える。
static const float WALL_TRICKLE_WOBBLE_COLUMN_SCALE = 1.3f;
static const float WALL_TRICKLE_WOBBLE_HEIGHT_SCALE = 0.08f;
static const float WALL_TRICKLE_MIN_WIDTH = 0.07f;
static const float WALL_TRICKLE_MAX_WIDTH = 0.17f;
static const float WALL_TRICKLE_WIDTH_SEED = 9.1f;

// 粒の流れをワールド座標に固定し、経過時間で下へ移動させる。
static const float WALL_TRICKLE_MIN_SPEED = 2.5f;
static const float WALL_TRICKLE_MAX_SPEED = 6.0f;
static const float WALL_TRICKLE_SPEED_SEED = 5.3f;
static const float WALL_TRICKLE_BEAD_FREQUENCY = 0.11f;
static const float WALL_TRICKLE_PHASE_SEED_SCALE = 7.0f;

// 天井付近で水が流れ始め、湿った区間からはみ出さないようにする。
static const float WALL_TRICKLE_DAMP_NOISE_SCALE = 0.035f;
static const float WALL_TRICKLE_DAMP_NOISE_SEED = 7.0f;
static const float WALL_TRICKLE_DAMP_BIAS = 0.25f;
static const float WALL_TRICKLE_DAMP_START = 0.56f;
static const float WALL_TRICKLE_DAMP_FULL = 0.72f;

// 水の筋のくねりの幅（ノイズに掛ける量と、線をずらす量）
static const float WALL_TRICKLE_WOBBLE_AMOUNT = 0.6f;
// 流れ落ちる水の粒：粒になる範囲（濃くなり始め・濃くなりきる）と、水の膜の濃さ（最低＋粒の幅）
static const float WALL_TRICKLE_BEAD_START = 0.55f;
static const float WALL_TRICKLE_BEAD_END = 0.95f;
static const float WALL_TRICKLE_FILM_BASE = 0.55f;
static const float WALL_TRICKLE_FILM_RANGE = 0.45f;
// 天井の際から流れ始める速さ（高さの割合に掛ける）と、天井近くの水の量（床に近いほど1）
static const float WALL_TRICKLE_CEILING_FADE = 8.0f;
static const float WALL_TRICKLE_TOP_AMOUNT = 0.55f;

float GetWallTrickle(float3 worldPosition, float3 worldNormal, float damp)
{
    const float3 axis = abs(normalize(worldNormal));
    // 縦の面だけ（天井や床、上を向いた面には流れない）
    const float verticalSurface = saturate(1.0f - axis.y * WALL_TRICKLE_TILT_FADE);
    const float along = axis.x > axis.z ? worldPosition.z : worldPosition.x;

    const float columnWidth = WALL_TRICKLE_COLUMN_WIDTH;
    const float column = floor(along / columnWidth);
    const float local = frac(along / columnWidth) - 0.5f;
    const float seed = FastHash21(float2(column, WALL_TRICKLE_COLUMN_SEED));
    // 水の筋が出る列だけで、流れや濡れ具合を計算する。
    [branch]
    if (seed < WALL_TRICKLE_COLUMN_THRESHOLD || verticalSurface <= 0.0f)
    {
        return 0.0f;
    }
    // 筋の線：列ごとの太さで、高さに沿ってわずかにくねらせている
    const float wobble = (FastValueNoise(float2(column * WALL_TRICKLE_WOBBLE_COLUMN_SCALE, worldPosition.y * WALL_TRICKLE_WOBBLE_HEIGHT_SCALE)) - 0.5f) * WALL_TRICKLE_WOBBLE_AMOUNT;
    const float width = lerp(WALL_TRICKLE_MIN_WIDTH, WALL_TRICKLE_MAX_WIDTH, FastHash21(float2(column, WALL_TRICKLE_WIDTH_SEED)));
    const float trickleLine = 1.0f - smoothstep(width * 0.5f, width, abs(local - wobble * WALL_TRICKLE_WOBBLE_AMOUNT));

    // 上から下へ流れ落ちる水の粒（濃淡の模様を、時間とともに下へずらしている）
    const float speed = lerp(WALL_TRICKLE_MIN_SPEED, WALL_TRICKLE_MAX_SPEED, FastHash21(float2(column, WALL_TRICKLE_SPEED_SEED)));
    const float flowPhase = worldPosition.y * WALL_TRICKLE_BEAD_FREQUENCY + CausticTime * speed * WALL_TRICKLE_BEAD_FREQUENCY + seed * WALL_TRICKLE_PHASE_SEED_SCALE;
    const float beads = smoothstep(WALL_TRICKLE_BEAD_START, WALL_TRICKLE_BEAD_END, frac(flowPhase));
    const float film = WALL_TRICKLE_FILM_BASE + beads * WALL_TRICKLE_FILM_RANGE;

    // 天井の際から流れ始め、床に近いほど水が多い
    const float height = (worldPosition.y - WallFloorY) / WallHeight;
    const float fromCeiling = saturate((1.0f - height) * WALL_TRICKLE_CEILING_FADE);
    const float lowerAmount = lerp(1.0f, WALL_TRICKLE_TOP_AMOUNT, saturate(height));

    // 壁の湿った区間（壁に沿った大きなまだら）だけに流している。床際の水の跡（damp）がある所は、少し流れやすくしている
    const float wetSection = FastValueNoise(float2(along * WALL_TRICKLE_DAMP_NOISE_SCALE, WALL_TRICKLE_DAMP_NOISE_SEED)) + damp * WALL_TRICKLE_DAMP_BIAS;
    const float dampArea = smoothstep(WALL_TRICKLE_DAMP_START, WALL_TRICKLE_DAMP_FULL, wetSection);
    return trickleLine * film * fromCeiling * lowerAmount * dampArea * verticalSurface;
}

#endif
