// ============================================================================
// 共通の処理: 床の近くに溜まり、部屋全体にも薄く漂って、ゆっくり流れる霧（壁・天井・小物の litTexturePS と、床の wetFloorPS で共通）。
// 主な技術: 高さで薄くなる霧の、視線に沿った量の解析的な積分、時間で流れる2層のノイズ（霧の濃淡・筋）、
//           懐中電灯の円錐の中だけ霧が光る散乱（懐中電灯は目の位置にあるので、視線の方向の光の強さを霧全体に使える）
//           天井の照明から下へ広がる光の筋（視線が光の届く球を通る区間だけを、少ない点で調べて足す）
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

// 天井の照明・非常灯・裸電球から下へ広がる光の筋（霧に散った光）。
// 視線が光の届く球を通る区間だけを、少ない点で調べて足し合わせている（光の届く範囲の外は計算しない）。
// 点光源の一覧は、その画素のタイルの一覧を使っている（タイルの一覧は、画面の奥の面より手前の光源をすべて含む）。
// 光の筋の調べる点の数と、霧が光を散らす濃さ
#define LIGHT_SHAFT_SAMPLES 5
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
    // 画素ごとに調べる位置を少しずらし、少ない点でも縞模様が出ないようにしている（インターリーブド・グラディエント・ノイズ）
    const float jitter = frac(52.9829189f * frac(dot(pixelPosition, float2(0.06711056f, 0.00583715f))));
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
        // 霧のむら：ゆっくり流れるノイズで、光の筋の中に濃い所と薄い所を作っている（区間の真ん中で1回だけ求めている）
        const float3 middlePoint = eye + rayDirection * ((segmentStart + segmentEnd) * 0.5f);
        const float haze = 0.45f + FastValueNoise(middlePoint.xz * 0.05f + flow) * 0.90f;
        float lightScatter = 0.0f;
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
            lightScatter += attenuation * cone * awayFromFixture;
        }
        scattered += pointLight.ColorIntensity.rgb * pointLight.ColorIntensity.a * lightScatter * haze * stepLength;
    }
    // 光源の真下を長く見通したときに白く飛ばないよう、明るいほど伸びを抑えている（上限はおよそ0.35）
    const float3 shaft = scattered * LIGHT_SHAFT_DENSITY;
    return shaft / (1.0f + shaft * 2.8f);
}

#endif
