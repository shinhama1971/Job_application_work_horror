// ============================================================================
// ファイルの役割: タイトル画面の入力と表示、ゲーム開始への切り替えを管理している。
// 主な技術: Sceneの継承、2DのUI、入力によるシーンの切り替え
// ============================================================================

#include "TitleScene.h"

#include "Application.h"

#include "Game.h"
#include "Input.h"

#include <algorithm>
#include <cmath>

int TitleScene::s_SelectedStage = 0;

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

// Q（B）でゲームを終える（終了の確認が出る）。←→（A・D、十字キー、左スティック）か1・2のキーで始める面を選び、
// Enter（AかSTART）で選んだ面を始めている。2面から始めたプレイは、クリアしてもベスト記録に残さない（Game::ChangeScene）。
void TitleScene::Update()
{
    m_TitleTime += Application::GetDeltaTime();
    if (Input::GetKeyTrigger(VK_Q) ||
        Input::GetButtonTrigger(XINPUT_B))
    {
        PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
        return;
    }

    // 左スティックは、倒した瞬間に1回だけ動かしている（戻すまで次は動かさない）
    const float stickX = Input::GetLeftAnalogStick().x;
    const bool stickLeft = m_StickReleased && stickX < -0.6f;
    const bool stickRight = m_StickReleased && stickX > 0.6f;
    if (stickLeft || stickRight)
    {
        m_StickReleased = false;
    }
    else if (std::abs(stickX) < 0.3f)
    {
        m_StickReleased = true;
    }

    const int previousStage = s_SelectedStage;
    if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A) ||
        Input::GetButtonTrigger(XINPUT_LEFT) || stickLeft)
    {
        s_SelectedStage = (std::max)(0, s_SelectedStage - 1);
    }
    if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D) ||
        Input::GetButtonTrigger(XINPUT_RIGHT) || stickRight)
    {
        s_SelectedStage = (std::min)(2, s_SelectedStage + 1);
    }
    if (Input::GetKeyTrigger(VK_1))
    {
        s_SelectedStage = 0;
    }
    if (Input::GetKeyTrigger(VK_2))
    {
        s_SelectedStage = 1;
    }
    if (Input::GetKeyTrigger(VK_3))
    {
        s_SelectedStage = 2;
    }
    if (s_SelectedStage != previousStage)
    {
        Input::SetVibration(1, 0.05f);
    }

    if (Input::GetKeyTrigger(VK_RETURN) ||
        Input::GetButtonTrigger(XINPUT_A) ||
        Input::GetButtonTrigger(XINPUT_START))
    {
        // 外の森（試作）は、まだ1面にはつながらない（森の中を歩いて研究所まで行ける。Esc のポーズメニューからタイトルへ戻る）
        constexpr SceneName stages[] = { SceneName::Stage, SceneName::Stage2, SceneName::Forest };
        Core::Game::GetInstance()->RequestSceneChange(stages[s_SelectedStage]);
    }
}

// 題名・操作の説明・ベスト記録・始める面の選択を描いている
void TitleScene::Draw(Camera* camera)
{
    (void)camera;
    Core::Game* game = Core::Game::GetInstance();
    m_Hud.DrawTitle(
        m_TitleTime,
        game->HasClearRecord(),
        game->GetBestClearTimeSeconds(),
        game->GetBestCaughtCount(),
        s_SelectedStage);
}

// HUDを片付けている
void TitleScene::Uninit()
{
    m_Hud.Uninit();
}
