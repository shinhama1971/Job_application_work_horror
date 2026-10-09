// ============================================================================
// ファイルの役割: クリアした後の評価を表示するリザルト画面を管理している。
// 主な技術: Sceneの継承、成績の集計、2DのUI、入力によるシーンの切り替え
// ============================================================================

#include "ResultScene.h"
#include "Application.h"

#include "Game.h"
#include "Input.h"
#include "Stage2AnomalyPlan.h"

ResultScene::ResultScene()
{
    Init();
}

ResultScene::~ResultScene()
{
    Uninit();
}

// HUDを準備し、2面で出た異変の名前を作り、画面効果をリザルト用の落ち着いた設定にしている
void ResultScene::Init()
{
    m_ResultTimer = 0.0f;
    m_Hud.Init();

    Core::Game* game = Core::Game::GetInstance();
    // 2面の1周目・2周目に出た異変の名前を「・」でつないでいる
    const auto first = static_cast<Stage2Anomaly>(game->GetStage2FirstAnomaly());
    const auto second = static_cast<Stage2Anomaly>(game->GetStage2SecondAnomaly());
    m_Stage2AnomalyText.clear();
    if (first != Stage2Anomaly::None && second != Stage2Anomaly::None)
    {
        m_Stage2AnomalyText = std::string(Stage2AnomalyPlan::GetDisplayName(first)) +
            "・" + Stage2AnomalyPlan::GetDisplayName(second);
    }

    // 光の筋を消し、ノイズや歪みを弱め、少し光らせて画面を明るく始めている
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

// 開いてから0.85秒は入力を受け付けない（クリアした勢いで押したボタンで、すぐ画面が変わらないように）
void ResultScene::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    m_ResultTimer += deltaTime;
    if (m_ResultTimer < 0.85f)
    {
        return;
    }

    // R（コントローラーはX）でもう一度1面から、Enter（AかSTART）でタイトルへ戻っている
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

// 1.1秒かけて項目を浮かび上がらせながら、成績を描いている
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
        game->GetWallWritingsRead(),
        game->IsHiddenRoomEscaped(),
        m_Stage2AnomalyText,
        game->IsLastRunBestTime(),
        game->IsLastRunBestCaught());
}

// HUDを片付け、画面効果の設定を普段の値に戻している
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
