// ============================================================================
// シェーダーの役割: 走査線・ノイズ・周辺減光・画面の乱れ・レンズの水滴・色調を、半透明の層として画面に重ねている（ブラウン管風の効果）。
// 定数バッファのスロットと入出力の形は、CPU側の定義（FullScreenQuad.cppのTimeBuffer）と一致させている。
// ============================================================================

// 頂点シェーダー（unlitTextureVS）から受け取る値
struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// 画面効果の値（FullScreenQuad.cppのTimeBufferと同じ並び）
#include "postProcessBuffer.hlsli"
#include "fastNoise.hlsli"

// 描いた画面（t0）とサンプラー（s0）
Texture2D SceneTexture : register(t0);
SamplerState LinearSampler : register(s0);

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

float4 main(PS_IN input) : SV_TARGET
{
    // コマ落ち：影が近くラジオの雑音が強いとき、一瞬だけ映像が止まる。止まった画面は縦に少しずれ、
    // ずれの継ぎ目に黒い帯が走り、色が少し抜ける（古いビデオの同期が外れたように見せている）
    [branch]
    if (frameHold > 0.5f)
    {
        const float heldY = frac(input.uv.y + frameHoldOffset);
        float3 held = SceneTexture.SampleLevel(LinearSampler, float2(input.uv.x, heldY), 0.0f).rgb;
        const float seam = smoothstep(0.0f, CRT_HOLD_SEAM_WIDTH, min(heldY, 1.0f - heldY));
        const float scanline = CRT_HOLD_SCANLINE_BASE + CRT_HOLD_SCANLINE_GAIN * step(0.5f, frac(input.uv.y * CRT_HOLD_SCANLINE_COUNT));
        held = lerp(dot(held, CRT_LUMINANCE_WEIGHTS).xxx, held, CRT_HOLD_SATURATION);
        return float4(held * scanline * lerp(CRT_HOLD_SEAM_BRIGHTNESS, 1.0f, seam), 1.0f);
    }
    const float2 center = float2(0.5f, 0.5f);
    float2 centered = input.uv - center;
    // ノイズと周辺減光の強さ：驚かせる演出・緊張・信号の乱れで強めている
    const float eventNoise = saturate(
        noiseAmount + horrorPulseStrength * CRT_PULSE_NOISE_GAIN +
        corridorTension * CRT_TENSION_NOISE_GAIN + signalInterference * CRT_SIGNAL_NOISE_GAIN);
    const float eventVignette =
        vignetteStrength + horrorPulseStrength * CRT_PULSE_VIGNETTE_GAIN +
        corridorTension * CRT_TENSION_VIGNETTE_GAIN;

    // 遊ぶのに必要な情報を隠さない程度の、柔らかい周辺減光。
    float edgeDistance = length(centered * float2(CRT_VIGNETTE_ASPECT, 1.0f));
    const float2 radialDirection = centered /
        max(length(centered), CRT_EFFECT_EPSILON);
    // 普段の緊張感だけでは、元の画像をずらさない。色ずれと歪みは、
    // 驚かせる演出・信号の異常・終盤の強い緊張のときだけにしている。
    const float highTension = smoothstep(CRT_HIGH_TENSION_START, CRT_HIGH_TENSION_FULL, corridorTension);
    const float distortionEvent = saturate(
        horrorPulseStrength * CRT_PULSE_WARP_GAIN +
        highTension * CRT_TENSION_WARP_GAIN +
        signalInterference);
    // 画面の端には、いつもわずかな色ずれを残している（古いレンズのような、P.T. の画面の質感）
    const float eventAberration = saturate(
        CRT_BASE_ABERRATION + horrorPulseStrength +
        highTension * lensDistortionStrength * CRT_TENSION_ABERRATION_GAIN +
        signalInterference * CRT_SIGNAL_ABERRATION_GAIN);
    const float edgeAberration = smoothstep(CRT_ABERRATION_EDGE_START, CRT_ABERRATION_EDGE_FULL, edgeDistance);
    const float2 aspectCentered =
        centered * float2(screenAspect, 1.0f);
    const float radialSquared = dot(aspectCentered, aspectCentered);
    const float radialWarp = lensDistortionStrength * distortionEvent *
        (CRT_BASE_RADIAL_WARP + highTension * CRT_TENSION_RADIAL_WARP_GAIN);
    // 信号の復旧に失敗すると、画面が横の帯ごとにばらばらにずれる。
    // ずれる量にはわざと上限を付け、プレイヤーが進む方向を見失わないようにしている。
    const float signalBand = floor(input.uv.y * CRT_SIGNAL_BAND_COUNT);
    const float signalFrame = floor(time * CRT_SIGNAL_UPDATES_PER_SECOND);
    const float signalSeed = FastHash21(float2(signalBand, signalFrame));
    const float signalGate = smoothstep(CRT_SIGNAL_GATE_START, CRT_SIGNAL_GATE_END, signalSeed);
    const float signalJitter = (signalSeed - 0.5f) * CRT_SIGNAL_JITTER_LIMIT *
        signalInterference * signalGate;
    const float2 distortedUv = saturate(
        input.uv + centered * radialSquared * radialWarp +
        float2(signalJitter, 0.0f));
    const float chromaOffset =
        (CRT_BASE_CHROMA_OFFSET + horrorPulseStrength * CRT_PULSE_CHROMA_OFFSET_GAIN +
         highTension * lensDistortionStrength * CRT_TENSION_CHROMA_OFFSET_GAIN) *
        edgeAberration + signalInterference * signalGate * CRT_SIGNAL_CHROMA_OFFSET_GAIN;
    const float3 sceneCenter = SceneTexture.SampleLevel(
        LinearSampler,
        input.uv,
        0.0f).rgb;
    float3 distortedScene = sceneCenter;
    // 普段はdistortedUvとinput.uvが同じ。驚かせる演出のときだけ2回目を読み、
    // 普段の全画面のテクスチャの読み取りを1回分減らしている。
    [branch]
    if (distortionEvent > CRT_EFFECT_EPSILON)
    {
        distortedScene = SceneTexture.SampleLevel(
            LinearSampler,
            distortedUv,
            0.0f).rgb;
    }
    float3 chromaticScene = distortedScene;
    // 色ずれは演出のときだけの効果。普段は、この全体で同じ分岐によって、
    // 1画素あたり、全解像度のテクスチャの読み取りを2回省いている。
    [branch]
    if (eventAberration > CRT_EFFECT_EPSILON)
    {
        chromaticScene = float3(
            SceneTexture.SampleLevel(
                LinearSampler,
                saturate(distortedUv + radialDirection * chromaOffset),
                0.0f).r,
            distortedScene.g,
            SceneTexture.SampleLevel(
                LinearSampler,
                saturate(distortedUv - radialDirection * chromaOffset),
                0.0f).b);
    }
    // ずれた画像・色ずれした画像は、元の画像との差がある所だけ、少し重ねている
    const float distortionDifference = length(
        abs(distortedScene - sceneCenter));
    const float distortionAlpha = saturate(
        edgeAberration * lensDistortionStrength * distortionEvent *
        (CRT_WARP_ALPHA_BASE + highTension * CRT_WARP_ALPHA_TENSION_GAIN) +
        distortionDifference * distortionEvent * CRT_WARP_DIFFERENCE_GAIN);
    const float chromaDifference = length(
        abs(chromaticScene - sceneCenter));
    const float chromaAlpha = saturate(
        eventAberration * edgeAberration * CRT_CHROMA_ALPHA_BASE +
        chromaDifference * (horrorPulseStrength * CRT_CHROMA_ALPHA_PULSE_GAIN + edgeAberration * CRT_CHROMA_ALPHA_EDGE_GAIN));
    // 周辺減光は強めにし、画面の端から闇が迫って見えるようにしている（中央は暗くしない）
    float vignette = smoothstep(CRT_VIGNETTE_START, CRT_VIGNETTE_END, edgeDistance) *
        CRT_VIGNETTE_GAIN * eventVignette;

    // 1行おきの走査線。SV_POSITIONを使っているので、
    // ウィンドウの解像度が違っても線の太さが変わらない。
    float scanWave = sin(input.pos.y * CRT_SCANLINE_PHASE_PER_PIXEL) * 0.5f + 0.5f;
    float scanline = (1.0f - scanWave) * CRT_SCANLINE_OPACITY * eventNoise;

    // 1/30秒ごとに変わる粒状のノイズ（暗い粒）
    float frame = floor(time * CRT_GRAIN_UPDATES_PER_SECOND);
    float noise = FastHash21(floor(input.pos.xy) + frame * CRT_GRAIN_FRAME_SEED);
    // 粒は暗い所ほど強く出し、闇がざらついて見えるようにしている
    const float grainLuminance = dot(sceneCenter,
        CRT_LUMINANCE_WEIGHTS);
    // フィルムの粒：暗い粒と明るい粒の両方を、はっきり見える強さで出している（サイレントヒルの画面の粒を参考）。
    // 暗い所ほど強く出し、闇がざらついて見えるようにしている
    const float grainStrength = (CRT_GRAIN_BASE_STRENGTH + eventNoise) * lerp(1.0f, CRT_GRAIN_BRIGHT_REGION_SCALE, smoothstep(CRT_GRAIN_FADE_START, CRT_GRAIN_FADE_END, grainLuminance));
    float darkGrain = smoothstep(CRT_DARK_GRAIN_THRESHOLD, 1.0f, noise) * CRT_DARK_GRAIN_OPACITY * grainStrength;
    const float brightNoise = FastHash21(floor(input.pos.xy) + frame * CRT_BRIGHT_GRAIN_FRAME_SEED + CRT_BRIGHT_GRAIN_SEED);
    const float brightGrain = smoothstep(CRT_BRIGHT_GRAIN_THRESHOLD, 1.0f, brightNoise) * CRT_BRIGHT_GRAIN_OPACITY * grainStrength;

    // ごく薄い、ゆっくり流れる帯で、アナログ映像らしい動きを出している。
    float rolling = sin(input.uv.y * CRT_ROLLING_BANDS - time * CRT_ROLLING_SPEED) * 0.5f + 0.5f;
    rolling = pow(rolling, CRT_ROLLING_SHARPNESS) * CRT_ROLLING_OPACITY * eventNoise;

    // まれに出る埃の画素は明るいが、透明度はとても低くしている。
    float dust = step(CRT_DUST_THRESHOLD, noise);

    float darkAlpha = saturate(vignette + scanline + darkGrain + rolling);
    float dustAlpha = dust * CRT_DUST_OPACITY * eventNoise + brightGrain;

    // レンズのしぶきは、走って水たまりの水を強く蹴ったときだけ、画面の下の方に小さく付く。
    // 付く位置・大きさ・流れ落ちる速さは、しぶきごとの乱数の種（lensDropSeed）から決めるので、毎回違う。
    // 粒は付いた所から重さで加速しながら下へ流れ、流れるほど縦に伸び、lensMoisture が0へ戻ると乾いて消える。
    float3 lensDrop = 0.0f;
    float2 lensRefraction = 0.0f;
    const float moisture = saturate(lensMoisture);
    // レンズのしぶきは、ほとんどの時間は出ていない。しぶきが付くまで、粒の距離の計算を普段の全画面の処理から外している。
    [branch]
    if (moisture > CRT_LENS_DROP_MIN_MOISTURE)
    {
        [unroll]
        for (int dropIndex = 0; dropIndex < CRT_LENS_DROP_COUNT; ++dropIndex)
        {
            const float2 seed = float2(lensDropSeed * CRT_LENS_DROP_SEED_SCALE + dropIndex * CRT_LENS_DROP_INDEX_SEED_X, dropIndex * CRT_LENS_DROP_INDEX_SEED_Y + CRT_LENS_DROP_SEED_OFFSET_Y);
            const float randomX = FastHash21(seed);
            const float randomY = FastHash21(seed + CRT_LENS_DROP_SEED_Y);
            const float randomSize = FastHash21(seed + CRT_LENS_DROP_SEED_SIZE);
            const float randomSpeed = FastHash21(seed + CRT_LENS_DROP_SEED_SPEED);
            // 画面の下の方（下から4割まで）の、左右に散らばった位置に付く
            const float2 start = float2(CRT_LENS_DROP_LEFT_MARGIN + randomX * CRT_LENS_DROP_HORIZONTAL_SPAN, CRT_LENS_DROP_TOP + randomY * CRT_LENS_DROP_VERTICAL_SPAN);
            // 大きい粒ほど重く、早く流れる。小さい粒は、ほとんど止まったまま乾く
            const float radius = lerp(CRT_LENS_DROP_MIN_RADIUS, CRT_LENS_DROP_MAX_RADIUS, randomSize * randomSize);
            const float speed = lerp(0.0f, CRT_LENS_DROP_MAX_SPEED, randomSpeed) * smoothstep(CRT_LENS_DROP_SLIDE_START_RADIUS, CRT_LENS_DROP_SLIDE_FULL_RADIUS, radius);
            // 流れた量（最初の速さに、少しずつの加速を足している。2.6秒で、速い粒は画面の高さの約2割流れる）
            const float fall = speed * lensDropAge + 0.5f * speed * CRT_LENS_DROP_FALL_ACCELERATION_SCALE * lensDropAge * lensDropAge;
            const float stretch = saturate(fall * CRT_LENS_DROP_STRETCH_PER_DISTANCE) * CRT_LENS_DROP_MAX_STRETCH;
            const float2 dropCenter = start + float2(0.0f, fall);
            lensDrop = max(lensDrop, GetLensDrop(input.uv, dropCenter, radius, screenAspect, stretch));
            lensRefraction += GetLensDropRefraction(input.uv, dropCenter, radius, screenAspect, stretch);
        }
    }
    // 水滴が付いている間だけ、ずらした位置の景色を読んでいる
    float3 refractedScene = sceneCenter;
    [branch]
    if (moisture > CRT_LENS_DROP_MIN_MOISTURE)
    {
        refractedScene = SceneTexture.SampleLevel(
            LinearSampler,
            saturate(input.uv + lensRefraction),
            0.0f).rgb;
    }
    // 粒は、主に後ろの景色を逆さに映して見せる（本体の中は、ずらした景色に置き換える。乾くにつれて薄くなる）。
    // 縁は白い輪にせず、水の厚みで暗くなる細い線にし、光る所は左上の小さな点だけにしている
    const float dropRefractionAlpha = saturate(lensDrop.y * CRT_DROP_REFRACTION_ALPHA * moisture);
    const float dropAlpha = saturate(
        (lensDrop.x * CRT_DROP_EDGE_ALPHA + lensDrop.z * CRT_DROP_HIGHLIGHT_ALPHA) * moisture);

    // 驚かせる演出では、半透明の同期の乱れ（横の裂け目）を加えている。この層は景色を置き換えないので、
    // テクスチャが無くても画面が真っ黒になることはない。
    float bandId = floor(input.uv.y * CRT_TEAR_BAND_ROWS);
    float bandNoise = FastHash21(float2(bandId, floor(time * CRT_TEAR_UPDATES_PER_SECOND)));
    float tear = step(CRT_TEAR_THRESHOLD, bandNoise) * horrorPulseStrength;
    tear = max(tear, signalGate * signalInterference * CRT_TEAR_SIGNAL_STRENGTH);
    float thinLine = 1.0f - smoothstep(
        CRT_TEAR_LINE_CORE,
        CRT_TEAR_LINE_EDGE,
        abs(frac(input.uv.y * CRT_TEAR_BAND_COUNT) - 0.5f));
    const float tearOffset =
        (bandNoise - 0.5f) * CRT_TEAR_MAX_OFFSET * horrorPulseStrength * thinLine;
    float3 shiftedScene = sceneCenter;
    [branch]
    if (horrorPulseStrength > CRT_EFFECT_EPSILON || signalInterference > CRT_EFFECT_EPSILON)
    {
        shiftedScene = SceneTexture.SampleLevel(
            LinearSampler,
            saturate(input.uv + float2(tearOffset, 0.0f)),
            0.0f).rgb;
    }
    // 信号の乱れの横線
    float tearAlpha = tear * thinLine * CRT_TEAR_LINE_ALPHA;
    const float signalLine = 1.0f - smoothstep(
        CRT_SIGNAL_LINE_CORE, CRT_SIGNAL_LINE_EDGE,
        abs(frac(input.uv.y * CRT_SIGNAL_BAND_COUNT) - 0.5f));
    const float signalAlpha = signalInterference *
        (signalGate * signalLine * CRT_SIGNAL_LINE_ALPHA +
         (1.0f - signalLine) * CRT_SIGNAL_BACKGROUND_ALPHA);
    float alpha = saturate(
        darkAlpha + dustAlpha + tearAlpha + dropAlpha +
        dropRefractionAlpha + chromaAlpha + distortionAlpha +
        signalAlpha);

    // 重ねる層の色：埃・裂け目・信号の乱れ・歪み・色ずれ・水滴を、それぞれの強さで混ぜている
    // 暗い層（周辺減光・暗い粒）と明るい層（埃・明るい粒）を、それぞれの強さの割合で混ぜた色にしている
    float3 overlayColor = lerp(
        float3(0.0f, 0.0f, 0.0f),
        CRT_DUST_COLOR,
        saturate(dustAlpha / max(darkAlpha + dustAlpha, CRT_MIN_DIVISOR))
    );
    float channelChoice = FastHash21(float2(bandId + CRT_TEAR_CHANNEL_SEED, floor(time * CRT_TEAR_UPDATES_PER_SECOND)));
    float3 tearColor = channelChoice < 0.5f
        ? CRT_TEAR_CYAN
        : CRT_TEAR_RED;
    tearColor = lerp(shiftedScene, tearColor, CRT_TEAR_COLOR_BLEND);
    overlayColor = lerp(overlayColor, tearColor, saturate(tear));
    const float3 signalColor = lerp(
        CRT_SIGNAL_BLUE,
        CRT_SIGNAL_RED,
        signalSeed);
    overlayColor = lerp(
        overlayColor,
        lerp(shiftedScene, signalColor, CRT_SIGNAL_COLOR_BLEND),
        saturate(signalAlpha * CRT_SIGNAL_COLOR_GAIN));
    overlayColor = lerp(
        overlayColor,
        distortedScene,
        saturate(distortionAlpha * CRT_DISTORTION_COLOR_GAIN));
    overlayColor = lerp(
        overlayColor,
        chromaticScene,
        saturate(chromaAlpha * CRT_CHROMA_COLOR_GAIN));
    // 縁は暗い水の色、光る所だけ明るい色（縁の明るさは混ぜない）
    const float3 waterColor = lerp(
        CRT_DROP_DARK_COLOR,
        CRT_DROP_HIGHLIGHT_COLOR,
        saturate(lensDrop.z * CRT_DROP_HIGHLIGHT_GAIN));
    // 逆さに映った景色を先に置き、その上に縁の暗い線と光る点を重ねている（逆の順だと、縁が景色で消える）
    overlayColor = lerp(
        overlayColor,
        refractedScene,
        saturate(dropRefractionAlpha * CRT_DROP_REFRACTION_COLOR_GAIN));
    overlayColor = lerp(
        overlayColor,
        waterColor,
        saturate(dropAlpha * CRT_DROP_COLOR_GAIN));

    // 廊下の奥ほど、ゆっくり冷たい色で彩度を落としている。ごく控えめで半透明なので、
    // 進むための情報は隠れない。
    const float lowerScreen = smoothstep(CRT_TENSION_LOWER_START, CRT_TENSION_LOWER_END, input.uv.y);
    const float tensionVeil = corridorTension *
        (CRT_TENSION_VEIL_BASE + lowerScreen * CRT_TENSION_VEIL_LOWER);
    const float3 tensionColor = CRT_TENSION_COLOR;
    overlayColor = lerp(
        overlayColor,
        tensionColor,
        saturate(tensionVeil * CRT_TENSION_COLOR_GAIN));
    alpha = saturate(alpha + tensionVeil);

    // 恐怖で視界が狭まる：緊張が高いほど、画面の端から、ぼけて色の抜けた闇が中央へ迫る。心拍が鳴るたびに一瞬さらに迫る。
    // 端の景色を中心へ向かう向きに5点読んでぼかし、白黒に寄せて暗くした色を、端ほど濃く、今の層の上に重ねている。
    // 視線の中央（見たい所）は隠さない。緊張していない間は、この全体で同じ分岐で計算を丸ごと省いている
    [branch]
    if (fearVision > CRT_EFFECT_EPSILON)
    {
        const float fear = smoothstep(0.0f, 1.0f, fearVision);
        const float pulse = fearBeat * fearBeat;
        // ここより外が狭まる、視界の縁。怖いほど内側へ寄り、心拍で一瞬さらに寄る
        const float inner = lerp(CRT_FEAR_EDGE_CALM, CRT_FEAR_EDGE_AFRAID, fear) - pulse * CRT_FEAR_PULSE_PULL * fear;
        const float fearEdge = smoothstep(inner, inner + CRT_FEAR_EDGE_WIDTH, edgeDistance);
        float3 blurred = 0.0f;
        [unroll]
        for (int tap = 0; tap < CRT_FEAR_TAP_COUNT; ++tap)
        {
            const float pull = CRT_FEAR_TAP_PULL * (float)(tap + 1) * fearEdge * fear;
            blurred += SceneTexture.SampleLevel(LinearSampler, saturate(input.uv - centered * pull), 0.0f).rgb;
        }
        blurred *= CRT_FEAR_TAP_AVERAGE;
        const float blurredLuminance = dot(blurred, CRT_LUMINANCE_WEIGHTS);
        // 色を抜いて少し冷たくし、端ほど暗くしている
        const float3 fearColor = lerp(blurred, blurredLuminance.xxx * CRT_FEAR_TINT, CRT_FEAR_DESATURATE) *
            lerp(CRT_FEAR_INNER_SHADE, CRT_FEAR_OUTER_SHADE, fearEdge);
        const float fearAlpha = saturate(fearEdge * fear * (CRT_FEAR_ALPHA_BASE + pulse * CRT_FEAR_ALPHA_PULSE));
        const float layeredAlpha = fearAlpha + alpha * (1.0f - fearAlpha);
        overlayColor = (fearColor * fearAlpha + overlayColor * alpha * (1.0f - fearAlpha)) /
            max(layeredAlpha, CRT_MIN_DIVISOR);
        alpha = layeredAlpha;
    }

    // 映画のような色分け：影の部分は冷たい色に、照明の部分は控えめな暖かいにじみを残している。
    // これも半透明の層として重ねているので、
    // 取り込んだ景色が黒で置き換わることはない。
    const float sceneLuminance = dot(
        sceneCenter,
        CRT_LUMINANCE_WEIGHTS);
    const float shadowWeight = 1.0f - smoothstep(
        CRT_FILM_SHADOW_START, CRT_FILM_SHADOW_END, sceneLuminance);
    const float highlightWeight = smoothstep(
        CRT_FILM_HIGHLIGHT_START, CRT_FILM_HIGHLIGHT_END, sceneLuminance);
    const float filmAlpha = saturate(filmGradeStrength) *
        (shadowWeight * CRT_FILM_SHADOW_ALPHA + highlightWeight * CRT_FILM_HIGHLIGHT_ALPHA);
    const float3 coldShadow = CRT_FILM_COLD_SHADOW;
    const float3 warmLight = CRT_FILM_WARM_LIGHT;
    const float3 filmColor = lerp(
        coldShadow,
        warmLight,
        saturate(highlightWeight * CRT_FILM_WARM_GAIN));
    // 2つの半透明の層を1つにまとめ、合わせた色と透明度を返している
    const float combinedAlpha = alpha + filmAlpha * (1.0f - alpha);
    overlayColor = (
        overlayColor * alpha +
        filmColor * filmAlpha * (1.0f - alpha)) /
        max(combinedAlpha, CRT_MIN_DIVISOR);
    alpha = combinedAlpha;

    return float4(overlayColor, alpha);
}
