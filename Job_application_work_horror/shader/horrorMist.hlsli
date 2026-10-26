// ============================================================================
// 共通の処理: 床の近くに溜まり、部屋全体にも薄く漂って、ゆっくり流れる霧（壁・天井・小物の litTexturePS と、床の wetFloorPS で共通）。
// 主な技術: 高さで薄くなる霧の、視線に沿った量の解析的な積分、時間で流れる2層のノイズ（霧の濃淡・筋）、
//           懐中電灯の円錐の中だけ霧が光る散乱（懐中電灯は目の位置にあるので、視線の方向の光の強さを霧全体に使える）
// waterCaustics.hlsli（プレイヤーのビュー行列と時間）・flashlightLighting.hlsli・fastNoise.hlsli の後に読み込んでいる。
//
// ・懐中電灯の外では、霧は闇とほとんど同じ色で、遠くと足元をぼかすだけにしている。
// ・懐中電灯を向けると、光の円錐の中の霧が白く浮かび上がり、霧の流れが見える。
// ============================================================================
#ifndef HORROR_MIST_INCLUDED
#define HORROR_MIST_INCLUDED

// 床の高さ、霧が薄くなる高さ（この高さ上がるごとに約1/3）、濃さ
#define MIST_FLOOR_Y (-100.0f)
#define MIST_HEIGHT_SCALE 13.0f
#define MIST_DENSITY 0.016f
// 部屋全体に漂う薄い霧の濃さ（サイレントヒルのように、懐中電灯を向けた先が白くかすみ、遠くまで見通せない）
#define MIST_AIR_DENSITY 0.0024f

// プレイヤーの目の位置（ビュー行列から求めている。ビュー = 回転R と 移動t で、目の位置 = -t・Rの転置）
float3 GetMistEyePosition()
{
    return -mul((float3x3)CausticView, CausticView[3].xyz);
}

// 霧を重ねた色を返している。viewPosition はビュー空間の位置（懐中電灯の円錐の向きを求めるのに使う）。
// surroundingLight はその面が受けている光（天井の電球や非常灯）で、霧がその光を受けて色づく（電球のまわりが暖かくかすむ）
float3 ApplyHorrorMist(float3 color, float3 worldPosition, float3 viewPosition, float3 surroundingLight)
{
    const float3 eye = GetMistEyePosition();
    const float3 toPixel = worldPosition - eye;
    const float distance = length(toPixel);
    if (distance < 0.5f)
    {
        return color;
    }

    // 視線に沿って、高さで薄くなる霧の量を積分している（exp の高さの式は、視線の高さの変化で割れば解ける）
    const float heightEye = (eye.y - MIST_FLOOR_Y) / MIST_HEIGHT_SCALE;
    const float heightPixel = (worldPosition.y - MIST_FLOOR_Y) / MIST_HEIGHT_SCALE;
    const float heightChange = heightEye - heightPixel;
    const float layer = abs(heightChange) > 0.001f
        ? (exp(-heightPixel) - exp(-heightEye)) / heightChange
        : exp(-heightPixel);
    float amount = MIST_DENSITY * distance * layer;
    // 床の近くの霧に、部屋全体に漂う薄い霧を足している（流れるむらは、下で一緒に掛けている）
    amount += MIST_AIR_DENSITY * distance;

    // 霧の濃淡：視線の先の床の近くを、ゆっくり流れる2層のノイズで、筋やかたまりにしている
    const float2 mistPoint = lerp(eye.xz, worldPosition.xz, 0.65f);
    const float2 flow = float2(CausticTime * 1.10f, CausticTime * 0.45f);
    const float broad = FastValueNoise(mistPoint * 0.022f + flow * 0.020f);
    const float wisps = FastValueNoise(float2(mistPoint.x * 0.060f - flow.x * 0.045f, mistPoint.y * 0.030f + flow.y * 0.040f) + 7.0f);
    amount *= 0.35f + broad * 0.95f + wisps * 0.55f;
    const float mist = saturate(1.0f - exp(-amount));

    // 霧の色：懐中電灯の外は闇に近い暗い灰緑。円錐の中は、懐中電灯の光を散らして白く浮かぶ（遠いほど光が届かない）
    float3 mistColor = float3(0.012f, 0.015f, 0.015f) + Light.Ambient.rgb * 0.035f;
    // 電球や非常灯の光を受けた霧は、その光の色でかすむ（P.T. の廊下の電球のまわりの暖かいもや）
    mistColor += surroundingLight * 0.10f;
    if (Light.Enable && Light.FlashlightEnabled)
    {
        const float3 pixelDirection = viewPosition / max(length(viewPosition), 0.001f);
        const float beam = GetFlashlightBeamProfile(pixelDirection);
        const float reach = saturate(1.0f - distance / max(Light.Range, 1.0f));
        mistColor += Light.Diffuse.rgb * GetFlashlightTint(pixelDirection) * Light.Intensity * beam * (0.06f + reach * 0.15f);
    }
    return lerp(color, mistColor, mist);
}

#endif
