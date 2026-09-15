// ============================================================================
// ファイルの役割: タイトル画面の入力、表示、ゲーム開始への遷移を管理します。
// 主な技術: Scene継承、2D UI、入力フォーカス、シーン遷移
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "TitleScene.h"

#include "Application.h"
#include "Game.h"
#include "Input.h"

// 処理内容: TitleSceneを生成し、初期状態を準備します。
TitleScene::TitleScene()
{
    Init();
}

// 処理内容: TitleSceneが所有する処理とリソースを終了します。
TitleScene::~TitleScene()
{
    Uninit();
}

// 処理内容: 必要な状態とGPU・音声リソースを初期化します。
void TitleScene::Init()
{
    m_TitleTime = 0.0f;
    m_Hud.Init();
}

// 処理内容: 経過時間と入力を使い、このフレームの状態を更新します。
void TitleScene::Update()
{
    m_TitleTime += 1.0f / 60.0f;
    if (Input::GetKeyTrigger(VK_Q) ||
        // 処理内容: 保持している値または参照を取得します。
        Input::GetButtonTrigger(XINPUT_B))
    {
        PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
        return;
    }

    if (Input::GetKeyTrigger(VK_RETURN) ||
        // 処理内容: 保持している値または参照を取得します。
        Input::GetButtonTrigger(XINPUT_A) ||
        Input::GetButtonTrigger(XINPUT_START))
    {
        Core::Game::GetInstance()->RequestSceneChange(SceneName::Stage);
    }
}

// 処理内容: 現在の状態に対応する描画命令を発行します。
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

// 処理内容: 所有するリソースを依存関係の逆順で解放します。
void TitleScene::Uninit()
{
    m_Hud.Uninit();
}
