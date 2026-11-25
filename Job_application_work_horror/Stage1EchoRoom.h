// ============================================================================
// ファイルの役割: 1面の「音を立てたときだけ輪郭が浮かぶ真っ暗な部屋」（開始地点の部屋）の進行。
// 主な技術: 状態の切り替え（待機 → 準備 → 真っ暗 → 終わり）、歩いた距離から作る足音の輪、
//           音の輪（中心・半径・強さ）の広がりと弱まり、最後に聞こえた音へ向かって歩く人影
//
// ・ループ廊下を2周した後、最後のドアから開始地点の部屋へ戻った瞬間に、部屋の照明が消え、懐中電灯も点かなくなる（1回のプレイで一度だけ）。
//   ほかの所から部屋へ入ったとき（赤い水に捕まって戻されたときなど）には起きない。
// ・歩くと、一歩ごとに足元から音の輪が広がり、壁・床・物の輪郭が一瞬浮かぶ。走ると輪は大きく遠くまで届く。立ち止まると真っ暗。
// ・部屋の奥には人影がいて、最後に聞こえた音の所へ歩いていく。人影の足音も小さな輪になるので、その輪でしか居場所が分からない。
// ・人影に触れられると驚かされて部屋の暗さが解け、部屋を出ても解ける。
// 描画・音・人影に依存せず、音の輪・人影の位置・「いつ何をするか」の合図（Cue）だけを返している。
// 照明・懐中電灯・音・人影を動かす処理と、音の輪を描画へ渡す処理は StageScene がしている。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <array>
#include <vector>

class Stage1EchoRoom final
{
public:
    // 同時に広がる音の輪の数（shader/renderingConfig.hlsli の SIGNAL_LOST_MAX_ECHO_PULSES と同じ）
    static constexpr std::size_t MaxPulses = 4;

    // StageScene へ渡す合図：真っ暗になった・暗さが解けた・人影に触れられた・足音を鳴らす（Position）
    enum class CueType
    {
        Darken,
        Restore,
        Caught,
        FigureStep
    };

    struct Cue
    {
        CueType Type = CueType::Darken;
        DirectX::SimpleMath::Vector3 Position;
    };

    // 部屋の内側（開始地点の部屋。壁の内側から少し入った所）
    static constexpr float MinX = -86.0f;
    static constexpr float MaxX = 86.0f;
    static constexpr float MinZ = -176.0f;
    static constexpr float MaxZ = -74.0f;

    // 面の始めに戻している
    void Reset();
    // ループ廊下の最後のドアから開始地点の部屋へ戻った瞬間に呼ぶ（loopCount はその周回の数。2周目からだけ準備する）。
    // 撮影では、条件を見ずに準備する（force）
    void ArmOnLoopArrival(int loopCount, bool force = false);
    // 毎フレーム進めている。running は走っているか
    void Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition, bool running,
        std::vector<Cue>& cues);

    // 真っ暗な間か（懐中電灯を点けさせない・ほかの物音や演出を止める）
    bool IsDark() const { return m_State == State::Dark; }
    // 人影を出しているか、その位置
    bool IsFigureVisible() const { return m_State == State::Dark; }
    DirectX::SimpleMath::Vector3 GetFigurePosition() const { return m_FigurePosition; }
    // 描画へ渡す音の輪（xyz = 音が出た所、w = 今の半径）と強さ（0〜1）
    const std::array<DirectX::SimpleMath::Vector4, MaxPulses>& GetPulses() const { return m_PulseShapes; }
    const std::array<float, MaxPulses>& GetPulseStrengths() const { return m_PulseStrengths; }

private:
    enum class State
    {
        Waiting,
        Armed,
        Dark,
        Finished
    };

    // 音の輪を1つ足している（空きがなければ、一番弱い輪と入れ替える）
    void AddPulse(const DirectX::SimpleMath::Vector3& origin, float maxRadius, float strength);
    void Finish(std::vector<Cue>& cues);
    // Update の段階：戻った直後に部屋へ入ったら暗くする／暗い部屋の足音の輪と人影／輪を広げて弱める
    void UpdateArmed(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition, std::vector<Cue>& cues);
    void UpdateDark(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition, bool running, std::vector<Cue>& cues);
    void AdvancePulses(float deltaTime);
    static bool IsInside(const DirectX::SimpleMath::Vector3& position);

    struct Pulse
    {
        DirectX::SimpleMath::Vector3 Origin;
        float Radius = 0.0f;
        float MaxRadius = 0.0f;
        float Strength = 0.0f;
    };

    State m_State = State::Waiting;
    // 準備してからの秒数（部屋へ入るのを待つ）
    float m_ArmTimer = 0.0f;
    std::array<Pulse, MaxPulses> m_Pulses{};
    std::array<DirectX::SimpleMath::Vector4, MaxPulses> m_PulseShapes{};
    std::array<float, MaxPulses> m_PulseStrengths{};
    // 前のフレームのプレイヤーの位置と、前の足音から歩いた距離
    DirectX::SimpleMath::Vector3 m_LastPlayerPosition;
    float m_StrideTravel = 0.0f;
    // 人影の位置・向かう所・次の足音までの秒数
    DirectX::SimpleMath::Vector3 m_FigurePosition;
    DirectX::SimpleMath::Vector3 m_FigureTarget;
    float m_FigureStepTimer = 0.0f;
};
