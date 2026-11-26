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
#include "renderingConfig.hlsli"
#include "fastNoise.hlsli"

// 描いた画面（t0）とサンプラー（s0）
Texture2D SceneTexture : register(t0);
// 残像に使う、少し前に写し取って止めておいた画面（PostProcess の m_EchoTexture）
Texture2D EchoTexture : register(SIGNAL_LOST_TEXTURE_REGISTER(SIGNAL_LOST_CRT_ECHO_TEXTURE_SLOT));
SamplerState LinearSampler : register(s0);

#include "crtOverlayParts.hlsli"

float4 main(PS_IN input) : SV_TARGET
{
    // コマ落ち：影が近くラジオの雑音が強いとき、一瞬だけ映像が止まる。止まった画面は縦に少しずれ、
    // ずれの継ぎ目に黒い帯が走り、色が少し抜ける（古いビデオの同期が外れたように見せている）
    [branch]
    if (frameHold > CRT_FRAME_HOLD_THRESHOLD)
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
    // 影のまわりの空間のゆがみ：影の画面の位置を中心にした円の中だけ、流れるノイズと波で景色をずらしている。
    // 影が暗闇に溶けていても、ゆがみで「そこに何かいる」と分かる（近いほど強く、円も大きい）
    float2 presenceOffset = float2(0.0f, 0.0f);
    float presenceMask = 0.0f;
    [unroll]
    for (int warpIndex = 0; warpIndex < PRESENCE_WARP_COUNT; ++warpIndex)
    {
        const float4 warp = presenceWarp[warpIndex];
        [branch]
        if (warp.w > CRT_EFFECT_EPSILON)
        {
            const float radius = max(warp.z, CRT_EFFECT_EPSILON);
            const float2 local = (input.uv - warp.xy) * float2(screenAspect, 1.0f) / radius;
            const float falloff = 1.0f - smoothstep(PRESENCE_WARP_EDGE_START, 1.0f, length(local));
            [branch]
            if (falloff > 0.0f)
            {
                const float2 flow = float2(time, -time) * PRESENCE_WARP_FLOW_SPEED;
                const float2 noise = float2(
                    FastValueNoise(local * PRESENCE_WARP_NOISE_SCALE + flow),
                    FastValueNoise(local * PRESENCE_WARP_NOISE_SCALE - flow + PRESENCE_WARP_NOISE_SEED)) - 0.5f;
                const float2 wave = float2(
                    sin(local.y * PRESENCE_WARP_WAVE_FREQUENCY + time * PRESENCE_WARP_WAVE_SPEED),
                    cos(local.x * PRESENCE_WARP_WAVE_FREQUENCY - time * PRESENCE_WARP_WAVE_SPEED));
                const float2 swirl = lerp(noise * 2.0f, wave, PRESENCE_WARP_WAVE_WEIGHT);
                const float amount = falloff * warp.w;
                presenceOffset += swirl * (radius * PRESENCE_WARP_AMPLITUDE * amount) / float2(screenAspect, 1.0f);
                presenceMask = max(presenceMask, amount);
            }
        }
    }
    const float2 distortedUv = saturate(
        input.uv + centered * radialSquared * radialWarp +
        float2(signalJitter, 0.0f) + presenceOffset);
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
    if (distortionEvent > CRT_EFFECT_EPSILON || presenceMask > CRT_EFFECT_EPSILON)
    {
        distortedScene = SceneTexture.SampleLevel(
            LinearSampler,
            distortedUv,
            0.0f).rgb;
        // 影のまわりは、ほんの少しだけ暗く沈めている
        distortedScene *= 1.0f - presenceMask * PRESENCE_WARP_DARKEN;
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
        distortionDifference * distortionEvent * CRT_WARP_DIFFERENCE_GAIN +
        presenceMask * PRESENCE_WARP_ALPHA_GAIN);
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
    float3 tearColor = channelChoice < CRT_TEAR_COLOR_SPLIT
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

    // 残像：少し前に止めた画面を、少しずらして、色を抜いて重ねている。今の画面と違う所（動いた物・視点を動かした分）ほど濃い。
    // 止めた画面は約0.16秒ごとに写し直すので、視点を動かすと、景色が二重にぶれて遅れて追いかけてくる
    [branch]
    if (echo.x > CRT_EFFECT_EPSILON)
    {
        const float3 echoScene = EchoTexture.SampleLevel(LinearSampler, saturate(input.uv + echo.yz), 0.0f).rgb;
        const float echoLuminance = dot(echoScene, CRT_LUMINANCE_WEIGHTS);
        const float3 echoColor = lerp(echoLuminance.xxx * ECHO_TINT, echoScene, ECHO_SATURATION) * ECHO_BRIGHTNESS;
        const float band = FastHash21(float2(floor(input.uv.y * ECHO_BAND_COUNT), echo.w));
        const float echoDifference = length(echoScene - sceneCenter);
        const float echoAlpha = saturate(echo.x * (ECHO_BASE_ALPHA + echoDifference * ECHO_DIFFERENCE_ALPHA) *
            (1.0f - band * ECHO_BAND_VARIATION)) * ECHO_MAX_ALPHA;
        const float echoLayered = echoAlpha + alpha * (1.0f - echoAlpha);
        overlayColor = (echoColor * echoAlpha + overlayColor * alpha * (1.0f - echoAlpha)) /
            max(echoLayered, CRT_MIN_DIVISOR);
        alpha = echoLayered;
    }

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

    // 水に呑まれる：画面の下から、波打つ水面の線が上がってくる。線より下は、景色がゆらいで赤く濁り、泡が上っていく。
    // 線の上は今までの層のまま。水が上がりきると、画面の全部が水の中になる（この後、Scene が暗転させる）
    [branch]
    if (submerge.x > CRT_EFFECT_EPSILON)
    {
        const float wave =
            sin(input.uv.x * SUBMERGE_WAVE_FREQUENCY + time * SUBMERGE_WAVE_SPEED) * SUBMERGE_WAVE_HEIGHT +
            sin(input.uv.x * SUBMERGE_RIPPLE_FREQUENCY - time * SUBMERGE_RIPPLE_SPEED) * SUBMERGE_RIPPLE_HEIGHT;
        // uv の y は画面の下ほど大きい。高さ0で線は画面の下の外、1で上の外
        const float waterline = 1.0f - submerge.x * (1.0f + SUBMERGE_OVERSHOOT) + wave;
        const float under = smoothstep(waterline - SUBMERGE_LINE_SOFTNESS, waterline + SUBMERGE_LINE_SOFTNESS, input.uv.y);

        // 水の中の景色：縦横に揺らいだ所を読み、明るさだけを赤く染め、線から深いほど暗い赤へ沈めている
        const float2 refraction = float2(
            sin(input.uv.y * SUBMERGE_REFRACT_FREQUENCY + time * SUBMERGE_REFRACT_SPEED),
            cos(input.uv.x * SUBMERGE_REFRACT_FREQUENCY - time * SUBMERGE_REFRACT_SPEED)) * SUBMERGE_REFRACT_STRENGTH;
        const float3 underScene = SceneTexture.SampleLevel(LinearSampler, saturate(input.uv + refraction), 0.0f).rgb;
        const float underLuminance = dot(underScene, CRT_LUMINANCE_WEIGHTS);
        const float depth = saturate((input.uv.y - waterline) * SUBMERGE_DEPTH_GAIN);
        float3 submergeColor = lerp(underLuminance.xxx * SUBMERGE_BLOOD_TINT, underScene, SUBMERGE_SCENE_KEEP);
        submergeColor = lerp(submergeColor, SUBMERGE_DEEP_COLOR, depth * SUBMERGE_DEPTH_DARKEN);

        // 上っていく泡：画面を縦の列とマスに分け、一部のマスにだけ、輪の形の泡を置いて上へ流している。
        // 列ごとに上る速さと縦の位置をずらし、横に揺らして、泡が行と列にそろって見えないようにしている
        const float bubbleColumn = floor(input.uv.x * screenAspect * SUBMERGE_BUBBLE_CELLS);
        const float columnRandom = FastHash21(float2(bubbleColumn, SUBMERGE_BUBBLE_SEED));
        const float columnRise = time * SUBMERGE_BUBBLE_RISE *
            (SUBMERGE_BUBBLE_SPEED_MIN + columnRandom * SUBMERGE_BUBBLE_SPEED_RANGE) + columnRandom * SUBMERGE_BUBBLE_COLUMN_OFFSET;
        const float2 bubbleSpace = float2(input.uv.x * screenAspect * SUBMERGE_BUBBLE_CELLS,
            (input.uv.y + columnRise) * SUBMERGE_BUBBLE_CELLS);
        const float2 bubbleCell = floor(bubbleSpace);
        const float bubbleRandom = FastHash21(bubbleCell + SUBMERGE_BUBBLE_SEED);
        const float2 bubbleCenter = float2((bubbleRandom - 0.5f) * SUBMERGE_BUBBLE_JITTER +
            sin(time * SUBMERGE_BUBBLE_SWAY_SPEED + bubbleCell.y + bubbleRandom * SUBMERGE_BUBBLE_COLUMN_OFFSET) * SUBMERGE_BUBBLE_SWAY, 0.0f);
        const float bubbleRadius = SUBMERGE_BUBBLE_RADIUS_MIN + bubbleRandom * SUBMERGE_BUBBLE_RADIUS_RANGE;
        const float bubbleDistance = length(frac(bubbleSpace) - 0.5f - bubbleCenter);
        const float bubbleRing = smoothstep(bubbleRadius, bubbleRadius * SUBMERGE_BUBBLE_INNER, bubbleDistance) *
            smoothstep(bubbleRadius * SUBMERGE_BUBBLE_INNER * SUBMERGE_BUBBLE_INNER, bubbleRadius * SUBMERGE_BUBBLE_INNER, bubbleDistance);
        const float bubble = bubbleRing * step(SUBMERGE_BUBBLE_CHANCE, FastHash21(bubbleCell)) * under;
        submergeColor += bubble * SUBMERGE_BUBBLE_BRIGHTNESS * SUBMERGE_LINE_COLOR;

        // 水面の線のすぐそばを、明るく光らせている（水面を下から・横から見た光の縁）
        const float lineGlow = exp(-abs(input.uv.y - waterline) * SUBMERGE_LINE_SHARPNESS) * SUBMERGE_LINE_GLOW;
        const float submergeAlpha = saturate(under * SUBMERGE_ALPHA + lineGlow);
        submergeColor = lerp(submergeColor, SUBMERGE_LINE_COLOR, saturate(lineGlow / max(submergeAlpha, CRT_MIN_DIVISOR)));
        const float layeredAlpha = submergeAlpha + alpha * (1.0f - submergeAlpha);
        overlayColor = (submergeColor * submergeAlpha + overlayColor * alpha * (1.0f - submergeAlpha)) /
            max(layeredAlpha, CRT_MIN_DIVISOR);
        alpha = layeredAlpha;
    }

    // 一瞬だけ映る血まみれの別の場所：同じ景色が、赤黒く錆びて血の垂れた場所として、まばたきのように一瞬だけ映る（crtOverlayParts.hlsli）。
    // ほかの層の上に重ね、映っている間は景色をほぼ置き換えている
    [branch]
    if (submerge.y > CRT_EFFECT_EPSILON)
    {
        const float3 otherworldColor = GetOtherworldColor(input.uv, edgeDistance, submerge.z);
        const float otherworldAlpha = saturate(submerge.y);
        const float layeredAlpha = otherworldAlpha + alpha * (1.0f - otherworldAlpha);
        overlayColor = (otherworldColor * otherworldAlpha + overlayColor * alpha * (1.0f - otherworldAlpha)) /
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
