// 光の筋だけを半解像度で積分する。wには奥行きを持ち、拡大時の光漏れを防ぐ。
#include "fastNoise.hlsli"
struct ENVIRONMENT_POINT_LIGHT { float4 PositionRange; float4 ColorIntensity; };
cbuffer ShaftParameters : register(b0) {
 matrix InverseViewProjection; float4 EyeTime; float4 Viewport; uint LightCount; float3 Padding;
 // 見えない影の人影（xyz = 足もと、w = 見え方。figureShadow.hlsli と同じ名前にしている）
 float4 UnseenFigure;
};
Texture2D<float> SceneDepth : register(t0);
StructuredBuffer<ENVIRONMENT_POINT_LIGHT> Lights : register(t1);
RWTexture2D<float4> Output : register(u0);
float3 GetMistEyePosition() { return EyeTime.xyz; }
// 見えない影の人影が光を遮る割合（床や壁の影と同じ計算。霧の中の光の筋にも、人の形の暗い切れ目ができる）
// プレイヤーの体の影は、霧の中の光の筋には付けない（体の値はこの定数バッファにないため）
#define FIGURE_SHADOW_WITHOUT_PLAYER_BODY
#include "figureShadow.hlsli"
#define CausticTime EyeTime.w
ENVIRONMENT_POINT_LIGHT GetPixelLight(uint offset, uint i) { return Lights[i]; }
// 半分の解像度で計算しているので、画素ごとに調べる位置をずらす（ノイズ）代わりに、調べる点を増やしてなめらかにしている。
// 2画素おきの座標でずらすと、ずらし方の模様が斜めの縞になり、拡大したときに斜めの線として見えたため。
#define LIGHT_SHAFT_SAMPLES 10
#define LIGHT_SHAFT_DENSITY 0.0018f
// 光の筋を出す光源の高さの下限。天井の近くの照明（天井の器具・吊るした電球）だけにし、壁の表示や肖像画の光のように
// 面に付いた光源は除いている（光源が面の上にあると、その面に向かう視線の終わりで光の筋が強く出すぎるため）
#define LIGHT_SHAFT_MIN_HEIGHT (-58.0f)

float3 GetLightShafts(float3 worldPosition, uint lightListOffset, uint lightCount, float2 pixelPosition)
{
    const float3 eye = GetMistEyePosition();
    const float3 toPixel = worldPosition - eye;
    const float rayLength = length(toPixel);
    const float3 rayDirection = toPixel / max(rayLength, 0.001f);
    // 調べる点は区間を等分した各部分の真ん中に置いている（画素ごとにずらさない）
    const float jitter = 0.5f;
    const float2 flow = float2(CausticTime * 0.60f, CausticTime * 0.25f);

    float3 scattered = 0.0f;
    [loop]
    for (uint i = 0; i < lightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight = GetPixelLight(lightListOffset, i);
        const float3 center = pointLight.PositionRange.xyz;
        const float range = max(pointLight.PositionRange.w, 0.001f);
        [branch]
        if (center.y < LIGHT_SHAFT_MIN_HEIGHT)
        {
            continue;
        }
        // 視線が光の届く球を通る区間（球と直線の交わり）を、カメラから画素までの間に切り詰めている
        const float closest = dot(center - eye, rayDirection);
        const float3 offsetFromRay = center - (eye + rayDirection * closest);
        const float missSquared = dot(offsetFromRay, offsetFromRay);
        [branch]
        if (missSquared >= range * range)
        {
            continue;
        }
        const float halfChord = sqrt(range * range - missSquared);
        float segmentStart = max(closest - halfChord, 0.0f);
        float segmentEnd = min(closest + halfChord, rayLength);
        // 光の円錐は照明より下にしかないので、視線が照明の高さより上を通る部分は調べない
        if (abs(rayDirection.y) > 0.0001f)
        {
            const float crossing = (center.y - eye.y) / rayDirection.y;
            if (eye.y < center.y)
            {
                segmentEnd = rayDirection.y > 0.0f ? min(segmentEnd, crossing) : segmentEnd;
            }
            else
            {
                segmentStart = rayDirection.y < 0.0f ? max(segmentStart, crossing) : segmentEnd;
            }
        }
        [branch]
        if (segmentEnd <= segmentStart)
        {
            continue;
        }

        const float stepLength = (segmentEnd - segmentStart) / LIGHT_SHAFT_SAMPLES;
        // 見えない影の人影の影は、人影から光源と反対の向きに床まで伸びる。視線のこの区間が、その影の芯（人影の胸から床までの線分）の
        // そばを通るときだけ、霧の中の点ごとに人影の影を調べている（ほとんどの画素と光源は、この1回の判定で終わる）
        bool figureInRange = false;
        [branch]
        if (UnseenFigure.w > 0.001f)
        {
            const float3 figureCenter = UnseenFigure.xyz + float3(0.0f, 19.0f, 0.0f);
            const float3 away = figureCenter - center;
            const float3 shadowEnd = away.y < -0.001f
                ? figureCenter + away * ((UnseenFigure.y - figureCenter.y) / away.y)
                : figureCenter + normalize(away) * range;
            float unused;
            figureInRange = SegmentSegmentDistance(eye + rayDirection * segmentStart, eye + rayDirection * segmentEnd,
                figureCenter, shadowEnd, unused) < 28.0f;
        }
        // 霧のむら：ゆっくり流れるノイズで、光の筋の中に濃い所と薄い所を作っている（区間の真ん中で1回だけ求めている）
        const float3 middlePoint = eye + rayDirection * ((segmentStart + segmentEnd) * 0.5f);
        const float haze = 0.45f + FastValueNoise(middlePoint.xz * 0.05f + flow) * 0.90f;
        float lightScatter = 0.0f;
        // 見えない人影の影（霧の中の点が人影に遮られて残る光の割合）。1点おきに求め、間の点は前の値を使っている
        float figureShadow = 1.0f;
        [unroll]
        for (int sampleIndex = 0; sampleIndex < LIGHT_SHAFT_SAMPLES; ++sampleIndex)
        {
            const float3 samplePoint = eye + rayDirection *
                (segmentStart + (float(sampleIndex) + jitter) * stepLength);
            const float3 toLight = center - samplePoint;
            const float lightDistance = length(toLight);
            float attenuation = saturate(1.0f - lightDistance / range);
            attenuation *= attenuation;
            // 天井の器具から下へ広がる円錐の中だけを光らせている（真下ほど濃く、約60度より横では消える）
            const float downward = saturate(toLight.y / max(lightDistance, 0.001f));
            const float cone = smoothstep(0.55f, 0.92f, downward);
            // 光源のすぐそばは、照明そのものの明るさに任せて薄くしている
            const float awayFromFixture = smoothstep(2.0f, 9.0f, lightDistance);
            // 霧はぼんやり見えるだけなので、床や壁より軽い、頭まで1本のカプセルで遮っている。
            // （HLSL の ?: は両側を計算してしまうため、if で範囲の外の光源では計算しないようにしている）
            [branch]
            if (figureInRange && (sampleIndex & 1) == 0)
            {
                figureShadow = 1.0f - GetCapsuleOcclusion(samplePoint, center, UnseenFigure.xyz + float3(0.0f, 4.0f, 0.0f),
                    UnseenFigure.xyz + float3(0.0f, 35.0f, 0.0f), 5.0f) * 0.97f * saturate(UnseenFigure.w);
            }
            lightScatter += attenuation * cone * awayFromFixture * figureShadow;
        }
        scattered += pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a * lightScatter * haze * stepLength;
    }
    // 光源の真下を長く見通したときに白く飛ばないよう、明るいほど伸びを抑えている（上限はおよそ0.35）
    const float3 shaft = scattered * LIGHT_SHAFT_DENSITY;
    return shaft / (1.0f + shaft * 2.8f);
}


[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
 uint2 size = uint2(ceil(Viewport.xy * 0.5f));
 if(any(id.xy >= size)) return;
 uint2 pixel = min(id.xy * 2, uint2(Viewport.xy)-1);
 float depth = SceneDepth.Load(int3(pixel + uint2(Viewport.zw),0));
 float2 uv=(float2(pixel)+0.5f)/Viewport.xy;
 float4 world=mul(float4(uv.x*2-1,1-uv.y*2,depth,1),InverseViewProjection);
 world.xyz/=world.w;
 Output[id.xy]=float4(GetLightShafts(world.xyz,0,LightCount,float2(pixel)),length(world.xyz-EyeTime.xyz));
}
