#ifndef LIGHT_SHAFT_SEGMENT_INCLUDED
#define LIGHT_SHAFT_SEGMENT_INCLUDED

static const float LIGHT_SHAFT_DENSITY = 0.0018f;
// 壁の表示灯を除き、天井付近の照明だけを積分する。
static const float LIGHT_SHAFT_MIN_HEIGHT = -58.0f;

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
    return float2(segmentStart, segmentEnd);
}

float GetLightShaftConeWeight(float3 toLight, float lightDistance)
{
    const float downward = saturate(toLight.y / max(lightDistance, 0.001f));
    return smoothstep(0.55f, 0.92f, downward);
}

#endif
