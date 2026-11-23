// ============================================================================
// ファイルの役割: 濡れた床の材質、浸水の境界、色と反射の調整値をまとめる。
// ============================================================================

#ifndef SIGNAL_LOST_WET_FLOOR_MATERIAL_SETTINGS_HLSLI
#define SIGNAL_LOST_WET_FLOOR_MATERIAL_SETTINGS_HLSLI

// --- 床の色の値（main。型付きの定数にすると FXC の最適化が変わるため #define にしている）
// 明るさを求める重み（Rec.709）、床の素材の色を灰色に寄せる明るさと強さ（古さに掛ける）、素材の色を残す割合
#define LUMINANCE_WEIGHTS float3(0.2126f, 0.7152f, 0.0722f)
#define FLOOR_GRAY_LEVEL (0.40f)
#define FLOOR_GRAY_STRENGTH (0.65f)
#define FLOOR_TEXTURE_KEEP (0.10f)
// 広い範囲の明るさのむら（細かさ・ずらし・暗い所・明るい所）と、少し緑がかった灰色
#define FLOOR_BROAD_SCALE (0.020f)
#define FLOOR_BROAD_SEED (6.4f)
#define FLOOR_BROAD_DARK (0.88f)
#define FLOOR_BROAD_BRIGHT (1.04f)
#define FLOOR_CONCRETE_TINT float3(0.72f, 0.75f, 0.73f)
// 目地とひびの凹凸の強さ
#define FLOOR_BUMP_STRENGTH (0.9f)
// 床一面が水に浸かった範囲：内側とみなす距離（出始め・出きる）、ゆるい波（x・z の細かさ、速さ、強さ）
#define FLOOD_EDGE_START (-0.5f)
#define FLOOD_EDGE_FULL (1.5f)
#define FLOOD_WAVE_SCALE_X (0.21f)
#define FLOOD_WAVE_SCALE_Z (0.13f)
#define FLOOD_WAVE_SPEED (0.5f)
#define FLOOD_WAVE_STRENGTH (0.35f)
// 押し寄せる赤い水：先頭の縁を崩すノイズの細かさ・流れる速さ・削る深さ（縁から内側へ）と、波を荒くする倍率
#define FLOOD_FRONT_NOISE_SCALE (0.11f)
#define FLOOD_FRONT_NOISE_SPEED (1.8f)
#define FLOOD_FRONT_NOISE_DEPTH (9.0f)
#define FLOOD_TURBULENT_WAVE_GAIN (3.0f)
// 雨漏りの水たまり：調べる範囲に足すゆとり、縁のゆがみ（細かさ・強さ）、半径の下限、水たまりの縁（ぼかし始め）、
// 岸の線（幅・位置・濃さ）、水滴の輪（間隔・速さ・強さ・遠くで弱める量）
#define LEAK_REACH_MARGIN (3.0f)
#define LEAK_EDGE_NOISE_SCALE (0.35f)
#define LEAK_EDGE_NOISE_STRENGTH (0.35f)
#define LEAK_MIN_RADIUS (0.01f)
#define LEAK_PUDDLE_EDGE (0.80f)
#define LEAK_SHORE_WIDTH (0.08f)
#define LEAK_SHORE_POSITION (0.97f)
#define LEAK_SHORE_STRENGTH (0.6f)
#define LEAK_RING_FREQUENCY (2.2f)
#define LEAK_RING_SPEED (2.6f)
#define LEAK_RING_STRENGTH (0.30f)
#define LEAK_RING_FALLOFF (0.25f)
// 水が引いた柱の脇：調べる範囲（x・z の半分）、縁のゆがみ（細かさ・強さ）、残った水の楕円（中心のずれと半径）、
// 残った水を水たまりにする割合、湿りの楕円（中心のずれと半径）
#define DRAIN_AREA_HALF_X (32.0f)
#define DRAIN_AREA_HALF_Z (27.0f)
#define DRAIN_EDGE_NOISE_SCALE (0.22f)
#define DRAIN_EDGE_NOISE_STRENGTH (0.28f)
#define DRAIN_POOL_CENTER float2(13.0f, -8.0f)
#define DRAIN_POOL_RADIUS float2(13.0f, 8.0f)
#define DRAIN_POOL_PUDDLE (0.92f)
#define DRAIN_DAMP_CENTER float2(7.0f, -5.0f)
#define DRAIN_DAMP_RADIUS float2(23.0f, 18.0f)
// 足跡を出さない水たまりの濃さ（水たまりの濃さに掛ける）
#define FOOTPRINT_PUDDLE_EXCLUDE (2.0f)
// 水たまり・岸とみなす濃さの下限（これより薄いと波の計算を省く）。光と反射の計算の下限も同じ
#define WATER_EPSILON (0.001f)
// 波：水滴の輪を波の模様に足す量、流れるノイズの速さ（x・z）、細かさ、傾きを求めるずらし、
// 平らな面へ寄せる強さ、凹凸の強さ（最低＋波の模様の幅）、本物の波を模様に足す量と法線を傾ける強さ
#define DRIP_RING_PATTERN (1.35f)
#define RIPPLE_FLOW_X (0.018f)
#define RIPPLE_FLOW_Z (0.013f)
#define RIPPLE_NOISE_SCALE (0.095f)
#define RIPPLE_SLOPE_OFFSET (0.035f)
#define PUDDLE_FLATTEN_GAIN (1.5f)
#define RIPPLE_BUMP_BASE (0.72f)
#define RIPPLE_BUMP_PATTERN (0.55f)
#define SIMULATED_WAVE_PATTERN (1.4f)
#define SIMULATED_WAVE_TILT (1.6f)
// 濡れた床の色：水を通して透ける床の暗さ（浅い所・深い所）、水の色と明るさに足す量、混ぜ方、水たまりの濃さに掛ける割合
#define WET_SHALLOW_SHADE (0.76f)
#define WET_DEEP_SHADE (0.58f)
#define WET_WATER_TINT float3(0.018f, 0.030f, 0.034f)
#define WET_LUMINANCE_LIFT (0.035f)
#define WET_DEPTH_BLEND (0.38f)
#define WET_PUDDLE_BLEND (0.84f)
// 浸水した水を赤くするとき：水面の色（反射を含む）に掛ける色（緑と青を吸う）と、足す暗い赤
#define FLOOD_BLOOD_FILTER float3(0.55f, 0.05f, 0.04f)
#define FLOOD_BLOOD_TINT float3(0.020f, 0.001f, 0.001f)
// 暗くする量：岸の線、水が引いた後の湿り、残った水
#define SHORE_DARKEN (0.035f)
#define DRAINED_DAMP_DARKEN (0.24f)
#define REMAINING_WATER_DARKEN (0.15f)
// 濡れた足跡：暗さ、水の色、混ぜ方
#define FOOTPRINT_SHADE (0.38f)
#define FOOTPRINT_WATER_TINT float3(0.004f, 0.007f, 0.008f)
#define FOOTPRINT_BLEND (0.90f)
// 点光源（litTexturePS と同じ）：なだらかな当たり方（最低＋割合）、横へ残す光の量
#define SOFT_LAMBERT_BASE (0.20f)
#define SOFT_LAMBERT_RANGE (0.80f)
#define FIXTURE_SIDE_LIGHT (0.22f)
// 水たまりの点光源の光沢：鋭さと強さ。天井の照明が水面に映る範囲（光の届く距離の割合）、鋭さ、強さ
#define POINT_GLOSS_EXPONENT (12.0f)
#define POINT_GLOSS_STRENGTH (0.58f)
#define FIXTURE_REFLECTION_RADIUS (0.46f)
#define FIXTURE_REFLECTION_EXPONENT (4.5f)
#define FIXTURE_REFLECTION_STRENGTH (0.38f)
// 懐中電灯：なだらかな当たり方（最低＋割合）、水面で反射へ回す分（水たまりで残す割合）
#define FLASHLIGHT_SOFT_LAMBERT_BASE (0.25f)
#define FLASHLIGHT_SOFT_LAMBERT_RANGE (0.75f)
#define FLASHLIGHT_PUDDLE_DIFFUSE (0.74f)
// 濡れた面の鏡面反射の鋭さ（乾いた床・湿りで下げる量・水たまり）と強さ（乾いた床・湿りで足す量・水たまり）
#define WET_SPECULAR_DRY_EXPONENT (18.0f)
#define WET_SPECULAR_DAMP_EXPONENT (6.0f)
#define WET_SPECULAR_PUDDLE_EXPONENT (92.0f)
#define WET_SPECULAR_DRY (0.025f)
#define WET_SPECULAR_DAMP (0.035f)
#define WET_SPECULAR_PUDDLE (0.20f)
// 濡れた足跡の光沢（鋭さと強さ）、浅い水の広い反射（最低＋フレネルの幅）、波の光の輪（傾きに掛ける量と強さ）
#define FOOTPRINT_GLOSS_EXPONENT (48.0f)
#define FOOTPRINT_GLOSS_STRENGTH (0.55f)
#define SHALLOW_REFLECTION_BASE (0.025f)
#define SHALLOW_REFLECTION_FRESNEL (0.08f)
#define WAVE_GLINT_SLOPE_GAIN (3.0f)
#define WAVE_GLINT_STRENGTH (0.40f)
// 比べ用の反射の重み、人影で水面を沈める量（水たまりの濃さに掛ける量と暗さ）
#define ALTERNATE_REFLECTION_WEIGHT (0.38f)
#define FIGURE_PUDDLE_GAIN (1.4f)
#define FIGURE_PUDDLE_DARKEN (0.82f)

// 床の画素の色を計算している：床の色と古さ → 水たまり → 照明 → 光沢 → 反射 → 霧 → 色調
// 水面と濡れた足跡は別の色と強さで光を返す。
#define PUDDLE_SKY_GLINT_TINT float3(0.12f, 0.15f, 0.16f)
#define PUDDLE_SKY_GLINT_BASE 0.07f
#define PUDDLE_SKY_GLINT_FRESNEL 0.54f
#define WET_FOOTPRINT_GLINT_TINT float3(0.08f, 0.10f, 0.11f)
#define WET_FOOTPRINT_GLINT_BASE 0.06f
#define WET_FOOTPRINT_GLINT_FRESNEL 0.55f

// 水滴の輪と波の傾き、岸の縁に明るさの差を付け、照明が弱くても水が分かるようにする。
#define PUDDLE_PATTERN_GLINT_EXPONENT 10.0f
#define PUDDLE_PATTERN_GLINT_TINT float3(0.12f, 0.16f, 0.17f)
#define PUDDLE_PATTERN_GLINT_GAIN 0.22f
#define PUDDLE_WAVE_GLINT_TINT float3(0.07f, 0.09f, 0.10f)
#define PUDDLE_WAVE_GLINT_BASE 0.30f
#define PUDDLE_WAVE_GLINT_FRESNEL 0.70f
#define PUDDLE_DRIP_GLINT_TINT float3(0.28f, 0.34f, 0.35f)
#define PUDDLE_DRIP_GLINT_GAIN 0.20f
#define PUDDLE_SHORE_GLINT_TINT float3(0.10f, 0.13f, 0.125f)
#define PUDDLE_SHORE_GLINT_BASE 0.24f
#define PUDDLE_SHORE_GLINT_FRESNEL 0.46f

// 水面の油膜と汚れに時間変化を与え、荒さは波と水の浅さでも変える。
#define PUDDLE_FILM_FLOW_X 0.006f
#define PUDDLE_FILM_FLOW_Z 0.004f
#define PUDDLE_FILM_NOISE_SCALE 0.045f
#define PUDDLE_GRIME_NOISE_SCALE 0.021f
#define PUDDLE_GRIME_NOISE_SEED 37.0f
#define PUDDLE_BASE_ROUGHNESS 0.18f
#define PUDDLE_FILM_ROUGHNESS_WEIGHT 0.50f
#define PUDDLE_WAVE_ROUGHNESS_WEIGHT 0.22f
#define PUDDLE_SHALLOW_MASK_GAIN 1.6f
#define PUDDLE_SHALLOW_ROUGHNESS_WEIGHT 0.35f

#endif
