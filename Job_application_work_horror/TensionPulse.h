// ============================================================================
// ファイルの役割: プレイヤー自身の心拍音と呼吸音を、いつ・どれくらいの大きさで鳴らすかを決めます。
// 主な技術: 緊張度のなめらかな追従（上がるのは速く、下がるのはゆっくり）、間隔の計算、入力や描画に依存しない状態クラス
// 音を鳴らす処理とコントローラーの振動は、呼び出し側（Stage2Scene）が担当します。
//
// ・緊張度（0〜1）は、危険ゲージと同じ「足音の危険度・影までの距離・最後の追跡」から呼び出し側が渡します。
// ・緊張が高いほど心拍は速く大きくなり、息も荒くなります。
// ・ロッカーに隠れている間は、静けさの中で自分の心拍がはっきり聞こえるようにし、息は殺して小さく、ゆっくりにします。
// ============================================================================

#pragma once

#include <algorithm>

class TensionPulse final
{
public:
    // Updateの結果です。beat / breath が true のフレームで、その音を1回鳴らします。
    struct Output
    {
        bool beat = false;
        float beatVolume = 0.0f;    // 素材の音量に掛ける倍率
        float beatPitch = 1.0f;
        float vibration = 0.0f;     // 0なら振動させない（コントローラーのモーターの強さ 0〜1）
        bool breath = false;
        float breathVolume = 0.0f;
        float breathPitch = 1.0f;
    };

    // 心拍が聞こえ始める緊張度です。これより低いときは鳴らしません（いつも鳴っていると慣れてしまうため）。
    static constexpr float HeartAudibleLevel = 0.18f;
    // 荒い呼吸が聞こえ始める緊張度です。
    static constexpr float BreathAudibleLevel = 0.32f;
    // 隠れている間の最低の緊張度です。静かな中で、自分の心拍だけが聞こえる状態にします。
    static constexpr float HidingMinimumLevel = 0.36f;

private:
    // 緊張は急に高まり、ゆっくり静まります（1秒あたりの変化量）。危険が去っても数秒は心拍が残ります。
    static constexpr float RiseSpeed = 1.6f;
    static constexpr float FallSpeed = 0.11f;
    // 隠れている間の、息を殺した呼吸の間隔です。
    static constexpr float HeldBreathInterval = 5.2f;
    // 心拍の速さ（1分あたりの回数）。緊張度0で RestingRate、1で RestingRate + RateRange になります。
    static constexpr float RestingRate = 62.0f;
    static constexpr float RateRange = 88.0f;

    float m_Level = 0.0f;
    float m_BeatTimer = 0.0f;
    // 前の呼吸からの経過秒。次の呼吸までの間隔は、その時点の緊張度で毎フレーム決め直します。
    float m_BreathElapsed = 0.0f;
    bool m_WasBreathAudible = false;
    bool m_WasHiding = false;

    static float SmoothStep(float edge0, float edge1, float value) noexcept
    {
        const float t = (std::clamp)((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

public:
    void Reset() noexcept
    {
        m_Level = 0.0f;
        m_BeatTimer = 0.0f;
        m_BreathElapsed = 0.0f;
        m_WasBreathAudible = false;
        m_WasHiding = false;
    }

    float GetLevel() const noexcept { return m_Level; }
    float GetBeatsPerMinute() const noexcept { return RestingRate + RateRange * m_Level; }

    // tension は今の緊張度（0〜1）、hiding はロッカーに隠れているかです。毎フレーム呼びます。
    Output Update(float deltaTime, float tension, bool hiding) noexcept
    {
        float target = (std::clamp)(tension, 0.0f, 1.0f);
        if (hiding)
        {
            target = (std::max)(target, HidingMinimumLevel);
        }
        const float speed = target > m_Level ? RiseSpeed : FallSpeed;
        const float step = speed * (std::max)(deltaTime, 0.0f);
        m_Level = target > m_Level
            ? (std::min)(target, m_Level + step)
            : (std::max)(target, m_Level - step);

        Output output;

        // 心拍。聞こえない間もタイマーは進めず、聞こえ始めた瞬間に1拍目を打ちます。
        if (m_Level >= HeartAudibleLevel)
        {
            m_BeatTimer -= deltaTime;
            if (m_BeatTimer <= 0.0f)
            {
                m_BeatTimer += 60.0f / GetBeatsPerMinute();
                m_BeatTimer = (std::max)(m_BeatTimer, 0.05f);
                const float loudness = SmoothStep(HeartAudibleLevel, 0.75f, m_Level);
                output.beat = true;
                // 隠れている間は周りが静かなぶん、自分の心拍を少し大きく聞かせます。
                output.beatVolume = (0.30f + 0.70f * loudness) * (hiding ? 1.15f : 1.0f);
                output.beatPitch = 0.94f + 0.12f * m_Level;
                // 強い緊張のときだけ、心拍に合わせてコントローラーを短く震わせます。
                output.vibration = m_Level >= 0.45f ? 0.05f + 0.20f * m_Level : 0.0f;
            }
        }
        else
        {
            m_BeatTimer = 0.0f;
        }

        // 呼吸。隠れている間は「息を殺す」ため、緊張度に関係なく小さくゆっくりにします。
        const float heavy = SmoothStep(BreathAudibleLevel, 0.85f, m_Level);
        const bool breathAudible = hiding || m_Level >= BreathAudibleLevel;
        m_BreathElapsed += (std::max)(deltaTime, 0.0f);
        if (hiding && !m_WasHiding)
        {
            // 隠れた直後は息を止め、少ししてから最初の静かな呼吸をします。
            m_BreathElapsed = HeldBreathInterval - 1.4f;
        }
        else if (!hiding && m_WasHiding && m_Level >= HeartAudibleLevel)
        {
            // ロッカーから出た瞬間に、止めていた息を吐きます（「ふぅ」）。
            output.breath = true;
            output.breathVolume = 0.55f + 0.35f * heavy;
            output.breathPitch = 0.92f;
            m_BreathElapsed = 0.0f;
        }
        else if (breathAudible && !m_WasBreathAudible)
        {
            // 息が荒くなり始めた瞬間に1回目を鳴らします。
            m_BreathElapsed = 1000.0f;
        }

        if (breathAudible && !output.breath)
        {
            // 素材は約2.1秒。高い音で鳴らすと短くなるため、間隔は素材が鳴り終わる長さ以上にします。
            const float interval = hiding ? HeldBreathInterval : 3.6f - 1.5f * heavy;
            if (m_BreathElapsed >= interval)
            {
                m_BreathElapsed = 0.0f;
                output.breath = true;
                output.breathVolume = hiding ? 0.22f : 0.35f + 0.65f * heavy;
                output.breathPitch = hiding ? 0.86f : 1.0f + 0.16f * heavy;
            }
        }
        m_WasBreathAudible = breathAudible;
        m_WasHiding = hiding;
        return output;
    }
};
