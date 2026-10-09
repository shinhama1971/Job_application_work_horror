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

// テクスチャ（t0）とサンプラー（今は読んでいない）
Texture2D tex : register(t0);
SamplerState samp : register(s0);

// 2次元の値から0〜1の疑似乱数を作っている
float rand(float2 co)
{
    return frac(sin(dot(co, float2(12.9898f, 78.233f))) * 43758.5453f);
}

// 画面を128x72のマスに分けてゆっくり流し、まれなマスにだけ丸い埃の粒を置いている
float4 main(PS_IN pin) : SV_TARGET
{
    const float2 eventUv = pin.uv;
    const float eventPower = saturate(power);
    const float2 eventMovingUv = eventUv +
        float2(time * 0.0035f, -time * 0.0060f);
    const float2 eventGridSize = float2(128.0f, 72.0f);
    const float2 eventGridPosition = eventMovingUv * eventGridSize;
    const float2 eventCell = floor(eventGridPosition);
    const float2 eventLocal = frac(eventGridPosition) - 0.5f;
    const float eventSeed = rand(eventCell);

    // 計算で作った半透明の粒だけを描いている。t0は読まない。
    // t0には、光の筋や画面のテクスチャがまだ設定されたままの場合があるためである。
    const float eventMoteMask = step(0.9925f, eventSeed);
    const float eventMoteRadius = lerp(
        0.055f, 0.14f, rand(eventCell + 9.3f));
    const float eventMote = (1.0f - smoothstep(
        eventMoteRadius * 0.45f,
        eventMoteRadius,
        length(eventLocal))) * eventMoteMask;
    // 細かい走査線と、明滅する粒を合わせた透明度にしている（見えないほど薄い画素は描かない）
    const float eventScanWave =
        sin(pin.pos.y * 3.14159265f + time * 9.0f);
    const float eventScanAlpha =
        (eventScanWave * 0.5f + 0.5f) * 0.004f;
    const float eventPhase = sin(time * 23.0f) * 0.5f + 0.5f;
    const float eventMoteAlpha =
        eventMote * lerp(0.018f, 0.055f, eventPhase);
    const float eventAlpha = saturate(
        (eventScanAlpha + eventMoteAlpha) * eventPower);
    clip(eventAlpha - 0.0005f);
    // 粒は暖かい灰色、走査線は灰色で返している
    const float3 eventColor = lerp(
        float3(0.40f, 0.43f, 0.42f),
        float3(0.72f, 0.66f, 0.54f),
        eventMote);
    return float4(eventColor, eventAlpha);
    // ここから下は、上のreturnの後なので実行されない（前の版の処理が残っている）。
    float2 uv = pin.uv;
    float2 center = float2(0.5f, 0.5f);

    float4 color = tex.Sample(samp, uv);

    float noise = rand(uv * 800.0f + time * 20.0f);
    noise = smoothstep(0.45f, 1.0f, noise);
    color.rgb = lerp(color.rgb, float3(0.8f, 0.75f, 0.65f), noise * 0.45f);

    float scan = sin(uv.y * 500.0f + time * 25.0f);
    scan = scan * 0.5f + 0.5f;
    scan = pow(scan, 6.0f);
    color.rgb += scan * 0.08f;

    // 一定の間隔でザザッと強いノイズを出していた
    float burst = step(0.86f, frac(time * 0.7f));
    float burstNoise = rand(uv * 1600.0f + time * 80.0f);
    burstNoise = smoothstep(0.25f, 1.0f, burstNoise);

    color.rgb = lerp(color.rgb, float3(burstNoise, burstNoise, burstNoise), burst * 0.65f);

    // ザザッとなる瞬間だけ、横線を強くしていた
    float burstLine = sin(uv.y * 900.0f + time * 80.0f);
    burstLine = burstLine * 0.5f + 0.5f;
    burstLine = pow(burstLine, 3.0f);
    color.rgb += burstLine * burst * 0.25f;

    float dist = distance(uv, center);
    float vignette = 1.0f - smoothstep(0.3f, 0.9f, dist);
    color.rgb *= vignette;

    float flicker = rand(float2(time * 5.0f, time * 13.0f));
    flicker = lerp(0.65f, 1.15f, flicker);
    color.rgb *= flicker;

    color.rgb *= float3(1.15f, 0.8f, 0.65f);
    color.rgb *= 0.75f;

    return color;
}
