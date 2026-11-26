// ============================================================================
// ファイルの役割: 1面の静かな時間に起こす、小さな恐怖の出来事の進行。
// （説明はStage1FearEvents.hを参照）
// ============================================================================

#include "Stage1FearEvents.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 面を始めてから最初の出来事までの落ち着いた時間と、出来事どうしの間の落ち着いた時間（最低＋ばらつきの幅）。
    // 起こせる出来事がなかったときに、もう一度調べるまでの秒数
    constexpr float FirstWaitSeconds = 40.0f;
    constexpr float WaitBaseSeconds = 35.0f;
    constexpr float WaitRangeSeconds = 25.0f;
    constexpr float RetrySeconds = 3.0f;
    // 間の時間のばらつきを、起こした数から決める乱数（黄金比の小数部分を足していく）
    constexpr float WaitScatterStep = 0.618034f;
    // 怯えが消えるまでの秒数
    constexpr float FrightFadeSeconds = 10.0f;

    // --- 扉の向こうのノック：軽く3回叩いた後、間を置いて、強く速く4回叩く（秒・音量・音の高さ）
    struct KnockBeat
    {
        float Time;
        float Volume;
        float Pitch;
    };
    constexpr std::array<KnockBeat, 7> KnockBeats =
    {{
        { 0.00f, 0.9f, 0.94f },
        { 0.26f, 0.9f, 0.92f },
        { 0.52f, 0.9f, 0.95f },
        { 2.10f, 1.8f, 0.80f },
        { 2.26f, 1.9f, 0.78f },
        { 2.42f, 2.0f, 0.81f },
        { 2.58f, 2.1f, 0.77f },
    }};
    // 強く叩き始めた1回目で怯えさせる量と、最後に叩いてから終わるまでの秒数
    constexpr std::size_t KnockHardBeat = 3;
    constexpr float KnockFright = 0.55f;
    constexpr float KnockTailSeconds = 1.0f;

    // --- 懐中電灯：ちらつく秒数、消えている秒数、点き直してから人影を消すまでの秒数、そのときの怯え
    constexpr float FlashlightSputterSeconds = 1.2f;
    constexpr float FlashlightOutSeconds = 0.7f;
    constexpr float FigureSeconds = 0.45f;
    constexpr float FlashlightFright = 1.0f;

    // --- 行く手の照明：1つずつ消す間隔、全部消えてから戻り始めるまでの秒数、1つずつ点け直す間隔、消し終えたときの怯え
    constexpr float LightOffInterval = 0.7f;
    constexpr float LightsDarkSeconds = 5.0f;
    constexpr float LightOnInterval = 0.45f;
    constexpr float LightsOutFright = 0.5f;

    // --- 別の場所：映る時間（始まりと終わりの秒。まばたきのように短く2回）と、映り始めと消え際のなじませ、怯え
    struct OtherworldWindow
    {
        float Start;
        float End;
    };
    constexpr std::array<OtherworldWindow, 2> OtherworldWindows =
    {{
        { 0.00f, 0.09f },
        { 0.62f, 0.78f },
    }};
    constexpr float OtherworldEdgeSeconds = 0.015f;
    constexpr float OtherworldTailSeconds = 0.6f;
    constexpr float OtherworldFright = 0.8f;
    // 映るたびに変える乱数の種の進め方
    constexpr float OtherworldSeedStep = 0.371f;
}

void Stage1FearEvents::Reset()
{
    m_Active = Kind::Count;
    m_Elapsed = 0.0f;
    m_Step = 0;
    m_Next = Kind::Knock;
    m_Wait = FirstWaitSeconds;
    m_EventCount = 0;
    m_Fright = 0.0f;
    m_Lights.clear();
    m_LightsOff.clear();
    m_OtherworldSeed = 0.0f;
}

void Stage1FearEvents::Update(const Input& input, std::vector<Cue>& cues)
{
    m_Fright = (std::max)(0.0f, m_Fright - input.DeltaTime / FrightFadeSeconds);

    if (IsActive())
    {
        m_Elapsed += input.DeltaTime;
        switch (m_Active)
        {
        case Kind::Knock:
            UpdateKnock(cues);
            break;
        case Kind::Flashlight:
            UpdateFlashlight(cues);
            break;
        case Kind::LightsOut:
            UpdateLightsOut(cues);
            break;
        case Kind::Otherworld:
            UpdateOtherworld(cues);
            break;
        default:
            break;
        }
        return;
    }

    // 落ち着いた時間だけを数えている（ほかの演出の最中や、怯えが残っている間は進めない）
    if (!input.Calm || m_Fright > 0.0f)
    {
        return;
    }
    m_Wait -= input.DeltaTime;
    if (m_Wait > 0.0f)
    {
        return;
    }

    // 次の番の出来事から順に、今起こせるものを探している
    const int count = static_cast<int>(Kind::Count);
    for (int offset = 0; offset < count; ++offset)
    {
        const Kind kind = static_cast<Kind>((static_cast<int>(m_Next) + offset) % count);
        if (Start(kind, input, cues))
        {
            m_Next = static_cast<Kind>((static_cast<int>(kind) + 1) % count);
            const float scatter = std::fmod(static_cast<float>(m_EventCount) * WaitScatterStep, 1.0f);
            m_Wait = WaitBaseSeconds + WaitRangeSeconds * scatter;
            return;
        }
    }
    m_Wait = RetrySeconds;
}

bool Stage1FearEvents::Start(Kind kind, const Input& input, std::vector<Cue>& cues)
{
    if (IsActive() || !CanStart(kind, input))
    {
        return false;
    }
    m_Active = kind;
    m_Elapsed = 0.0f;
    m_Step = 0;
    ++m_EventCount;
    switch (kind)
    {
    case Kind::Knock:
        m_KnockPosition = input.KnockPosition;
        UpdateKnock(cues);
        break;
    case Kind::Flashlight:
        UpdateFlashlight(cues);
        break;
    case Kind::LightsOut:
        m_Lights = input.LightsAhead;
        m_LightsOff.clear();
        UpdateLightsOut(cues);
        break;
    case Kind::Otherworld:
        UpdateOtherworld(cues);
        break;
    default:
        break;
    }
    return true;
}

float Stage1FearEvents::GetOtherworld() const
{
    if (m_Active != Kind::Otherworld)
    {
        return 0.0f;
    }
    // 窓の中だけ映し、始まりと終わりはごく短くなじませている（1フレームで切り替わるちらつきを和らげる）
    float strength = 0.0f;
    for (const OtherworldWindow& window : OtherworldWindows)
    {
        const float rise = (std::clamp)((m_Elapsed - window.Start) / OtherworldEdgeSeconds + 1.0f, 0.0f, 1.0f);
        const float fall = (std::clamp)((window.End - m_Elapsed) / OtherworldEdgeSeconds, 0.0f, 1.0f);
        strength = (std::max)(strength, (std::min)(rise, fall));
    }
    return strength;
}

bool Stage1FearEvents::CanStart(Kind kind, const Input& input) const
{
    switch (kind)
    {
    case Kind::Knock:
        return input.KnockAvailable;
    case Kind::Flashlight:
        return input.FlashlightOn;
    case Kind::LightsOut:
        return !input.LightsAhead.empty();
    case Kind::Otherworld:
        return true;
    default:
        return false;
    }
}

// 決めた秒になったノックを鳴らし、強く叩き始めたら怯えさせている
void Stage1FearEvents::UpdateKnock(std::vector<Cue>& cues)
{
    while (m_Step < static_cast<int>(KnockBeats.size()) && m_Elapsed >= KnockBeats[static_cast<std::size_t>(m_Step)].Time)
    {
        const KnockBeat& beat = KnockBeats[static_cast<std::size_t>(m_Step)];
        Cue cue;
        cue.Type = CueType::Knock;
        cue.Position = m_KnockPosition;
        cue.Pitch = beat.Pitch;
        cue.Volume = beat.Volume;
        cues.push_back(cue);
        if (static_cast<std::size_t>(m_Step) == KnockHardBeat)
        {
            Frighten(KnockFright);
        }
        ++m_Step;
    }
    if (m_Elapsed >= KnockBeats.back().Time + KnockTailSeconds)
    {
        Finish();
    }
}

// ちらつき → 消える → 点き直すと目の前に人影 → 人影が消える、の順に合図を出している
void Stage1FearEvents::UpdateFlashlight(std::vector<Cue>& cues)
{
    const float times[] =
    {
        0.0f,
        FlashlightSputterSeconds,
        FlashlightSputterSeconds + FlashlightOutSeconds,
        FlashlightSputterSeconds + FlashlightOutSeconds + FigureSeconds,
    };
    const CueType types[] =
    {
        CueType::FlashlightSputter,
        CueType::FlashlightOut,
        CueType::FlashlightBack,
        CueType::FigureHide,
    };
    constexpr int StepCount = static_cast<int>(sizeof(times) / sizeof(times[0]));
    while (m_Step < StepCount && m_Elapsed >= times[m_Step])
    {
        Cue cue;
        cue.Type = types[m_Step];
        cue.Seconds = FlashlightSputterSeconds;
        cues.push_back(cue);
        if (cue.Type == CueType::FlashlightBack)
        {
            Frighten(FlashlightFright);
        }
        ++m_Step;
    }
    if (m_Step >= StepCount)
    {
        Finish();
    }
}

// 奥の照明から1つずつ消し、暗いまましばらく待ってから、手前から1つずつ点け直している
void Stage1FearEvents::UpdateLightsOut(std::vector<Cue>& cues)
{
    const int lightCount = static_cast<int>(m_Lights.size());
    const float darkStart = static_cast<float>(lightCount - 1) * LightOffInterval;
    while (m_Step < lightCount && m_Elapsed >= static_cast<float>(m_Step) * LightOffInterval)
    {
        Cue cue;
        cue.Type = CueType::LightOff;
        cue.LightNumber = m_Lights[static_cast<std::size_t>(m_Step)];
        cues.push_back(cue);
        m_LightsOff.push_back(cue.LightNumber);
        if (m_Step == lightCount - 1)
        {
            Frighten(LightsOutFright);
        }
        ++m_Step;
    }
    while (m_Step >= lightCount && m_Step < lightCount * 2)
    {
        const int restoreIndex = m_Step - lightCount;
        if (m_Elapsed < darkStart + LightsDarkSeconds + static_cast<float>(restoreIndex) * LightOnInterval)
        {
            break;
        }
        // 手前（消したのが最後の照明）から点け直している
        Cue cue;
        cue.Type = CueType::LightOn;
        cue.LightNumber = m_Lights[static_cast<std::size_t>(lightCount - 1 - restoreIndex)];
        cues.push_back(cue);
        std::erase(m_LightsOff, cue.LightNumber);
        ++m_Step;
    }
    if (m_Step >= lightCount * 2)
    {
        Finish();
    }
}

// 映り始めるたびに合図を出し、乱数の種を変えている
void Stage1FearEvents::UpdateOtherworld(std::vector<Cue>& cues)
{
    while (m_Step < static_cast<int>(OtherworldWindows.size()) &&
        m_Elapsed >= OtherworldWindows[static_cast<std::size_t>(m_Step)].Start)
    {
        m_OtherworldSeed = std::fmod(m_OtherworldSeed + OtherworldSeedStep, 1.0f);
        Cue cue;
        cue.Type = CueType::OtherworldFlash;
        cues.push_back(cue);
        Frighten(OtherworldFright);
        ++m_Step;
    }
    if (m_Elapsed >= OtherworldWindows.back().End + OtherworldTailSeconds)
    {
        Finish();
    }
}

void Stage1FearEvents::Finish()
{
    m_Active = Kind::Count;
    m_Elapsed = 0.0f;
    m_Step = 0;
    m_Lights.clear();
}

void Stage1FearEvents::Frighten(float amount)
{
    m_Fright = (std::max)(m_Fright, amount);
}
