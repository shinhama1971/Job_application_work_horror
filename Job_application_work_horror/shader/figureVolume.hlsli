// ============================================================================
// 共通の処理: 焼いた距離場（3Dテクスチャ）で、鏡と水たまりにだけ映る人の形を描いている（mirrorPS・wetFloorPS）。
// 主な技術: スフィアトレーシング（その点から一番近い面までの距離だけ光線を進める）、距離場の勾配による法線、
//           光線と箱の交差（スラブ法）で距離場の範囲だけを調べる、空間をゆがめて手足を振る（ドメインの変形）、
//           光線が形に一番近づいた距離からの、輪郭のぼけた影
// ・距離場は tools/maya/build_figure_volumes.py が焼いている。範囲・格子の数・間隔は、そのスクリプトの値と同じにしている。
// ・人影（t13）は距離だけ、自分の体（t6）は距離と色（rgb = 一番近い部位の色）を持っている。
// ・座標は、足もとが原点、x が右、y が上、z が前（ゲームの単位）。呼ぶ側で、光線をこの空間へ移してから渡している。
// FigureVolumes.cpp が t6・t13・s2 を設定している。
// ============================================================================
#ifndef FIGURE_VOLUME_INCLUDED
#define FIGURE_VOLUME_INCLUDED

Texture3D<float4> g_SelfVolume : register(t6);
Texture3D<float> g_FigureVolume : register(t13);
SamplerState g_VolumeSampler : register(s2);

// 格子の間隔と、範囲（最小の角・大きさ）・格子の数（build_figure_volumes.py と同じ）
static const float FIGURE_VOLUME_VOXEL = 0.4f;
static const float3 FIGURE_VOLUME_MIN = float3(-7.5f, -0.5f, -4.0f);
static const float3 FIGURE_VOLUME_SIZE = float3(15.0f, 40.5f, 10.0f);
static const float3 FIGURE_VOLUME_COUNT = float3(39.0f, 102.0f, 26.0f);
static const float3 SELF_VOLUME_MIN = float3(-8.5f, -0.5f, -4.5f);
static const float3 SELF_VOLUME_SIZE = float3(17.0f, 42.0f, 21.5f);
static const float3 SELF_VOLUME_COUNT = float3(43.0f, 106.0f, 55.0f);
// 人影の距離場の、頭の中心の高さ（呼ぶ側が、描きたい頭の高さに合わせて大きさを決めるのに使う）
static const float FIGURE_VOLUME_HEAD_HEIGHT = 35.4f;

// 位置を、格子点の中心に合わせたテクスチャ座標（0〜1）にしている
float3 GetVolumeCoordinate(float3 position, float3 minimum, float3 count)
{
    return ((position - minimum) / FIGURE_VOLUME_VOXEL + 0.5f) / count;
}

float SampleFigureDistance(float3 position)
{
    return g_FigureVolume.SampleLevel(g_VolumeSampler,
        GetVolumeCoordinate(position, FIGURE_VOLUME_MIN, FIGURE_VOLUME_COUNT), 0.0f);
}

// 光線（origin から単位ベクトル direction）が箱に入る距離と出る距離を求めている（当たらなければ false）
bool IntersectVolumeBox(float3 origin, float3 direction, float3 minimum, float3 maximum, out float enter, out float leave)
{
    const float3 safeDirection = abs(direction) > 1e-5f ? direction : 1e-5f;
    const float3 entryPlane = (minimum - origin) / safeDirection;
    const float3 exitPlane = (maximum - origin) / safeDirection;
    const float3 low = min(entryPlane, exitPlane);
    const float3 high = max(entryPlane, exitPlane);
    enter = max(max(low.x, low.y), max(low.z, 0.0f));
    leave = min(min(high.x, high.y), high.z);
    return leave > enter;
}

// 人影の距離場に光線を通し、輪郭のぼけた影の濃さ（0〜1）と、人影に一番近づいた所までの距離（depth）を返している。
// 当たらなくても、光線が形にどこまで近づいたかで、縁を softness の幅でぼかしている（霧の中の黒い人影）
float TraceFigureVolume(float3 origin, float3 direction, float softness, out float depth)
{
    depth = 1e6f;
    float enter;
    float leave;
    [branch]
    if (!IntersectVolumeBox(origin, direction, FIGURE_VOLUME_MIN, FIGURE_VOLUME_MIN + FIGURE_VOLUME_SIZE, enter, leave))
    {
        return 0.0f;
    }
    float travel = enter;
    float closest = 1e6f;
    [loop]
    for (int step = 0; step < 40; ++step)
    {
        const float distance = SampleFigureDistance(origin + direction * travel);
        if (distance < closest)
        {
            closest = distance;
            depth = travel;
        }
        if (distance < 0.05f || travel > leave)
        {
            break;
        }
        travel += max(distance * 0.9f, 0.15f);
    }
    return smoothstep(softness, 0.0f, closest);
}

// 歩く手足の振り：脚は股関節、左腕は肩を中心に前後へ回した空間で距離場を読み、焼いた立ち姿のまま手足を振って見せている。
// 振れ幅を、関節の近くでは0にして、胴との境目が切れないようにしている
float3 WarpSelfPosition(float3 position, float swing)
{
    const float side = position.x >= 0.0f ? 1.0f : -1.0f;
    const float legAngle = swing * 0.45f * side * smoothstep(17.5f, 11.0f, position.y);
    const float armAngle = -swing * 0.35f * smoothstep(-3.8f, -4.8f, position.x) * smoothstep(30.0f, 25.0f, position.y);
    const float angle = position.y < 17.5f ? legAngle : armAngle;
    const float3 pivot = position.y < 17.5f ? float3(0.0f, 17.5f, 0.0f) : float3(0.0f, 30.0f, 0.0f);
    float sine;
    float cosine;
    sincos(-angle, sine, cosine);
    const float3 local = position - pivot;
    return pivot + float3(local.x, local.y * cosine - local.z * sine, local.y * sine + local.z * cosine);
}

float4 SampleSelf(float3 position, float swing)
{
    return g_SelfVolume.SampleLevel(g_VolumeSampler,
        GetVolumeCoordinate(WarpSelfPosition(position, swing), SELF_VOLUME_MIN, SELF_VOLUME_COUNT), 0.0f);
}

// 自分の体の距離場に光線を通し、当たった距離（当たらなければ負）と、そこの法線と色を返している
float TraceSelfVolume(float3 origin, float3 direction, float swing, out float3 normal, out float3 albedo)
{
    normal = float3(0.0f, 1.0f, 0.0f);
    albedo = 0.0f;
    float enter;
    float leave;
    [branch]
    if (!IntersectVolumeBox(origin, direction, SELF_VOLUME_MIN, SELF_VOLUME_MIN + SELF_VOLUME_SIZE, enter, leave))
    {
        return -1.0f;
    }
    float travel = enter;
    float hit = -1.0f;
    [loop]
    for (int step = 0; step < 56; ++step)
    {
        const float distance = SampleSelf(origin + direction * travel, swing).r;
        if (distance < 0.04f)
        {
            hit = travel;
            break;
        }
        travel += max(distance * 0.85f, 0.08f);
        if (travel > leave)
        {
            break;
        }
    }
    [branch]
    if (hit < 0.0f)
    {
        return -1.0f;
    }
    // 法線：正四面体の4つの向きで距離の変化を調べた勾配
    const float3 position = origin + direction * hit;
    const float epsilon = FIGURE_VOLUME_VOXEL * 0.6f;
    const float2 k = float2(1.0f, -1.0f);
    normal = normalize(
        k.xyy * SampleSelf(position + k.xyy * epsilon, swing).r +
        k.yyx * SampleSelf(position + k.yyx * epsilon, swing).r +
        k.yxy * SampleSelf(position + k.yxy * epsilon, swing).r +
        k.xxx * SampleSelf(position + k.xxx * epsilon, swing).r);
    albedo = SampleSelf(position, swing).gba;
    return hit;
}

#endif
