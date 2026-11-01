// ============================================================================
// シェーダーの役割: 2面の壁の鏡を描いている。景色をもう一度描かずに、鏡で反射した光線を、このシェーダーの中で飛ばしている（レイ反射）。
// 主な技術: 床の水たまりと共通のレイ反射（rayReflection.hlsli：SSRと、部屋の形・小物の箱との交差）、
//           光線とカプセルの解析的な交差（鏡に映る自分の体）、光線とカプセルの芯の一番近い距離（鏡にだけ映る黒い人影）
// ・鏡に映る自分の体（一人称なので、本描画には体がない）は、胴・脚・腕・頭をカプセルで表し、光線と交差させている。
//   当たった所の法線で、天井の照明と、鏡で跳ね返った懐中電灯の光を当てている。位置・向き・手足の振りは MirrorSelf が渡している（b12）。
// ・鏡の異変の人影は、物として置かず、ここで光線と人の形のカプセルの距離から、輪郭のぼけた黒い影として重ねている（MirrorAnomaly）。
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

// 光線（origin から単位ベクトル direction の向き）が、カプセル（芯の線分 a-b、半径 radius）に当たる距離を返している（当たらなければ負）。
// 筒の部分と2次方程式で交差させ、筒の外へ出たら、端の球と交差させ直している
float IntersectCapsule(float3 origin, float3 direction, float3 a, float3 b, float radius)
{
    const float3 axis = b - a;
    const float3 offset = origin - a;
    const float axisLength2 = dot(axis, axis);
    const float axisAlong = dot(axis, direction);
    const float axisOffset = dot(axis, offset);
    const float directionOffset = dot(direction, offset);
    const float offsetLength2 = dot(offset, offset);
    // 芯が長さ0（球）のときや、光線が芯と平行なときも割り算が壊れないよう、下限を付けている
    const float quadraticA = max(axisLength2 - axisAlong * axisAlong, 1e-5f);
    float quadraticB = axisLength2 * directionOffset - axisOffset * axisAlong;
    float quadraticC = axisLength2 * offsetLength2 - axisOffset * axisOffset - radius * radius * axisLength2;
    float discriminant = quadraticB * quadraticB - quadraticA * quadraticC;
    float distance = -1.0f;
    if (discriminant >= 0.0f)
    {
        distance = (-quadraticB - sqrt(discriminant)) / quadraticA;
        const float along = axisOffset + distance * axisAlong;
        if (along <= 0.0f || along >= axisLength2)
        {
            const float3 toCap = along <= 0.0f ? offset : origin - b;
            quadraticB = dot(direction, toCap);
            quadraticC = dot(toCap, toCap) - radius * radius;
            discriminant = quadraticB * quadraticB - quadraticC;
            distance = discriminant > 0.0f ? -quadraticB - sqrt(discriminant) : -1.0f;
        }
    }
    return distance;
}

// カプセル1つと光線を交差させ、今までより近ければ、距離・法線・色を書き換えている
void TestBodyPart(float3 origin, float3 direction, float3 a, float3 b, float radius, float3 albedo,
    inout float nearest, inout float3 normal, inout float3 color)
{
    const float distance = IntersectCapsule(origin, direction, a, b, radius);
    if (distance > 0.0f && distance < nearest)
    {
        nearest = distance;
        const float3 hitPoint = origin + direction * distance;
        const float3 axis = b - a;
        const float along = saturate(dot(hitPoint - a, axis) / max(dot(axis, axis), 1e-5f));
        normal = (hitPoint - (a + axis * along)) / radius;
        color = albedo;
    }
}

// 点 position を、回転の中心 pivot のまわりに、x軸で angle だけ回している（脚と腕を前後に振る）
float3 SwingAroundX(float3 position, float3 pivot, float angle)
{
    const float3 local = position - pivot;
    float sine;
    float cosine;
    sincos(angle, sine, cosine);
    return pivot + float3(local.x, local.y * cosine - local.z * sine, local.y * sine + local.z * cosine);
}

// 鏡に映る自分の体と光線を交差させ、当たったら距離（当たらなければ負）と、照らした色を返している。
// 体の形は、もとの MirrorSelf のメッシュ（高さ約2.1を20倍、足もとが0、前が+z、右が+x）と同じ寸法のカプセルにしている
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

    // 服・ズボン・靴・肌・髪・懐中電灯の色（MirrorSelf と同じ）
    const float3 jacket = float3(0.16f, 0.17f, 0.16f);
    const float3 pants = float3(0.10f, 0.10f, 0.11f);
    const float3 shoes = float3(0.05f, 0.045f, 0.04f);
    const float3 skin = float3(0.34f, 0.27f, 0.22f);
    const float3 hair = float3(0.035f, 0.030f, 0.028f);
    const float3 flashlight = float3(0.06f, 0.06f, 0.07f);

    float nearest = 1e6f;
    float3 normal = float3(0.0f, 1.0f, 0.0f);
    float3 albedo = 0.0f;
    // 歩くと、左脚と右脚は逆向きに、左腕は左脚と逆向きに、股関節と肩を中心に前後へ振れる
    const float swing = MirrorSelfMotion.x;
    [unroll]
    for (int side = -1; side <= 1; side += 2)
    {
        const float3 hip = float3(2.4f * side, 18.4f, 0.0f);
        const float3 ankle = SwingAroundX(float3(2.4f * side, 1.6f, 0.0f), hip, swing * 0.45f * -side);
        TestBodyPart(localOrigin, localDirection, hip, ankle, 1.9f, pants, nearest, normal, albedo);
        TestBodyPart(localOrigin, localDirection, ankle + float3(0.0f, -0.4f, 0.4f), ankle + float3(0.0f, -0.4f, 2.4f),
            1.3f, shoes, nearest, normal, albedo);
    }
    // 腰・胴・肩・首・頭・髪
    TestBodyPart(localOrigin, localDirection, float3(0.0f, 17.8f, 0.0f), float3(0.0f, 20.6f, 0.0f), 3.7f, pants, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, float3(0.0f, 22.4f, 0.0f), float3(0.0f, 28.8f, 0.0f), 4.6f, jacket, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, float3(-4.8f, 31.0f, 0.0f), float3(4.8f, 31.0f, 0.0f), 2.2f, jacket, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, float3(0.0f, 32.6f, 0.1f), float3(0.0f, 35.4f, 0.1f), 1.25f, skin, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, float3(0.0f, 37.6f, 0.3f), float3(0.0f, 37.6f, 0.3f), 2.5f, skin, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, float3(0.0f, 38.6f, -0.7f), float3(0.0f, 38.6f, -0.7f), 2.55f, hair, nearest, normal, albedo);
    // 懐中電灯を前に構えた右腕（振らない）
    TestBodyPart(localOrigin, localDirection, float3(6.6f, 31.4f, 0.0f), float3(7.2f, 28.0f, 5.0f), 1.25f, jacket, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, float3(7.2f, 28.0f, 5.0f), float3(6.0f, 29.4f, 11.0f), 1.05f, jacket, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, float3(5.8f, 29.6f, 12.0f), float3(5.8f, 29.6f, 12.0f), 1.1f, skin, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, float3(5.8f, 30.0f, 11.4f), float3(5.8f, 30.4f, 16.4f), 0.95f, flashlight, nearest, normal, albedo);
    // 左腕（肩を中心に、左脚と逆向きに振る）
    const float3 shoulder = float3(-6.6f, 31.4f, 0.0f);
    const float armAngle = -swing * 0.35f;
    const float3 elbow = SwingAroundX(float3(-7.4f, 24.4f, 0.0f), shoulder, armAngle);
    const float3 wrist = SwingAroundX(float3(-7.2f, 18.4f, 0.6f), shoulder, armAngle);
    TestBodyPart(localOrigin, localDirection, shoulder, elbow, 1.2f, jacket, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, elbow, wrist, 1.05f, jacket, nearest, normal, albedo);
    TestBodyPart(localOrigin, localDirection, wrist, wrist + float3(0.0f, -0.9f, 0.0f), 1.0f, skin, nearest, normal, albedo);
    [branch]
    if (nearest >= 1e5f)
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

// 光線と線分 a-b が一番近づくときの離れ具合を返している。rayDistance はそのときの光線上の距離
float RayToSegmentDistance(float3 origin, float3 direction, float3 a, float3 b, out float rayDistance)
{
    const float3 segment = b - a;
    const float3 offset = origin - a;
    const float segmentLength2 = dot(segment, segment);
    const float along = dot(direction, segment);
    const float rayOffset = dot(direction, offset);
    const float segmentOffset = dot(segment, offset);
    const float segmentT = segmentLength2 > 1e-4f
        ? saturate((segmentOffset - along * rayOffset) / max(segmentLength2 - along * along, 1e-4f))
        : 0.0f;
    rayDistance = max(along * segmentT - rayOffset, 0.0f);
    return length(origin + direction * rayDistance - (a + segment * segmentT));
}

// 鏡の異変の人影（MirrorFigure。xyz = 足もと、w = 見え方）が光線をどれだけ覆うか（0〜1）を返している。depth は人影までの光線上の距離。
// 床の水たまりの人影（wetFloorPS の TracePuddleFigure）と同じ描き方で、背の高さだけ MirrorAnomaly の頭（足もとから39）に合わせている
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
    float rayDistance;
    const float legs = RayToSegmentDistance(origin, direction, foot + float3(0.0f, 1.0f, 0.0f), foot + float3(0.0f, 18.0f, 0.0f), rayDistance);
    float coverage = rayDistance > 0.5f ? smoothstep(4.6f, 2.8f, legs) : 0.0f;
    const float torso = RayToSegmentDistance(origin, direction, foot + float3(0.0f, 20.0f, 0.0f), foot + float3(0.0f, 30.0f, 0.0f), rayDistance);
    depth = rayDistance;
    coverage = max(coverage, rayDistance > 0.5f ? smoothstep(6.4f, 4.6f, torso) : 0.0f);
    const float head = RayToSegmentDistance(origin, direction, foot + float3(0.0f, 38.5f, 0.0f), foot + float3(0.0f, 39.5f, 0.0f), rayDistance);
    coverage = max(coverage, rayDistance > 0.5f ? smoothstep(4.8f, 3.4f, head) : 0.0f);
    // 両腕は、光線から見た左右（光線と直角な水平の向き）に垂らしている
    const float2 side = normalize(float2(-direction.z, direction.x) + 1e-5f);
    [unroll]
    for (int arm = 0; arm < 2; ++arm)
    {
        const float3 offset = float3(side.x, 0.0f, side.y) * (arm == 0 ? 6.8f : -6.8f);
        const float reach = RayToSegmentDistance(origin, direction,
            foot + offset * 1.08f + float3(0.0f, 13.0f, 0.0f), foot + offset + float3(0.0f, 30.0f, 0.0f), rayDistance);
        coverage = max(coverage, rayDistance > 0.5f ? smoothstep(3.0f, 1.7f, reach) : 0.0f);
    }
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
