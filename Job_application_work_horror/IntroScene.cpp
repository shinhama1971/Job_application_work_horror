// ============================================================================
// ファイルの役割: 1面の前の導入（説明は IntroScene.h を参照）。
// ============================================================================

#include "IntroScene.h"

#include "Application.h"
#include "Game.h"
#include "Input.h"
#include "PostProcess.h"
#include "StoryText.h"

#include <algorithm>
#include <string>
#include <iterator>
#include <vector>

using namespace DirectX::SimpleMath;

namespace
{
    using StoryText::TransmissionHeader;
    using StoryText::TransmissionLines;
    using StoryText::SignalLost;
    using StoryText::MissionLead;
    using StoryText::MissionGoal;
    using StoryText::Utf8Prefix;

    // 研究棠の正面へ、ゆっくり近づくカメラ（森の研究所。z = 140 を越えると、2階の窓の明かりが消える）
    const std::vector<ForestBackdrop::Shot> IntroShots =
    {
        { Vector3(0.0f, -99.0f, 95.0f), Vector3(0.0f, -99.0f, 165.0f), 0.0f, 0.0f, 0.03f, 0.07f, 45.0f },
    };
    float Fade(float time, float start, float length)
    {
        return std::clamp((time - start) / length, 0.0f, 1.0f);
    }
}

IntroScene::IntroScene(bool startAtMission)
    : m_StartAtMission(startAtMission),
      m_ShowingMission(startAtMission),
      m_StaticPlaying(!startAtMission)
{
    m_Hud.Init();
    if (!m_StartAtMission)
    {
        m_Backdrop.Init(IntroShots, false);
    }
}

IntroScene::~IntroScene()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game != nullptr && m_StaticPlaying)
    {
        game->StopAudioCue(SOUND_CUE_RADIO_STATIC);
    }
    m_Hud.Uninit();
    if (!m_StartAtMission)
    {
        m_Backdrop.Uninit();
    }
}

void IntroScene::ShowMission()
{
    if (m_ShowingMission)
    {
        return;
    }
    m_ShowingMission = true;
    m_MissionTimer = 0.0f;
    if (m_StaticPlaying)
    {
        Core::Game::GetInstance()->StopAudioCue(SOUND_CUE_RADIO_STATIC);
        m_StaticPlaying = false;
    }
}

float IntroScene::GetNoise() const
{
    if (m_StartAtMission)
    {
        return 0.0f;
    }
    if (m_ShowingMission)
    {
        // 目的の画面に移った瞬間だけ、少し乱している
        return (std::max)(0.0f, 0.5f - m_MissionTimer / 0.5f * 0.5f);
    }
    // ふだんも、かすかに乱れている（遠くの無線を受けているように）
    float noise = 0.06f;
    // 行が始まる瞬間に、電波が乱れる
    for (float start : LineStartTimes)
    {
        const float since = m_Time - (start - 0.3f);
        if (since >= 0.0f && since < 0.5f)
        {
            noise = (std::max)(noise, 0.55f * (1.0f - since / 0.5f));
        }
    }
    // 「通信途絶」で、強く乱れる
    const float sinceLost = m_Time - SignalLostTime;
    if (sinceLost >= 0.0f && sinceLost < 1.3f)
    {
        noise = (std::max)(noise, 1.0f - sinceLost / 1.3f);
    }
    return noise;
}

void IntroScene::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    m_Time += deltaTime;
    if (m_ShowingMission)
    {
        m_MissionTimer += deltaTime;
    }
    m_Backdrop.Update(deltaTime);

    // 「通信途絶」か、受信記録を飛ばした時点で無線の雑音が途切れる。
    if (m_StaticPlaying && (m_ShowingMission || m_Time >= SignalLostTime))
    {
        Core::Game::GetInstance()->StopAudioCue(SOUND_CUE_RADIO_STATIC);
        m_StaticPlaying = false;
    }
    if (!m_ShowingMission && m_Time >= MissionTime)
    {
        ShowMission();
    }

    const float noise = GetNoise();
    Effect::PostProcess* postProcess = Core::Game::GetInstance()->GetPostProcess();
    postProcess->SetSignalInterference(noise * 0.8f);
    postProcess->SetPresenceStatic(noise * 0.5f);

    // Enter（A・START）：受信記録の途中なら目的の画面へ飛ばし、目的の画面なら1面を始める（押し間違えないよう、少し待ってから）
    if (Input::GetKeyTrigger(VK_RETURN) || Input::GetKeyTrigger(VK_SPACE) ||
        Input::GetButtonTrigger(XINPUT_A) || Input::GetButtonTrigger(XINPUT_START))
    {
        if (!m_ShowingMission)
        {
            ShowMission();
        }
        else if (m_MissionTimer > 0.6f)
        {
            Core::Game::GetInstance()->RequestSceneChange(SceneName::Stage);
        }
    }
}

void IntroScene::Draw(Camera* camera)
{
    (void)camera;
    Hud::IntroView view;
    view.Time = m_Time;
    view.Noise = GetNoise();
    view.HideBackdrop = m_StartAtMission;
    view.Header = TransmissionHeader;
    view.HeaderAlpha = m_ShowingMission ? 0.0f : Fade(m_Time, HeaderTime, 0.6f);
    static_assert(std::size(TransmissionLines) == std::size(LineStartTimes), "LineStartTimes must match StoryText");
    for (std::size_t index = 0; index < std::size(TransmissionLines); ++index)
    {
        const float typed = (m_Time - LineStartTimes[index]) * TypingCharactersPerSecond;
        view.Lines.push_back(typed > 0.0f ? Utf8Prefix(TransmissionLines[index], static_cast<int>(typed)) : std::string());
    }
    view.LinesAlpha = m_ShowingMission ? 0.0f : 1.0f;
    view.SignalLost = SignalLost;
    view.SignalLostAlpha = m_ShowingMission ? 0.0f : Fade(m_Time, SignalLostTime, 0.15f);
    view.MissionLead = MissionLead;
    view.MissionGoal = MissionGoal;
    view.MissionAlpha = m_ShowingMission ? Fade(m_MissionTimer, 0.0f, 0.5f) : 0.0f;
    m_Hud.DrawIntro(view);
    // ポーズ中はポーズメニューを重ねている（受信記録の途中でも、ESC でゲームを終えたりタイトルへ戻ったりできるように）
    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->IsGuideEnabled(),
            game->GetResolutionLevel(),
            game->GetPauseSettingIndex(),
            0,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
}

namespace
{
    constexpr const wchar_t* IntroStoryText[] =
    {
        L"受信記録を確認。",
        L"対象地点は森の中。",
        L"玄関から建物へ入る。",
        L"通信を終了する。"
    };

    static_assert(
        std::size(IntroStoryText) == 4,
        "StoryText must contain four lines");
}

static constexpr float LineStartTimes[4] =
{
    2.2f,
    8.4f,
    13.8f,
    18.6f
};

static_assert(
    std::size(LineStartTimes) == std::size(IntroStoryText),
    "LineStartTimes must match StoryText");
