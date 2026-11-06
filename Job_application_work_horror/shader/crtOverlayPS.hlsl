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
cbuffer TimeBuffer : register(b0)
{
    float time;
    float bloomIntensity;
    float noiseAmount;
    float vignetteStrength;
    float screenAspect;
    float volumeIntensity;
    float lensDistortionStrength;
    float horrorPulseStrength;
    float exposure;
    float lensMoisture;
    float corridorTension;
    float filmGradeStrength;
    float lensDirtStrength;
    float signalInterference;
    // コマ落ち（1なら、写し取りを止めた前のフレームの画面を全面に出す）と、そのときの縦のずれ
    float frameHold;
    float frameHoldOffset;
    // 恐怖で視界が狭まる強さ（0〜1）と、心拍の脈（0〜1。鳴った瞬間に1）。PostProcess::SetFearVision
    float fearVision;
    float fearBeat;
    float2 fearPadding;
};

// 描いた画面（t0）とサンプラー（s0）
Texture2D SceneTexture : register(t0);
SamplerState LinearSampler : register(s0);

// 2次元の値から0〜1の疑似乱数を作っている
float Hash(float2 value)
{
    // 全画面の粒状のノイズは、1画素あたり何度もこれを呼ぶ。sin()を使わない計算式の乱数にして、
    // 古いアナログ映像のような不安定な模様を保ちつつ、計算を軽くしている。
    float3 p3 = frac(float3(value.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

// 画面に付いた1粒の水滴の形を返している（x=縁、y=本体、z=光る所）
float3 GetLensDrop(
    float2 uv,
    float2 center,
    float radius,
    float aspect)
{
    const float2 offset = (uv - center) * float2(aspect, 1.0f);
    const float distanceToCenter = length(offset);
    const float rim = 1.0f - smoothstep(
        0.004f,
        0.014f,
        abs(distanceToCenter - radius));
    const float body = 1.0f - smoothstep(
        radius * 0.58f,
        radius * 0.96f,
        distanceToCenter);
    const float highlight = (1.0f - smoothstep(
        radius * 0.16f,
        radius * 0.52f,
        length(offset - float2(-radius * 0.30f, -radius * 0.28f)))) *
        body;
    return float3(rim, body, highlight);
}

// 水滴の縁で、後ろの景色が曲がって見える量（UVのずれ）を返している
float2 GetLensDropRefraction(
    float2 uv,
    float2 center,
    float radius,
    float aspect)
{
    const float2 aspectOffset =
        (uv - center) * float2(aspect, 1.0f);
    const float distanceToCenter = length(aspectOffset);
    const float body = 1.0f - smoothstep(
        radius * 0.42f,
        radius * 0.96f,
        distanceToCenter);
    const float curvedEdge = smoothstep(
        radius * 0.10f,
        radius * 0.78f,
        distanceToCenter) * body;
    const float2 direction = aspectOffset /
        max(distanceToCenter, 0.001f);
    return direction * curvedEdge * radius * 0.24f *
        float2(1.0f / aspect, 1.0f);
}

// 重ねる層の色と透明度を返している（アルファブレンドで画面に重なる）
float4 main(PS_IN input) : SV_TARGET
{
    // コマ落ち：影が近くラジオの雑音が強いとき、一瞬だけ映像が止まる。止まった画面は縦に少しずれ、
    // ずれの継ぎ目に黒い帯が走り、色が少し抜ける（古いビデオの同期が外れたように見せている）
    [branch]
    if (frameHold > 0.5f)
    {
        const float heldY = frac(input.uv.y + frameHoldOffset);
        float3 held = SceneTexture.SampleLevel(LinearSampler, float2(input.uv.x, heldY), 0.0f).rgb;
        const float seam = smoothstep(0.0f, 0.018f, min(heldY, 1.0f - heldY));
        const float scanline = 0.86f + 0.14f * step(0.5f, frac(input.uv.y * 240.0f));
        held = lerp(dot(held, float3(0.2126f, 0.7152f, 0.0722f)).xxx, held, 0.65f);
        return float4(held * scanline * lerp(0.08f, 1.0f, seam), 1.0f);
    }
    const float2 center = float2(0.5f, 0.5f);
    float2 centered = input.uv - center;
    // ノイズと周辺減光の強さ：驚かせる演出・緊張・信号の乱れで強めている
    const float eventNoise = saturate(
        noiseAmount + horrorPulseStrength * 1.20f +
        corridorTension * 0.16f + signalInterference * 0.48f);
    const float eventVignette =
        vignetteStrength + horrorPulseStrength * 0.52f +
        corridorTension * 0.18f;

    // 遊ぶのに必要な情報を隠さない程度の、柔らかい周辺減光。
    float edgeDistance = length(centered * float2(1.15f, 1.0f));
    const float2 radialDirection = centered /
        max(length(centered), 0.001f);
    // 普段の緊張感だけでは、元の画像をずらさない。色ずれと歪みは、
    // 驚かせる演出・信号の異常・終盤の強い緊張のときだけにしている。
    const float highTension = smoothstep(0.62f, 0.92f, corridorTension);
    const float distortionEvent = saturate(
        horrorPulseStrength * 1.15f +
        highTension * 0.32f +
        signalInterference);
    // 画面の端には、いつもわずかな色ずれを残している（古いレンズのような、P.T. の画面の質感）
    const float eventAberration = saturate(
        0.30f + horrorPulseStrength +
        highTension * lensDistortionStrength * 0.22f +
        signalInterference * 0.72f);
    const float edgeAberration = smoothstep(0.20f, 0.72f, edgeDistance);
    const float2 aspectCentered =
        centered * float2(screenAspect, 1.0f);
    const float radialSquared = dot(aspectCentered, aspectCentered);
    const float radialWarp = lensDistortionStrength * distortionEvent *
        (0.0015f + highTension * 0.0040f);
    // 信号の復旧に失敗すると、画面が横の帯ごとにばらばらにずれる。
    // ずれる量にはわざと上限を付け、プレイヤーが進む方向を見失わないようにしている。
    const float signalBand = floor(input.uv.y * 54.0f);
    const float signalFrame = floor(time * 18.0f);
    const float signalSeed = Hash(float2(signalBand, signalFrame));
    const float signalGate = smoothstep(0.70f, 0.96f, signalSeed);
    const float signalJitter = (signalSeed - 0.5f) * 0.010f *
        signalInterference * signalGate;
    const float2 distortedUv = saturate(
        input.uv + centered * radialSquared * radialWarp +
        float2(signalJitter, 0.0f));
    const float chromaOffset =
        (0.0014f + horrorPulseStrength * 0.0038f +
         highTension * lensDistortionStrength * 0.0012f) *
        edgeAberration + signalInterference * signalGate * 0.0024f;
    const float3 sceneCenter = SceneTexture.SampleLevel(
        LinearSampler,
        input.uv,
        0.0f).rgb;
    float3 distortedScene = sceneCenter;
    // 普段はdistortedUvとinput.uvが同じ。驚かせる演出のときだけ2回目を読み、
    // 普段の全画面のテクスチャの読み取りを1回分減らしている。
    [branch]
    if (distortionEvent > 0.001f)
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
    if (eventAberration > 0.001f)
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
        (0.008f + highTension * 0.012f) +
        distortionDifference * distortionEvent * 0.08f);
    const float chromaDifference = length(
        abs(chromaticScene - sceneCenter));
    const float chromaAlpha = saturate(
        eventAberration * edgeAberration * 0.022f +
        chromaDifference * (horrorPulseStrength * 0.75f + edgeAberration * 0.45f));
    // 周辺減光は強めにし、画面の端から闇が迫って見えるようにしている（中央は暗くしない）
    float vignette = smoothstep(0.22f, 0.80f, edgeDistance) *
        0.55f * eventVignette;

    // 1行おきの走査線。SV_POSITIONを使っているので、
    // ウィンドウの解像度が違っても線の太さが変わらない。
    float scanWave = sin(input.pos.y * 3.14159265f) * 0.5f + 0.5f;
    float scanline = (1.0f - scanWave) * 0.018f * eventNoise;

    // 1/30秒ごとに変わる粒状のノイズ（暗い粒）
    float frame = floor(time * 30.0f);
    float noise = Hash(floor(input.pos.xy) + frame * float2(17.0f, 31.0f));
    // 粒は暗い所ほど強く出し、闇がざらついて見えるようにしている
    const float grainLuminance = dot(sceneCenter,
        float3(0.2126f, 0.7152f, 0.0722f));
    // フィルムの粒：暗い粒と明るい粒の両方を、はっきり見える強さで出している（サイレントヒルの画面の粒を参考）。
    // 暗い所ほど強く出し、闇がざらついて見えるようにしている
    const float grainStrength = (0.35f + eventNoise) * lerp(1.0f, 0.40f, smoothstep(0.02f, 0.30f, grainLuminance));
    float darkGrain = smoothstep(0.55f, 1.0f, noise) * 0.080f * grainStrength;
    const float brightNoise = Hash(floor(input.pos.xy) + frame * float2(31.0f, 17.0f) + 7.3f);
    const float brightGrain = smoothstep(0.62f, 1.0f, brightNoise) * 0.055f * grainStrength;

    // ごく薄い、ゆっくり流れる帯で、アナログ映像らしい動きを出している。
    float rolling = sin(input.uv.y * 10.0f - time * 1.8f) * 0.5f + 0.5f;
    rolling = pow(rolling, 12.0f) * 0.012f * eventNoise;

    // まれに出る埃の画素は明るいが、透明度はとても低くしている。
    float dust = step(0.9985f, noise);

    float darkAlpha = saturate(vignette + scanline + darkGrain + rolling);
    float dustAlpha = dust * 0.030f * eventNoise + brightGrain;

    // レンズの水滴は、プレイヤーが水たまりを踏んだ後だけ出る。水滴ごとに
    // 流れ落ちる速さを変え、模様がスタンプのように見えないようにしている。
    const float slide0 = frac(0.12f + time * 0.010f);
    const float slide1 = frac(0.48f + time * 0.006f);
    const float slide2 = frac(0.76f + time * 0.008f);
    float3 lensDrop = 0.0f;
    float2 lensRefraction = 0.0f;
    const float moisture = saturate(lensMoisture);
    // レンズの水滴は、ほとんどの時間は出ていない。水しぶきが起きるまで、
    // 10回の距離の計算を、普段の全画面の処理から外している。
    [branch]
    if (moisture > 0.001f)
    {
        lensDrop = max(lensDrop, GetLensDrop(
            input.uv, float2(0.18f, slide0 * 1.18f - 0.09f),
            0.025f, screenAspect));
        lensDrop = max(lensDrop, GetLensDrop(
            input.uv, float2(0.34f, slide2 * 1.14f - 0.07f),
            0.014f, screenAspect));
        lensDrop = max(lensDrop, GetLensDrop(
            input.uv, float2(0.62f, slide1 * 1.20f - 0.10f),
            0.031f, screenAspect));
        lensDrop = max(lensDrop, GetLensDrop(
            input.uv, float2(0.79f, slide0 * 1.12f - 0.06f),
            0.018f, screenAspect));
        lensDrop = max(lensDrop, GetLensDrop(
            input.uv, float2(0.91f, slide2 * 1.17f - 0.08f),
            0.011f, screenAspect));
        lensRefraction += GetLensDropRefraction(
            input.uv, float2(0.18f, slide0 * 1.18f - 0.09f),
            0.025f, screenAspect);
        lensRefraction += GetLensDropRefraction(
            input.uv, float2(0.34f, slide2 * 1.14f - 0.07f),
            0.014f, screenAspect);
        lensRefraction += GetLensDropRefraction(
            input.uv, float2(0.62f, slide1 * 1.20f - 0.10f),
            0.031f, screenAspect);
        lensRefraction += GetLensDropRefraction(
            input.uv, float2(0.79f, slide0 * 1.12f - 0.06f),
            0.018f, screenAspect);
        lensRefraction += GetLensDropRefraction(
            input.uv, float2(0.91f, slide2 * 1.17f - 0.08f),
            0.011f, screenAspect);
        // 水滴によるずれは、合計で一定の量までにしている
        const float refractionMagnitude = length(lensRefraction);
        if (refractionMagnitude > 0.008f)
        {
            lensRefraction *= 0.008f / refractionMagnitude;
        }
    }
    // 水滴が付いている間だけ、ずらした位置の景色を読んでいる
    float3 refractedScene = sceneCenter;
    [branch]
    if (moisture > 0.001f)
    {
        refractedScene = SceneTexture.SampleLevel(
            LinearSampler,
            saturate(input.uv - lensRefraction * moisture),
            0.0f).rgb;
    }
    const float dropRefractionAlpha = saturate(
        (lensDrop.y * 0.085f + lensDrop.x * 0.025f) * moisture);
    const float dropAlpha = saturate(
        (lensDrop.x * 0.13f + lensDrop.y * 0.025f +
         lensDrop.z * 0.09f) * moisture);

    // 驚かせる演出では、半透明の同期の乱れ（横の裂け目）を加えている。この層は景色を置き換えないので、
    // テクスチャが無くても画面が真っ黒になることはない。
    float bandId = floor(input.uv.y * 38.0f);
    float bandNoise = Hash(float2(bandId, floor(time * 24.0f)));
    float tear = step(0.86f, bandNoise) * horrorPulseStrength;
    tear = max(tear, signalGate * signalInterference * 0.58f);
    float thinLine = 1.0f - smoothstep(
        0.02f,
        0.12f,
        abs(frac(input.uv.y * 38.0f) - 0.5f));
    const float tearOffset =
        (bandNoise - 0.5f) * 0.030f * horrorPulseStrength * thinLine;
    float3 shiftedScene = sceneCenter;
    [branch]
    if (horrorPulseStrength > 0.001f || signalInterference > 0.001f)
    {
        shiftedScene = SceneTexture.SampleLevel(
            LinearSampler,
            saturate(input.uv + float2(tearOffset, 0.0f)),
            0.0f).rgb;
    }
    // 信号の乱れの横線
    float tearAlpha = tear * thinLine * 0.16f;
    const float signalLine = 1.0f - smoothstep(
        0.015f, 0.085f,
        abs(frac(input.uv.y * 54.0f) - 0.5f));
    const float signalAlpha = signalInterference *
        (signalGate * signalLine * 0.055f +
         (1.0f - signalLine) * 0.006f);
    float alpha = saturate(
        darkAlpha + dustAlpha + tearAlpha + dropAlpha +
        dropRefractionAlpha + chromaAlpha + distortionAlpha +
        signalAlpha);

    // 重ねる層の色：埃・裂け目・信号の乱れ・歪み・色ずれ・水滴を、それぞれの強さで混ぜている
    // 暗い層（周辺減光・暗い粒）と明るい層（埃・明るい粒）を、それぞれの強さの割合で混ぜた色にしている
    float3 overlayColor = lerp(
        float3(0.0f, 0.0f, 0.0f),
        float3(0.62f, 0.65f, 0.62f),
        saturate(dustAlpha / max(darkAlpha + dustAlpha, 0.0001f))
    );
    float channelChoice = Hash(float2(bandId + 13.0f, floor(time * 24.0f)));
    float3 tearColor = channelChoice < 0.5f
        ? float3(0.12f, 0.72f, 0.80f)
        : float3(0.82f, 0.12f, 0.20f);
    tearColor = lerp(shiftedScene, tearColor, 0.24f);
    overlayColor = lerp(overlayColor, tearColor, saturate(tear));
    const float3 signalColor = lerp(
        float3(0.025f, 0.20f, 0.28f),
        float3(0.38f, 0.055f, 0.035f),
        signalSeed);
    overlayColor = lerp(
        overlayColor,
        lerp(shiftedScene, signalColor, 0.20f),
        saturate(signalAlpha * 13.0f));
    overlayColor = lerp(
        overlayColor,
        distortedScene,
        saturate(distortionAlpha * 18.0f));
    overlayColor = lerp(
        overlayColor,
        chromaticScene,
        saturate(chromaAlpha * 16.0f));
    const float3 waterColor = lerp(
        float3(0.025f, 0.045f, 0.050f),
        float3(0.68f, 0.78f, 0.80f),
        saturate(lensDrop.x * 0.55f + lensDrop.z));
    overlayColor = lerp(
        overlayColor,
        waterColor,
        saturate(dropAlpha * 6.0f));
    overlayColor = lerp(
        overlayColor,
        refractedScene,
        saturate(dropRefractionAlpha * 9.0f));

    // 廊下の奥ほど、ゆっくり冷たい色で彩度を落としている。ごく控えめで半透明なので、
    // 進むための情報は隠れない。
    const float lowerScreen = smoothstep(0.22f, 0.92f, input.uv.y);
    const float tensionVeil = corridorTension *
        (0.014f + lowerScreen * 0.010f);
    const float3 tensionColor = float3(0.035f, 0.060f, 0.070f);
    overlayColor = lerp(
        overlayColor,
        tensionColor,
        saturate(tensionVeil * 12.0f));
    alpha = saturate(alpha + tensionVeil);

    // 恐怖で視界が狭まる：緊張が高いほど、画面の端から、ぼけて色の抜けた闇が中央へ迫る。心拍が鳴るたびに一瞬さらに迫る。
    // 端の景色を中心へ向かう向きに5点読んでぼかし、白黒に寄せて暗くした色を、端ほど濃く、今の層の上に重ねている。
    // 視線の中央（見たい所）は隠さない。緊張していない間は、この全体で同じ分岐で計算を丸ごと省いている
    [branch]
    if (fearVision > 0.001f)
    {
        const float fear = smoothstep(0.0f, 1.0f, fearVision);
        const float pulse = fearBeat * fearBeat;
        // ここより外が狭まる、視界の縁。怖いほど内側へ寄り、心拍で一瞬さらに寄る
        const float inner = lerp(0.62f, 0.28f, fear) - pulse * 0.06f * fear;
        const float fearEdge = smoothstep(inner, inner + 0.40f, edgeDistance);
        float3 blurred = 0.0f;
        [unroll]
        for (int tap = 0; tap < 5; ++tap)
        {
            const float pull = 0.022f * (float)(tap + 1) * fearEdge * fear;
            blurred += SceneTexture.SampleLevel(LinearSampler, saturate(input.uv - centered * pull), 0.0f).rgb;
        }
        blurred *= 0.2f;
        const float blurredLuminance = dot(blurred, float3(0.2126f, 0.7152f, 0.0722f));
        // 色を抜いて少し冷たくし、端ほど暗くしている
        const float3 fearColor = lerp(blurred, blurredLuminance.xxx * float3(0.80f, 0.86f, 0.92f), 0.78f) *
            lerp(0.62f, 0.22f, fearEdge);
        const float fearAlpha = saturate(fearEdge * fear * (0.70f + pulse * 0.25f));
        const float layeredAlpha = fearAlpha + alpha * (1.0f - fearAlpha);
        overlayColor = (fearColor * fearAlpha + overlayColor * alpha * (1.0f - fearAlpha)) /
            max(layeredAlpha, 0.0001f);
        alpha = layeredAlpha;
    }

    // 映画のような色分け：影の部分は冷たい色に、照明の部分は控えめな暖かいにじみを残している。
    // これも半透明の層として重ねているので、
    // 取り込んだ景色が黒で置き換わることはない。
    const float sceneLuminance = dot(
        sceneCenter,
        float3(0.2126f, 0.7152f, 0.0722f));
    const float shadowWeight = 1.0f - smoothstep(
        0.08f, 0.48f, sceneLuminance);
    const float highlightWeight = smoothstep(
        0.42f, 1.10f, sceneLuminance);
    const float filmAlpha = saturate(filmGradeStrength) *
        (shadowWeight * 0.050f + highlightWeight * 0.022f);
    const float3 coldShadow = float3(0.020f, 0.040f, 0.052f);
    const float3 warmLight = float3(0.160f, 0.095f, 0.040f);
    const float3 filmColor = lerp(
        coldShadow,
        warmLight,
        saturate(highlightWeight * 1.35f));
    // 2つの半透明の層を1つにまとめ、合わせた色と透明度を返している
    const float combinedAlpha = alpha + filmAlpha * (1.0f - alpha);
    overlayColor = (
        overlayColor * alpha +
        filmColor * filmAlpha * (1.0f - alpha)) /
        max(combinedAlpha, 0.0001f);
    alpha = combinedAlpha;

    return float4(overlayColor, alpha);
}
