// ============================================================================
// ファイルの役割: タイトル画面の入力、表示、ゲーム開始への遷移を管理します。
// ============================================================================

#include "TitleScene.h"

#include "Application.h"
#include "Game.h"
#include "Input.h"

TitleScene::TitleScene()
{
    Init();
}

TitleScene::~TitleScene()
{
    Uninit();
}

void TitleScene::Init()
{
    m_TitleTime = 0.0f;
    m_Hud.Init();
}

void TitleScene::Update()
{
    m_TitleTime += 1.0f / 60.0f;
    if (Input::GetKeyTrigger(VK_Q) ||
        Input::GetButtonTrigger(XINPUT_B))
    {
        PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
        return;
    }

    if (Input::GetKeyTrigger(VK_RETURN) ||
        Input::GetButtonTrigger(XINPUT_A) ||
        Input::GetButtonTrigger(XINPUT_START))
    {
        Core::Game::GetInstance()->RequestSceneChange(SceneName::Stage);
    }
}

void TitleScene::Draw(Camera* camera)
{
    (void)camera;
    Core::Game* game = Core::Game::GetInstance();
    m_Hud.DrawTitle(
        m_TitleTime,
        game->HasClearRecord(),
        game->GetBestClearTimeSeconds(),
        game->GetBestCaughtCount());
}

void TitleScene::Uninit()
{
    m_Hud.Uninit();
}
