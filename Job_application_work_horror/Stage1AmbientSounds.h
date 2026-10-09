// ============================================================================
// ファイルの役割: 1面で、姿の見えない物音（天井裏の足音・配管を叩く音・遠くの扉）を
//                 ランダムな間隔と場所で起こしている。
// 主な技術: 立体音響と組み合わせた環境の演出、乱数による出現の制御、時間をずらして鳴らす音の予定
// 音を鳴らす処理やゲームの状態には依存せず、「いつ・どこで・どの音を鳴らすか」だけを決めている。
// 鳴らしてよいかどうか（演出中でないか）はSceneが判断して渡している。
// ============================================================================

#pragma once

#include "sound.h"

#include <SimpleMath.h>
#include <random>
#include <vector>

// 1回分の物音。Sceneはこれを受け取り、Game::PlayAudioCueAtで鳴らしている。
struct AmbientSoundCue
{
    // 音の種類、鳴らす位置、音の高さ（再生速度）、音量
    SOUND_LABEL Label = SOUND_CUE_FOOTSTEP;
    DirectX::SimpleMath::Vector3 Position;
    float Pitch = 1.0f;
    float Volume = 1.0f;
    float MinimumOcclusion = 0.0f;  // 天井裏の音は、壁がなくてもこもらせている
};

// 1面の物音の予定を立てるクラス。StageSceneが毎フレーム呼び、鳴らす音を受け取っている。
class Stage1AmbientSounds final
{
public:
    // 最初の物音は、施設の暗さに慣れた頃（24秒後）に鳴らしている。
    static constexpr float FirstDelay = 24.0f;
    // その後は16〜30秒おきに鳴らしている
    static constexpr float MinInterval = 16.0f;
    static constexpr float MaxInterval = 30.0f;
    // 電力が戻ると施設が動き出し、物音の間隔を短く（0.72倍）している。
    static constexpr float PoweredIntervalScale = 0.72f;

    // 物音の種類
    enum class Kind
    {
        CeilingSteps,   // 天井裏を何かが歩いていく
        PipeKnocks,     // 壁際の配管を叩く音
        DistantDoor     // 離れた扉がきしむ
    };

    // 最初の状態に戻している
    void Reset();

    // 時間を進め、このフレームに鳴らす物音をcuesへ追加している。
    // allowedがfalseの間（台本の演出中など）は新しい物音を始めず、途中の物音も止めている。
    void Update(
        float deltaTime,
        bool allowed,
        bool powerRestored,
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward,
        std::vector<AmbientSoundCue>& cues);

    // 台本の演出から、天井裏の足音をすぐに始めている（壁の文字が書き換わった後など）。
    // 次のランダムな物音はそこから数え直し、続けて鳴りすぎないようにしている。
    bool StartCeilingStepsNow(
        bool powerRestored,
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);

    // デバッグ表示用。次の物音までの秒数を返している。
    float GetSecondsUntilNext() const { return m_WaitTimer; }

private:
    // 鳴らすまでの残り秒数が付いた1音。足音や続けて叩く音を、時間をずらして鳴らすために使っている。
    struct ScheduledCue
    {
        float Delay = 0.0f;
        AmbientSoundCue Cue;
    };

    // 物音の種類を選んで予定を立てている（場所が見つからなければfalse）
    bool TryStartEvent(
        bool powerRestored,
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    // 種類ごとに、鳴らす場所と音の並びを決めている
    bool ScheduleCeilingSteps(
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    bool SchedulePipeKnocks(
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    bool ScheduleDistantDoor(
        const DirectX::SimpleMath::Vector3& listenerPosition,
        const DirectX::SimpleMath::Vector3& listenerForward);
    // minimum〜maximumの一様な乱数を返している
    float RandomRange(float minimum, float maximum);
    // 次の物音までの間隔を決めている（電力が戻っていれば短くしている）
    float NextInterval(bool powerRestored);

    // 予定している音、次の物音までの秒数、前回の種類、乱数
    std::vector<ScheduledCue> m_Scheduled;
    float m_WaitTimer = FirstDelay;
    Kind m_LastKind = Kind::DistantDoor;
    std::mt19937 m_Random{ std::random_device{}() };
};
