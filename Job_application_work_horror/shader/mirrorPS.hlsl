// ============================================================================
// シェーダーの役割: 2面の壁の鏡を描いている。景色をもう一度描かずに、鏡で反射した光線を、このシェーダーの中で飛ばしている（レイ反射）。
// 主な技術: 床の水たまりと共通のレイ反射（rayReflection.hlsli：SSRと、部屋の形・小物の箱との交差）、
//           焼いた距離場のスフィアトレーシング（鏡に映る自分の体と、鏡にだけ映る黒い人影。figureVolume.hlsli）
// ・鏡に映る自分の体（一人称なので、本描画には体がない）は、懐中電灯を前に持った立ち姿の距離場（色つき）に光線を通している。
//   手足の振りは、関節のまわりに回した空間で距離場を読んで付けている。当たった所の法線（距離場の勾配）で、
//   天井の照明と、鏡で跳ね返った懐中電灯の光を当てている。位置・向き・手足の振りは MirrorSelf が渡している（b12）。
// ・鏡の異変の人影は、物として置かず、人影のモデルと同じ形の距離場に光線を通し、形に一番近づいた距離から、
//   輪郭のぼけた黒い影として重ねている（MirrorAnomaly）。
// 古い鏡らしく、縁から銀が剥げた黒いしみ、全体のくもり、古いガラスのわずかなうねりを足し、
// 懐中電灯を正面から当てたときの照り返しを返している。
// 定数バッファのスロットと入出力の形は、CPU側の定義と一致させている。
// ============================================================================

#include "common.hlsl"

SamplerState g_SamplerState : register(s0);

#include "fastNoise.hlsli"
#include "flashlightLighting.hlsli"
#include "waterCaustics.hlsli"
#include "roomOcclusion.hlsli"
#include "horrorGrade.hlsli"
#include "horrorMist.hlsli"
#include "rayReflection.hlsli"
#include "figureVolume.hlsli"

// 頂点シェーダー（litTextureVS）から受け取る値
struct MIRROR_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float depth : TEXCOORD1;
    float3 viewPos : TEXCOORD2;
    float3 viewNormal : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float3 worldNormal : TEXCOORD5;
    float4 shadowPos : TEXCOORD6;
};

// 鏡に映る自分の体と光線を交差させ、当たったら距離（当たらなければ負）と、照らした色を返している。
// 体の形は、懐中電灯を前に持った立ち姿の距離場（足もとが0、前が+z、右が+x。build_figure_volumes.py）
float TraceMirrorSelf(float3 origin, float3 direction, float3 mirrorViewDirection, out float3 shaded)
{
    shaded = 0.0f;
    [branch]
    if (MirrorSelf.w <= 0.5f)
    {
        return -1.0f;
    }
    // 光線を、体の向き（右・上・前）の空間へ移している
    const float3 right = float3(MirrorSelfBasis.x, 0.0f, MirrorSelfBasis.y);
    const float3 forward = float3(MirrorSelfBasis.z, 0.0f, MirrorSelfBasis.w);
    const float3 fromFoot = origin - MirrorSelf.xyz;
    const float3 localOrigin = float3(dot(fromFoot, right), fromFoot.y, dot(fromFoot, forward));
    const float3 localDirection = float3(dot(direction, right), direction.y, dot(direction, forward));
    // 体を囲む球（中心は腰の高さ、半径26）のそばを通らない光線は、点と光線の距離だけで先に飛ばしている
    const float3 toCenter = float3(0.0f, 20.0f, 0.0f) - localOrigin;
    const float alongCenter = max(dot(toCenter, localDirection), 0.0f);
    const float3 missCenter = toCenter - localDirection * alongCenter;
    [branch]
    if (dot(missCenter, missCenter) > 26.0f * 26.0f)
    {
        return -1.0f;
    }

    // 歩くと、左脚と右脚は逆向きに、左腕は左脚と逆向きに、股関節と肩を中心に前後へ振れる
    float3 normal;
    float3 albedo;
    const float nearest = TraceSelfVolume(localOrigin, localDirection, MirrorSelfMotion.x, normal, albedo);
    [branch]
    if (nearest < 0.0f)
    {
        return -1.0f;
    }

    // 当たった所を、上下で色を変えた環境光と、近くの天井の照明で照らしている
    const float3 worldNormal = normalize(right * normal.x + float3(0.0f, normal.y, 0.0f) + forward * normal.z);
    const float3 worldPosition = origin + direction * nearest;
    float3 lighting = GetHemisphereAmbient(worldNormal);
    [loop]
    for (uint i = 0; i < PointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight = g_PointLights[i];
        const float3 offset = pointLight.PositionRange.xyz - worldPosition;
        const float lightDistance2 = dot(offset, offset);
        const float range = max(pointLight.PositionRange.w, 0.001f);
        [branch]
        if (lightDistance2 < range * range)
        {
            const float lightDistance = sqrt(lightDistance2);
            float attenuation = saturate(1.0f - lightDistance / range);
            attenuation *= attenuation;
            const float lambert = saturate(dot(worldNormal, offset / max(lightDistance, 0.001f)));
            lighting += pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a * attenuation * (0.15f + lambert * 0.85f);
        }
    }
    // 懐中電灯で鏡を照らすと、跳ね返った光が自分の体の前を照らす（鏡のこの点が光の円錐の中にあるかで強さを決めている）
    if (Light.Enable && Light.FlashlightEnabled)
    {
        const float travel = length(worldPosition - GetMistEyePosition()) + nearest;
        const float normalizedDistance = saturate(travel / max(Light.Range, 0.001f));
        const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
        lighting += Light.Diffuse.rgb * GetFlashlightTint(mirrorViewDirection) * Light.Intensity *
            GetFlashlightBeamProfile(mirrorViewDirection) * rangeFade * rangeFade * 0.28f *
            saturate(dot(worldNormal, -direction));
    }
    float3 color = albedo * lighting;
    // 光線が進んだ距離の分だけ、霧の闇に沈めている（部屋の形に当たった色と同じ処理）
    color = lerp(color, GetHorrorFogColor(Light.Ambient.rgb, LoopDepth), GetHorrorDistanceFog(nearest, LoopDepth));
    shaded = ApplyFilmicHorrorGrade(color, LoopDepth);
    return nearest;
}

// 鏡の異変の人影（MirrorFigure。xyz = 足もと、w = 見え方）が光線をどれだけ覆うか（0〜1）を返している。depth は人影までの光線上の距離。
// 人影のモデルと同じ形の距離場を、頭が MirrorAnomaly の頭（足もとから39）の高さになる大きさにして、鏡の面の方を向かせている
float TraceMirrorFigure(float3 origin, float3 direction, out float depth)
{
    depth = 1e6f;
    [branch]
    if (MirrorFigure.w <= 0.001f)
    {
        return 0.0f;
    }
    const float3 foot = MirrorFigure.xyz;
    const float3 toCenter = foot + float3(0.0f, 20.0f, 0.0f) - origin;
    const float alongCenter = max(dot(toCenter, direction), 0.0f);
    const float3 missCenter = toCenter - direction * alongCenter;
    [branch]
    if (dot(missCenter, missCenter) > 30.0f * 30.0f)
    {
        return 0.0f;
    }
    // 人影の空間（前 = 鏡の面の方、右、上）へ光線を移し、大きさで割っている
    const float scale = 39.0f / FIGURE_VOLUME_HEAD_HEIGHT;
    const float3 forward = normalize(float3(origin.x - foot.x, 0.0f, origin.z - foot.z) + float3(1e-4f, 0.0f, 0.0f));
    const float3 right = float3(forward.z, 0.0f, -forward.x);
    const float3 fromFoot = (origin - foot) / scale;
    const float3 localOrigin = float3(dot(fromFoot, right), fromFoot.y, dot(fromFoot, forward));
    const float3 localDirection = float3(dot(direction, right), direction.y, dot(direction, forward));
    float localDepth;
    const float coverage = TraceFigureVolume(localOrigin, localDirection, 1.6f, localDepth);
    depth = localDepth * scale;
    return coverage * saturate(MirrorFigure.w);
}

float4 main(MIRROR_PS_IN input) : SV_Target
{
    const float2 face = saturate(input.tex);
    // 古いガラスは平らではないため、面の向きをほんの少しうねらせ、映りを揺らしている
    const float3 surfaceNormal = normalize(input.worldNormal);
    const float3 tangent = normalize(cross(float3(0.0f, 1.0f, 0.0f), surfaceNormal) + float3(1e-4f, 0.0f, 0.0f));
    const float3 bitangent = cross(surfaceNormal, tangent);
    const float2 wobble = float2(
        FastValueNoise(face * float2(3.0f, 4.0f) + 5.1f),
        FastValueNoise(face * float2(3.0f, 4.0f) - 2.7f)) - 0.5f;
    const float3 mirrorNormal = normalize(surfaceNormal + (tangent * wobble.x + bitangent * wobble.y) * 0.006f);

    // 目から鏡の点への光線を鏡で反射させ、部屋の形・小物の箱（画面の外も映る）と、画面に映っている物（SSR）をたどっている
    const float3 eye = GetMistEyePosition();
    const float3 direction = reflect(normalize(input.worldPos - eye), mirrorNormal);
    const float3 origin = input.worldPos + surfaceNormal * 0.1f;
    float roomDistance;
    float3 color = TraceRoomReflection(origin, direction, 0.12f, roomDistance);
    [branch]
    if (ReflectionParams.y > 0.5f)
    {
        const float4 screen = TraceScreenSpaceReflection(input.worldPos, mirrorNormal, 20, 1.22f);
        color = lerp(color, screen.rgb, screen.w);
    }

    // 鏡の異変の黒い人影と、自分の体を、遠い方から重ねている（どちらも、部屋の形に当たった所より手前にあるときだけ）
    float figureDepth;
    const float figure = TraceMirrorFigure(origin, direction, figureDepth);
    float3 selfColor;
    const float selfDistance = TraceMirrorSelf(origin, direction, normalize(input.viewPos), selfColor);
    const bool selfVisible = selfDistance > 0.0f && selfDistance < roomDistance;
    const float3 figureColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth) * 0.10f;
    const float figureCoverage = figureDepth < roomDistance ? figure : 0.0f;
    if (selfVisible && selfDistance < figureDepth)
    {
        // 自分の方が手前：人影は自分の体に隠れる
        color = selfColor;
    }
    else
    {
        // 人影の方が手前（すぐ後ろに立つ人影が、肩の横から覗く）
        if (selfVisible)
        {
            color = selfColor;
        }
        color = lerp(color, figureColor, figureCoverage);
    }

    // くすんだ銀：少し緑がかった古いガラスの色で、映りを2割ほど暗くしている
    color *= float3(0.80f, 0.84f, 0.80f);

    // 縁から銀が剥げた黒いしみ（縁に近いほど多く、ノイズで不規則に広がる）と、面の途中の小さな剥げ
    const float2 edge = min(face, 1.0f - face);
    const float edgeDistance = min(edge.x, edge.y);
    const float blotch =
        FastValueNoise(face * float2(7.0f, 11.0f) + 13.0f) * 0.6f +
        FastValueNoise(face * 23.0f - 4.0f) * 0.4f;
    const float edgeLoss = smoothstep(0.15f, 0.0f, edgeDistance - blotch * 0.09f);
    const float spotLoss = smoothstep(0.80f, 0.90f, FastValueNoise(face * float2(5.0f, 7.0f) + 31.0f)) * 0.65f;
    const float desilver = saturate(edgeLoss + spotLoss);
    color = lerp(color, float3(0.030f, 0.026f, 0.022f), desilver * 0.88f);

    // 埃のくもり：映りを少し白っぽく、薄くしている
    const float haze = FastValueNoise(face * float2(2.0f, 3.0f) + 7.0f);
    color = lerp(color, color * 0.72f + float3(0.010f, 0.011f, 0.010f), haze * 0.35f);

    // 懐中電灯を正面から当てると、跳ね返った光が目に戻ってきて、鏡の一点がまぶしく光る（懐中電灯は目の位置にある）
    if (Light.Enable && Light.FlashlightEnabled)
    {
        const float3 toPixel = normalize(input.viewPos);
        const float3 bounced = reflect(toPixel, normalize(input.viewNormal));
        const float towardEye = saturate(dot(bounced, -toPixel));
        const float beam = GetFlashlightBeamProfile(toPixel);
        const float glare = (pow(towardEye, 2500.0f) * 0.55f + pow(towardEye, 90.0f) * 0.05f) * beam;
        // くもった所は、光を広くにじませている
        color += Light.Diffuse.rgb * Light.Intensity * glare * (1.0f - desilver) * (1.0f + haze * 0.5f);
    }
    return float4(color, 1.0f);
}
