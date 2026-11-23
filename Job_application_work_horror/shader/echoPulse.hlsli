// ============================================================================
// シェーダーの部品: 音で輪郭が浮かぶ部屋の「音の輪」。
// 音が出た所から球の殻のように広がる輪が、壁・床・物の表面を通り過ぎる所を光らせ、通り過ぎた後ろには面の角（輪郭）だけを
// 薄く残している。真っ暗な部屋で、足音や物音を立てたときだけ、部屋の形が一瞬浮かんで見える。
// litTexturePS（壁・天井・小物・扉）と wetFloorPS（床）が、色を決めた最後に足している。値は RoomOcclusionBuffer（b11）。
// ============================================================================

#ifndef ECHO_PULSE_INCLUDED
#define ECHO_PULSE_INCLUDED

#include "roomOcclusion.hlsli"

// 輪の太さ（この距離で弱まる）と、輪が通り過ぎた後ろに輪郭が残る長さ、その明るさ
#define ECHO_RING_WIDTH 3.0f
#define ECHO_TRAIL_LENGTH 28.0f
#define ECHO_TRAIL_WEIGHT 0.55f
// 面の向きが急に変わる所（角・物の縁）を輪郭とみなす強さと、輪が平らな面にも出す明るさの割合
#define ECHO_EDGE_GAIN 1.6f
#define ECHO_FLAT_SURFACE_SHARE 0.9f
// 輪の色と明るさ（冷たい白っぽい青）
#define ECHO_COLOR float3(0.55f, 0.82f, 1.0f)
#define ECHO_BRIGHTNESS 4.5f
// これより弱い輪は計算しない
#define ECHO_MIN_STRENGTH 0.001f

// その画素に足す、音の輪の光を返している。worldNormal は面の法線（輪郭を見つけるのに使う）
float3 GetEchoReveal(float3 worldPosition, float3 worldNormal)
{
    // 輪郭：隣の画素と法線が大きく違う所（箱の角・物の縁）。微分を使うので、分岐やループの前で求めている
    const float edge = saturate(length(fwidth(worldNormal)) * ECHO_EDGE_GAIN);
    float ring = 0.0f;
    float trail = 0.0f;
    [loop]
    for (uint pulseIndex = 0; pulseIndex < SIGNAL_LOST_MAX_ECHO_PULSES; ++pulseIndex)
    {
        const float strength = EchoPulseStrength[pulseIndex].x;
        if (strength <= ECHO_MIN_STRENGTH)
        {
            continue;
        }
        const float4 pulse = EchoPulses[pulseIndex];
        // 輪の前の縁からの距離（正 = まだ届いていない、負 = 通り過ぎた後ろ）
        const float front = distance(worldPosition, pulse.xyz) - pulse.w;
        ring += exp(-(front * front) / (ECHO_RING_WIDTH * ECHO_RING_WIDTH)) * strength;
        trail += (front < 0.0f ? saturate(1.0f + front / ECHO_TRAIL_LENGTH) : 0.0f) * strength;
    }
    return ECHO_COLOR * ECHO_BRIGHTNESS *
        (ring * (ECHO_FLAT_SURFACE_SHARE + edge) + trail * edge * ECHO_TRAIL_WEIGHT);
}

#endif
