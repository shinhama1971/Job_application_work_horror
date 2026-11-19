// ============================================================================
// シェーダーの役割: 画面に、漂う埃の粒と細かい走査線を半透明で重ねている（ScreenDustOverlayで使っている）。
// 定数バッファのスロットと入出力の形は、CPU側の定義（ScreenDustOverlay.hのTimeBuffer）と一致させている。
// ============================================================================

// 頂点シェーダー（unlitTextureVS）から受け取る値
struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// 経過時間、強さ（0〜1）、16バイトにそろえる詰め物
cbuffer TimeBuffer : register(b0)
{
    float time;
    float power;
    float dummy1;
    float dummy2;
};

// 疑似乱数の係数（よく使われる sin のハッシュ。決まった値）
#define DUST_HASH_DIRECTION float2(12.9898f, 78.233f)
#define DUST_HASH_SCALE 43758.5453f

// 2次元の値から0〜1の疑似乱数を作っている
float rand(float2 co)
{
    return frac(sin(dot(co, DUST_HASH_DIRECTION)) * DUST_HASH_SCALE);
}

// 流れる速さ（横・縦）、マスの数、粒を置くマスの割合（乱数のしきい値）、粒の半径（最小・最大と乱数のずらし）、粒の芯の割合
#define DUST_DRIFT_SPEED_X 0.0035f
#define DUST_DRIFT_SPEED_Y 0.0060f
#define DUST_GRID_SIZE float2(128.0f, 72.0f)
#define DUST_MOTE_THRESHOLD 0.9925f
#define DUST_MOTE_MIN_RADIUS 0.055f
#define DUST_MOTE_MAX_RADIUS 0.14f
#define DUST_MOTE_RADIUS_SEED 9.3f
#define DUST_MOTE_CORE 0.45f
// 走査線（円周率・流れる速さ・濃さ）、粒の明滅（速さ・濃さの最小・最大）、描かない薄さ
#define DUST_PI 3.14159265f
#define DUST_SCAN_SPEED 9.0f
#define DUST_SCAN_ALPHA 0.004f
#define DUST_FLICKER_SPEED 23.0f
#define DUST_MOTE_MIN_ALPHA 0.018f
#define DUST_MOTE_MAX_ALPHA 0.055f
#define DUST_MIN_ALPHA 0.0005f
// 走査線の灰色と、粒の暖かい灰色
#define DUST_SCAN_COLOR float3(0.40f, 0.43f, 0.42f)
#define DUST_MOTE_COLOR float3(0.72f, 0.66f, 0.54f)

// 画面を128x72のマスに分けてゆっくり流し、まれなマスにだけ丸い埃の粒を置いている
float4 main(PS_IN pin) : SV_TARGET
{
    const float2 eventUv = pin.uv;
    const float eventPower = saturate(power);
    const float2 eventMovingUv = eventUv +
        float2(time * DUST_DRIFT_SPEED_X, -time * DUST_DRIFT_SPEED_Y);
    const float2 eventGridSize = DUST_GRID_SIZE;
    const float2 eventGridPosition = eventMovingUv * eventGridSize;
    const float2 eventCell = floor(eventGridPosition);
    const float2 eventLocal = frac(eventGridPosition) - 0.5f;
    const float eventSeed = rand(eventCell);

    // 計算で作った半透明の粒だけを描いている。t0は読まない。
    // t0には、光の筋や画面のテクスチャがまだ設定されたままの場合があるためである。
    const float eventMoteMask = step(DUST_MOTE_THRESHOLD, eventSeed);
    const float eventMoteRadius = lerp(
        DUST_MOTE_MIN_RADIUS, DUST_MOTE_MAX_RADIUS, rand(eventCell + DUST_MOTE_RADIUS_SEED));
    const float eventMote = (1.0f - smoothstep(
        eventMoteRadius * DUST_MOTE_CORE,
        eventMoteRadius,
        length(eventLocal))) * eventMoteMask;
    // 細かい走査線と、明滅する粒を合わせた透明度にしている（見えないほど薄い画素は描かない）
    const float eventScanWave =
        sin(pin.pos.y * DUST_PI + time * DUST_SCAN_SPEED);
    const float eventScanAlpha =
        (eventScanWave * 0.5f + 0.5f) * DUST_SCAN_ALPHA;
    const float eventPhase = sin(time * DUST_FLICKER_SPEED) * 0.5f + 0.5f;
    const float eventMoteAlpha =
        eventMote * lerp(DUST_MOTE_MIN_ALPHA, DUST_MOTE_MAX_ALPHA, eventPhase);
    const float eventAlpha = saturate(
        (eventScanAlpha + eventMoteAlpha) * eventPower);
    clip(eventAlpha - DUST_MIN_ALPHA);
    // 粒は暖かい灰色、走査線は灰色で返している
    const float3 eventColor = lerp(
        DUST_SCAN_COLOR,
        DUST_MOTE_COLOR,
        eventMote);
    return float4(eventColor, eventAlpha);
}
