// ============================================================================
// シェーダーの部品: 壁・天井・小物（litTexturePS）の main を段階ごとに分けた関数。
// 壁に残る水の跡 → 天井の照明 → 懐中電灯 の順に、main から呼んでいる。
// 処理の中身と順番は main に書いていたときと同じ。litTexturePS を読み込む肖像画・時計・扉・岩・樹皮・外壁・呼吸する壁・
// 印刷物のシェーダーでも、#if の分かれ方は main と同じに働く。
// ============================================================================

#ifndef SIGNAL_LOST_LIT_TEXTURE_STAGES_HLSLI
#define SIGNAL_LOST_LIT_TEXTURE_STAGES_HLSLI

// 建物の壁に残る水の跡：西棟の水が引いた柱の根元の湿り、1面の濁流が這い上がった跡、2面の血の手形の異変。
// color に色を掛け、濡れて光る度合い（wetness）を強めている
void ApplyWallWaterMarks(LIT_PS_IN input, inout float4 color, inout float wetness)
{
    // 水が引いた柱の根元には、元の水位の少し上まで吸い上げた水が残る。
    const float2 fromDrainColumn = abs(input.worldPos.xz - DrainTrace.xy);
    const float heightAboveWater = input.worldPos.y - CausticWaterY;
    [branch]
    if (DrainTrace.w > 0.0f && Material.WeatheringSurface && abs(input.worldNormal.y) < DRAIN_SIDE_NORMAL_LIMIT &&
        max(fromDrainColumn.x, fromDrainColumn.y) < DRAIN_COLUMN_HALF_WIDTH && heightAboveWater < DRAIN_MAX_HEIGHT)
    {
        const float exposed = smoothstep(0.0f, DRAIN_EXPOSE_DISTANCE, DrainTrace.z - input.worldPos.z);
        const float dampTop = DRAIN_DAMP_TOP + FastValueNoise(input.worldPos.xz * DRAIN_DAMP_NOISE_SCALE) * DRAIN_DAMP_TOP_RANGE;
        const float heightFromFloor = heightAboveWater + DrainTrace.w;
        const float band = exposed * smoothstep(DRAIN_BAND_BOTTOM_START, DRAIN_BAND_BOTTOM_END, heightFromFloor) *
            (1.0f - smoothstep(dampTop - DRAIN_BAND_TOP_BLUR, dampTop, heightFromFloor));
        color.rgb *= lerp(1.0f.xxx, DRAIN_DAMP_COLOR, band);
        wetness = max(wetness, band * DRAIN_DAMP_WETNESS);
    }

    // 1面の濁流が通った壁には、這い上がった水の高さまで赤黒く濡れた跡と、上がりきった所の線が残る（乾くと消えていく）
    [branch]
    if (FloodWallStain.w > 0.0f && Material.WeatheringSurface && abs(input.worldNormal.y) < DRAIN_SIDE_NORMAL_LIMIT)
    {
        const float2 p = input.worldPos.xz;
        const bool inCorridor = abs(p.x) < FLOOD_STAIN_CORRIDOR_HALF_X &&
            p.y > FloodWallStain.x && p.y < FLOOD_STAIN_CORRIDOR_END_Z;
        const bool inInlet = p.x > FLOOD_STAIN_INLET_MIN_X && p.x < FLOOD_STAIN_INLET_MAX_X &&
            p.y > FLOOD_STAIN_INLET_MIN_Z && p.y < FLOOD_STAIN_INLET_MAX_Z;
        if (inCorridor || inInlet)
        {
            // 壁に沿った向きで、上端の高さをゆっくり上下させている（水が当たった勢いの違い）
            const float along = abs(input.worldNormal.x) > abs(input.worldNormal.z) ? p.y : p.x;
            const float top = FloodWallStain.y *
                (FLOOD_STAIN_TOP_BASE + FastValueNoise(float2(along * FLOOD_STAIN_NOISE_SCALE, 0.0f)) * FLOOD_STAIN_TOP_RANGE);
            const float heightFromFloor = input.worldPos.y - SCENE_BASE_FLOOR_Y;
            const float band = 1.0f - smoothstep(top - FLOOD_STAIN_TOP_BLUR, top, heightFromFloor);
            const float tide = exp(-abs(heightFromFloor - top) * FLOOD_STAIN_TIDE_FALLOFF);
            const float3 stainColor = lerp(FLOOD_STAIN_DAMP_COLOR, FLOOD_STAIN_RED_COLOR, FloodWallStain.z);
            color.rgb *= lerp(1.0f.xxx, stainColor, band * FloodWallStain.w);
            color.rgb *= 1.0f - tide * FLOOD_STAIN_TIDE_DARKEN * FloodWallStain.w;
            wetness = max(wetness, band * FloodWallStain.w * FLOOD_STAIN_WETNESS);
        }
    }

    // 2面の「血の手形」の異変（その周回だけ、照らすと次の手形が奥に現れる）。濡れた血として光沢も返す
    const float anomalyHandprint = GetAnomalyHandprint(input.worldPos, normalize(input.worldNormal));
    color.rgb *= lerp(1.0f.xxx, ANOMALY_BLOOD_COLOR, anomalyHandprint * ANOMALY_BLOOD_BLEND);
    wetness = max(wetness, anomalyHandprint * ANOMALY_BLOOD_WETNESS);
}

// 天井の点光源（タイルごとに選んだもの）の拡散光と、鏡面反射を足している
void AddPointLights(LIT_PS_IN input, uint pointLightListOffset, uint pointLightCount, float3 detailWorldNormal,
    float3 detailViewNormal, float3 viewDirection, PbrSurface surface, inout float3 lighting, inout float3 specularLighting)
{
    // 天井の照明の接触影（棚・机の足元の細い影。一番強く照らしている点光源の向きで求めてある）
    const float screenContactShadow = GetScreenContactShadow(input.pos.xy);
    [loop]
    for (uint i = 0; i < pointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight =
            GetPixelLight(pointLightListOffset, i);

        const float3 offsetToLight =
            pointLight.PositionRange.xyz - input.worldPos;
        const float lightRange = max(pointLight.PositionRange.w, LIGHT_EPSILON);
        const float distanceSquaredToLight = dot(offsetToLight, offsetToLight);
        // 光が届かない距離の光源は、計算を省いている
        [branch]
        if (distanceSquaredToLight >= lightRange * lightRange)
        {
            continue;
        }
        const float distanceToLight = sqrt(distanceSquaredToLight);
        const float3 directionToPointLight =
            offsetToLight / max(distanceToLight, LIGHT_EPSILON);
        float pointAttenuation = saturate(1.0f - distanceToLight / lightRange);
        pointAttenuation *= pointAttenuation;

        const float pointLambert = saturate(dot(
            detailWorldNormal, directionToPointLight));
        const float softPointLambert = SOFT_LAMBERT_BASE + pointLambert * SOFT_LAMBERT_RANGE;
        // 天井のパネルを裸の電球ではなく、下向きに広がる面の光源として近似している。
        // 床へ光を集めつつ、横の方向にも少しだけ光を残している。
        const float downwardAmount = saturate(directionToPointLight.y);
        const float fixtureDistribution = lerp(
            FIXTURE_SIDE_LIGHT,
            1.0f,
            smoothstep(FIXTURE_BEAM_DOT_START, FIXTURE_BEAM_DOT_END, downwardAmount));

        // 揺れる裸電球の光は、電球との間に物があれば届かない（pointShadow.hlsli）。
        // 物と触れている所の細い影（接触影。SSAOと一緒に求めた画像。screenAmbientOcclusion.hlsli）も掛けている。
        // 光源との間に建物の壁があれば、光は届かない（壁を抜けて隣の部屋を照らさないように。roomOcclusion.hlsli）
        const float3 pointRadiance = pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * pointAttenuation
            * fixtureDistribution
            * screenContactShadow
            * GetRoomLightVisibility(input.worldPos, pointLight.PositionRange.xyz,
                GetPixelLightWallMask(pointLightListOffset, i))
            * GetPointLightShadow(input.worldPos, input.worldNormal, pointLight.PositionRange.xyz)
            * GetUnseenFigureShadow(input.worldPos, pointLight.PositionRange.xyz)
            * GetPlayerBodyShadow(input.worldPos, pointLight.PositionRange.xyz);
        lighting += pointRadiance * softPointLambert;

#if !defined(PRINTED_SURFACE)
        // 鏡面反射は、懐中電灯と同じビュー空間で計算している（光の向きだけをビュー空間へ回している）
        const float3 viewSpaceDirectionToLight = normalize(mul(
            float4(directionToPointLight, 0.0f), View).xyz);
        specularLighting += pointRadiance * EvaluateSpecularGGX(
            surface, detailViewNormal, viewDirection, viewSpaceDirectionToLight);
#endif
    }
}

// 懐中電灯の光：円錐の形・距離による弱まり・レンズの模様・面の向き・影を掛け合わせ、拡散光と鏡面反射を足している
void AddFlashlight(LIT_PS_IN input, float distanceFromCamera, float3 detailViewNormal, float3 viewDirection,
    PbrSurface surface, float flashlightOcclusion, inout float3 lighting, inout float3 specularLighting)
{
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > LIGHT_EPSILON)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
        // 円錐の外では、レンズの汚れのノイズとシャドウマップの参照を丸ごと省いている。
        [branch]
        if (beamProfile > LIGHT_EPSILON)
        {
            const float normalizedDistance = saturate(
                distanceFromCamera / max(Light.Range, LIGHT_EPSILON));
            const float rangeFade = saturate(
                1.0f - normalizedDistance * normalizedDistance);
            const float attenuation = rangeFade * rangeFade;
            const float lensPattern = GetFlashlightLensPattern(pixelDirection);
            const float3 flashlightColor = Light.Diffuse.rgb * GetFlashlightTint(pixelDirection);
            // 実際の光のように距離の2乗で弱まる分を、少しだけ混ぜている
            const float physicalFalloff = rcp(
                1.0f + distanceFromCamera * distanceFromCamera * FLASHLIGHT_PHYSICAL_FALLOFF_SCALE);
            const float naturalAttenuation = attenuation *
                lerp(1.0f, physicalFalloff, FLASHLIGHT_PHYSICAL_FALLOFF_BLEND);

            const float3 normal = detailViewNormal;
            const float3 directionToLight = -pixelDirection;
            const float lambert = saturate(dot(normal, directionToLight));
            // 横から当たる面も真っ暗にならず、正面の面も明るくなりすぎないよう、幅を抑えている（flashlightLighting.hlsli）
            const float softenedLambert = GetFlashlightDiffuseResponse(lambert);

            const float3 flashlightRadiance = flashlightColor
                * Light.Intensity
                * beamProfile
                * naturalAttenuation
                * lensPattern
                * GetFlashlightShadow(input.shadowPos)
                * GetUnseenFigureFlashlightShadow(input.worldPos)
                * flashlightOcclusion;
            lighting += flashlightRadiance * softenedLambert;
#if !defined(PRINTED_SURFACE)
            // 懐中電灯は目のすぐそばにあるので、金属や濡れた面を正面から照らすと、光の芯がはっきり映る
            specularLighting += flashlightRadiance * EvaluateSpecularGGX(
                surface, normal, viewDirection, directionToLight);
#endif
        }
    }
}

#endif
