// ============================================================================
// シェーダーの部品: 水たまりに映る人影（距離場への光線）と、部屋の形へのレイ反射（wetFloorPS.hlsl が使う）
// ============================================================================

#ifndef SIGNAL_LOST_PUDDLEREFLECTION_HLSLI
#define SIGNAL_LOST_PUDDLEREFLECTION_HLSLI

// 2面の「水たまり」の異変の人影（PuddleFigure。xyz = 足もと、w = 見え方）が、反射の光線をどれだけ遮るか（0〜1）を返している。
// 人影のモデルと同じ形の距離場に光線を通し（figureVolume.hlsli）、形に一番近づいた距離から、輪郭のぼけた影の濃さを求めている。
// 人影は目の方を向き、頭が足もとから36の高さになる大きさにしている（PuddleReflectionAnomaly::FigureHeadHeight と合わせている）。
// 物として置いていないので、画面にも鏡にも映らず、レイ反射の水面にだけ映る（PuddleReflectionAnomaly.h）
// PuddleFigure.w が負のときは、1面の西棟の天井に逆さまに張り付いた人影（足もとが天井で、頭が下）として、同じ形を上下逆に描いている
// 水面に映る人影：見えていないとみなす見え方、胸の高さ（囲む球の中心）と球の半径、頭の高さ（PuddleReflectionAnomaly::FigureHeadHeight と同じ）、
// 向きを決めるときに足すわずかなずれ（目の真上でも向きが決まるように）、距離場の輪郭のぼけ
static const float FIGURE_MIN_VISIBILITY = 0.001f;
static const float FIGURE_CHEST_HEIGHT = 19.0f;
static const float FIGURE_BOUND_RADIUS = 28.0f;
static const float FIGURE_HEAD_HEIGHT = 36.0f;
static const float3 FIGURE_FORWARD_NUDGE = float3(1e-4f, 0.0f, 0.0f);
static const float FIGURE_EDGE_SOFTNESS = 1.6f;

float TracePuddleFigure(float3 origin, float3 direction)
{
    const float visibility = abs(PuddleFigure.w);
    [branch]
    if (visibility <= FIGURE_MIN_VISIBILITY)
    {
        return 0.0f;
    }
    const float3 foot = PuddleFigure.xyz;
    // 足もとから頭への向き（床に立つ人影は上、天井に張り付いた人影は下）
    const float upSign = PuddleFigure.w < 0.0f ? -1.0f : 1.0f;
    // 人影を囲む球（中心は胸の高さ、半径28）のそばを通らない光線は、点と光線の距離だけで先に飛ばしている
    const float3 toCenter = foot + float3(0.0f, upSign * FIGURE_CHEST_HEIGHT, 0.0f) - origin;
    const float alongCenter = max(dot(toCenter, direction), 0.0f);
    const float3 missCenter = toCenter - direction * alongCenter;
    [branch]
    if (dot(missCenter, missCenter) > FIGURE_BOUND_RADIUS * FIGURE_BOUND_RADIUS)
    {
        return 0.0f;
    }
    // 人影の空間（前 = 目の方、右、上）へ光線を移し、大きさで割っている。天井の人影は上下を反転している
    const float scale = FIGURE_HEAD_HEIGHT / FIGURE_VOLUME_HEAD_HEIGHT;
    const float2 toEye = GetMistEyePosition().xz - foot.xz;
    const float3 forward = normalize(float3(toEye.x, 0.0f, toEye.y) + FIGURE_FORWARD_NUDGE);
    const float3 right = float3(forward.z, 0.0f, -forward.x);
    const float3 fromFoot = (origin - foot) / scale;
    const float3 localOrigin = float3(dot(fromFoot, right), fromFoot.y * upSign, dot(fromFoot, forward));
    const float3 localDirection = float3(dot(direction, right), direction.y * upSign, dot(direction, forward));
    float depth;
    return TraceFigureVolume(localOrigin, localDirection, FIGURE_EDGE_SOFTNESS, depth) * saturate(visibility);
}

// 水たまりのSSR：光線をたどる回数と、1歩ごとに歩幅を広げる割合
// 波紋で反射像が揺れる強さ。傾きを制限して、反射が大きく飛ばないようにする。
static const float PUDDLE_REFLECTION_SLOPE_SCALE = 0.28f;
static const float PUDDLE_REFLECTION_MAX_SLOPE = 0.045f;

// 水を正面から見たときと斜めから見たときの反射を調整し、汚れのある箇所では弱める。
static const float PUDDLE_REFLECTION_FACE = 0.07f;
static const float PUDDLE_REFLECTION_NEAR_WALL = 0.62f;
static const float PUDDLE_REFLECTION_GRAZING_START = 0.04f;
static const float PUDDLE_REFLECTION_GRAZING_END = 0.27f;
static const float PUDDLE_REFLECTION_GRAZING = 0.72f;
static const float PUDDLE_REFLECTION_GRIME_MIN = 0.80f;
static const float PUDDLE_REFLECTION_GRIME_START = 0.25f;
static const float PUDDLE_REFLECTION_GRIME_END = 0.75f;

// 反射が強いところでは、映り込みを読み取れるよう流れる霧を少し薄める。
static const float PUDDLE_REFLECTION_MIST_REDUCTION = 0.16f;

// 水面は正面から見たときに反射を抑え、浅い角度で見たときに強く出す。
static const float PUDDLE_FRESNEL_EXPONENT = 4.0f;

// 法線の上向き成分で、床の水・岸・濡れた足跡を同じ範囲に制限する。
static const float WET_FLOOR_UPWARD_START = 0.55f;
static const float WET_FLOOR_UPWARD_FULL = 0.92f;

// 水面SSRは反射距離でぼけを増やし、床への自己交差を除外する。
static const float WATER_SSR_DISTANCE_PER_MIP = 85.0f;
static const float WATER_SSR_BASE_BLUR = 0.55f;
static const float WATER_SSR_MIN_HIT_HEIGHT = 1.5f;
// 通常のSSRと、レイ反射を補うSSRはそれぞれの歩数と歩幅を使う。
static const int WATER_SSR_STEP_COUNT = 40;
static const float WATER_SSR_STEP_GROWTH = 1.12f;
// 遠くでも波紋が分かるよう、距離に応じて波の傾きと光沢を強める。
static const float WATER_RIPPLE_FAR_BOOST = 1.5f;
static const float WATER_RIPPLE_BOOST_START_DISTANCE = 40.0f;
static const float WATER_RIPPLE_BOOST_FULL_DISTANCE = 240.0f;

// 排水が進む境界をなだらかにし、残った水の周囲に湿った床を出す。
static const float DRAIN_WATER_EDGE_BLEND_DISTANCE = 5.0f;
static const float DRAIN_POOL_EDGE_START = 0.55f;
static const float DRAIN_DAMP_EDGE_START = 0.35f;

static const int SsrStepCount = 48;
static const float SsrStepGrowth = 1.10f;
// 水たまりのSSR：カメラからこの距離の間で、SSRの映り込みを薄めて消している（遠くは部屋の形と環境マップで映す）
static const float SsrFadeStartDistance = 160.0f;
static const float SsrFadeEndDistance = 230.0f;
// 部屋の形の反射を霧の色へなじませる距離（始め・終わり）
static const float ROOM_REFLECTION_FADE_START = 300.0f;
static const float ROOM_REFLECTION_FADE_END = 380.0f;
// 反射の光線が床へ潜らないよう上へ向ける下限と、光線を出す位置を水面から上げる量
static const float REFLECTION_MIN_UPWARD = 0.02f;
static const float REFLECTION_ORIGIN_LIFT = 0.05f;
// SSRが当たった所で、画面の色を混ぜる割合
static const float SSR_SCENE_WEIGHT = 0.85f;
// 水面に映る人影の暗さ（霧の色に掛ける）
static const float FIGURE_REFLECTION_SHADE = 0.12f;

// レイ反射の本体：部屋の形との交差を基準にし、近くで前後の画面が一致する所だけSSRの細部を足す。
// SSRは深度プリパスの深度と前のフレームの画面を使うため、それが用意されているとき（ReflectionParams.y）だけ使っている。
// figure は「水たまり」の異変の人影が光線を遮った度合い（0〜1）で、床の色そのものも沈めるのに使っている。
// 同じ位置の水面には、重なったメッシュのどちらから描いても同じ距離で反射を求める。
float3 TraceRayReflection(float3 worldPosition, float3 worldNormal, float roughness, float distanceFromCamera,
    out float figure, out float nearVertical)
{
    figure = 0.0f;
    nearVertical = 0.0f;
    // 遠くでは部屋の反射を霧の色へなじませる。技術デモでは距離による簡略化を切り替えられる。
    const float roomWeight = LeakInfo.y > FLOOR_FLAG_THRESHOLD ? 1.0f - smoothstep(ROOM_REFLECTION_FADE_START, ROOM_REFLECTION_FADE_END, distanceFromCamera) : 1.0f;
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    [branch]
    if (roomWeight <= 0.0f)
    {
        return fogColor;
    }
    float3 direction = reflect(normalize(worldPosition - GetMistEyePosition()), normalize(worldNormal));
    // 波の法線で光線が床へ潜らないよう、少しだけ上へ向けている
    direction = normalize(float3(direction.x, max(direction.y, REFLECTION_MIN_UPWARD), direction.z));
    float roomDistance;
    // 画面の外の物は、部屋の形に当たった位置から、環境マップ（本物の景色）の色を読んでいる（rayReflection.hlsli）
    float3 color = TraceRoomReflection(worldPosition + float3(0.0f, REFLECTION_ORIGIN_LIFT, 0.0f), direction,
        roughness, roomDistance, nearVertical, true);
    // SSRの寄与は画素までの距離で薄め、薄くなりきった場所では光線をたどる計算も省く。
    // 60fpsに抑えて空いたGPUの時間で、SSRで本物の景色を映す範囲を、カメラから105〜155から160〜230へ広げている
    const float nearWeight = 1.0f - smoothstep(SsrFadeStartDistance, SsrFadeEndDistance, distanceFromCamera);
    [branch]
    if (nearWeight > 0.0f && ReflectionParams.y > FLOOR_FLAG_THRESHOLD)
    {
        // 近い水たまりは、画面の中を細かい歩幅で多めにたどっている（48歩、1歩ごとに歩幅を1.10倍。届く距離は約146）。
        // 歩幅が粗いと光線が物をすり抜けて外れ、部屋の形の簡単な色で埋めることになり、映り込みが濁るため。
        // 以前の32歩・1.14倍（届く距離は約99）より歩幅が細かく、遠くの物まで届く。
        // 映る物が水面から離れているほど、前フレームの画面のぼやけた段を読む（距離85で1段、最大3段）。
        // 粗い水面（浅い縁や波の強い所）は、最初から少しぼかしている
        // 水面より1.5以上高い物に当たったときだけ映し、床そのものへの誤った当たりは捨てている（かくかくした映り込みを防ぐ）
        const float4 screen = TraceScreenSpaceReflection(worldPosition, worldNormal, SsrStepCount, SsrStepGrowth,
            1.0f / WATER_SSR_DISTANCE_PER_MIP, WATER_SSR_BASE_BLUR + roughness, WATER_SSR_MIN_HIT_HEIGHT);
        // 画面に見えている物は、部屋の形で近づけた色より本物なので、SSRが当たった所はその色を主にしている
        color = lerp(color, screen.rgb, screen.w * nearWeight * SSR_SCENE_WEIGHT);
        // SSRが水面より上に立つ物（ポンプ・机・扉など）に当たったときは、箱で映す小物と同じく「近くの縦の面」として扱い、
        // 見下ろす角度でも輪郭が読める強さで映している（これがないと、見下ろしたときの映り込みは約7%しか混ざらない）
        nearVertical = max(nearVertical, screen.w * nearWeight);
    }
    // 人影は画面に描いていないため、SSRで拾った物より手前に立っているものとして、最後に重ねている（ほぼ黒い影）。
    // 人の形が崩れないよう、人影も細かい凹凸を除いた同じ向きで映す。
    const float3 calmNormal = normalize(worldNormal);
    float3 figureDirection = reflect(normalize(worldPosition - GetMistEyePosition()), calmNormal);
    figureDirection = normalize(float3(figureDirection.x, max(figureDirection.y, REFLECTION_MIN_UPWARD), figureDirection.z));
    figure = TracePuddleFigure(worldPosition + float3(0.0f, REFLECTION_ORIGIN_LIFT, 0.0f), figureDirection);
    color = lerp(color, fogColor * FIGURE_REFLECTION_SHADE, figure);
    figure *= roomWeight;
    nearVertical *= roomWeight;
    return lerp(fogColor, color, roomWeight);
}

#endif
