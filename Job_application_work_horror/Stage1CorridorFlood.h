// ============================================================================
// ファイルの役割: 1面で、西棟の水を抜いた後に一度だけ起きる「抜いた水が、ループ廊下の奥から赤い濁流になって押し寄せる」演出の進行。
// 主な技術: 有限状態機械（待機 → 前触れ → 押し寄せる → 止まる／追いつかれた暗転 → 引いていく）、
//           水の先頭の位置（床の浸水範囲として Ground へ渡す）、プレイヤーとの位置関係から決める結果
//
// ・西棟の排水かヒューズ3本の回収を終えた後、南北の廊下の入口を抜けると濁流が迫る。
// ・少し間を置いて、北の角から赤い水が南へ押し寄せてくる（歩くより速く、走るより遅い）。
// ・南の入口（中央ホールへのドア）まで戻れば、水は入口で止まり、しばらくして引いていく。照明も戻る。
// ・水に追いつかれると、画面が暗転し、開始地点の部屋からやり直しになる（周回の数はそのまま）。
// 描画・音・照明に依存せず、水の先頭の位置と「いつ何をするか」の合図（Cue）だけを返している。
// 照明・音・画面効果・プレイヤーを戻す処理は StageScene がしている。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <vector>

class Stage1CorridorFlood final
{
public:
    // StageScene へ渡す合図の種類：照明を消す・水の音を鳴らす・追いつかれた・プレイヤーを開始地点へ戻す・照明を戻す
    enum class CueType
    {
        LightsOut,
        Rush,
        Caught,
        Restart,
        LightsOn
    };

    struct Cue
    {
        CueType Type = CueType::LightsOut;
        // 音を鳴らす所（Rush・Caught）
        DirectX::SimpleMath::Vector3 Position;
    };

    // 浸水する範囲（南北の廊下。x は両側の壁の内側、z は南の入口から北の角まで）
    static constexpr float CorridorMinX = -45.0f;
    static constexpr float CorridorMaxX = 45.0f;
    static constexpr float CorridorEntranceZ = 40.0f;
    static constexpr float CorridorEndZ = 275.0f;

    // 面の始めに戻している（まだ起きていない状態）
    void Reset();
    // ready は排水かヒューズ回収を終えたか、allowed はほかの大きな演出で操作が止まっていないか。
    // 起きた合図は cues に足している
    void Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition, bool ready, bool allowed,
        std::vector<Cue>& cues);
    // 撮影用：次の Update で、条件を見ずに前触れから始めている
    void ForceStart();
    bool IsFinished() const { return m_State == State::Finished; }

    // 水があるか（前触れの間は、まだ水はない）と、水の先頭の z（ここから北の角までが水）
    bool HasWater() const;
    float GetFrontZ() const { return m_FrontZ; }
    // 水の赤さ（0〜1）。押し寄せ始めると濃くなり、引いていくと薄くなる
    float GetRedness() const { return m_Redness; }
    // 演出の最中か（ほかの物音や演出を止めるため）
    bool IsActive() const { return m_State != State::Idle && m_State != State::Finished; }
    // 追いつかれた後の暗転の濃さ（0〜1）
    float GetBlackout() const;
    // 追いつかれた後の、画面の下から上がってくる水面の高さ（0〜1。PostProcess::SetSubmerge へ渡す）
    float GetSubmerge() const;

private:
    enum class State
    {
        Idle,
        Warning,
        Rushing,
        Holding,
        CaughtBlackout,
        Receding,
        Finished
    };

    void StartWarning(std::vector<Cue>& cues);

    State m_State = State::Idle;
    float m_StateTimer = 0.0f;
    float m_FrontZ = CorridorEndZ;
    float m_Redness = 0.0f;
    // 水の音を次に鳴らすまでの秒数
    float m_RushSoundTimer = 0.0f;
    // 撮影から、条件を見ずに始めるよう頼まれたか
    bool m_ForceStart = false;
};
