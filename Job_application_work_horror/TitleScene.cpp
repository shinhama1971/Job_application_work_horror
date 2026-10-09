// ============================================================================
// ファイルの役割: タイトル画面の入力と表示、ゲーム開始への切り替えを管理している。
// 主な技術: Sceneの継承、2DのUI、入力によるシーンの切り替え
// ============================================================================

#include "TitleScene.h"

#include "Application.h"

#include "Game.h"
#include "Input.h"

// 作るときに初期化している
TitleScene::TitleScene()
{
    Init();
}

// 壊すときに後片付けをしている
TitleScene::~TitleScene()
{
    Uninit();
}

// 経過時間を0にし、HUDを準備している
void TitleScene::Init()
{
    m_TitleTime = 0.0f;
    m_Hud.Init();
}

// Q（B）でゲームを終える（終了の確認が出る）、Enter（AかSTART）で1面を始めている
void TitleScene::Update()
{
    m_TitleTime += Application::GetDeltaTime();
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

// 題名・操作の説明・ベスト記録を描いている
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

// HUDを片付けている
void TitleScene::Uninit()
{
    m_Hud.Uninit();
}
