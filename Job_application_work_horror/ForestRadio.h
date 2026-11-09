// ============================================================================
// ファイルの役割: 外の森を歩いている間に、研究棟からの無線（受信記録の3行と「通信途絶」）を、画面の下に字幕で流している。
// 主な技術: プレイヤーの進み具合（小道の z）で順に流す字幕、1文字ずつタイプされる文字、行の始まりの電波の乱れと無線の雑音
//
// ・1行目は歩き出してすぐ、2行目と3行目は小道を北へ進んだ所で流れる。前の行が消えるまでは次の行を流さないので、
//   走って進んでも、行が重なったり飛んだりしない。
// ・研究所が見えてくる所（2階の窓の明かりが消える辺り）で「通信途絶」になり、無線の雑音が途切れる。
// ・文章は、森を飛ばして始めたときの導入（IntroScene）と同じもの（StoryText.h）。
// ============================================================================

#pragma once

#include "Hud.h"

#include <SimpleMath.h>

class ForestRadio
{
public:
    // 毎フレーム、プレイヤーの位置で次の行を流すかを決め、字幕と雑音を進めている。電波の乱れの強さ（0〜1）を返す
    float Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition);
    // 字幕を描く内容を詰めている
    void FillView(Hud::RadioView& view) const;
    // 「通信途絶」まで進んだか（目的の表示を「研究所の玄関を調べる」に変えるのに使う）
    bool IsSignalLost() const { return m_SignalLost; }
    // 無線の雑音を止めている（森を出るとき）
    void Stop();

private:
    // 受信記録の行の数（StoryText::TransmissionLines と同じ。ForestRadio.cpp で確かめている）
    static constexpr int LineCount = 3;
    // 行を流し始める、小道の z（1行目は歩き出してすぐ）。最後は「通信途絶」
    static constexpr float TriggerZ[LineCount + 1] = { -100000.0f, -260.0f, -130.0f, 130.0f };
    // 1行目を流し始めるまでの秒数、1秒にタイプする文字の数、タイプし終えてから消え始めるまでの秒数、消える秒数
    static constexpr float FirstLineDelay = 3.0f;
    static constexpr float TypingCharactersPerSecond = 14.0f;
    static constexpr float HoldSeconds = 4.5f;
    static constexpr float FadeSeconds = 0.6f;
    // 「通信途絶」を出している秒数
    static constexpr float SignalLostSeconds = 3.5f;

    void StartLine(int index);

    float m_Time = 0.0f;
    // 次に流す行（0〜2が受信記録、LineCount が「通信途絶」、それより後は終わり）
    int m_NextLine = 0;
    // 流している行（-1 なら流していない）と、流し始めてからの秒数
    int m_ActiveLine = -1;
    float m_LineTime = 0.0f;
    bool m_SignalLost = false;
    float m_SignalLostTime = 0.0f;
    bool m_StaticPlaying = false;
};
