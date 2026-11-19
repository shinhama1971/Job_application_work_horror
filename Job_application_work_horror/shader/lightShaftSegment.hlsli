#ifndef LIGHT_SHAFT_SEGMENT_INCLUDED
#define LIGHT_SHAFT_SEGMENT_INCLUDED

// 光の筋は真下に集め、外側に向けて薄くする。FXC に渡すリテラルを保つため展開する。
#define LIGHT_SHAFT_CONE_START 0.55f
#define LIGHT_SHAFT_CONE_FULL 0.92f

static const float LIGHT_SHAFT_DENSITY = 0.0018f;
// 壁の表示灯を除き、天井付近の照明だけを積分する。
static const float LIGHT_SHAFT_MIN_HEIGHT = -58.0f;

// 以下は lightShaftsCS（半分の解像度で積分）と horrorMist（同じ解像度で積分、または半分の解像度から復元）で共通の値
// 0で割らないための下限、視線が水平に近いとみなす縦の向きの下限
#define LIGHT_SHAFT_MIN_DIVISOR 0.001f
#define LIGHT_SHAFT_MIN_VERTICAL_DIRECTION 0.0001f
// 霧のむらの流れる速さ（x・z）、ノイズの細かさ、濃さ（最低＋幅）
#define LIGHT_SHAFT_FLOW_SPEED_X 0.60f
#define LIGHT_SHAFT_FLOW_SPEED_Z 0.25f
#define LIGHT_SHAFT_HAZE_SCALE 0.05f
#define LIGHT_SHAFT_HAZE_BASE 0.45f
#define LIGHT_SHAFT_HAZE_RANGE 0.90f
// 光源のすぐそばで薄くする距離（始め・終わり）と、明るいほど伸びを抑える強さ
#define LIGHT_SHAFT_FIXTURE_FADE_START 2.0f
#define LIGHT_SHAFT_FIXTURE_FADE_END 9.0f
#define LIGHT_SHAFT_COMPRESSION 2.8f
// 光の筋の画像の解像度（描く範囲の半分）
#define LIGHT_SHAFT_RESOLUTION_SCALE 0.5f
#define LIGHT_SHAFT_DOWNSAMPLE_FACTOR 2

float2 GetLightShaftSegment(float3 eye, float3 rayDirection, float rayLength,
    float3 center, float range)
{
    // 視線が光の届く球を通る区間（球と直線の交わり）を、カメラから画素までの間に切り詰めている
    const float closest = dot(center - eye, rayDirection);
    const float3 offsetFromRay = center - (eye + rayDirection * closest);
    const float missSquared = dot(offsetFromRay, offsetFromRay);
    // 球に届かない視線は長さ 0 の区間になる。呼び出し側で積分を省く。
    const float halfChord = sqrt(max(range * range - missSquared, 0.0f));
    float segmentStart = max(closest - halfChord, 0.0f);
    float segmentEnd = min(closest + halfChord, rayLength);
    // 光の円錐は照明より下にしかないので、視線が照明の高さより上を通る部分は調べない
    if (abs(rayDirection.y) > LIGHT_SHAFT_MIN_VERTICAL_DIRECTION)
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
    return float2(segmentStart, segmentEnd);
}

float GetLightShaftConeWeight(float3 toLight, float lightDistance)
{
    const float downward = saturate(toLight.y / max(lightDistance, LIGHT_SHAFT_MIN_DIVISOR));
    return smoothstep(LIGHT_SHAFT_CONE_START, LIGHT_SHAFT_CONE_FULL, downward);
}

#endif
