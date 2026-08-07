#include "Game.h"
#include "ScareTrigger.h"
#include "Input.h"
#include "Player.h"
#include "ScreenDustOverlay.h"
#include "ShadowMan.h"

using namespace DirectX::SimpleMath;

void ScareTrigger::Init()
{
}

void ScareTrigger::Update()
{
    Core::Game* game = Core::Game::GetInstance();
    if (m_HasTriggered ||
        (m_RequiresPower && !game->IsPowerRestored()) ||
        !IsPlayerInside())
    {
        return;
    }

    m_HasTriggered = true;
    const Vector3 shadowPosition = m_ShadowPosition;
    game->RequestAddObject<ShadowMan>(
        [shadowPosition](ShadowMan& shadow)
        {
            shadow.SetPosition(
                shadowPosition.x,
                shadowPosition.y,
                shadowPosition.z);
        });

    Input::SetVibration(14, 0.34f);
    game->GetPostProcess()->TriggerHorrorPulse(1.0f, 0.65f);

    ScreenDustOverlay* crt =
        game->GetObj<ScreenDustOverlay>("CRTNoise");
    if (crt != nullptr)
    {
        crt->SetPower(0.82f);
        crt->SetActive(true);
        crt->SetTimer(0.55f);
    }
}

void ScareTrigger::Draw(Camera* camera)
{
    (void)camera;
}

void ScareTrigger::Uninit()
{
}

bool ScareTrigger::IsPlayerInside() const
{
    const std::vector<Player*> players =
        Core::Game::GetInstance()->GetObjects<Player>();
    if (players.empty() || players.front() == nullptr)
    {
        return false;
    }

    const Vector3 playerPosition = players.front()->GetPosition();
    return
        playerPosition.x >= m_Position.x - m_Size.x * 0.5f &&
        playerPosition.x <= m_Position.x + m_Size.x * 0.5f &&
        playerPosition.y >= m_Position.y - m_Size.y * 0.5f &&
        playerPosition.y <= m_Position.y + m_Size.y * 0.5f &&
        playerPosition.z >= m_Position.z - m_Size.z * 0.5f &&
        playerPosition.z <= m_Position.z + m_Size.z * 0.5f;
}
