#include "ExitTrigger.h"
#include "Game.h"
#include "Player.h"

void ExitTrigger::Init()
{}

void ExitTrigger::Update()
{}

void ExitTrigger::Interact(Player& player)
{
    (void)player;
    Core::Game::GetInstance()->RequestSceneChange(SceneName::Result);
}

void ExitTrigger::Draw(Camera* cam)
{
    // 莉翫・菴輔ｂ謠冗判縺励↑縺・
}

void ExitTrigger::Uninit()
{}
