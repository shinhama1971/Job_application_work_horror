// ============================================================================
// ファイルの役割: 1面の「おかしな部屋」2つの進行。
//   ・時間が止まった部屋（右の倉庫）：入ると世界の時計がゆっくり止まり、水滴・波・照明のちらつき・埃が宙で止まる。
//     動いているのは自分と、部屋の奥から近づいてくる人影だけ。人影に触れられるか照らすと、時間が戻る。
//   ・傾く部屋（南西の広間）：奥へ進むほど視界がゆっくり傾いていく。暖かい灯り（光の安全地帯）の下だけはまっすぐに戻る。
// 主な技術: 状態の切り替え（外 → 止まっていく → 止まった → 戻る）、世界の時計の速さ（Application::SetWorldTimeScale）、
//           部屋の入口からの距離で決めるカメラの傾き（Camera::SetViewRoll）
// 描画・音・人影に依存せず、時計の速さ・傾き・「いつ何をするか」の合図（Cue）だけを返している。
// 照明・音・人影を動かす処理は StageScene がしている。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <vector>

// 時間が止まった部屋（右の倉庫）
class Stage1TimeStopRoom final
{
public:
    // StageScene へ渡す合図：時間が止まり始めた・時間が戻った・人影を出す（Position）
    enum class CueType
    {
        Freeze,
        Resume,
        SpawnFigure
    };

    struct Cue
    {
        CueType Type = CueType::Freeze;
        DirectX::SimpleMath::Vector3 Position;
    };

    // 部屋の内側（壁の内側から少し入った所）
    static constexpr float MinX = 94.0f;
    static constexpr float MaxX = 216.0f;
    static constexpr float MinZ = -176.0f;
    static constexpr float MaxZ = -74.0f;

    // 面の始めに戻している
    void Reset();
    // 毎フレーム進めている。loopCount はループ廊下を回った数（1周した後から起きる）、
    // allowed はほかの演出と重ならないか（人影を出すときだけ見る）
    void Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition, int loopCount, bool allowed,
        std::vector<Cue>& cues);
    // 人影に触れられたか、照らして崩したときに呼ぶ。時間がすぐに戻り、人影は二度と出ない
    void BreakSpell();

    // 世界の時計の速さ（0〜1）。Application::SetWorldTimeScale へ渡す
    float GetWorldTimeScale() const { return m_TimeScale; }
    // 時間が止まっている・止まりかけているか（ほかの物音や演出を止めるため）
    bool IsActive() const { return m_TimeScale < 1.0f; }

private:
    static bool IsInside(const DirectX::SimpleMath::Vector3& position);

    bool m_Inside = false;
    // 一度触れられるか崩すと、その後は入っても止まらない
    bool m_Broken = false;
    bool m_FigureShown = false;
    float m_TimeScale = 1.0f;
    // 止まりきってからの秒数（人影を出すまでの間を数える）
    float m_FrozenSeconds = 0.0f;
};

// 傾く部屋（南西の広間）
class Stage1TiltRoom final
{
public:
    // 部屋の内側と、傾き始める入口（北の壁の入口のあたり）
    static constexpr float MinX = -376.0f;
    static constexpr float MaxX = -124.0f;
    static constexpr float MinZ = -328.0f;
    static constexpr float MaxZ = -184.0f;

    void Reset();
    // 毎フレーム進めている。calm は暖かい灯りの下にいる度合い（0〜1。1でまっすぐに戻る）
    void Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition, int loopCount, float calm);
    // カメラの傾き（ラジアン。Camera::SetViewRoll へ渡す）
    float GetRoll() const { return m_Roll; }

private:
    float m_Roll = 0.0f;
};
