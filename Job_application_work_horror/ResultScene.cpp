// ============================================================================
// ファイルの役割: ゲーム終了後の評価とリザルト画面を管理します。
// 主な技術: Scene継承、記録集計、2D UI、入力遷移
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "ResultScene.h"

#include "Game.h"
#include "Input.h"

// 処理内容: ResultSceneを生成し、初期状態を準備します。
ResultScene::ResultScene()
{
    Init();
}

// 処理内容: ResultSceneが所有する処理とリソースを終了します。
ResultScene::~ResultScene()
{
    Uninit();
}

// 処理内容: 必要な状態とGPU・音声リソースを初期化します。
void ResultScene::Init()
{
    m_ResultTimer = 0.0f;
    m_Hud.Init();

    Effect::PostProcess* postProcess =
        Core::Game::GetInstance()->GetPostProcess();
    postProcess->SetCorridorTension(0.0f);
    postProcess->SetVolumetricLight(false);
    postProcess->SetVolumetricIntensity(0.0f);
    postProcess->SetExposure(0.94f);
    postProcess->SetAtmosphere(0.06f, 0.58f);
    postProcess->SetLensDistortionStrength(0.08f);
    postProcess->SetFilmGradeStrength(0.62f);
    postProcess->SetLensDirtStrength(0.08f);
    postProcess->TriggerBloomPulse(0.72f, 0.90f);
}

// 処理内容: 経過時間と入力を使い、このフレームの状態を更新します。
void ResultScene::Update()
{
    constexpr float deltaTime = 1.0f / 60.0f;
    m_ResultTimer += deltaTime;
    if (m_ResultTimer < 0.85f)
    {
        return;
    }

    if (Input::GetKeyTrigger('R') ||
        // 処理内容: 保持している値または参照を取得します。
        Input::GetButtonTrigger(XINPUT_X))
    {
        Core::Game::GetInstance()->RequestSceneChange(SceneName::Stage);
    }
    else if (Input::GetKeyTrigger(VK_RETURN) ||
        // 処理内容: 保持している値または参照を取得します。
        Input::GetButtonTrigger(XINPUT_A) ||
        Input::GetButtonTrigger(XINPUT_START))
    {
        Core::Game::GetInstance()->RequestSceneChange(SceneName::Title);
    }
}

// 処理内容: 現在の状態に対応する描画命令を発行します。
void ResultScene::Draw(Camera* camera)
{
    (void)camera;
    const float reveal = m_ResultTimer < 1.10f
        ? m_ResultTimer / 1.10f
        : 1.0f;
    Core::Game* game = Core::Game::GetInstance();
    m_Hud.DrawResult(
        reveal,
        game->GetLastClearTimeSeconds(),
        game->GetCaughtCount(),
        game->GetAnomaliesHandled(),
        game->GetPuzzleMistakes(),
        game->GetChargersUsed(),
        game->GetEvidenceCollected(),
        game->IsLastRunBestTime(),
        game->IsLastRunBestCaught());
}

// 処理内容: 所有するリソースを依存関係の逆順で解放します。
void ResultScene::Uninit()
{
    m_Hud.Uninit();

    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }

    Effect::PostProcess* postProcess = game->GetPostProcess();
    postProcess->SetAtmosphere(0.18f, 0.55f);
    postProcess->SetExposure(1.0f);
    postProcess->SetLensDistortionStrength(0.20f);
    postProcess->SetFilmGradeStrength(0.55f);
    postProcess->SetLensDirtStrength(0.10f);
}
