#include "ExitTrigger.h"
#include "Game.h"
#include "Player.h"

using namespace DirectX::SimpleMath;

void ExitTrigger::Init()
{}

void ExitTrigger::Update()
{
    std::vector<Player*> players =
        Core::Game::GetInstance()->GetObjects<Player>();

    if (players.empty()) return;

    Player* player = players[0];

    Vector3 diff =
        player->GetPosition() - m_Position;

    float distance = diff.Length();

    if (distance <= m_Radius)
    {
        Core::Game::GetInstance()->RequestSceneChange(RESULT);
    }
}

void ExitTrigger::Draw(Camera* cam)
{
    // 莉翫・菴輔ｂ謠冗判縺励↑縺・
}

void ExitTrigger::Uninit()
{}
