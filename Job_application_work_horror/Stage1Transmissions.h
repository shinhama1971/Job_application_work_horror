// ============================================================================
// ファイルの役割: 1面で画面の下に流れる字幕（ボイスレコーダーの録音と、また届く無線）の順番と、無線の嘘の進み具合を管理している。
// 主な技術: 行の順番待ちの列、1文字ずつタイプされる字幕、進み具合で次の行を決める段階の管理、字幕と雑音の強さの連動
//
// ・ボイスレコーダー（4つ）を調べると、その録音の3行を順に流す。流している途中に別の録音や無線が来たら、終わるまで待たせる。
// ・無線の嘘：1本目のヒューズを拾ってしばらくすると、森で聞いた調査員の声で無線がまた届き、
//   「書類保管室の奥の机に予備のヒューズを置いてきた」と言う（ヒューズは無い。机にあるのは調査員自身の録音）。
//   書類保管室に入ると「奥の机まで行け」、プレイヤーが影を照らして立ち止まる様子を見た後に「ライトを消せ。光に寄ってくる」と言う
//   （本当は照らしている間だけ止まる。壁の文字「あかりを けすな」と逆のことを言っている）。
// ・調査員の録音（4つ目）を聞き終えると、すぐに無線が「……聞いたのか。」と言って途切れ、それからは二度と届かない。
//   録音を先に聞いていれば、無線はそもそも届かない（嘘だと知った人には、もう声をかけてこない）。
// ・電力が戻った後は、新しい無線は届かない。
// 描画・入力に依存せず、Scene が判定した状態を受け取り、字幕の内容（Hud::RadioView）と雑音の強さを返している。
// ============================================================================

#pragma once

#include "Hud.h"

#include <SimpleMath.h>

#include <deque>

class Stage1Transmissions final
{
public:
    // 1フレーム分の状態（Scene が求めて渡している）
    struct FrameInput
    {
        float DeltaTime = 0.0f;
        DirectX::SimpleMath::Vector3 PlayerPosition;
        int FuseCount = 0;
        bool PowerRestored = false;
        // 新しい無線を予約してよいか（操作でき、閉じ込めや照明の演出・監視映像の最中でない）
        bool RadioAllowed = false;
        // 書類保管室の影が現れ、遮られていない懐中電灯の光で止まっているか
        bool ArchiveStalkerLit = false;
    };

    void Reset();
    // ボイスレコーダーの録音（0〜3）を、順番待ちの列に加えている
    void PlayRecording(int index);
    void Update(const FrameInput& input);
    // 字幕を描く内容を詰めている
    void FillView(Hud::RadioView& view) const;
    // 携帯ラジオの雑音の下限（0〜1）。無線が届いている間は強く、録音を流している間はテープのかすかな雑音にしている
    float GetStaticLevel() const;

private:
    // 字幕の1行（見出し・文章・録音か無線か・行の前に空ける秒数・この行の後に「通信途絶」を出すか・録音の最後の行か）
    struct Line
    {
        const char* Header = "";
        const char* Text = "";
        bool Recording = false;
        float Delay = 0.0f;
        bool SignalLostAfter = false;
        bool RecordingEnd = false;
    };
    // 無線の嘘の段階（1本目のヒューズを待つ → 呼びかけた → 書類保管室で誘った → ライトを消せと言った → 明かされた）
    enum class RadioStep { WaitFuse, Called, InRoom, ToldLight, Exposed };

    // 1秒にタイプする文字の数、タイプし終えてから消え始めるまでの秒数、消える秒数
    static constexpr float TypingCharactersPerSecond = 14.0f;
    static constexpr float HoldSeconds = 3.6f;
    static constexpr float FadeSeconds = 0.6f;
    // 1本目のヒューズを拾ってから無線が届くまでの秒数（拾った直後の影の演出と重ならないように）
    static constexpr float CallDelaySeconds = 14.0f;
    static constexpr float ObserveStalkerSeconds = 0.65f;
    // 「通信途絶」を出している秒数
    static constexpr float SignalLostSeconds = 3.0f;

    void Enqueue(const Line& line) { m_Queue.push_back(line); }
    bool IsIdle() const { return !m_HasActive && m_Queue.empty(); }
    float GetTypingSeconds(const Line& line) const;

    float m_Time = 0.0f;
    std::deque<Line> m_Queue;
    // 流している行と、流し始めてからの秒数（Delay の間はまだ字幕を出さない）
    Line m_Active;
    bool m_HasActive = false;
    float m_LineTime = 0.0f;
    // 「通信途絶」を出し始めてからの秒数（-1 なら出していない）
    float m_SignalLostTime = -1.0f;

    RadioStep m_Step = RadioStep::WaitFuse;
    float m_FuseTimer = 0.0f;
    float m_StalkerLitTime = 0.0f;
};
