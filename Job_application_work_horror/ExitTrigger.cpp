#include "ExitTrigger.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "ScreenDustOverlay.h"

void ExitTrigger::Init()
{
    m_IsEscaping = false;
    m_EscapeTimer = 0.0f;
    m_EscapePhase = 0;
    m_NextScene = SceneName::Result;
    m_InteractionEnabled = true;
}

void ExitTrigger::Update()
{
    if (!m_IsEscaping)
    {
        return;
    }

    constexpr float deltaTime = 1.0f / 60.0f;
    m_EscapeTimer += deltaTime;

    Core::Game* game = Core::Game::GetInstance();
    if (m_EscapePhase == 0 && m_EscapeTimer >= 0.28f)
    {
        game->GetPostProcess()->TriggerBloomPulse(0.92f, 0.52f);
        Input::SetVibration(7, 0.13f);
        m_EscapePhase = 1;
    }
    else if (m_EscapePhase == 1 && m_EscapeTimer >= 0.86f)
    {
        game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.38f);
        Input::SetVibration(10, 0.22f);

        ScreenDustOverlay* crt =
            game->GetObj<ScreenDustOverlay>("CRTNoise");
        if (crt != nullptr)
        {
            crt->SetPower(0.88f);
            crt->SetActive(true);
            crt->SetTimer(0.72f);
        }
        m_EscapePhase = 2;
    }
    else if (m_EscapePhase == 2 && m_EscapeTimer >= 1.48f)
    {
        game->GetPostProcess()->TriggerBloomPulse(2.15f, 0.75f);
        Input::SetVibration(16, 0.30f);
        m_EscapePhase = 3;
    }
    else if (m_EscapePhase == 3 && m_EscapeTimer >= 2.12f)
    {
        m_EscapePhase = 4;
        game->RequestSceneChange(m_NextScene);
    }
}

void ExitTrigger::Interact(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    if (m_InteractionEnabled && game->IsPowerRestored() && !m_IsEscaping)
    {
        m_IsEscaping = true;
        m_EscapeTimer = 0.0f;
        m_EscapePhase = 0;
        player.SetCanControl(false);
        game->GetPostProcess()->TriggerHorrorPulse(0.18f, 0.25f);
        Input::SetVibration(6, 0.12f);
    }
}

const char* ExitTrigger::GetInteractionPrompt() const
{
    if (!m_InteractionEnabled || m_IsEscaping)
    {
        return "";
    }

    return Core::Game::GetInstance()->IsPowerRestored()
        ? "Leave facility"
        : "Exit has no power";
}

void ExitTrigger::Draw(Camera* cam)
{
    // 莉翫・菴輔ｂ謠冗判縺励↑縺・
}

void ExitTrigger::Uninit()
{}
