// ============================================================================
// ファイルの役割: 外の森を歩いている間の無線の字幕（説明は ForestRadio.h を参照）。
// ============================================================================

#include "ForestRadio.h"

#include "Game.h"
#include "StoryText.h"

#include <algorithm>
#include <iterator>

void ForestRadio::StartLine(int index)
{
    m_ActiveLine = index;
    m_LineTime = 0.0f;
    // 行が届いている間だけ、無線の雑音を小さく鳴らしている
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_RADIO_STATIC, 1.0f, 0.32f);
    m_StaticPlaying = true;
}

void ForestRadio::Stop()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game != nullptr && m_StaticPlaying)
    {
        game->StopAudioCue(SOUND_CUE_RADIO_STATIC);
    }
    m_StaticPlaying = false;
}

float ForestRadio::Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition)
{
    static_assert(std::size(StoryText::TransmissionLines) == LineCount, "ForestRadio::LineCount must match StoryText");
    m_Time += deltaTime;
    float noise = 0.0f;

    // 流している行：タイプし終えて少し待ったら消し、雑音も止める
    if (m_ActiveLine >= 0)
    {
        m_LineTime += deltaTime;
        const float typingSeconds = static_cast<float>(StoryText::Utf8Length(StoryText::TransmissionLines[m_ActiveLine])) /
            TypingCharactersPerSecond;
        // 行が届いた瞬間に電波が乱れ、届いている間もかすかに乱れている
        noise = (std::max)(0.05f, 0.55f * (1.0f - m_LineTime / 0.5f));
        if (m_LineTime > typingSeconds + HoldSeconds + FadeSeconds)
        {
            m_ActiveLine = -1;
            Stop();
        }
    }
    // 次の行：前の行が消えていて、小道をその場所まで進んでいたら流す
    else if (m_NextLine < LineCount)
    {
        const bool ready = m_NextLine > 0 || m_Time >= FirstLineDelay;
        if (ready && playerPosition.z >= TriggerZ[m_NextLine])
        {
            StartLine(m_NextLine);
            ++m_NextLine;
        }
    }
    else if (m_NextLine == LineCount && playerPosition.z >= TriggerZ[LineCount])
    {
        // 「通信途絶」：強く乱れ、雑音が途切れる
        m_SignalLost = true;
        m_SignalLostTime = 0.0f;
        m_NextLine = LineCount + 1;
        Stop();
    }

    if (m_SignalLost)
    {
        m_SignalLostTime += deltaTime;
        if (m_SignalLostTime < 1.3f)
        {
            noise = (std::max)(noise, 1.0f - m_SignalLostTime / 1.3f);
        }
    }
    return noise;
}

void ForestRadio::FillView(Hud::RadioView& view) const
{
    view.Time = m_Time;
    view.Header = StoryText::TransmissionHeader;
    view.SignalLost = StoryText::SignalLost;
    view.LineAlpha = 0.0f;
    view.SignalLostAlpha = 0.0f;
    if (m_ActiveLine >= 0)
    {
        const char* line = StoryText::TransmissionLines[m_ActiveLine];
        const float typingSeconds = static_cast<float>(StoryText::Utf8Length(line)) / TypingCharactersPerSecond;
        view.Line = StoryText::Utf8Prefix(line, static_cast<int>(m_LineTime * TypingCharactersPerSecond));
        const float fadeStart = typingSeconds + HoldSeconds;
        view.LineAlpha = std::clamp(1.0f - (m_LineTime - fadeStart) / FadeSeconds, 0.0f, 1.0f) *
            std::clamp(m_LineTime / 0.2f, 0.0f, 1.0f);
        view.Noise = (std::max)(0.0f, 0.55f * (1.0f - m_LineTime / 0.5f));
    }
    if (m_SignalLost && m_SignalLostTime < SignalLostSeconds)
    {
        view.SignalLostAlpha = std::clamp((SignalLostSeconds - m_SignalLostTime) / 0.6f, 0.0f, 1.0f);
        view.Noise = (std::max)(view.Noise, (std::max)(0.0f, 1.0f - m_SignalLostTime / 1.3f));
    }
}
