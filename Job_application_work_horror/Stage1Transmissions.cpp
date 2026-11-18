// ============================================================================
// ファイルの役割: 1面の録音と無線の字幕（説明は Stage1Transmissions.h を参照）。
// ============================================================================

#include "Stage1Transmissions.h"

#include "Game.h"
#include "Stage1LightStalker.h"
#include "StoryText.h"

#include <algorithm>

namespace
{
    // 録音：再生ボタンを押してから、テープが回り始めるまでの間（1行目・2行目以降）
    constexpr float RecordingFirstLineDelay = 0.7f;
    constexpr float RecordingNextLineDelay = 0.3f;
    // 調査員の録音を聞き終えてから、同じ声の無線が割り込むまでの間
    constexpr float RevealDelay = 1.4f;
    // 無線の呼びかけ：2行目以降の間
    constexpr float RadioNextLineDelay = 0.4f;
    // 録音の最後の行の、停止ボタンの音（再生ボタンの音をさらに低くしたもの。高さと音量）
    constexpr float StopClickPitch = 0.5f;
    constexpr float StopClickVolume = 0.6f;
    // 行の文字の浮かび方と乱れ方は、森の無線と同じ（Hud の Radio* の値）
    constexpr float LineFadeInSeconds = Hud::RadioLineFadeInSeconds;
    constexpr float LineNoiseStrength = Hud::RadioLineNoiseStrength;
    constexpr float LineNoiseSeconds = Hud::RadioLineNoiseSeconds;
    constexpr float SignalLostFadeSeconds = Hud::RadioSignalLostFadeSeconds;
    constexpr float SignalLostNoiseSeconds = Hud::RadioSignalLostNoiseSeconds;
    // 雑音の音量：録音のテープのかすかな雑音、無線が話している間に残る音量、届いた瞬間の強さと消える秒数、
    // 「通信途絶」の瞬間の強さ
    constexpr float TapeHissLevel = 0.04f;
    constexpr float RadioIdleStaticLevel = 0.10f;
    constexpr float RadioArrivalStaticLevel = 0.45f;
    constexpr float RadioArrivalStaticSeconds = 0.6f;
    constexpr float SignalLostStaticLevel = 0.8f;
}

void Stage1Transmissions::Reset()
{
    m_Time = 0.0f;
    m_Queue.clear();
    m_Active = Line{};
    m_HasActive = false;
    m_LineTime = 0.0f;
    m_SignalLostTime = -1.0f;
    m_Step = RadioStep::WaitFuse;
    m_FuseTimer = 0.0f;
    m_StalkerLitTime = 0.0f;
}

float Stage1Transmissions::GetTypingSeconds(const Line& line) const
{
    return static_cast<float>(StoryText::Utf8Length(line.Text)) / TypingCharactersPerSecond;
}

void Stage1Transmissions::PlayRecording(int index)
{
    if (index < 0 || index >= StoryText::RecordingCount)
    {
        return;
    }
    const StoryText::Recording& recording = StoryText::Recordings[index];
    for (int i = 0; i < StoryText::RecordingLineCount; ++i)
    {
        Line line;
        line.Header = recording.Header;
        line.Text = recording.Lines[i];
        line.Recording = true;
        // 再生ボタンを押してから、テープが回り始めるまでの間
        line.Delay = i == 0 ? RecordingFirstLineDelay : RecordingNextLineDelay;
        line.RecordingEnd = i == StoryText::RecordingLineCount - 1;
        Enqueue(line);
    }

    // 調査員の録音（最後の1つ）：聞き終えた直後に、同じ声の無線が割り込んで途切れる。それからは無線は届かない
    if (index == StoryText::RecordingCount - 1 && m_Step != RadioStep::Exposed)
    {
        Line reveal;
        reveal.Header = StoryText::FalseRadioHeader;
        reveal.Text = StoryText::FalseRadioRevealLine;
        reveal.Delay = RevealDelay;
        reveal.SignalLostAfter = true;
        Enqueue(reveal);
        m_Step = RadioStep::Exposed;
    }
}

void Stage1Transmissions::Update(const FrameInput& input)
{
    m_Time += input.DeltaTime;
    if (m_SignalLostTime >= 0.0f)
    {
        m_SignalLostTime += input.DeltaTime;
    }

    // 流している行：タイプし終えて少し待ったら消し、次の行へ進んでいる
    if (m_HasActive)
    {
        m_LineTime += input.DeltaTime;
        const float end = m_Active.Delay + GetTypingSeconds(m_Active) + HoldSeconds + FadeSeconds;
        if (m_LineTime >= end)
        {
            Core::Game* game = Core::Game::GetInstance();
            // 録音の最後の行：停止ボタンの音（再生ボタンの音をさらに低くしたもの）
            if (m_Active.RecordingEnd && game != nullptr)
            {
                game->PlayAudioCue(SOUND_CUE_FLASHLIGHT, StopClickPitch, StopClickVolume);
            }
            if (m_Active.SignalLostAfter)
            {
                m_SignalLostTime = 0.0f;
            }
            m_HasActive = false;
        }
    }
    if (!m_HasActive && !m_Queue.empty())
    {
        m_Active = m_Queue.front();
        m_Queue.pop_front();
        m_HasActive = true;
        m_LineTime = 0.0f;
    }

    // 入室と影を照らした時間は、ほかの字幕を再生している間も拾う。
    const bool canCall = input.RadioAllowed && !input.PowerRestored;
    switch (m_Step)
    {
    case RadioStep::WaitFuse:
        if (input.FuseCount >= 1)
        {
            m_FuseTimer += input.DeltaTime;
        }
        if (canCall && IsIdle() && m_FuseTimer >= CallDelaySeconds)
        {
            for (int i = 0; i < 2; ++i)
            {
                Line line;
                line.Header = StoryText::FalseRadioHeader;
                line.Text = StoryText::FalseRadioCallLines[i];
                line.Delay = i == 0 ? 0.0f : RadioNextLineDelay;
                Enqueue(line);
            }
            m_Step = RadioStep::Called;
        }
        break;
    case RadioStep::Called:
        if (canCall && Stage1LightStalker::IsInsideRoom(input.PlayerPosition))
        {
            Line line;
            line.Header = StoryText::FalseRadioHeader;
            line.Text = StoryText::FalseRadioRoomLine;
            Enqueue(line);
            m_Step = RadioStep::InRoom;
        }
        break;
    case RadioStep::InRoom:
        if (!canCall || !Stage1LightStalker::IsInsideRoom(input.PlayerPosition))
        {
            m_StalkerLitTime = 0.0f;
            break;
        }
        // 一瞬かすめただけでは流さず、影が止まるのを見てから指示が届くようにする。
        // 観察できた後は、室内にいる間だけ、その結果を字幕が空くまで覚えておく。
        if (m_StalkerLitTime < ObserveStalkerSeconds)
        {
            m_StalkerLitTime = input.ArchiveStalkerLit ? m_StalkerLitTime + input.DeltaTime : 0.0f;
        }
        if (m_StalkerLitTime >= ObserveStalkerSeconds && IsIdle())
        {
            Line line;
            line.Header = StoryText::FalseRadioHeader;
            line.Text = StoryText::FalseRadioLightLine;
            Enqueue(line);
            m_Step = RadioStep::ToldLight;
        }
        break;
    case RadioStep::ToldLight:
    case RadioStep::Exposed:
        break;
    }
}

void Stage1Transmissions::FillView(Hud::RadioView& view) const
{
    view.Time = m_Time;
    view.LineAlpha = 0.0f;
    view.SignalLostAlpha = 0.0f;
    view.Noise = 0.0f;
    view.SignalLost = StoryText::SignalLost;
    if (m_HasActive && m_LineTime >= m_Active.Delay)
    {
        const float lineTime = m_LineTime - m_Active.Delay;
        const float typingSeconds = GetTypingSeconds(m_Active);
        view.Header = m_Active.Header;
        view.Recording = m_Active.Recording;
        view.Line = StoryText::Utf8Prefix(m_Active.Text, static_cast<int>(lineTime * TypingCharactersPerSecond));
        const float fadeStart = typingSeconds + HoldSeconds;
        view.LineAlpha = std::clamp(1.0f - (lineTime - fadeStart) / FadeSeconds, 0.0f, 1.0f) *
            std::clamp(lineTime / LineFadeInSeconds, 0.0f, 1.0f);
        // 無線の行は、届いた瞬間に文字が乱れる（録音は乱さない）
        view.Noise = m_Active.Recording ? 0.0f : (std::max)(0.0f, LineNoiseStrength * (1.0f - lineTime / LineNoiseSeconds));
    }
    if (m_SignalLostTime >= 0.0f && m_SignalLostTime < SignalLostSeconds)
    {
        view.SignalLostAlpha = std::clamp((SignalLostSeconds - m_SignalLostTime) / SignalLostFadeSeconds, 0.0f, 1.0f);
        view.Noise = (std::max)(view.Noise, (std::max)(0.0f, 1.0f - m_SignalLostTime / SignalLostNoiseSeconds));
    }
}

float Stage1Transmissions::GetStaticLevel() const
{
    float level = 0.0f;
    if (m_HasActive && m_LineTime >= m_Active.Delay)
    {
        const float lineTime = m_LineTime - m_Active.Delay;
        // 無線：届いた瞬間に強く鳴り、話している間は小さく残る。録音：テープのかすかな雑音
        level = m_Active.Recording
            ? TapeHissLevel
            : (std::max)(RadioIdleStaticLevel, RadioArrivalStaticLevel * (1.0f - lineTime / RadioArrivalStaticSeconds));
    }
    if (m_SignalLostTime >= 0.0f && m_SignalLostTime < SignalLostNoiseSeconds)
    {
        level = (std::max)(level, SignalLostStaticLevel * (1.0f - m_SignalLostTime / SignalLostNoiseSeconds));
    }
    return level;
}
