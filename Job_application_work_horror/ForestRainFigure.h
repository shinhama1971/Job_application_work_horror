// ============================================================================
// ファイルの役割: 外の森の小道に、雨でだけ輪郭が浮かぶ「見えない人影」を一度だけ立たせる演出の進行。
// 主な技術: 有限状態機械（待機 → 雨の中に浮かぶ → 見つめられると崩れ落ちる → 消えた）、
//           懐中電灯で照らしているかの判定（視線との角度と距離）
//
// ・小道を歩き始めてすぐ、少し先の小道の上に、人影が立つ。姿は描かず、雨（ForestRain）が人の形の表面を伝って流れ落ち、
//   頭と肩で跳ねるので、懐中電灯や灯りの光の中でだけ、雨粒で人の輪郭が浮かぶ。真下には雨が降らない。
// ・懐中電灯でしばらく照らし続けるか、近づきすぎると、表面の水が一気に崩れ落ちて、人影は消える（水が地面を打つ音）。
//   近づいて崩れたときは、低い衝撃音とともに恐怖の画面効果を強く出す。
// 描画は ForestRain が受け持ち、ここでは足元・見え方・崩れ始めてからの秒数を決めて渡している。
// ============================================================================

#pragma once

#include <SimpleMath.h>

class ForestRainFigure final
{
public:
    // 1フレーム分進めている。playerPosition はプレイヤーの足元、eye と forward は目の位置と視線の向き、groundY は地面の高さ。
    // 恐怖の画面効果の強さ（0〜1）を返している
    float Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition,
        const DirectX::SimpleMath::Vector3& eye, const DirectX::SimpleMath::Vector3& forward, bool flashlightOn, float groundY);

    // 人影の足元、見え方（0〜1）、崩れ始めてからの秒数（0 なら崩れていない）。ForestRain::SetFigure へ渡している
    const DirectX::SimpleMath::Vector3& GetFoot() const { return m_Foot; }
    float GetVisibility() const;
    float GetCollapseSeconds() const { return m_State == State::Collapsing ? m_StateTime : 0.0f; }

private:
    enum class State
    {
        Waiting,
        Standing,
        Collapsing,
        Gone
    };

    // 崩れ始めている（scared = 近づかれて崩れたときは、強く驚かせる）
    void StartCollapse(bool scared);

    State m_State = State::Waiting;
    float m_StateTime = 0.0f;
    DirectX::SimpleMath::Vector3 m_Foot = DirectX::SimpleMath::Vector3::Zero;
    // 懐中電灯で照らし続けている秒数と、恐怖の画面効果の強さ
    float m_LitTime = 0.0f;
    // 崩れ始めたときの見え方（浮かび上がりきる前に崩れたときは、そこから消していく）
    float m_CollapseFrom = 1.0f;
    float m_Fear = 0.0f;
};
