// ============================================================================
// シェーダーの部品: 濡れた床（wetFloorPS）の main を段階ごとに分けた関数。
// 床の材質 → 水たまり・浸水・雨漏りの範囲 → 波の法線 → 天井の照明 → 懐中電灯 の順に、main から呼んでいる。
// 処理の中身と順番は main に書いていたときと同じ（関数は main の中へ展開されるので、コンパイル結果も同じ）。
// ============================================================================

#ifndef SIGNAL_LOST_WET_FLOOR_STAGES_HLSLI
#define SIGNAL_LOST_WET_FLOOR_STAGES_HLSLI

// 床の材質：素材の画像を湿った灰色のコンクリートに寄せ、1面では目地・ひびの色と凹凸を足している。
// color に色を掛け、目地とひびの凹凸を入れた法線を返す（古さのない面では元の法線のまま）
float3 ApplyFloorMaterial(LIT_PS_IN input, inout float4 color)
{
    float3 agedWorldNormal = normalize(input.worldNormal);
    if (Material.TextureEnable)
    {
        // 元の画像の草のような細かい模様を、正のMipバイアスで弱めようとしている。
        // 色の幅を抑えて、湿った汚いコンクリートに見せ、素材を足さずに済ませている。
        const float3 sampledFloor = g_Texture.SampleBias(
            g_SamplerState,
            input.tex,
            FLOOR_TEXTURE_MIP_BIAS).rgb;
        // 1面（床の古さあり）は、素材の明るさを平均（field.jpgで約0.40）へ寄せて砂利のようなざらつきを抑え、
        // 床の模様は目地・染み・引きずった跡（GetFloorAgeing）で作っている。
        // 素材にMipマップがないため、Mipバイアスではぼかせない。
        const float floorLuminance = lerp(
            dot(sampledFloor, LUMINANCE_WEIGHTS),
            FLOOR_GRAY_LEVEL,
            FLOOR_GRAY_STRENGTH * WallWeathering);
        float3 concreteFloor = lerp(
            floorLuminance.xxx,
            sampledFloor,
            FLOOR_TEXTURE_KEEP);
        // 広い範囲の明るさのむらを付け、少し緑がかった灰色にしている
        const float broadVariation = saturate(FastValueNoise(
            input.worldPos.xz * FLOOR_BROAD_SCALE + FLOOR_BROAD_SEED));
        concreteFloor *= FLOOR_CONCRETE_TINT *
            lerp(FLOOR_BROAD_DARK, FLOOR_BROAD_BRIGHT, broadVariation);
        color.rgb *= concreteFloor;

        // 床の古さ（WallWeathering が0より大きい面だけ）。色を掛け、目地とひびの凹凸で法線を作っている。
        // TextureEnable は描画の単位で一定なので、この中で画面上の偏微分を使っても問題ない。
        const FloorAgeing floorAgeing = GetFloorAgeing(input.worldPos, WallWeathering);
        color.rgb *= floorAgeing.tint;
        agedWorldNormal = ApplyHeightToNormal(
            input.worldPos, agedWorldNormal, floorAgeing.height, FLOOR_BUMP_STRENGTH);
    }
    else
    {
        color *= Material.Diffuse;
    }
    return agedWorldNormal;
}

// 水たまりの濃さ・岸の線・波の模様と、床一面が水に浸かった範囲の濃さと赤さ
struct FloorWater
{
    float shore;
    float ripplePattern;
    float puddle;
    // 床一面が水に浸かった範囲の濃さ（水を赤くするときに使う）と、その水の赤さ
    float floodAmount;
    float surfaceFloodRedness;
};

// 水たまり（GetPuddleMask）・床一面の浸水（西棟と1面の赤い濁流）・天井の雨漏りの下の水たまりを重ねている
FloorWater GetFloorWater(LIT_PS_IN input, int waterLevel)
{
    float shore = 0.0f;
    float ripplePattern = 0.0f;
    float puddle = 0.0f;
    float floodAmount = 0.0f;
    float surfaceFloodRedness = 0.0f;
    [branch]
    if (waterLevel > WATER_LEVEL_DRY_FLOOR)
    {
        puddle = GetPuddleMask(
            input.worldPos.xz,
            WetTime,
            shore,
            ripplePattern);
        // 床一面が水に浸かった範囲（1面の西棟）。範囲の内側は全面を水たまりと同じに扱い、ゆるい波を立てている。
        {
            const float2 fromMinimum = input.worldPos.xz - FloodRect.xy;
            const float2 toMaximum = FloodRect.zw - input.worldPos.xz;
            // 押し寄せる赤い水（LeakInfo.z が0より大きい）は、縁を流れるノイズで崩し、波を荒くしている（西棟の水は元のまま）
            surfaceFloodRedness = saturate(LeakInfo.z);
            const float frontNoise = FastValueNoise(
                input.worldPos.xz * FLOOD_FRONT_NOISE_SCALE + float2(0.0f, WetTime * FLOOD_FRONT_NOISE_SPEED));
            // 大きなうねり：細かいノイズだけでは縁がほぼ直線に見えるため、ゆっくり流れる大きな出入りを重ねている
            const float broadNoise = FastValueNoise(
                input.worldPos.xz * FLOOD_FRONT_BROAD_SCALE + float2(0.0f, WetTime * FLOOD_FRONT_BROAD_SPEED));
            // 縁のノイズは、水が進んでくる先頭の縁（南北の廊下は z の小さい側）だけにかけ、壁際と奥の縁は壁まで水を届かせる
            const float frontRaggedness = frontNoise * FLOOD_FRONT_NOISE_DEPTH + broadNoise * FLOOD_FRONT_BROAD_DEPTH;
            float insideDistance = min(min(fromMinimum.x, fromMinimum.y - frontRaggedness * surfaceFloodRedness),
                min(toMaximum.x, toMaximum.y));
            // 右から流れ込んだ水と、南へ進んだ水が交差する角でも水面をつなぐ。
            [branch]
            if (FloodInletRect.x < FloodInletRect.z)
            {
                // 右の通路の水は、x の小さい側が先頭
                const float2 inletFrom = input.worldPos.xz - FloodInletRect.xy;
                const float2 inletTo = FloodInletRect.zw - input.worldPos.xz;
                const float inletDistance = min(min(inletFrom.x - frontRaggedness * saturate(LeakInfo.w), inletTo.x),
                    min(inletFrom.y, inletTo.y));
                surfaceFloodRedness = inletDistance > insideDistance ? saturate(LeakInfo.w) : surfaceFloodRedness;
                // 2つの水面の継ぎ目が角張らないよう、なめらかに大きい方を取っている（smooth max）
                const float blend = saturate(0.5f + 0.5f * (inletDistance - insideDistance) / FLOOD_JUNCTION_BLEND);
                insideDistance = lerp(insideDistance, inletDistance, blend) + FLOOD_JUNCTION_BLEND * blend * (1.0f - blend);
            }
            // 赤い水の縁は、浅い水が薄く広がるように、幅を持たせてぼかしている
            const float flood = smoothstep(FLOOD_EDGE_START, lerp(FLOOD_EDGE_FULL, FLOOD_RED_EDGE_FULL, surfaceFloodRedness), insideDistance);
            ripplePattern += sin(input.worldPos.x * FLOOD_WAVE_SCALE_X + input.worldPos.z * FLOOD_WAVE_SCALE_Z + WetTime * FLOOD_WAVE_SPEED) *
                FLOOD_WAVE_STRENGTH * flood * (1.0f - puddle) * (1.0f + surfaceFloodRedness * FLOOD_TURBULENT_WAVE_GAIN);
            puddle = max(puddle, flood);
            floodAmount = flood;
        }
        // 天井の雨漏りの真下にできた小さな水たまり（少しずつ広がる。縁はノイズでゆがめ、落ちてくる水滴の輪が広がる）
        {
            const int leakCount = (int)(LeakInfo.x + 0.5f);
            [loop]
            for (int leakIndex = 0; leakIndex < leakCount; ++leakIndex)
            {
                const float4 leak = Leaks[leakIndex];
                const float2 leakOffset = input.worldPos.xz - leak.xy;
                const float reach = leak.z + LEAK_REACH_MARGIN;
                if (dot(leakOffset, leakOffset) > reach * reach)
                {
                    continue;
                }
                const float leakDistance = length(leakOffset);
                const float edgeNoise = (FastValueNoise(input.worldPos.xz * LEAK_EDGE_NOISE_SCALE + leak.xy) - 0.5f) * LEAK_EDGE_NOISE_STRENGTH;
                const float normalizedDistance = leakDistance / max(leak.z, LEAK_MIN_RADIUS) + edgeNoise;
                const float leakPuddle = 1.0f - smoothstep(LEAK_PUDDLE_EDGE, 1.0f, normalizedDistance);
                shore = max(shore, (1.0f - smoothstep(0.0f, LEAK_SHORE_WIDTH, abs(normalizedDistance - LEAK_SHORE_POSITION))) * LEAK_SHORE_STRENGTH);
                ripplePattern += sin(leakDistance * LEAK_RING_FREQUENCY - WetTime * LEAK_RING_SPEED) * LEAK_RING_STRENGTH * leakPuddle * exp(-leakDistance * LEAK_RING_FALLOFF);
                puddle = max(puddle, leakPuddle);
            }
        }
    }
    FloorWater water;
    water.shore = shore;
    water.ripplePattern = ripplePattern;
    water.puddle = puddle;
    water.floodAmount = floodAmount;
    water.surfaceFloodRedness = surfaceFloodRedness;
    return water;
}

// 水たまりと細い岸だけ、流れるノイズの波と、足音や水滴でできた本物の波（WaterRippleSimulation）で法線を傾けている。
// ripplePattern に水滴の輪と波の山を足し、dripRing（水滴の輪）と rippleWave（波の高さと傾き）を返す
float3 GetFloorDetailNormal(LIT_PS_IN input, float3 agedWorldNormal, float puddle, float shore,
    inout float ripplePattern, out float dripRing, out float3 rippleWave)
{
    dripRing = 0.0f;
    float3 detailWorldNormal = agedWorldNormal;
    // 足音や水滴でできた波（WaterRippleSimulation）の高さ（x）と傾き（yz）
    rippleWave = 0.0f;
    // 動く波の法線を作り直す処理は、水たまりと細い岸だけにしている。
    // 乾いた床では、3回のノイズの計算を省いている。
    [branch]
    if (puddle > WATER_EPSILON || shore > WATER_EPSILON)
    {
        dripRing = GetDripRing(input.worldPos.xz) * puddle;
        ripplePattern += dripRing * DRIP_RING_PATTERN;
        const float2 animatedNoiseOffset = float2(
            WetTime * RIPPLE_FLOW_X,
            -WetTime * RIPPLE_FLOW_Z);
        // 少しずつ流れるノイズの傾きから、波の凹凸の法線を作っている
        const float ripple = FastValueNoise(
            input.worldPos.xz * RIPPLE_NOISE_SCALE + animatedNoiseOffset);
        const float rippleX = FastValueNoise(
            input.worldPos.xz * RIPPLE_NOISE_SCALE + animatedNoiseOffset +
            float2(RIPPLE_SLOPE_OFFSET, 0.0f));
        const float rippleZ = FastValueNoise(
            input.worldPos.xz * RIPPLE_NOISE_SCALE + animatedNoiseOffset +
            float2(0.0f, RIPPLE_SLOPE_OFFSET));
        // 水たまりの中は、目地やひびが水で埋まるため、平らな面から波の凹凸を作っている。
        const float3 puddleBaseNormal = normalize(lerp(
            agedWorldNormal, normalize(input.worldNormal), saturate(puddle * PUDDLE_FLATTEN_GAIN)));
        detailWorldNormal = normalize(
            puddleBaseNormal +
            float3(ripple - rippleX, 0.0f, ripple - rippleZ) *
            (RIPPLE_BUMP_BASE + abs(ripplePattern) * RIPPLE_BUMP_PATTERN) * puddle * RippleStrength);

        // 足音や水滴でできた本物の波。広がって重なり、岸で跳ね返る輪の傾きで法線を傾け、
        // 波の山を光らせている（波の模様に足すと、下の光沢の計算で輪が光る）。
        rippleWave = GetRippleHeightAndSlope(input.worldPos.xz) * puddle;
        ripplePattern += rippleWave.x * SIMULATED_WAVE_PATTERN;
        detailWorldNormal = normalize(
            detailWorldNormal + float3(-rippleWave.y, 0.0f, -rippleWave.z) * SIMULATED_WAVE_TILT);
    }
    return detailWorldNormal;
}

// 天井の点光源（タイルごとに選んだもの）の光と、水たまりに映る光沢を足している
void AddFloorPointLights(LIT_PS_IN input, uint pointLightListOffset, uint pointLightCount, float3 detailWorldNormal,
    float puddle, inout float3 lighting, inout float3 specularLighting)
{
    // 天井の照明の接触影（一番強く照らしている点光源の向きで求めてある）
    const float screenContactShadow = GetScreenContactShadow(input.pos.xy);
    [loop]
    for (uint i = 0; i < pointLightCount; ++i)
    {
        const ENVIRONMENT_POINT_LIGHT pointLight =
            GetPixelLight(pointLightListOffset, i);

        const float3 offsetToLight =
            pointLight.PositionRange.xyz - input.worldPos;
        const float lightRange = max(pointLight.PositionRange.w, WATER_EPSILON);
        const float distanceSquaredToLight = dot(offsetToLight, offsetToLight);
        // 光が届かない距離の光源は、計算を省いている
        [branch]
        if (distanceSquaredToLight >= lightRange * lightRange)
        {
            continue;
        }
        const float distanceToLight = sqrt(distanceSquaredToLight);
        const float3 directionToPointLight =
            offsetToLight / max(distanceToLight, WATER_EPSILON);
        float pointAttenuation = saturate(1.0f - distanceToLight / lightRange);
        pointAttenuation *= pointAttenuation;
        // 棚・机・ロッカーの足元の細い影（接触影。SSAOと一緒に求めた画像。screenAmbientOcclusion.hlsli）
        pointAttenuation *= screenContactShadow;
        // 光源との間に建物の壁があれば、光は届かない（壁を抜けて隣の部屋を照らさないように。roomOcclusion.hlsli）
        pointAttenuation *= GetRoomLightVisibility(input.worldPos, pointLight.PositionRange.xyz,
            GetPixelLightWallMask(pointLightListOffset, i));
        // 揺れる裸電球の光は、電球との間に物があれば届かない（床に落ちる、揺れる影。pointShadow.hlsli）
        pointAttenuation *= GetPointLightShadow(input.worldPos, input.worldNormal, pointLight.PositionRange.xyz);
        // 見えない影の異変では、姿の見えない人影が、この光源の光を遮って床に影だけを落とす（figureShadow.hlsli）
        pointAttenuation *= GetUnseenFigureShadow(input.worldPos, pointLight.PositionRange.xyz) *
            GetPlayerBodyShadow(input.worldPos, pointLight.PositionRange.xyz);

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

        lighting += pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * pointAttenuation
            * softPointLambert
            * fixtureDistribution;

        // 水たまりでは、点光源の鋭い光沢を足している
        specularLighting += pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * pointAttenuation
            * pow(pointLambert, POINT_GLOSS_EXPONENT)
            * puddle * POINT_GLOSS_STRENGTH;

        // 柔らかい光の範囲を作り、天井の照明が水面へ映っていることを分かりやすくしている。
        const float horizontalDistance = length(offsetToLight.xz);
        const float reflectedFixture = pow(saturate(
            1.0f - horizontalDistance / max(lightRange * FIXTURE_REFLECTION_RADIUS, WATER_EPSILON)),
            FIXTURE_REFLECTION_EXPONENT);
        specularLighting += pointLight.ColorIntensity.rgb
            * pointLight.ColorIntensity.a
            * reflectedFixture * puddle * FIXTURE_REFLECTION_STRENGTH;
    }
}

// 懐中電灯の光：円錐の中だけ、拡散光と、濡れた面・足跡・浅い水・波の斜面の照り返しを足している
void AddFloorFlashlight(LIT_PS_IN input, float distanceFromCamera, float3 viewDirection, float3 detailWorldNormal,
    float puddle, float drainedDamp, float footprintWet, float fresnel, float3 rippleWave, float farRippleBoost,
    float flashlightOcclusion, inout float3 lighting, inout float3 specularLighting)
{
    if (Light.Enable && Light.FlashlightEnabled && distanceFromCamera > FLOOR_MIN_DIVISOR)
    {
        const float3 pixelDirection = input.viewPos / distanceFromCamera;
        const float beamProfile = GetFlashlightBeamProfile(pixelDirection);
        // 円錐の外の床では、レンズの汚れ・影・鏡面反射の計算を行っていない。
        [branch]
        if (beamProfile > FLOOR_MIN_BEAM)
        {
            const float normalizedDistance = saturate(
                distanceFromCamera / max(Light.Range, FLOOR_MIN_DIVISOR));
            const float rangeFade = saturate(
                1.0f - normalizedDistance * normalizedDistance);
            const float attenuation = rangeFade * rangeFade;
            const float lensPattern = GetFlashlightLensPattern(pixelDirection);
            const float3 flashlightColor = Light.Diffuse.rgb * GetFlashlightTint(pixelDirection);
            const float physicalFalloff = rcp(
                1.0f + distanceFromCamera * distanceFromCamera * FLASHLIGHT_PHYSICAL_FALLOFF_SCALE);
            const float naturalAttenuation = attenuation *
                lerp(1.0f, physicalFalloff, FLASHLIGHT_PHYSICAL_FALLOFF_BLEND);

            // 1面は目地・ひび・水の波の凹凸を懐中電灯にも反映している（2面は前と同じく平らな面で計算）。
            const float3 normal = WallWeathering > 0.0f
                ? normalize(mul(float4(detailWorldNormal, 0.0f), View).xyz)
                : normalize(input.viewNormal);
            const float3 directionToLight = -pixelDirection;
            const float lambert = saturate(dot(normal, directionToLight));
            const float softenedLambert = FLASHLIGHT_SOFT_LAMBERT_BASE + lambert * FLASHLIGHT_SOFT_LAMBERT_RANGE;

            const float shadow = GetFlashlightShadow(input.shadowPos) * GetUnseenFigureFlashlightShadow(input.worldPos);
            const float flashlightAmount = Light.Intensity
                * beamProfile * naturalAttenuation * lensPattern * shadow * flashlightOcclusion;

            // 水面は光の一部を反射へ回すため、透けて見える床の白飛びを抑える。
            lighting += flashlightColor
                * flashlightAmount
                * softenedLambert
                * lerp(1.0f, FLASHLIGHT_PUDDLE_DIFFUSE, puddle);

            // 濡れた面の鏡面反射（水たまりほど鋭く強い）
            const float3 halfVector = normalize(directionToLight + viewDirection);
            const float wetSpecular = pow(
                saturate(dot(normal, halfVector)),
                lerp(WET_SPECULAR_DRY_EXPONENT - drainedDamp * WET_SPECULAR_DAMP_EXPONENT, WET_SPECULAR_PUDDLE_EXPONENT, puddle));
            specularLighting += flashlightColor
                * flashlightAmount
                * wetSpecular
                * lerp(WET_SPECULAR_DRY + drainedDamp * WET_SPECULAR_DAMP, WET_SPECULAR_PUDDLE, puddle);
            // 濡れた足跡も、懐中電灯の光を細く返す
            specularLighting += flashlightColor
                * flashlightAmount
                * pow(saturate(dot(normal, halfVector)), FOOTPRINT_GLOSS_EXPONENT)
                * FOOTPRINT_GLOSS_STRENGTH * footprintWet;

            // 完全な鏡面反射の方向がカメラを外れても、浅い水は懐中電灯を弱く広く反射する。
            // 水面が黒く沈むのを防いでいる。
            specularLighting += flashlightColor
                * flashlightAmount
                * puddle
                * (SHALLOW_REFLECTION_BASE + fresnel * SHALLOW_REFLECTION_FRESNEL);

            // 足音や水滴の波：傾いた斜面が懐中電灯の光をきらっと返し、照らした所に光の輪が広がって見える
            specularLighting += flashlightColor
                * flashlightAmount
                * saturate(length(rippleWave.yz) * WAVE_GLINT_SLOPE_GAIN * farRippleBoost)
                * WAVE_GLINT_STRENGTH;
        }
    }
}

#endif
