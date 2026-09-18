// ============================================================================
// シェーダーの役割: 材質、環境光、懐中電灯、天井灯、影を合成して3D表面を照明します。
// 定数バッファのスロットと入出力構造はCPU側の定義と必ず一致させてください。
// ============================================================================

#include "common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);
#include "flashlightShadow.hlsli"


#include "fastNoise.hlsli"
#include "flashlightLighting.hlsli"

struct LIT_PS_IN
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


float GetProceduralGrime(float3 worldPosition, float3 worldNormal)
{
    const float3 normal = abs(normalize(worldNormal));
    const float verticalSurface =
        saturate(1.0f - normal.y * normal.y);

    // 壁の主要軸を選んでワールドスケールの模様を作り、UVが大きく引き伸ばされた物体でも密度を保ちます。
    const float wallCoordinate = normal.x > normal.z
        ? worldPosition.z
        : worldPosition.x;
    const float2 wallUV = float2(wallCoordinate, worldPosition.y);

    const float broadStain = saturate(
        (FastValueNoise(wallUV * float2(0.026f, 0.019f) + 37.2f) - 0.43f)
        * 1.65f);
    // 細かな粒は補間ノイズではなくセル単位のハッシュで十分です。
    // 壁が画面の大半を占める場面のピクセル負荷を抑えます。
    const float fineDust = FastHash21(floor(
        wallUV * float2(0.115f, 0.082f) - 11.8f));

    // 主に一次元のマスクから、重力方向へ伸びる縦長の汚れを作ります。
    const float dripSeed = FastValueNoise(
        float2(wallCoordinate * 0.052f, 8.7f));
    const float dripBreakup = FastValueNoise(
        wallUV * float2(0.017f, 0.033f) + 4.1f);
    const float drip = smoothstep(0.64f, 0.91f, dripSeed) *
        smoothstep(0.30f, 0.78f, dripBreakup);

    // The stage floor is near world Y=-100; moisture accumulates at wall feet.
    const float heightFromFloor = max(worldPosition.y + 100.0f, 0.0f);
    const float floorDamp = exp(-heightFromFloor * 0.060f);

    return saturate(verticalSurface *
        (floorDamp * 0.38f + broadStain * 0.25f +
         drip * 0.17f + fineDust * 0.055f));
}

float GetProceduralSurfaceHeight(float3 worldPosition, float3 worldNormal)
{
    const float3 normal = abs(normalize(worldNormal));
    const float wallCoordinate = normal.x > normal.z
        ? worldPosition.z
        : worldPosition.x;
    const float2 wallUV = float2(wallCoordinate, worldPosition.y);

    const float broad = FastValueNoise(wallUV * 0.19f + 3.7f);
    const float plaster = FastValueNoise(wallUV * 0.63f - 12.4f);
    const float fine = FastHash21(floor(wallUV * 1.45f + 27.1f));
    return broad * 0.52f + plaster * 0.33f + fine * 0.15f;
}

float3 GetBumpedWorldNormal(float3 worldPosition, float3 worldNormal)
{
    const float3 normal = normalize(worldNormal);
    const float height = GetProceduralSurfaceHeight(
        worldPosition,
        worldNormal);
    const float heightDx = ddx(height);
    const float heightDy = ddy(height);
    const float3 positionDx = ddx(worldPosition);
    const float3 positionDy = ddy(worldPosition);
    const float3 gradientX = cross(positionDy, normal);
    const float3 gradientY = cross(normal, positionDx);
    const float determinant = dot(positionDx, gradientX);
    const float inverseDeterminant =
        (determinant < 0.0f ? -1.0f : 1.0f) /
        max(abs(determinant), 0.0001f);
    const float3 surfaceGradient =
        (gradientX * heightDx + gradientY * heightDy) *
        inverseDeterminant;
    return normalize(normal - surfaceGradient * 0.72f);
}

float3 ApplyFilmicHorrorGrade(float3 color)
{
    color = max(color, 0.0f);

    // 影を潰す強いクリップを避け、控えめなフィルム調ハイライト圧縮を合成します。
    const float3 acesColor = saturate(
        (color * (2.51f * color + 0.03f)) /
        (color * (2.43f * color + 0.59f) + 0.14f));
    color = lerp(color, acesColor, 0.28f);

    const float luminance = dot(
        color,
        float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luminance.xxx, color, 0.86f);
    const float shadowWeight =
        1.0f - smoothstep(0.08f, 0.42f, luminance);
    const float highlightWeight =
        smoothstep(0.38f, 0.90f, luminance);
    color *= lerp(
        1.0f.xxx,
        float3(0.91f, 0.96f, 1.035f),
        shadowWeight * 0.48f);
    color *= lerp(
        1.0f.xxx,
        float3(1.025f, 0.995f, 0.955f),
        highlightWeight * 0.36f);
    return saturate(color);
}


float4 main(in LIT_PS_IN input) : SV_Target
{
    float4 color = input.col;

    if (Material.TextureEnable)
    {
        color *= g_Texture.Sample(g_SamplerState, input.tex);
    }
    else
    {
        color *= Material.Diffuse;
    }

    // プロシージャル汚れは、不透明でテクスチャのない建材だけへ適用します。
    // 発光パネルは汚さず、ブルームと天井光の明るさを保ちます。
    const float emissionEnergy = dot(
        abs(Material.Emission.rgb),
        float3(0.3333f, 0.3333f, 0.3333f));
    float3 detailWorldNormal = normalize(input.worldNormal);
    float grime = 0.0f;
    if (!Material.TextureEnable && emissionEnergy < 0.001f)
    {
        grime = GetProceduralGrime(
            input.worldPos,
            input.worldNormal) * WallDampStrength;
        color.rgb *= 1.0f - grime * 0.22f;
        color.rgb = lerp(
            color.rgb,
            color.rgb * float3(0.72f, 0.80f, 0.69f),
            grime * 0.20f);
        detailWorldNormal = GetBumpedWorldNormal(
            input.worldPos,
            input.worldNormal);
    }
    const float3 detailViewNormal = normalize(mul(
        float4(detailWorldNormal, 0.0f),
        View).xyz);

    float3 lighting = GetHemisphereAmbient(detailWorldNormal);
    const float distanceFromCamera = length(input.viewPos);

    // 天井の点光源はパネルだけでなく、近くの床と壁も照らします。
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        if (i >= EnvironmentLightCount)
        {
            break;
        }

        const float3 offsetToLight =
            EnvironmentLights[i].PositionRange.xyz - input.worldPos;
        const float lightRange = max(EnvironmentLights[i].PositionRange.w, 0.001f);
        const float distanceSquaredToLight = dot(offsetToLight, offsetToLight);
        [branch]
        if (distanceSquaredToLight >= lightRange * lightRange)
        {
            continue;
        }
        const float distanceToLight = sqrt(distanceSquaredToLight);
        const float3 directionToPointLight =
            offsetToLight / max(distanceToLight, 0.001f);
        float pointAttenuation = saturate(1.0f - distanceToLight / lightRange);
        pointAttenuation *= pointAttenuation;

        const float pointLambert = saturate(dot(
            detailWorldNormal, directionToPointLight));
        const float softPointLambert = 0.20f + pointLambert * 0.80f;
        // 天井パネルを裸の点電球ではなく、下向きに広がる面光源として近似します。
        // 床へ光を集中させつつ、横方向にも少量の光を残します。
        const float downwardAmount = saturate(directionToPointLight.y);
        const float fixtureDistribution = lerp(
            0.22f,
            1.0f,
            smoothstep(0.04f, 0.72f, downwardAmount));

        lighting += EnvironmentLights[i].ColorIntensity.rgb
            * EnvironmentLights[i].ColorIntensity.a
            * pointAttenuation
            * softPointLambert
            * fixtureDistribution;
    }

    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > 0.001f)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
        // 円錐外ではレンズ汚れノイズとシャドウマップ参照を丸ごと省略します。
        [branch]
        if (beamProfile > 0.001f)
        {
            const float normalizedDistance = saturate(
                distanceFromCamera / max(Light.Range, 0.001f));
            const float rangeFade = saturate(
                1.0f - normalizedDistance * normalizedDistance);
            const float attenuation = rangeFade * rangeFade;
            const float lensPattern = GetFlashlightLensPattern(pixelDirection);
            const float physicalFalloff = rcp(
                1.0f + distanceFromCamera * distanceFromCamera * 0.000018f);
            const float naturalAttenuation = attenuation *
                lerp(1.0f, physicalFalloff, 0.32f);

            const float3 normal = detailViewNormal;
            const float3 directionToLight = -pixelDirection;
            const float lambert = saturate(dot(normal, directionToLight));
            const float softenedLambert = 0.25f + lambert * 0.75f;

            lighting += Light.Diffuse.rgb
                * Light.Intensity
                * beamProfile
                * naturalAttenuation
                * lensPattern
                * softenedLambert
                * GetFlashlightShadow(input.shadowPos);
        }
    }

    color.rgb *= lighting;

    // 湿った漆喰は浅い入射角で細く冷たいハイライトを返します。
    // 同じ汚れマスクを吸収と光沢へ使い、追加参照なしで見た目の整合性を保ちます。
    const float3 viewDirection = normalize(-input.viewPos);
    const float dampFresnel = pow(
        1.0f - saturate(dot(detailViewNormal, viewDirection)),
        4.0f);
    color.rgb += float3(0.055f, 0.070f, 0.076f) * grime *
        (0.045f + dampFresnel * 0.42f);

    color.rgb += Material.Emission.rgb;

    // 高さの異なる霧を重ね、近距離の移動視認性を保ちながら遠景の輪郭を分離します。
    // 部屋全体を一様に白くせず、床付近へ霧を集めます。
    const float distanceFog =
        smoothstep(110.0f, 390.0f, distanceFromCamera) * 0.72f;
    const float heightFromFloor = max(input.worldPos.y + 100.0f, 0.0f);
    const float heightDensity = exp(-heightFromFloor * 0.055f);
    const float nearSuppression =
        smoothstep(35.0f, 150.0f, distanceFromCamera);
    const float fogVariation = 0.78f +
        FastValueNoise(input.worldPos.xz * 0.018f + 21.0f) * 0.22f;
    const float heightFog =
        heightDensity * nearSuppression * fogVariation * 0.22f;
    const float fogFactor = saturate(distanceFog + heightFog);
    const float3 fogColor = max(
        Light.Ambient.rgb * 0.38f,
        float3(0.070f, 0.078f, 0.084f));
    color.rgb = lerp(color.rgb, fogColor, fogFactor);
    color.rgb = ApplyFilmicHorrorGrade(color.rgb);

    if (DebugViewMode == 1)
    {
        return float4(detailWorldNormal * 0.5f + 0.5f, 1.0f);
    }
    if (DebugViewMode == 2)
    {
        const float shadowVisibility =
            GetFlashlightShadow(input.shadowPos);
        return float4(shadowVisibility.xxx, 1.0f);
    }
    if (DebugViewMode == 3)
    {
        return float4(saturate(lighting * 0.5f), 1.0f);
    }
    if (DebugViewMode == 6)
    {
        return float4(saturate(grime).xxx, 1.0f);
    }
    if (DebugViewMode >= 4)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return color;
}

