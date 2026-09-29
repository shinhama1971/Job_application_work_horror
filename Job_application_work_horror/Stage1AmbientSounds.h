// ============================================================================
// ファイルの役割: 1面で、姿の見えない物音（天井裏の足音・配管を叩く音・遠くの扉）を
//                 ランダムな間隔と場所で起こします。
// 主な技術: 立体音響と組み合わせた環境演出、乱数による出現制御、時間差で鳴らす音の予約
// 音を鳴らす処理やゲームの状態には依存せず、「いつ・どこで・どの音を鳴らすか」だけを決めます。
// 鳴らしてよいかどうか（演出中でないか）はSceneが判断して渡します。
// ============================================================================

#pragma once

#include "sound.h"

#include <SimpleMath.h>
#include <random>
#include <vector>

// 1回分の物音。Sceneはこれを受け取り、Game::PlayAudioCueAtで鳴らします。
struct AmbientSoundCue
{
    SOUND_LABEL Label = SOUND_CUE_FOOTSTEP;
    DirectX::SimpleMath::Vector3 Position;
    float Pitch = 1.0f;
    float Volume = 1.0f;
    float MinimumOcclusion = 0.0f;  // 天井裏の音は、壁がなくてもこもらせます
};

class Stage1AmbientSounds final
{
public:
    // 最初の物音は、施設の暗さに慣れた頃に鳴らします。
    static constexpr float FirstDelay = 24.0f;
    static constexpr float MinInterval = 16.0f;
    static constexpr float MaxInterval = 30.0f;
    // 電力が戻ると施設が動き出し、物音の間隔が短くなります。
    static constexpr float PoweredIntervalScale = 0.72f;

    enum class Kind
    {
        CeilingSteps,   // 天井裏を何かが歩いていく
        PipeKnocks,     // 壁際の配管を叩く音
        DistantDoor     // 離れた扉がきしむ
    };

    void Reset();

    // 時間を進め、このフレームに鳴らす物音をcuesへ追加します。
    // allowedがfalseの間（台本の演出中など）は新しい物音を始めず、途中の物音も止めます。
    void Update(
        float deltaTime,
        bool allowed,
        bool powerRestored,
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward,
        std::vector<AmbientSoundCue>& cues);

    // デバッグ表示用。次の物音までの秒数です。
    float GetSecondsUntilNext() const { return m_WaitTimer; }

private:
    // 鳴らすまでの残り秒数付きの1音。足音や連続した打音を時間差で鳴らすために使います。
    struct ScheduledCue
    {
        float Delay = 0.0f;
        AmbientSoundCue Cue;
    };

    bool TryStartEvent(
        bool powerRestored,
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    bool ScheduleCeilingSteps(
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    bool SchedulePipeKnocks(
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    bool ScheduleDistantDoor(
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    float RandomRange(float minimum, float maximum);
    float NextInterval(bool powerRestored);

    std::vector<ScheduledCue> m_Scheduled;
    float m_WaitTimer = FirstDelay;
    Kind m_LastKind = Kind::DistantDoor;
    std::mt19937 m_Random{ std::random_device{}() };
};
