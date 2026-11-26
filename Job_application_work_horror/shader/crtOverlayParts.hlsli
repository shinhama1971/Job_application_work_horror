// ============================================================================
// シェーダーの部品: ブラウン管風の画面効果（crtOverlayPS）で使う値と小さな関数。
// レンズの水滴・信号の乱れ・止まった画面・周辺減光・走査線・粒状のノイズ・緊張したときの歪み・色のずれ・
// 影のまわりのゆがみと残像・水に呑まれる視界などの強さと大きさを、ここにまとめている（main は crtOverlayPS.hlsl）。
// ============================================================================

#ifndef SIGNAL_LOST_CRT_OVERLAY_PARTS_HLSLI
#define SIGNAL_LOST_CRT_OVERLAY_PARTS_HLSLI

#include "postProcessBuffer.hlsli"
#include "renderingConfig.hlsli"
#include "fastNoise.hlsli"

// 粒状のノイズには、三角関数を使わない FastHash21 を共用する。
// 水滴の縁と中身を半径に対する割合で決め、反射と屈折で輪郭を揃える。
static const float CRT_DROP_RIM_INNER = 0.06f;
static const float CRT_DROP_RIM_OUTER = 0.22f;
static const float CRT_DROP_RIM_RADIUS = 0.86f;
static const float CRT_DROP_BODY_INNER = 0.58f;
static const float CRT_DROP_BODY_OUTER = 0.96f;
static const float CRT_DROP_HIGHLIGHT_INNER = 0.16f;
static const float CRT_DROP_HIGHLIGHT_OUTER = 0.52f;
static const float CRT_DROP_HIGHLIGHT_X = 0.30f;
static const float CRT_DROP_HIGHLIGHT_Y = 0.28f;
static const float CRT_DROP_REFRACTION_INNER = 0.80f;
static const float CRT_DROP_REFRACTION_SCALE = 1.7f;

// 信号が乱れたときにだけ、帯の一部を一定間隔で横にずらす。
static const float CRT_SIGNAL_BAND_COUNT = 54.0f;
static const float CRT_SIGNAL_UPDATES_PER_SECOND = 18.0f;
static const float CRT_SIGNAL_GATE_START = 0.70f;
static const float CRT_SIGNAL_GATE_END = 0.96f;
static const float CRT_SIGNAL_JITTER_LIMIT = 0.010f;

// 黒帯の継ぎ目、走査線と彩度を変えて、止まった映像を識別できるようにする。
static const float CRT_HOLD_SEAM_WIDTH = 0.018f;
static const float CRT_HOLD_SCANLINE_BASE = 0.86f;
static const float CRT_HOLD_SCANLINE_GAIN = 0.14f;
static const float CRT_HOLD_SCANLINE_COUNT = 240.0f;
static const float CRT_HOLD_SATURATION = 0.65f;
static const float CRT_HOLD_SEAM_BRIGHTNESS = 0.08f;

// 画面に付いた1粒の水滴の形を返している（x=縁、y=本体、z=光る所）。
// stretch は、流れ落ちる間に縦へ伸びる量（0 = 丸い粒）。縁の幅は粒の大きさに合わせ、小さな粒が輪に見えないようにしている
float3 GetLensDrop(
    float2 uv,
    float2 center,
    float radius,
    float aspect,
    float stretch)
{
    const float2 offset = (uv - center) * float2(aspect, 1.0f / (1.0f + stretch));
    const float distanceToCenter = length(offset);
    const float rim = 1.0f - smoothstep(
        radius * CRT_DROP_RIM_INNER,
        radius * CRT_DROP_RIM_OUTER,
        abs(distanceToCenter - radius * CRT_DROP_RIM_RADIUS));
    const float body = 1.0f - smoothstep(
        radius * CRT_DROP_BODY_INNER,
        radius * CRT_DROP_BODY_OUTER,
        distanceToCenter);
    const float highlight = (1.0f - smoothstep(
        radius * CRT_DROP_HIGHLIGHT_INNER,
        radius * CRT_DROP_HIGHLIGHT_OUTER,
        length(offset - float2(-radius * CRT_DROP_HIGHLIGHT_X, -radius * CRT_DROP_HIGHLIGHT_Y)))) *
        body;
    return float3(rim, body, highlight);
}

// 水滴の縁で、後ろの景色が曲がって見える量（UVのずれ）を返している
float2 GetLensDropRefraction(
    float2 uv,
    float2 center,
    float radius,
    float aspect,
    float stretch)
{
    const float2 aspectOffset =
        (uv - center) * float2(aspect, 1.0f / (1.0f + stretch));
    const float distanceToCenter = length(aspectOffset);
    const float body = 1.0f - smoothstep(
        radius * CRT_DROP_REFRACTION_INNER,
        radius * CRT_DROP_BODY_OUTER,
        distanceToCenter);
    // 水滴は小さな凸レンズなので、粒の中には後ろの景色が小さく、上下左右が逆さに映る。
    // 中心から見た位置を逆向きに0.7倍した所を読むよう、ずらす量を返している（uv + ずれ = 中心 - 0.7 ×（uv - 中心））
    return -(uv - center) * CRT_DROP_REFRACTION_SCALE * body;
}

// 画面の端で暗くなり始める位置と、暗さが最大になる位置。
static const float CRT_VIGNETTE_START = 0.22f;
static const float CRT_VIGNETTE_END = 0.80f;
static const float CRT_VIGNETTE_GAIN = 0.55f;

// 走査線は2画素を周期にして、ブラウン管風の横線を薄く重ねる。
static const float CRT_SCANLINE_PHASE_PER_PIXEL = 3.14159265f;
static const float CRT_SCANLINE_OPACITY = 0.018f;

// 粒の更新を秒間30回に抑え、暗い所ほど粒を目立たせる。
static const float CRT_GRAIN_UPDATES_PER_SECOND = 30.0f;
static const float CRT_GRAIN_BASE_STRENGTH = 0.35f;
static const float CRT_GRAIN_BRIGHT_REGION_SCALE = 0.40f;
static const float CRT_GRAIN_FADE_START = 0.02f;
static const float CRT_GRAIN_FADE_END = 0.30f;
static const float CRT_DARK_GRAIN_THRESHOLD = 0.55f;
static const float CRT_DARK_GRAIN_OPACITY = 0.080f;
static const float CRT_BRIGHT_GRAIN_THRESHOLD = 0.62f;
static const float CRT_BRIGHT_GRAIN_OPACITY = 0.055f;

// 重ねる層の色と透明度を返している（アルファブレンドで画面に重なる）
// 緊張が最大に近づいたときだけ、ふだんの揺れより強い歪みを加える。
static const float CRT_HIGH_TENSION_START = 0.62f;
static const float CRT_HIGH_TENSION_FULL = 0.92f;
static const float CRT_PULSE_WARP_GAIN = 1.15f;
static const float CRT_TENSION_WARP_GAIN = 0.32f;

// 画面の縁で色ずれを出し、歪みの量は高い緊張度に応じて加える。
static const float CRT_ABERRATION_EDGE_START = 0.20f;
static const float CRT_ABERRATION_EDGE_FULL = 0.72f;
static const float CRT_BASE_RADIAL_WARP = 0.0015f;
static const float CRT_TENSION_RADIAL_WARP_GAIN = 0.0040f;

// 色ずれの基準量と、恐怖パルス・緊張・信号異常で足す量。すべてUV単位。
static const float CRT_BASE_CHROMA_OFFSET = 0.0014f;
static const float CRT_PULSE_CHROMA_OFFSET_GAIN = 0.0038f;
static const float CRT_TENSION_CHROMA_OFFSET_GAIN = 0.0012f;
static const float CRT_SIGNAL_CHROMA_OFFSET_GAIN = 0.0024f;

// 演出・緊張・信号異常が粒状ノイズと周辺減光へ伝わる強さ。
static const float CRT_PULSE_NOISE_GAIN = 1.20f;
static const float CRT_TENSION_NOISE_GAIN = 0.16f;
static const float CRT_SIGNAL_NOISE_GAIN = 0.48f;
static const float CRT_PULSE_VIGNETTE_GAIN = 0.52f;
static const float CRT_TENSION_VIGNETTE_GAIN = 0.18f;

// 普段のレンズの色ずれに、緊張や信号異常の分を重ねる。
static const float CRT_BASE_ABERRATION = 0.30f;
static const float CRT_TENSION_ABERRATION_GAIN = 0.22f;
static const float CRT_SIGNAL_ABERRATION_GAIN = 0.72f;

// 歪みや色ずれで元の画面との差が出た領域を、必要な強さだけ混ぜる。
static const float CRT_WARP_ALPHA_BASE = 0.008f;
static const float CRT_WARP_ALPHA_TENSION_GAIN = 0.012f;
static const float CRT_WARP_DIFFERENCE_GAIN = 0.08f;
static const float CRT_CHROMA_ALPHA_BASE = 0.022f;
static const float CRT_CHROMA_ALPHA_PULSE_GAIN = 0.75f;
static const float CRT_CHROMA_ALPHA_EDGE_GAIN = 0.45f;

// 粒がほぼ乾いたら、粒の形と屈折用のテクスチャ参照を両方省く。
static const float CRT_LENS_DROP_MIN_MOISTURE = 0.001f;
static const int CRT_LENS_DROP_COUNT = 6;
static const float CRT_LENS_DROP_SEED_SCALE = 977.0f;
static const float CRT_LENS_DROP_INDEX_SEED_X = 13.7f;
static const float CRT_LENS_DROP_INDEX_SEED_Y = 5.3f;
static const float CRT_LENS_DROP_SEED_OFFSET_Y = 1.9f;
static const float CRT_LENS_DROP_SEED_Y = 41.0f;
static const float CRT_LENS_DROP_SEED_SIZE = 83.0f;
static const float CRT_LENS_DROP_SEED_SPEED = 127.0f;

// レンズの下側へ粒を散らし、半径によって落ち始めるまでの差をつける。
static const float CRT_LENS_DROP_LEFT_MARGIN = 0.06f;
static const float CRT_LENS_DROP_HORIZONTAL_SPAN = 0.88f;
static const float CRT_LENS_DROP_TOP = 0.60f;
static const float CRT_LENS_DROP_VERTICAL_SPAN = 0.34f;
static const float CRT_LENS_DROP_MIN_RADIUS = 0.005f;
static const float CRT_LENS_DROP_MAX_RADIUS = 0.017f;
static const float CRT_LENS_DROP_MAX_SPEED = 0.05f;
static const float CRT_LENS_DROP_SLIDE_START_RADIUS = 0.008f;
static const float CRT_LENS_DROP_SLIDE_FULL_RADIUS = 0.016f;

// 落下した距離が長いほど粒を縦に伸ばす。加速の式にある半分の係数はそのまま使う。
static const float CRT_LENS_DROP_FALL_ACCELERATION_SCALE = 0.6f;
static const float CRT_LENS_DROP_STRETCH_PER_DISTANCE = 6.0f;
static const float CRT_LENS_DROP_MAX_STRETCH = 0.8f;

// 恐怖パルスに合わせて画面を裂く帯を出し、横方向のずれを抑える。
static const float CRT_TEAR_LINE_CORE = 0.02f;
static const float CRT_TEAR_LINE_EDGE = 0.12f;
static const float CRT_TEAR_BAND_COUNT = 38.0f;
static const float CRT_TEAR_MAX_OFFSET = 0.030f;

// 信号異常の横線は表示帯と同じ間隔に置き、線の外側でも薄く残す。
static const float CRT_TEAR_LINE_ALPHA = 0.16f;
static const float CRT_SIGNAL_LINE_CORE = 0.015f;
static const float CRT_SIGNAL_LINE_EDGE = 0.085f;
static const float CRT_SIGNAL_LINE_ALPHA = 0.055f;
static const float CRT_SIGNAL_BACKGROUND_ALPHA = 0.006f;

// --- main の残りの値
// 明るさを求める重み（Rec.709）、0で割らないための下限、効果を省くとみなす強さ
static const float3 CRT_LUMINANCE_WEIGHTS = float3(0.2126f, 0.7152f, 0.0722f);
static const float CRT_MIN_DIVISOR = 0.0001f;
static const float CRT_EFFECT_EPSILON = 0.001f;
// 周辺減光の形を横に少しつぶす割合（画面の横長に合わせる）
static const float CRT_VIGNETTE_ASPECT = 1.15f;
// 粒のノイズの乱数のずらし（暗い粒・明るい粒は x と y を入れ替える）と、明るい粒の追加のずらし
static const float2 CRT_GRAIN_FRAME_SEED = float2(17.0f, 31.0f);
static const float2 CRT_BRIGHT_GRAIN_FRAME_SEED = float2(31.0f, 17.0f);
static const float CRT_BRIGHT_GRAIN_SEED = 7.3f;
// ゆっくり流れる帯：本数、速さ、鋭さ、濃さ
static const float CRT_ROLLING_BANDS = 10.0f;
static const float CRT_ROLLING_SPEED = 1.8f;
static const float CRT_ROLLING_SHARPNESS = 12.0f;
static const float CRT_ROLLING_OPACITY = 0.012f;
// まれに出る埃の画素：出るしきい値と濃さ
static const float CRT_DUST_THRESHOLD = 0.9985f;
static const float CRT_DUST_OPACITY = 0.030f;
// レンズの水滴：本体の景色を置き換える濃さ、縁と光る点の濃さ
static const float CRT_DROP_REFRACTION_ALPHA = 0.85f;
static const float CRT_DROP_EDGE_ALPHA = 0.30f;
static const float CRT_DROP_HIGHLIGHT_ALPHA = 0.30f;
// 横の裂け目：帯の数、1秒あたりの更新、出るしきい値、信号の乱れでの濃さ、色を選ぶ乱数のずらし
static const float CRT_TEAR_BAND_ROWS = 38.0f;
static const float CRT_TEAR_UPDATES_PER_SECOND = 24.0f;
static const float CRT_TEAR_THRESHOLD = 0.86f;
static const float CRT_TEAR_SIGNAL_STRENGTH = 0.58f;
static const float CRT_TEAR_CHANNEL_SEED = 13.0f;
// 重ねる層の色：埃と明るい粒、裂け目の色（青緑・赤）と景色に混ぜる割合、信号の乱れの色（青・赤）と景色に混ぜる割合
static const float3 CRT_DUST_COLOR = float3(0.62f, 0.65f, 0.62f);
static const float3 CRT_TEAR_CYAN = float3(0.12f, 0.72f, 0.80f);
static const float3 CRT_TEAR_RED = float3(0.82f, 0.12f, 0.20f);
static const float CRT_TEAR_COLOR_BLEND = 0.24f;
static const float3 CRT_SIGNAL_BLUE = float3(0.025f, 0.20f, 0.28f);
static const float3 CRT_SIGNAL_RED = float3(0.38f, 0.055f, 0.035f);
static const float CRT_SIGNAL_COLOR_BLEND = 0.20f;
// 層の色を優先させる強さ（濃さに掛ける）：信号の乱れ・歪み・色ずれ・水滴の本体・水滴の縁
static const float CRT_SIGNAL_COLOR_GAIN = 13.0f;
static const float CRT_DISTORTION_COLOR_GAIN = 18.0f;
static const float CRT_CHROMA_COLOR_GAIN = 16.0f;
static const float CRT_DROP_REFRACTION_COLOR_GAIN = 9.0f;
static const float CRT_DROP_COLOR_GAIN = 3.0f;
// 水滴の縁の暗い水の色と、光る点の色（光る強さに掛ける量）
static const float3 CRT_DROP_DARK_COLOR = float3(0.020f, 0.035f, 0.040f);
static const float3 CRT_DROP_HIGHLIGHT_COLOR = float3(0.62f, 0.70f, 0.72f);
static const float CRT_DROP_HIGHLIGHT_GAIN = 1.4f;
// 廊下の奥の冷たい色：画面の下の範囲（始め・終わり）、濃さ（いつも＋下ほど足す量）、色、色を優先させる強さ
static const float CRT_TENSION_LOWER_START = 0.22f;
static const float CRT_TENSION_LOWER_END = 0.92f;
static const float CRT_TENSION_VEIL_BASE = 0.014f;
static const float CRT_TENSION_VEIL_LOWER = 0.010f;
static const float3 CRT_TENSION_COLOR = float3(0.035f, 0.060f, 0.070f);
static const float CRT_TENSION_COLOR_GAIN = 12.0f;
// 恐怖で視界が狭まる：視界の縁（怖くない・一番怖い）、心拍で寄せる量、縁のぼかしの幅、ぼかす点の数と1点ごとの引き寄せ、
// 色を抜いた冷たい色と抜く割合、暗さ（中央寄り・端）、濃さ（最低＋心拍の幅）
static const float CRT_FEAR_EDGE_CALM = 0.62f;
static const float CRT_FEAR_EDGE_AFRAID = 0.28f;
static const float CRT_FEAR_PULSE_PULL = 0.06f;
static const float CRT_FEAR_EDGE_WIDTH = 0.40f;
static const int CRT_FEAR_TAP_COUNT = 5;
static const float CRT_FEAR_TAP_PULL = 0.022f;
static const float CRT_FEAR_TAP_AVERAGE = 0.2f;
static const float3 CRT_FEAR_TINT = float3(0.80f, 0.86f, 0.92f);
static const float CRT_FEAR_DESATURATE = 0.78f;
static const float CRT_FEAR_INNER_SHADE = 0.62f;
static const float CRT_FEAR_OUTER_SHADE = 0.22f;
static const float CRT_FEAR_ALPHA_BASE = 0.70f;
static const float CRT_FEAR_ALPHA_PULSE = 0.25f;
// 映画のような色分け：影とみなす明るさ（始め・終わり）、光とみなす明るさ（始め・終わり）、濃さ（影・光）、
// 冷たい影の色、暖かい光の色、光の色へ寄せる強さ
static const float CRT_FILM_SHADOW_START = 0.08f;
static const float CRT_FILM_SHADOW_END = 0.48f;
static const float CRT_FILM_HIGHLIGHT_START = 0.42f;
static const float CRT_FILM_HIGHLIGHT_END = 1.10f;
static const float CRT_FILM_SHADOW_ALPHA = 0.050f;
static const float CRT_FILM_HIGHLIGHT_ALPHA = 0.022f;
static const float3 CRT_FILM_COLD_SHADOW = float3(0.020f, 0.040f, 0.052f);
static const float3 CRT_FILM_WARM_LIGHT = float3(0.160f, 0.095f, 0.040f);
static const float CRT_FILM_WARM_GAIN = 1.35f;
// コマ落ちしているかを見るしきい値（PostProcess が 0 か 1 で渡す）と、裂け目の色を青緑と赤に分ける乱数のしきい値
#define CRT_FRAME_HOLD_THRESHOLD 0.5f
#define CRT_TEAR_COLOR_SPLIT 0.5f

// --- 影のまわりの空間のゆがみ（presenceWarp。PostProcess::SetPresenceWarps）
// 数（postProcessBuffer.hlsli の presenceWarp と同じ）
#define PRESENCE_WARP_COUNT SIGNAL_LOST_PRESENCE_WARP_COUNT
// ゆらぎの模様：細かさ（半径を1とした長さあたり）、流れる速さ、2つのノイズを互いにずらす量
#define PRESENCE_WARP_NOISE_SCALE 3.2f
#define PRESENCE_WARP_FLOW_SPEED 0.45f
#define PRESENCE_WARP_NOISE_SEED 17.3f
// 水の中のように揺れる波（縦横の波の数・速さ）と、ノイズと波の混ぜ方
#define PRESENCE_WARP_WAVE_FREQUENCY 9.0f
#define PRESENCE_WARP_WAVE_SPEED 2.3f
#define PRESENCE_WARP_WAVE_WEIGHT 0.35f
// ずらす量の最大（縦の高さを1とした長さ）。半径に比例させ、遠くの小さい影ではゆがみも小さくしている
#define PRESENCE_WARP_AMPLITUDE 0.12f
// ゆがむ範囲の縁のぼかし（半径に対する割合。ここから外側へ消えていく）
#define PRESENCE_WARP_EDGE_START 0.35f
// ゆがんだ所の重ね方の強さと、ほんの少し暗くする量
#define PRESENCE_WARP_ALPHA_GAIN 0.85f
#define PRESENCE_WARP_DARKEN 0.18f

// --- 残像（echo。PostProcess::SetEcho）
// 重ねる濃さ：いつも重ねる分と、今の画面と違う所（動いた物の輪郭）に足す分
#define ECHO_BASE_ALPHA 0.16f
#define ECHO_DIFFERENCE_ALPHA 1.4f
#define ECHO_MAX_ALPHA 0.55f
// 色：残す色の割合（残りは白黒）、冷たく沈めた色、明るさ
#define ECHO_SATURATION 0.25f
#define ECHO_TINT float3(0.78f, 0.92f, 1.0f)
#define ECHO_BRIGHTNESS 0.9f
// 古いビデオの焼き付きのような横の帯：帯の数、帯ごとの濃さのむら
#define ECHO_BAND_COUNT 90.0f
#define ECHO_BAND_VARIATION 0.45f

// --- 水に呑まれる視界（submerge。1面のループ廊下の赤い水に追いつかれたとき）
// 水面の線：高さ 0〜1 で上がりきったときに、波の分だけ画面の上を越える余り、大きな波と細かい波（横の細かさ・速さ・高さ）、線のぼかし
#define SUBMERGE_OVERSHOOT 0.12f
#define SUBMERGE_WAVE_FREQUENCY 7.0f
#define SUBMERGE_WAVE_SPEED 2.6f
#define SUBMERGE_WAVE_HEIGHT 0.035f
#define SUBMERGE_RIPPLE_FREQUENCY 23.0f
#define SUBMERGE_RIPPLE_SPEED 5.5f
#define SUBMERGE_RIPPLE_HEIGHT 0.010f
#define SUBMERGE_LINE_SOFTNESS 0.006f
// 水の中の景色のゆらぎ（細かさ・速さ・ずらす量）
#define SUBMERGE_REFRACT_FREQUENCY 18.0f
#define SUBMERGE_REFRACT_SPEED 3.1f
#define SUBMERGE_REFRACT_STRENGTH 0.012f
// 水の中の色：景色の明るさに掛ける赤、深い所の暗い赤、線から下へこの割合で深くなる、深さで暗くする割合、景色の色を残す割合
#define SUBMERGE_BLOOD_TINT float3(0.85f, 0.10f, 0.07f)
#define SUBMERGE_DEEP_COLOR float3(0.10f, 0.004f, 0.003f)
#define SUBMERGE_DEPTH_GAIN 2.2f
#define SUBMERGE_DEPTH_DARKEN 0.85f
#define SUBMERGE_SCENE_KEEP 0.15f
// 水の中の濃さと、水面の線の明るい縁（明るさ・鋭さ・色）
#define SUBMERGE_ALPHA 0.94f
#define SUBMERGE_LINE_GLOW 0.55f
#define SUBMERGE_LINE_SHARPNESS 160.0f
#define SUBMERGE_LINE_COLOR float3(0.95f, 0.35f, 0.28f)
// 上っていく泡：画面の高さあたりのマスの数、上る速さ（画面の高さ/秒）、出るマスの割合、横のずれ、大きさ（最小＋乱数の幅）、輪の内側、明るさ、種
#define SUBMERGE_BUBBLE_CELLS 14.0f
#define SUBMERGE_BUBBLE_RISE 0.55f
#define SUBMERGE_BUBBLE_CHANCE 0.84f
#define SUBMERGE_BUBBLE_JITTER 0.30f
#define SUBMERGE_BUBBLE_RADIUS_MIN 0.06f
#define SUBMERGE_BUBBLE_RADIUS_RANGE 0.16f
#define SUBMERGE_BUBBLE_INNER 0.72f
#define SUBMERGE_BUBBLE_BRIGHTNESS 0.28f
#define SUBMERGE_BUBBLE_SEED 31.7f
// 泡の列ごとの違い：上る速さ（最低＋乱数の幅）と、列ごとの縦のずらし、横に揺れる速さと幅
// （横のずれの半分＋揺れ＋一番大きな半径が、マスの半分 0.5 を超えないようにしている。超えると泡がマスの境で切れる）
#define SUBMERGE_BUBBLE_SPEED_MIN 0.55f
#define SUBMERGE_BUBBLE_SPEED_RANGE 0.9f
#define SUBMERGE_BUBBLE_COLUMN_OFFSET 17.0f
#define SUBMERGE_BUBBLE_SWAY_SPEED 3.3f
#define SUBMERGE_BUBBLE_SWAY 0.10f


// --- 一瞬だけ映る血まみれの別の場所（otherworld。submerge.y = 強さ、submerge.z = 乱数の種。1面の Stage1FearEvents）
// まばたきのように0.1秒ほどだけ、同じ景色が、赤黒く錆びて血の垂れた別の場所として映る（サイレントヒルの「裏世界」の考え方）。
// 景色の明るさを強めに締めて赤へ移し、上から垂れる血の筋・飛び散った染み・錆のざらつき・強い周辺の闇を重ねている。
// 横の帯ごとのずれ：帯の数、ずらす量の最大
#define OTHERWORLD_TEAR_BANDS 24.0f
#define OTHERWORLD_TEAR_SHIFT 0.012f
// 明るさの締め方（掛ける量と、明るい所を残す曲がり方）と、赤の色・影の錆の色
#define OTHERWORLD_CONTRAST_GAIN 1.6f
#define OTHERWORLD_CONTRAST_CURVE 1.35f
#define OTHERWORLD_LIGHT_COLOR float3(1.05f, 0.16f, 0.09f)
#define OTHERWORLD_RUST_COLOR float3(0.11f, 0.025f, 0.012f)
// 上から垂れる血の筋：画面の幅あたりの列の数、筋が出る列の割合、長さ（最小＋乱数の幅。画面の高さを1）、
// 筋の芯の太さ（列の幅に対する割合）、揺れの細かさと幅、筋の先の丸い溜まりの大きさ、筋の色
#define OTHERWORLD_DRIP_COLUMNS 70.0f
#define OTHERWORLD_DRIP_CHANCE 0.62f
#define OTHERWORLD_DRIP_MIN_LENGTH 0.08f
#define OTHERWORLD_DRIP_LENGTH_RANGE 0.75f
#define OTHERWORLD_DRIP_WIDTH 0.18f
#define OTHERWORLD_DRIP_WOBBLE_FREQUENCY 23.0f
#define OTHERWORLD_DRIP_WOBBLE 0.12f
#define OTHERWORLD_DRIP_BEAD 0.011f
#define OTHERWORLD_DRIP_COLOR float3(0.16f, 0.0f, 0.0f)
// 飛び散った血の染み：細かさ、染みになるノイズのしきい値（始まり・濃い所）、濃さ
#define OTHERWORLD_SPLATTER_SCALE 7.0f
#define OTHERWORLD_SPLATTER_START 0.66f
#define OTHERWORLD_SPLATTER_FULL 0.80f
#define OTHERWORLD_SPLATTER_ALPHA 0.85f
// 錆のざらつき（細かさと強さ）、周辺の闇（始まる距離・暗くなりきる距離）、ちらつきの強さ
#define OTHERWORLD_GRIT_SCALE 260.0f
#define OTHERWORLD_GRIT_STRENGTH 0.35f
#define OTHERWORLD_VIGNETTE_START 0.22f
#define OTHERWORLD_VIGNETTE_END 0.82f
#define OTHERWORLD_FLICKER 0.12f
// 種ごとに筋と染みの場所を変えるときの、種に掛ける大きさ
#define OTHERWORLD_SEED_SCALE 37.0f
// 筋の長さの乱数のずらし、揺れの位相に使う一周の角度、ちらつきを変える1秒あたりの回数
#define OTHERWORLD_DRIP_LENGTH_SEED 5.1f
#define OTHERWORLD_TWO_PI 6.2831853f
#define OTHERWORLD_FLICKER_RATE 60.0f

// 一瞬だけ映る別の場所の色を返している。uv は画面の位置、edgeDistance は画面の中心からの距離（周辺減光と同じ）
float3 GetOtherworldColor(float2 uv, float edgeDistance, float seed)
{
    // 古いフィルムが跳んだように、横の帯ごとに少しずらして読んでいる
    const float band = floor(uv.y * OTHERWORLD_TEAR_BANDS);
    const float tear = (FastHash21(float2(band, seed)) - 0.5f) * 2.0f * OTHERWORLD_TEAR_SHIFT;
    const float3 scene = SceneTexture.SampleLevel(LinearSampler, saturate(uv + float2(tear, 0.0f)), 0.0f).rgb;
    const float luminance = saturate(dot(scene, CRT_LUMINANCE_WEIGHTS) * OTHERWORLD_CONTRAST_GAIN);
    // 暗い所は錆の色へ沈め、照らされた所だけが赤く浮かぶ
    float3 color = lerp(OTHERWORLD_RUST_COLOR, OTHERWORLD_LIGHT_COLOR, pow(luminance, OTHERWORLD_CONTRAST_CURVE));

    // 上から垂れる血の筋（列ごとに出るかと長さを決め、少し揺らした細い筋の先に丸い溜まりを付けている）
    const float seedOffset = seed * OTHERWORLD_SEED_SCALE;
    const float column = floor(uv.x * OTHERWORLD_DRIP_COLUMNS);
    const float columnRandom = FastHash21(float2(column, seedOffset));
    const float dripLength = OTHERWORLD_DRIP_MIN_LENGTH + OTHERWORLD_DRIP_LENGTH_RANGE * FastHash21(float2(column + seedOffset, OTHERWORLD_DRIP_LENGTH_SEED));
    const float wobble = sin(uv.y * OTHERWORLD_DRIP_WOBBLE_FREQUENCY + columnRandom * OTHERWORLD_TWO_PI) * OTHERWORLD_DRIP_WOBBLE;
    const float across = abs(frac(uv.x * OTHERWORLD_DRIP_COLUMNS) - 0.5f + wobble);
    const float core = 1.0f - smoothstep(OTHERWORLD_DRIP_WIDTH * 0.5f, OTHERWORLD_DRIP_WIDTH, across);
    const float alongDrip = 1.0f - smoothstep(dripLength - OTHERWORLD_DRIP_BEAD, dripLength, uv.y);
    const float bead = 1.0f - smoothstep(OTHERWORLD_DRIP_BEAD * 0.5f, OTHERWORLD_DRIP_BEAD,
        length(float2((frac(uv.x * OTHERWORLD_DRIP_COLUMNS) - 0.5f + wobble) / OTHERWORLD_DRIP_COLUMNS * screenAspect, uv.y - dripLength)));
    const float drip = step(OTHERWORLD_DRIP_CHANCE, columnRandom) * saturate(core * alongDrip + bead);
    color = lerp(color, OTHERWORLD_DRIP_COLOR, drip);

    // 飛び散った血の染み
    const float splatter = smoothstep(OTHERWORLD_SPLATTER_START, OTHERWORLD_SPLATTER_FULL,
        FastValueNoise(float2(uv.x * screenAspect, uv.y) * OTHERWORLD_SPLATTER_SCALE + seedOffset));
    color = lerp(color, OTHERWORLD_DRIP_COLOR, splatter * OTHERWORLD_SPLATTER_ALPHA);

    // 錆のざらつきと、強い周辺の闇
    const float grit = FastHash21(floor(uv * OTHERWORLD_GRIT_SCALE) + seedOffset);
    color *= 1.0f - grit * OTHERWORLD_GRIT_STRENGTH;
    color *= 1.0f - smoothstep(OTHERWORLD_VIGNETTE_START, OTHERWORLD_VIGNETTE_END, edgeDistance);
    // 映っている間も、明るさがわずかにちらつく
    color *= 1.0f - OTHERWORLD_FLICKER * FastHash21(float2(floor(time * OTHERWORLD_FLICKER_RATE), seed));
    return color;
}

#endif
