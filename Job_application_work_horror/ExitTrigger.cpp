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
    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        game->RequestSceneChange(SceneName::Result);
    }
}

const char* ExitTrigger::GetInteractionPrompt() const
{
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
