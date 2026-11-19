// ============================================================================
// 共通の処理: 反射の光線を、景色を描き直さずにシェーダーの中で飛ばして、映る色を求めている（床の水たまり wetFloorPS と、2面の壁の鏡 mirrorPS）。
// 主な技術: SSR（画面の深度に沿って光線をたどる）、部屋の形（壁を上から見た長方形・床と天井の高さ・小物の箱）とのスラブ法の交差、
//           当たった面の点光源と懐中電灯の照明、光線が光源のそばを通った分だけ光る照明の器具
// ・画面に映っている物（扉・ロッカー・小物・懐中電灯で照らした壁）は SSR（TraceScreenSpaceReflection）で拾っている。
// ・画面の外の物は、部屋の形（roomOcclusion.hlsli の b11 にある、壁を上から見た長方形と床・天井の高さ）と、
//   カメラの近くから選んだ小物の箱（b12 の ReflectionProps）との交差で求めている。
//   床を見下ろしているときの真上の照明や、鏡に映る自分の後ろの壁のように、画面の外にある物も正しい位置に映る。
// ・当たった天井・壁・床は、点光源と懐中電灯で簡単に照らし、照明の器具そのものは、光線が光源のそばを通った分だけ光らせている。
// common.hlsl・flashlightLighting.hlsli・waterCaustics.hlsli・roomOcclusion.hlsli・horrorGrade.hlsli・horrorMist.hlsli の後に読み込んでいる。
// サンプラー g_SamplerState（s0）は、読み込む側で用意している。
// ============================================================================
#ifndef RAY_REFLECTION_INCLUDED
#define RAY_REFLECTION_INCLUDED

// ノイズの関数（FastValueNoise・FastHash21）。読み込む側の順番に頼らず、この部品だけでも読めるようにしている
#include "fastNoise.hlsli"

// SSRの前のフレームの画面（t4。縮小画像の段を持つ）と、深度プリパスの深度（t8）（ScreenSpaceReflection）
Texture2D g_SsrScene : register(t4);
Texture2D<float> g_SsrDepth : register(t8);
Texture2D<float> g_PreviousSsrDepth : register(t17);
// 環境マップ（t14。カメラの位置から6方向を撮った本物の景色。GameRendering の EnvironmentProbe。pbrLighting.hlsli と同じ物）。
// 水たまりのレイ反射で、画面の外の物の色に使っている。撮れているときだけ ReflectionParams.w が1
TextureCube g_RayEnvironmentProbe : register(t14);
// 環境マップで読む一番小さい段（256画素の3段下 = 32画素。これより小さい段を引き伸ばすと四角いブロックが見える）
static const float RAY_ENVIRONMENT_MAX_BLUR_MIP = 3.0f;
static const float RAY_ENVIRONMENT_BLUR_ROUGHNESS_WEIGHT = 0.45f;
static const float RAY_ENVIRONMENT_BLUR_DISTANCE = 400.0f;
static const float RAY_ENVIRONMENT_MISS_BLUR = 0.6f;
// SSRでぶつかった位置を、1つ前の歩幅との間で半分ずつ詰める回数（7回で歩幅の1/128まで詰まる）
static const int SSR_REFINE_STEPS = 7;
// 厚みと履歴の許容差はビュー空間の距離、画面端の幅は UV の割合。
static const float SSR_MIN_HIT_THICKNESS = 3.0f;
static const float SSR_HIT_THICKNESS_PER_DISTANCE = 0.06f;
static const float SSR_MIN_HISTORY_TOLERANCE = 0.85f;
static const float SSR_HISTORY_TOLERANCE_PER_DEPTH = 0.008f;
static const float SSR_EDGE_FADE_UV = 0.13f;
static const float SSR_HISTORY_FADE_START_RATIO = 0.45f;
static const float SSR_HIT_HEIGHT_FADE_WIDTH = 1.5f;
static const float SSR_STEP_FADE_START_RATIO = 0.65f;
// スクリーン履歴の段数は ScreenSpaceReflection.cpp の HistoryMipLevels と合わせる。
static const float SSR_HISTORY_MAX_BLUR_LOD = 3.0f;
// ぼかしが小さい場合は、4方向のサンプリングを省いて元の画像を読む。
static const float SSR_SINGLE_SAMPLE_BLUR_LIMIT = 0.35f;
// SSR の既定の進める回数と歩幅の広げ方、ぶつかった高さを調べない既定の値（とても低い）
#define SSR_DEFAULT_STEP_COUNT 40
#define SSR_DEFAULT_STEP_GROWTH 1.12f
#define SSR_NO_MIN_HIT_HEIGHT -1.0e9f
// ぼかして読むときの4つの標本の斜めのずれと、平均の重み
#define SSR_BLUR_TAP_SKEW 0.4f
#define SSR_BLUR_TAP_WEIGHT 0.25f

// 部屋の形との交差：上下を向くとみなす向きの下限、0で割らないよう丸める向きの下限、
// 定数の印（1 = 使う）を見るしきい値、小物の箱の並び（中心・半分の大きさ・色の3つ）、扉とみなす向きの長さの2乗、壁とみなす法線の縦の上限
#define ROOM_REFLECTION_MIN_VERTICAL_DIRECTION 0.0001f
#define ROOM_REFLECTION_MIN_DIRECTION 1e-5f
#define ROOM_REFLECTION_FLAG_THRESHOLD 0.5f
#define REFLECTION_PROP_STRIDE 3
#define ROOM_REFLECTION_DOOR_AXIS_THRESHOLD 0.5f
#define ROOM_REFLECTION_WALL_NORMAL_LIMIT 0.5f
// 扉の鏡板（横の縁のぼかし始め・終わり、縦の縁のぼかし始め・終わり、中心の高さ、暗さ）と、扉以外の小物の暗さ
#define DOOR_REFLECTION_PANEL_X_START 0.62f
#define DOOR_REFLECTION_PANEL_X_END 0.68f
#define DOOR_REFLECTION_PANEL_Y_START 0.28f
#define DOOR_REFLECTION_PANEL_Y_END 0.34f
#define DOOR_REFLECTION_PANEL_CENTER_Y 0.44f
#define DOOR_REFLECTION_PANEL_SHADE 0.48f
#define PROP_REFLECTION_SHADE 0.55f
// 当たった面の照明：0で割らないための下限、懐中電灯の当て方（最低＋向きの幅）、環境マップに足す器具の光の重み
#define ROOM_REFLECTION_MIN_DIVISOR 0.001f
#define ROOM_REFLECTION_FLASHLIGHT_BASE 0.25f
#define ROOM_REFLECTION_FLASHLIGHT_FACING 0.75f
#define ROOM_ENVIRONMENT_GLOW_WEIGHT 0.25f

// SSR：反射の光線をビュー空間で少しずつ（だんだん大きな歩幅で）進め、画面の深度より奥へ入った所を「ぶつかった」とし、
// 当たった面を前のフレームのカメラへ投影し直して、同じ位置の色を読む。前の画面で隠れていた面は使わない。
// 返り値の xyz は色、w はぶつかった度合い（画面の端と、遠くまで進んだ所では0へ近づけている）。
// 最初の標本はわずかに離し、視点に近すぎる射影では交差を判定しない。
#define SSR_INITIAL_RAY_DISTANCE 1.5f
#define SSR_MIN_PROJECTION_W 0.001f

// 面から離れすぎた交差や、ほとんど見えない映り込みを捨てる。
#define SSR_MIN_THICKNESS_CONFIDENCE 0.001f
#define SSR_MIN_VISIBLE_WEIGHT 0.001f

// 壁や小物と交差するとき、光線の始点のすぐそばを除く。
#define ROOM_REFLECTION_MIN_INTERSECTION_DISTANCE 0.05f

// stepCount は進める回数、stepGrowth は1回ごとに歩幅を広げる割合（レイ反射では、遠くは部屋の形で補うので少なくしている）。
// blurPerDistance は、映る物が反射する面から離れているほどぼかす強さ（前のフレームの画面の縮小画像の段。0ならぼかさない）。
// 本物の水面は、水面に近い物（柱の根元）はくっきり、離れた物ほどぼやけて映るので、それに合わせている（2面の壁の鏡は0）
// minHitHeight は、ぶつかった物が反射する面よりこれ以上高くなければ捨てる高さ（床の水たまりで使う）。
// 床の水たまりに映るのは水面より上の物だけなので、すぐ近くの床そのものに「ぶつかった」と誤って判定した所（波で傾いた光線や
// 浅い角度の光線で起きる）を捨てている。捨てないと、懐中電灯で明るい床がギザギザの塊で映り、歩くたびに跳ねてかくかくする。
// 既定（とても小さい値）では判定しない（2面の壁の鏡）
float4 TraceScreenSpaceReflection(float3 worldPosition, float3 worldNormal, int stepCount = SSR_DEFAULT_STEP_COUNT,
    float stepGrowth = SSR_DEFAULT_STEP_GROWTH, float blurPerDistance = 0.0f, float baseBlur = 0.0f, float minHitHeight = SSR_NO_MIN_HIT_HEIGHT)
{
    uint width = 0;
    uint height = 0;
    g_SsrDepth.GetDimensions(width, height);
    const float2 dimensions = float2(width, height);
    const float3 viewPosition = mul(float4(worldPosition, 1.0f), CausticView).xyz;
    const float3 viewNormal = normalize(mul(float4(worldNormal, 0.0f), CausticView).xyz);
    const float3 rayDirection = reflect(normalize(viewPosition), viewNormal);
    // 深度バッファの値からビュー空間の z を戻すための、射影行列の値。
    // 前方の符号（[2][3]）は、左手系（このゲームのカメラ。前方が +z）なら +1、右手系なら -1。
    // 深度 = ([2][2] z + [3][2]) / ([2][3] z) なので、z = [3][2] / ([2][3] 深度 - [2][2]) で戻せる（どちらの手系でも同じ式）。
    // 以前は右手系の式（z = -[3][2] / (深度 + [2][2])）だけを使っていたため、左手系のこのゲームでは深さがずれ、SSR が何にも当たっていなかった
    const float depthScale = CausticProjection[2][2];
    const float depthOffset = CausticProjection[3][2];
    const float forwardSign = CausticProjection[2][3];
    float distance = SSR_INITIAL_RAY_DISTANCE;
    float previousDistance = 0.0f;
    // 出発点そのものは反射面。最初の試料を基準にし、自分自身を交差と判定しない。
    float previousBehind = 1.0f;
    [loop]
    for (int step = 0; step < stepCount; ++step)
    {
        const float3 rayPosition = viewPosition + rayDirection * distance;
        const float4 clip = mul(float4(rayPosition, 1.0f), CausticProjection);
        // 鏡のようにカメラの方へ戻る光線は、すぐにカメラの後ろへ出るので、ここで終わる
        if (clip.w <= SSR_MIN_PROJECTION_W)
        {
            break;
        }
        const float2 ndc = clip.xy / clip.w;
        const float2 uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
        if (any(uv < 0.0f) || any(uv >= 1.0f))
        {
            break;
        }
        const float sceneDepth = g_SsrDepth.Load(int3(uv * dimensions, 0));
        const float sceneViewZ = depthOffset / (forwardSign * sceneDepth - depthScale);
        // 光線が面より奥に入り、その差が面の厚みとみなせる範囲なら、ぶつかったとしている（前方の符号を掛けて、カメラからの奥行きの差にしている）
        const float behind = (rayPosition.z - sceneViewZ) * forwardSign;
        // 面の手前から奥へ入った区間だけを詰める。粗い歩幅で厚みを飛び越えても判定できる。
        const bool crossed = previousBehind <= 0.0f && behind > 0.0f && sceneDepth < 1.0f;
        const float bracketStart = previousDistance;
        previousDistance = distance;
        previousBehind = behind;
        if (crossed)
        {
            // ぶつかった位置を、1つ前の歩幅との間で半分ずつ詰めている（二分探索）。
            // 大きな歩幅のまま色を読むと、映る物の位置がずれて階段状にぼやけるため
            float nearDistance = bracketStart;
            float farDistance = distance;
            const int2 pixel = int2(uv * dimensions);
            float2 hitUv = (float2(pixel) + 0.5f) / dimensions;
            float hitViewZ = sceneViewZ;
            [unroll]
            for (int refine = 0; refine < SSR_REFINE_STEPS; ++refine)
            {
                const float middle = (nearDistance + farDistance) * 0.5f;
                const float3 middlePosition = viewPosition + rayDirection * middle;
                const float4 middleClip = mul(float4(middlePosition, 1.0f), CausticProjection);
                const float2 middleNdc = middleClip.xy / max(middleClip.w, SSR_MIN_PROJECTION_W);
                const float2 middleUv = float2(middleNdc.x * 0.5f + 0.5f, -middleNdc.y * 0.5f + 0.5f);
                const int2 middlePixel = clamp(int2(middleUv * dimensions), int2(0, 0), int2(width, height) - 1);
                const float middleDepth = g_SsrDepth.Load(int3(middlePixel, 0));
                const float middleSceneZ = depthOffset / (forwardSign * middleDepth - depthScale);
                if ((middlePosition.z - middleSceneZ) * forwardSign > 0.0f)
                {
                    farDistance = middle;
                    hitUv = (float2(middlePixel) + 0.5f) / dimensions;
                    hitViewZ = middleSceneZ;
                }
                else
                {
                    nearDistance = middle;
                }
            }
            // 今の深度から当たった面のワールド位置を戻し、前のカメラでその点が見えていた画素を求める。
            const float2 hitNdc = float2(hitUv.x * 2.0f - 1.0f, 1.0f - hitUv.y * 2.0f);
            // 物体の縁で深度が急に変わっただけなら、詰めても面までの差が残る。
            const float thickness = SSR_MIN_HIT_THICKNESS + farDistance * SSR_HIT_THICKNESS_PER_DISTANCE;
            const float residual = abs((viewPosition.z + rayDirection.z * farDistance - hitViewZ) * forwardSign);
            const float thicknessConfidence = 1.0f - smoothstep(thickness * 0.5f, thickness, residual);
            if (thicknessConfidence <= SSR_MIN_THICKNESS_CONFIDENCE)
            {
                distance *= stepGrowth;
                continue;
            }
            // 投影の w は「前方の符号 × z」（カメラからの奥行き）なので、それを掛けて画面の位置からビュー空間の x・y を戻している
            const float hitViewW = forwardSign * hitViewZ;
            const float3 hitView = float3(
                hitNdc.x * hitViewW / CausticProjection[0][0],
                hitNdc.y * hitViewW / CausticProjection[1][1], hitViewZ);
            const float3 eye = -mul((float3x3)CausticView, CausticView[3].xyz);
            const float3 hitWorld = eye + mul((float3x3)CausticView, hitView);
            const float heightAboveMinimum = hitWorld.y - worldPosition.y - minHitHeight;
            if (heightAboveMinimum <= 0.0f)
            {
                // 水面近くの床を拾っても、光線の先にある壁や物を探し続ける。
                distance *= stepGrowth;
                continue;
            }
            const float4 previousClip = mul(float4(hitWorld, 1.0f), PreviousReflectionViewProjection);
            if (previousClip.w <= SSR_MIN_PROJECTION_W)
            {
                return 0.0f;
            }
            const float2 previousNdc = previousClip.xy / previousClip.w;
            const float2 sampleUv = previousNdc * float2(0.5f, -0.5f) + 0.5f;
            if (any(sampleUv < 0.0f) || any(sampleUv >= 1.0f))
            {
                return 0.0f;
            }
            // 前の画面で別の物に隠れていた点や、前の深度に物がない点から色を拾わない。
            const float previousDepth = g_PreviousSsrDepth.Load(int3(sampleUv * dimensions, 0));
            // カメラからの奥行き（投影の w）= 前方の符号 × [3][2] / (前方の符号 × 深度 - [2][2])。xyz = 前のフレームの [2][2]・[3][2]・[2][3]
            const float previousForwardSign = PreviousReflectionDepth.z;
            const float previousSceneDistance = previousForwardSign * PreviousReflectionDepth.y /
                (previousForwardSign * previousDepth - PreviousReflectionDepth.x);
            const float tolerance = SSR_MIN_HISTORY_TOLERANCE + previousClip.w * SSR_HISTORY_TOLERANCE_PER_DEPTH;
            const float depthConfidence = 1.0f - smoothstep(tolerance * SSR_HISTORY_FADE_START_RATIO, tolerance,
                abs(previousSceneDistance - previousClip.w));
            const float2 edge = min(min(hitUv, 1.0f - hitUv), min(sampleUv, 1.0f - sampleUv));
            const float fade = smoothstep(0.0f, SSR_EDGE_FADE_UV, min(edge.x, edge.y)) * depthConfidence * thicknessConfidence *
                smoothstep(0.0f, SSR_HIT_HEIGHT_FADE_WIDTH, heightAboveMinimum) *
                (1.0f - smoothstep(SSR_STEP_FADE_START_RATIO, 1.0f, float(step) / float(stepCount)));
            if (fade <= SSR_MIN_VISIBLE_WEIGHT)
            {
                return 0.0f;
            }
            // 映る物までの距離（反射する面から）に合わせて、ぼかしている（最大3段 = 1/8 の大きさ相当）。
            // 小さい縮小画像を1点だけ読んで引き伸ばすと、四角いブロックが見えてかくかくするので、
            // 1段細かい縮小画像を、ぼかしの大きさの分だけ4方向にずらして読み、平均している（なめらかなぼけになる）
            const float blurLevel = min(baseBlur + farDistance * blurPerDistance, SSR_HISTORY_MAX_BLUR_LOD);
            float3 reflected;
            [branch]
            if (blurLevel < SSR_SINGLE_SAMPLE_BLUR_LIMIT)
            {
                reflected = g_SsrScene.SampleLevel(g_SamplerState, sampleUv, 0.0f).rgb;
            }
            else
            {
                const float sampleLevel = max(blurLevel - 1.0f, 0.0f);
                const float2 spread = exp2(blurLevel) * 0.5f / float2(width, height);
                reflected = (
                    g_SsrScene.SampleLevel(g_SamplerState, saturate(sampleUv + float2(spread.x, spread.y * SSR_BLUR_TAP_SKEW)), sampleLevel).rgb +
                    g_SsrScene.SampleLevel(g_SamplerState, saturate(sampleUv + float2(-spread.x * SSR_BLUR_TAP_SKEW, spread.y)), sampleLevel).rgb +
                    g_SsrScene.SampleLevel(g_SamplerState, saturate(sampleUv + float2(-spread.x, -spread.y * SSR_BLUR_TAP_SKEW)), sampleLevel).rgb +
                    g_SsrScene.SampleLevel(g_SamplerState, saturate(sampleUv + float2(spread.x * SSR_BLUR_TAP_SKEW, -spread.y)), sampleLevel).rgb) *
                    SSR_BLUR_TAP_WEIGHT;
            }
            return float4(reflected, fade);
        }
        distance *= stepGrowth;
    }
    return 0.0f;
}

// 部屋の形の、壁・天井・床の色（照らす前の色）と、光線を追う一番遠い距離
// 部屋を囲う壁がある場所ではキューブマップを弱く混ぜ、室内の反射が外の色に寄りすぎないようにする。
static const float ROOM_ENVIRONMENT_WEIGHT_ENCLOSED = 0.28f;
static const float ROOM_ENVIRONMENT_WEIGHT_OPEN = 0.62f;

static const float3 RoomReflectionWallAlbedo = float3(0.20f, 0.20f, 0.18f);
static const float3 RoomReflectionCeilingAlbedo = float3(0.13f, 0.13f, 0.12f);
static const float3 RoomReflectionFloorAlbedo = float3(0.11f, 0.105f, 0.10f);
static const float RoomReflectionMaxDistance = 600.0f;

// 光線が当たる一番近い天井・床・壁・小物までの距離と、当たった面の法線と色（照らす前の色）を求めている
// （どこにも当たらなければ RoomReflectionMaxDistance）
float IntersectRoom(float3 origin, float3 direction, out float3 hitNormal, out float3 hitAlbedo)
{
    float nearest = RoomReflectionMaxDistance;
    hitNormal = float3(0.0f, -1.0f, 0.0f);
    hitAlbedo = RoomReflectionCeilingAlbedo;
    // 上へ向かう光線は天井、下へ向かう光線（鏡に映る床）は床の高さで止めている
    if (direction.y > ROOM_REFLECTION_MIN_VERTICAL_DIRECTION)
    {
        nearest = min(nearest, (OcclusionCeilingY - origin.y) / direction.y);
    }
    else if (direction.y < -ROOM_REFLECTION_MIN_VERTICAL_DIRECTION)
    {
        nearest = min(nearest, (OcclusionFloorY - origin.y) / direction.y);
        hitNormal = float3(0.0f, 1.0f, 0.0f);
        hitAlbedo = RoomReflectionFloorAlbedo;
    }
    // 壁は床から天井までの箱として、上から見た長方形と光線を交差させている（スラブ法）。0で割らないよう向きを丸めている
    const float2 safeDirection = float2(
        abs(direction.x) > ROOM_REFLECTION_MIN_DIRECTION ? direction.x : ROOM_REFLECTION_MIN_DIRECTION,
        abs(direction.z) > ROOM_REFLECTION_MIN_DIRECTION ? direction.z : ROOM_REFLECTION_MIN_DIRECTION);
    const float2 inverseDirection = 1.0f / safeDirection;
    const uint boxCount = min(OcclusionBoxCount, MaxOcclusionBoxes);
    [loop]
    for (uint i = 0; i < boxCount; ++i)
    {
        const float4 box = OcclusionBoxes[i];
        const float2 slabNear = (box.xy - box.zw - origin.xz) * inverseDirection;
        const float2 slabFar = (box.xy + box.zw - origin.xz) * inverseDirection;
        const float2 entry = min(slabNear, slabFar);
        const float2 leave = max(slabNear, slabFar);
        const float enter = max(entry.x, entry.y);
        const float exit = min(leave.x, leave.y);
        if (enter > ROOM_REFLECTION_MIN_INTERSECTION_DISTANCE && enter <= exit && enter < nearest)
        {
            nearest = enter;
            hitNormal = entry.x > entry.y
                ? float3(-sign(safeDirection.x), 0.0f, 0.0f)
                : float3(0.0f, 0.0f, -sign(safeDirection.y));
            hitAlbedo = RoomReflectionWallAlbedo;
        }
    }
    // 画面外の小物と扉も箱に当てる。扉だけは光線を扉板の座標に直し、開いた角度に合わせる。
    const float3 safeDirection3 = float3(
        safeDirection.x, abs(direction.y) > ROOM_REFLECTION_MIN_DIRECTION ? direction.y : ROOM_REFLECTION_MIN_DIRECTION, safeDirection.y);
    const float3 inverseDirection3 = 1.0f / safeDirection3;
    const uint propCount = min(uint(ReflectionParams.z + 0.5f), (uint)MAX_REFLECTION_PROPS);
    [loop]
    for (uint prop = 0; prop < propCount; ++prop)
    {
        const float4 center = ReflectionProps[prop * REFLECTION_PROP_STRIDE];
        const float4 halfSize = ReflectionProps[prop * REFLECTION_PROP_STRIDE + 1];
        const float2 widthAxis = float2(center.w, halfSize.w);
        const float2 depthAxis = float2(-widthAxis.y, widthAxis.x);
        const bool door = dot(widthAxis, widthAxis) > ROOM_REFLECTION_DOOR_AXIS_THRESHOLD;
        float3 localOrigin = origin - center.xyz;
        float3 inverseDirection = inverseDirection3;
        [branch]
        if (door)
        {
            localOrigin.xz = float2(dot(localOrigin.xz, widthAxis), dot(localOrigin.xz, depthAxis));
            const float2 localDirection = float2(dot(direction.xz, widthAxis), dot(direction.xz, depthAxis));
            inverseDirection.xz = 1.0f / float2(
                abs(localDirection.x) > ROOM_REFLECTION_MIN_DIRECTION ? localDirection.x : ROOM_REFLECTION_MIN_DIRECTION,
                abs(localDirection.y) > ROOM_REFLECTION_MIN_DIRECTION ? localDirection.y : ROOM_REFLECTION_MIN_DIRECTION);
        }
        const float3 slabNear = (-halfSize.xyz - localOrigin) * inverseDirection;
        const float3 slabFar = (halfSize.xyz - localOrigin) * inverseDirection;
        const float3 entry = min(slabNear, slabFar);
        const float3 leave = max(slabNear, slabFar);
        const float enter = max(max(entry.x, entry.y), entry.z);
        const float exit = min(min(leave.x, leave.y), leave.z);
        if (enter > ROOM_REFLECTION_MIN_INTERSECTION_DISTANCE && enter <= exit && enter < nearest)
        {
            nearest = enter;
            hitNormal = entry.x >= entry.y && entry.x >= entry.z ? float3(-sign(inverseDirection.x), 0.0f, 0.0f)
                : (entry.y >= entry.z ? float3(0.0f, -sign(inverseDirection.y), 0.0f) : float3(0.0f, 0.0f, -sign(inverseDirection.z)));
            hitAlbedo = ReflectionProps[prop * REFLECTION_PROP_STRIDE + 2].rgb;
            [branch]
            if (door)
            {
                // 扉の表裏にある上下2枚の鏡板を、モデルと同じ位置で少し暗くする。
                const float2 doorUV = abs(localOrigin.xy + enter * rcp(inverseDirection.xy)) / halfSize.xy;
                const float panel = (1.0f - smoothstep(DOOR_REFLECTION_PANEL_X_START, DOOR_REFLECTION_PANEL_X_END, doorUV.x)) *
                    (1.0f - smoothstep(DOOR_REFLECTION_PANEL_Y_START, DOOR_REFLECTION_PANEL_Y_END,
                        abs(doorUV.y - DOOR_REFLECTION_PANEL_CENTER_Y))) * abs(hitNormal.z);
                hitAlbedo *= lerp(1.0f, DOOR_REFLECTION_PANEL_SHADE, panel);
                hitNormal.xz = hitNormal.x * widthAxis + hitNormal.z * depthAxis;
            }
            else
            {
                hitAlbedo *= PROP_REFLECTION_SHADE;
            }
        }
    }
    return nearest;
}

// 画面の外の壁・天井に映る模様（色に掛ける倍率）。部屋の形の壁と天井は色が一色なので、そのまま映すと濁った板に見える。
// 環境マップが撮れていないとき（TraceRoomReflection で useEnvironment を使わないとき）に、この模様で見た目を補っている。
// 壁は打ちっぱなしのコンクリートのパネル（横60・縦17。litTexturePS の GetWallAgeing と同じ大きさ）の継ぎ目と色むら、
// 天井は天井板の格子（30四方）と染みを、当たった位置から計算している。小物の箱には掛けない
// 反射の中に描く壁と床では、パネルの大きさを変えて継ぎ目を残す。
#define ROOM_REFLECTION_WALL_PANEL_SIZE float2(60.0f, 17.0f)
#define ROOM_REFLECTION_HORIZONTAL_PANEL_SIZE float2(30.0f, 30.0f)
#define ROOM_REFLECTION_PANEL_SEAM_CORE 0.2f
#define ROOM_REFLECTION_PANEL_SEAM_EDGE 0.9f

// 材質の色が一様にならないよう、床版の差、広い染み、細かいざらつきを重ねる。
#define ROOM_REFLECTION_PANEL_TINT_SEED 71.0f
#define ROOM_REFLECTION_PANEL_BLOTCH_SCALE 0.045f
#define ROOM_REFLECTION_PANEL_BLOTCH_SEED 17.0f
#define ROOM_REFLECTION_PANEL_FINE_SCALE 0.40f
#define ROOM_REFLECTION_PANEL_FINE_SEED 5.0f
#define ROOM_REFLECTION_PANEL_BASE_TINT 0.80f
#define ROOM_REFLECTION_PANEL_TINT_RANGE 0.18f
#define ROOM_REFLECTION_PANEL_NOISE_BASE 0.72f
#define ROOM_REFLECTION_PANEL_BLOTCH_WEIGHT 0.42f
#define ROOM_REFLECTION_PANEL_FINE_WEIGHT 0.10f
#define ROOM_REFLECTION_PANEL_SEAM_DARKENING 0.50f

float GetRoomReflectionDetail(float3 hitPosition, float3 hitNormal)
{
    const bool wall = abs(hitNormal.y) < ROOM_REFLECTION_WALL_NORMAL_LIMIT;
    // 壁は面に沿った横の座標と高さ、天井と床は x・z を模様の座標にしている
    const float2 uv = wall
        ? float2(dot(hitPosition.xz, float2(-hitNormal.z, hitNormal.x)), hitPosition.y - OcclusionFloorY)
        : hitPosition.xz;
    const float2 panelSize = wall ? ROOM_REFLECTION_WALL_PANEL_SIZE : ROOM_REFLECTION_HORIZONTAL_PANEL_SIZE;
    const float2 panelUV = uv / panelSize;
    const float2 edgeDistance = (0.5f - abs(frac(panelUV) - 0.5f)) * panelSize;
    const float seam = 1.0f - smoothstep(ROOM_REFLECTION_PANEL_SEAM_CORE, ROOM_REFLECTION_PANEL_SEAM_EDGE, min(edgeDistance.x, edgeDistance.y));
    // パネルごとの明るさの違い・大きな染み・細かいざらつき
    const float panelShade = FastHash21(floor(panelUV) + ROOM_REFLECTION_PANEL_TINT_SEED);
    const float blotch = FastValueNoise(uv * ROOM_REFLECTION_PANEL_BLOTCH_SCALE + ROOM_REFLECTION_PANEL_BLOTCH_SEED);
    const float fine = FastValueNoise(uv * ROOM_REFLECTION_PANEL_FINE_SCALE + ROOM_REFLECTION_PANEL_FINE_SEED);
    return (ROOM_REFLECTION_PANEL_BASE_TINT + panelShade * ROOM_REFLECTION_PANEL_TINT_RANGE) * (ROOM_REFLECTION_PANEL_NOISE_BASE + blotch * ROOM_REFLECTION_PANEL_BLOTCH_WEIGHT + fine * ROOM_REFLECTION_PANEL_FINE_WEIGHT) * (1.0f - seam * ROOM_REFLECTION_PANEL_SEAM_DARKENING);
}

// 部屋の形に光線を飛ばし、当たった天井・床・壁の色と、途中で光線がそばを通った照明の光を返している。
// roughness は面の荒さ（0〜1）で、荒いほど照明の映り込みをにじませている。
// hitDistance は当たった面までの距離（どこにも当たらなければ RoomReflectionMaxDistance）で、鏡に映る人影の前後を決めるのに使っている。
// useEnvironment なら、当たった面の色を、環境マップ（本物の景色）から読んでいる（水たまり。鏡は自分の後ろを映すので使わない）
// 反射の中で近い壁や扉の下側を残し、遠い壁や上部を天井の色へなじませる。
#define ROOM_REFLECTION_NEAR_WALL_START 30.0f
#define ROOM_REFLECTION_NEAR_WALL_END 85.0f
#define ROOM_REFLECTION_WALL_HEIGHT_FADE_START 0.62f
#define ROOM_REFLECTION_WALL_HEIGHT_FADE_END 0.95f

// 映る光源の芯とにじみの広さを、粗い面ほど広げる。
#define ROOM_REFLECTION_LIGHT_RADIUS 6.0f
#define ROOM_REFLECTION_LIGHT_ROUGHNESS_SPREAD 1.5f
#define ROOM_REFLECTION_LIGHT_REACH_MULTIPLIER 12.0f

// 光線の当たり位置の近くにある照明だけを映り込みとして調べる。
#define ROOM_REFLECTION_LIGHT_HIT_MARGIN 4.0f

// 光源の強い芯と弱いにじみを重ねて反射像に加える。
#define ROOM_REFLECTION_GLOW_CORE_WEIGHT 0.85f
#define ROOM_REFLECTION_GLOW_HALO_RADIUS 3.0f
#define ROOM_REFLECTION_GLOW_HALO_WEIGHT 0.03f

// 反射の面は斜めに照らされた場合も、薄い光が残るようにする。
#define ROOM_REFLECTION_LIGHT_GRAZING_RESPONSE 0.20f
#define ROOM_REFLECTION_LIGHT_LAMBERT_WEIGHT 0.80f

// 水面で跳ねた懐中電灯の光は、壁へ届く前に弱める。
#define ROOM_REFLECTION_FLASHLIGHT_BOUNCE_WEIGHT 0.42f

// 遠い照明が霧の中で強く光りすぎないようにする。
#define ROOM_REFLECTION_GLOW_FOG_FADE 0.6f

float3 TraceRoomReflection(float3 origin, float3 direction, float roughness,
    out float hitDistance, out float nearVertical, bool useEnvironment = false)
{
    float3 hitNormal = float3(0.0f, -1.0f, 0.0f);
    float3 albedo = RoomReflectionCeilingAlbedo;
    hitDistance = OcclusionBoxCount > 0 || ReflectionParams.z > ROOM_REFLECTION_FLAG_THRESHOLD
        ? IntersectRoom(origin, direction, hitNormal, albedo)
        : RoomReflectionMaxDistance;
    const bool hit = hitDistance < RoomReflectionMaxDistance;
    const float3 hitPosition = origin + direction * hitDistance;
    // 近くの壁や扉の下側は水面でも輪郭が読めるようにする。壁の上部と遠い壁は天井の反射へなじませる。
    const float roomHeight = max(OcclusionCeilingY - OcclusionFloorY, 1.0f);
    const float relativeHeight = (hitPosition.y - OcclusionFloorY) / roomHeight;
    nearVertical = (1.0f - abs(hitNormal.y)) * (1.0f - smoothstep(ROOM_REFLECTION_NEAR_WALL_START, ROOM_REFLECTION_NEAR_WALL_END, hitDistance)) *
        (1.0f - smoothstep(ROOM_REFLECTION_WALL_HEIGHT_FADE_START, ROOM_REFLECTION_WALL_HEIGHT_FADE_END, relativeHeight));
    // 建物の壁・天井・床に当たったときだけ、コンクリートや天井板の模様を付けている（小物の箱は小物の色のまま）
    const bool roomSurface = all(albedo == RoomReflectionWallAlbedo) ||
        all(albedo == RoomReflectionCeilingAlbedo) || all(albedo == RoomReflectionFloorAlbedo);
    if (hit && roomSurface)
    {
        albedo *= GetRoomReflectionDetail(hitPosition, hitNormal);
    }

    if (hit && DebugViewMode == DEBUG_VIEW_FULLBRIGHT_REFLECTION)
    {
        // 画面外の壁や扉も、光源の位置にかかわらず反射像で確認できるようにする。
        const float facing = abs(dot(hitNormal, direction));
        return GetFullbrightSurfaceColor(albedo, facing);
    }

    float3 lighting = GetHemisphereAmbient(hitNormal);
    float3 glow = 0.0f;
    const float glowRadius = ROOM_REFLECTION_LIGHT_RADIUS * (1.0f + roughness * ROOM_REFLECTION_LIGHT_ROUGHNESS_SPREAD);
    // 器具の光を調べる、光線からの距離の上限（にじみの裾がほぼ0になる所）の二乗
    const float glowReach2 = (glowRadius * ROOM_REFLECTION_LIGHT_REACH_MULTIPLIER) * (glowRadius * ROOM_REFLECTION_LIGHT_REACH_MULTIPLIER);
    [loop]
    for (uint i = 0; i < PointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight = g_PointLights[i];
        // 光線からも当たった面からも遠い光源は、二乗の距離だけで先に飛ばしている（照明の多い1面で、重い計算を減らすため）
        const float3 toLight = pointLight.PositionRange.xyz - origin;
        const float along = dot(toLight, direction);
        const float miss2 = dot(toLight, toLight) - along * along;
        const bool nearRay = along > 0.0f && along < hitDistance + ROOM_REFLECTION_LIGHT_HIT_MARGIN && miss2 < glowReach2;
        const float3 offset = pointLight.PositionRange.xyz - hitPosition;
        const float lightDistance2 = dot(offset, offset);
        const float range = max(pointLight.PositionRange.w, ROOM_REFLECTION_MIN_DIVISOR);
        const bool lightsHit = hit && lightDistance2 < range * range;
        [branch]
        if (nearRay || lightsHit)
        {
            const float3 lightColor = pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a;
            // 照明の器具：光線が光源に一番近づく所の離れ具合で光らせている（当たった面より奥の光源は隠れている）
            if (nearRay)
            {
                const float miss = sqrt(max(miss2, 0.0f));
                glow += lightColor * (exp(-miss2 / (glowRadius * glowRadius)) * ROOM_REFLECTION_GLOW_CORE_WEIGHT +
                    exp(-miss / (glowRadius * ROOM_REFLECTION_GLOW_HALO_RADIUS)) * ROOM_REFLECTION_GLOW_HALO_WEIGHT);
            }
            // 当たった面を、この光源で照らしている（床の点光源と同じ減衰）
            if (lightsHit)
            {
                const float lightDistance = sqrt(lightDistance2);
                float attenuation = saturate(1.0f - lightDistance / range);
                attenuation *= attenuation;
                const float lambert = saturate(dot(hitNormal, offset / max(lightDistance, ROOM_REFLECTION_MIN_DIVISOR)));
                lighting += lightColor * attenuation * (ROOM_REFLECTION_LIGHT_GRAZING_RESPONSE + lambert * ROOM_REFLECTION_LIGHT_LAMBERT_WEIGHT);
            }
        }
    }

    float3 color = 0.0f;
    if (hit)
    {
        // 懐中電灯で照らされた壁や天井も映している（懐中電灯は目の位置にあるので、目から当たった点への向きで円錐を調べている）
        if (Light.Enable && Light.FlashlightEnabled)
        {
            const float3 viewPosition = mul(float4(hitPosition, 1.0f), CausticView).xyz;
            const float viewDistance = length(viewPosition);
            const float3 pixelDirection = viewPosition / max(viewDistance, ROOM_REFLECTION_MIN_DIVISOR);
            const float normalizedDistance = saturate(viewDistance / max(Light.Range, ROOM_REFLECTION_MIN_DIVISOR));
            const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
            const float3 toEye = normalize(GetMistEyePosition() - hitPosition);
            lighting += Light.Diffuse.rgb * GetFlashlightTint(pixelDirection) * Light.Intensity *
                GetFlashlightBeamProfile(pixelDirection) * rangeFade * rangeFade *
                (ROOM_REFLECTION_FLASHLIGHT_BASE + saturate(dot(hitNormal, toEye)) * ROOM_REFLECTION_FLASHLIGHT_FACING);
            if (useEnvironment && nearVertical > 0.0f)
            {
                // 下を照らすときは、水面で跳ねた光が壁や扉を照らす。光が進んだ全距離で弱める。
                const float3 waterInView = mul(float4(origin, 1.0f), CausticView).xyz;
                const float waterDistance = max(length(waterInView), ROOM_REFLECTION_MIN_DIVISOR);
                const float3 waterDirection = waterInView / waterDistance;
                const float bounceDistance = saturate((waterDistance + hitDistance) / max(Light.Range, ROOM_REFLECTION_MIN_DIVISOR));
                const float bounceFade = 1.0f - bounceDistance * bounceDistance;
                lighting += Light.Diffuse.rgb * GetFlashlightTint(waterDirection) * Light.Intensity *
                    GetFlashlightBeamProfile(waterDirection) * bounceFade * bounceFade *
                    saturate(dot(hitNormal, -direction)) * nearVertical * ROOM_REFLECTION_FLASHLIGHT_BOUNCE_WEIGHT;
            }
        }
        color = albedo * lighting;
    }
    // 距離の霧を入れた、部屋の形と今の照明から求めた反射。
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    const float fog = hit ? GetHorrorDistanceFog(hitDistance, LoopDepth) : 1.0f;
    const float3 roomColor = ApplyFilmicHorrorGrade(
        lerp(color, fogColor, fog) + glow * (1.0f - fog * ROOM_REFLECTION_GLOW_FOG_FADE), LoopDepth);

    // 画面の外の物を、環境マップ（カメラの位置から撮った本物の景色）で補っている。
    // 反射の向きのまま読むと、撮った位置（目）と水面の位置が違うので、近くの物ほど映る位置がずれる。
    // そこで、光線が部屋の形に当たった位置を、目から見た向きで読んでいる（部屋の形を使った視差の補正）。
    // 部屋の形がない所（森）や、どこにも当たらない光線は、反射の向きのまま読む。
    // 環境マップには照明の器具も撮れているので、器具の光（glow）は少しだけ足している
    [branch]
    if (useEnvironment && ReflectionParams.w > ROOM_REFLECTION_FLAG_THRESHOLD)
    {
        // 環境マップを撮った位置（今の目の位置ではない）から、当たった位置への向きで読んでいる。
        // 環境マップは数フレームかけて1面ずつ撮り直すので、今の目の位置として読むと、歩くたびに映る位置がずれて跳ねるため
        const float3 probeCenter = ReflectionProbeCenter.xyz;
        const float3 probeDirection = hit ? normalize(hitPosition - probeCenter) : direction;
        // 離れた所ほど、粗い水面ほど、少しぼやけた段を読んでいる（遠くの映り込みのぼけ）。
        // 環境マップは1面256画素なので、32画素の段（3段目）より小さい段は読まない（四角いブロックが見えるため）
        const float environmentBlur = saturate(roughness * RAY_ENVIRONMENT_BLUR_ROUGHNESS_WEIGHT +
            (hit ? hitDistance / RAY_ENVIRONMENT_BLUR_DISTANCE : RAY_ENVIRONMENT_MISS_BLUR));
        const float mipLevel = environmentBlur * RAY_ENVIRONMENT_MAX_BLUR_MIP;
        // キューブマップの面は左手系、このゲームの座標は右手系なので、z を反転して読んでいる（ReflectionProbe.cpp）
        const float3 environment = g_RayEnvironmentProbe.SampleLevel(g_SamplerState,
            float3(probeDirection.x, probeDirection.y, -probeDirection.z), mipLevel).rgb;
        // 面が順番に更新されても映り込みの輪郭が跳ねないよう、室内では部屋の形と今の照明を主体にする。
        const float environmentWeight = OcclusionBoxCount > 0 ? ROOM_ENVIRONMENT_WEIGHT_ENCLOSED : ROOM_ENVIRONMENT_WEIGHT_OPEN;
        return lerp(roomColor, environment + glow * ROOM_ENVIRONMENT_GLOW_WEIGHT, environmentWeight);
    }

    return roomColor;
}

#endif
