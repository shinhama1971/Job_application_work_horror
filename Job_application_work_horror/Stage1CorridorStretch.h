// ============================================================================
// ファイルの役割: 1面のループ廊下で、振り返ると廊下が伸びて見える演出の進行。
// 主な技術: 見え方だけを z 方向へ引き伸ばすビュー行列（Camera::SetWorldStretchZ）、
//           振り返っている間だけ伸び、目を離すとゆっくり戻る進み具合、周回ごとに大きくなる伸び方
//
// ・ループ廊下を1周した後、南北にまっすぐな廊下の途中で、来た方（南のドア）を振り返ると、
//   見ている間に廊下がじわじわ伸び、ドアが遠ざかっていく（ドアの大きさの見え方は、遠ざかった分だけ小さくなる）。
// ・目を離しても、すぐには元に戻らない（ゆっくり戻るので、もう一度振り返るとまだ長い）。
// ・周回が進むほど、大きく伸びる。
// ・伸ばすのは見え方だけで、当たり判定・光・音の位置は元のまま（歩けばいつもと同じ距離でドアに着く）。
// 描画・音に依存せず、伸ばす倍率と「伸び始めた」合図だけを返している。カメラへの設定と音は StageScene がしている。
// ============================================================================

#pragma once

#include <SimpleMath.h>

class Stage1CorridorStretch final
{
public:
    // 面の始めに戻している（伸ばさない状態）
    void Reset();
    // ループ廊下を回った数から、この周回で伸ばせる量を決めている（0周なら伸ばさない）
    void SetLoopCount(int loopCount);
    // 毎フレーム進めている。allowed が false の間（ほかの演出の最中など）は伸ばさず、戻していく。
    // 伸び始めた瞬間（ほぼ元の長さから伸び始めたフレーム）だけ true を返している（音を鳴らす合図）
    bool Update(float deltaTime,
        const DirectX::SimpleMath::Vector3& playerPosition,
        const DirectX::SimpleMath::Vector3& forward,
        bool allowed);

    // カメラへ渡す、z 方向の見え方の倍率（1で元のまま）
    float GetStretchZ() const;
    // 伸び具合（0〜1。画面効果の強さに使う）
    float GetProgress() const { return m_Progress; }

private:
    // この周回で伸ばせる量（倍率から1を引いた値）
    float m_MaxAmount = 0.0f;
    // 伸び具合（0〜1）
    float m_Progress = 0.0f;
    // 前のフレームで伸びていたか（伸び始めの合図を1回だけ出すため）
    bool m_Growing = false;
};
