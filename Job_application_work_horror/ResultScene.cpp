// ============================================================================
// ファイルの役割: ゲーム終了後の評価とリザルト画面を管理します。
// ============================================================================

#include "ResultScene.h"

#include "Game.h"
#include "Input.h"

ResultScene::ResultScene()
{
    Init();
}

ResultScene::~ResultScene()
{
    Uninit();
}

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

void ResultScene::Update()
{
    constexpr float deltaTime = 1.0f / 60.0f;
    m_ResultTimer += deltaTime;
    if (m_ResultTimer < 0.85f)
    {
        return;
    }

    if (Input::GetKeyTrigger('R') ||
        Input::GetButtonTrigger(XINPUT_X))
    {
        Core::Game::GetInstance()->RequestSceneChange(SceneName::Stage);
    }
    else if (Input::GetKeyTrigger(VK_RETURN) ||
        Input::GetButtonTrigger(XINPUT_A) ||
        Input::GetButtonTrigger(XINPUT_START))
    {
        Core::Game::GetInstance()->RequestSceneChange(SceneName::Title);
    }
}

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
