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

// SSRの前のフレームの画面（t4）と、深度プリパスの深度（t8）（ScreenSpaceReflection）
Texture2D g_SsrScene : register(t4);
Texture2D<float> g_SsrDepth : register(t8);

// SSR：反射の光線をビュー空間で少しずつ（だんだん大きな歩幅で）進め、画面の深度より奥へ入った所を「ぶつかった」とし、
// その画素の色を前のフレームの画面から読む。画面に映っていない物は映らない。
// 返り値の xyz は色、w はぶつかった度合い（画面の端と、遠くまで進んだ所では0へ近づけている）。
// stepCount は進める回数、stepGrowth は1回ごとに歩幅を広げる割合（レイ反射では、遠くは部屋の形で補うので少なくしている）。
float4 TraceScreenSpaceReflection(float3 worldPosition, float3 worldNormal, int stepCount = 40, float stepGrowth = 1.12f)
{
    uint width = 0;
    uint height = 0;
    g_SsrDepth.GetDimensions(width, height);
    const float3 viewPosition = mul(float4(worldPosition, 1.0f), CausticView).xyz;
    const float3 viewNormal = normalize(mul(float4(worldNormal, 0.0f), CausticView).xyz);
    const float3 rayDirection = reflect(normalize(viewPosition), viewNormal);
    // 深度バッファの値からビュー空間の z を戻すための、射影行列の値（右手系：z = -[3][2] / (深度 + [2][2])）
    const float depthScale = CausticProjection[2][2];
    const float depthOffset = CausticProjection[3][2];
    float distance = 1.5f;
    [loop]
    for (int step = 0; step < stepCount; ++step)
    {
        const float3 rayPosition = viewPosition + rayDirection * distance;
        const float4 clip = mul(float4(rayPosition, 1.0f), CausticProjection);
        // 鏡のようにカメラの方へ戻る光線は、すぐにカメラの後ろへ出るので、ここで終わる
        if (clip.w <= 0.001f)
        {
            break;
        }
        const float2 ndc = clip.xy / clip.w;
        const float2 uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
        if (any(uv < 0.0f) || any(uv > 1.0f))
        {
            break;
        }
        const float sceneDepth = g_SsrDepth.Load(int3(uv * float2(width, height), 0));
        const float sceneViewZ = -depthOffset / (sceneDepth + depthScale);
        // 光線が面より奥に入り、その差が面の厚みとみなせる範囲なら、ぶつかったとしている
        const float behind = sceneViewZ - rayPosition.z;
        if (behind > 0.0f && behind < 3.0f + distance * 0.06f)
        {
            const float2 edge = min(uv, 1.0f - uv);
            const float fade = saturate(min(edge.x, edge.y) * 12.0f) * (1.0f - float(step) / float(stepCount));
            return float4(g_SsrScene.SampleLevel(g_SamplerState, uv, 0.0f).rgb, fade);
        }
        distance *= stepGrowth;
    }
    return 0.0f;
}

// 部屋の形の、壁・天井・床の色（照らす前の色）と、光線を追う一番遠い距離
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
    if (direction.y > 0.0001f)
    {
        nearest = min(nearest, (OcclusionCeilingY - origin.y) / direction.y);
    }
    else if (direction.y < -0.0001f)
    {
        nearest = min(nearest, (OcclusionFloorY - origin.y) / direction.y);
        hitNormal = float3(0.0f, 1.0f, 0.0f);
        hitAlbedo = RoomReflectionFloorAlbedo;
    }
    // 壁は床から天井までの箱として、上から見た長方形と光線を交差させている（スラブ法）。0で割らないよう向きを丸めている
    const float2 safeDirection = float2(
        abs(direction.x) > 1e-5f ? direction.x : 1e-5f,
        abs(direction.z) > 1e-5f ? direction.z : 1e-5f);
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
        if (enter > 0.05f && enter <= exit && enter < nearest)
        {
            nearest = enter;
            hitNormal = entry.x > entry.y
                ? float3(-sign(safeDirection.x), 0.0f, 0.0f)
                : float3(0.0f, 0.0f, -sign(safeDirection.y));
            hitAlbedo = RoomReflectionWallAlbedo;
        }
    }
    // 小物（棚・ロッカー・柱など）は、カメラの近くから選んだ箱（ReflectionProps）と、3方向のスラブ法で交差させている。
    // 色は小物の色を少し暗くして使い、画面の外にある小物も、水面や鏡の正しい位置に正しい色で映るようにしている
    const float3 safeDirection3 = float3(
        safeDirection.x, abs(direction.y) > 1e-5f ? direction.y : 1e-5f, safeDirection.y);
    const float3 inverseDirection3 = 1.0f / safeDirection3;
    const uint propCount = min(uint(ReflectionParams.z + 0.5f), (uint)MAX_REFLECTION_PROPS);
    [loop]
    for (uint prop = 0; prop < propCount; ++prop)
    {
        const float3 slabNear = (ReflectionProps[prop * 3].xyz - origin) * inverseDirection3;
        const float3 slabFar = (ReflectionProps[prop * 3 + 1].xyz - origin) * inverseDirection3;
        const float3 entry = min(slabNear, slabFar);
        const float3 leave = max(slabNear, slabFar);
        const float enter = max(max(entry.x, entry.y), entry.z);
        const float exit = min(min(leave.x, leave.y), leave.z);
        if (enter > 0.05f && enter <= exit && enter < nearest)
        {
            nearest = enter;
            hitNormal = entry.x >= entry.y && entry.x >= entry.z ? float3(-sign(safeDirection3.x), 0.0f, 0.0f)
                : (entry.y >= entry.z ? float3(0.0f, -sign(safeDirection3.y), 0.0f) : float3(0.0f, 0.0f, -sign(safeDirection3.z)));
            hitAlbedo = ReflectionProps[prop * 3 + 2].rgb * 0.55f;
        }
    }
    return nearest;
}

// 部屋の形に光線を飛ばし、当たった天井・床・壁の色と、途中で光線がそばを通った照明の光を返している。
// roughness は面の荒さ（0〜1）で、荒いほど照明の映り込みをにじませている。
// hitDistance は当たった面までの距離（どこにも当たらなければ RoomReflectionMaxDistance）で、鏡に映る人影の前後を決めるのに使っている
float3 TraceRoomReflection(float3 origin, float3 direction, float roughness, out float hitDistance)
{
    float3 hitNormal = float3(0.0f, -1.0f, 0.0f);
    float3 albedo = RoomReflectionCeilingAlbedo;
    hitDistance = OcclusionBoxCount > 0 || ReflectionParams.z > 0.5f
        ? IntersectRoom(origin, direction, hitNormal, albedo)
        : RoomReflectionMaxDistance;
    const bool hit = hitDistance < RoomReflectionMaxDistance;
    const float3 hitPosition = origin + direction * hitDistance;

    float3 lighting = GetHemisphereAmbient(hitNormal);
    float3 glow = 0.0f;
    const float glowRadius = 6.0f * (1.0f + roughness * 1.5f);
    // 器具の光を調べる、光線からの距離の上限（にじみの裾がほぼ0になる所）の二乗
    const float glowReach2 = (glowRadius * 12.0f) * (glowRadius * 12.0f);
    [loop]
    for (uint i = 0; i < PointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight = g_PointLights[i];
        // 光線からも当たった面からも遠い光源は、二乗の距離だけで先に飛ばしている（照明の多い1面で、重い計算を減らすため）
        const float3 toLight = pointLight.PositionRange.xyz - origin;
        const float along = dot(toLight, direction);
        const float miss2 = dot(toLight, toLight) - along * along;
        const bool nearRay = along > 0.0f && along < hitDistance + 4.0f && miss2 < glowReach2;
        const float3 offset = pointLight.PositionRange.xyz - hitPosition;
        const float lightDistance2 = dot(offset, offset);
        const float range = max(pointLight.PositionRange.w, 0.001f);
        const bool lightsHit = hit && lightDistance2 < range * range;
        [branch]
        if (nearRay || lightsHit)
        {
            const float3 lightColor = pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a;
            // 照明の器具：光線が光源に一番近づく所の離れ具合で光らせている（当たった面より奥の光源は隠れている）
            if (nearRay)
            {
                const float miss = sqrt(max(miss2, 0.0f));
                glow += lightColor * (exp(-miss2 / (glowRadius * glowRadius)) * 0.85f +
                    exp(-miss / (glowRadius * 3.0f)) * 0.03f);
            }
            // 当たった面を、この光源で照らしている（床の点光源と同じ減衰）
            if (lightsHit)
            {
                const float lightDistance = sqrt(lightDistance2);
                float attenuation = saturate(1.0f - lightDistance / range);
                attenuation *= attenuation;
                const float lambert = saturate(dot(hitNormal, offset / max(lightDistance, 0.001f)));
                lighting += lightColor * attenuation * (0.20f + lambert * 0.80f);
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
            const float3 pixelDirection = viewPosition / max(viewDistance, 0.001f);
            const float normalizedDistance = saturate(viewDistance / max(Light.Range, 0.001f));
            const float rangeFade = saturate(1.0f - normalizedDistance * normalizedDistance);
            const float3 toEye = normalize(GetMistEyePosition() - hitPosition);
            lighting += Light.Diffuse.rgb * GetFlashlightTint(pixelDirection) * Light.Intensity *
                GetFlashlightBeamProfile(pixelDirection) * rangeFade * rangeFade *
                (0.25f + saturate(dot(hitNormal, toEye)) * 0.75f);
        }
        color = albedo * lighting;
    }
    // 光線が進んだ距離の分だけ、霧の闇に沈めている（どこにも当たらなければ闇の色）
    const float3 fogColor = GetHorrorFogColor(Light.Ambient.rgb, LoopDepth);
    const float fog = hit ? GetHorrorDistanceFog(hitDistance, LoopDepth) : 1.0f;
    color = lerp(color, fogColor, fog) + glow * (1.0f - fog * 0.6f);
    return ApplyFilmicHorrorGrade(color, LoopDepth);
}

#endif
