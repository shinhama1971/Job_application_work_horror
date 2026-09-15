// ============================================================================
// ファイルの役割: 出口での操作判定と、安全なシーン遷移要求を管理します。
// 主な技術: トリガー領域、状態条件、遅延シーン遷移
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "ExitTrigger.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "ScreenDustOverlay.h"

// 処理内容: 必要な状態とGPU・音声リソースを初期化します。
void ExitTrigger::Init()
{
    m_IsEscaping = false;
    m_EscapeTimer = 0.0f;
    m_EscapePhase = 0;
    m_NextScene = SceneName::Result;
    m_InteractionEnabled = true;
}

// 処理内容: 経過時間と入力を使い、このフレームの状態を更新します。
void ExitTrigger::Update()
{
    if (!m_IsEscaping)
    {
        return;
    }

    constexpr float deltaTime = 1.0f / 60.0f;
    m_EscapeTimer += deltaTime;

    Core::Game* game = Core::Game::GetInstance();
    if (m_EscapePhase == 0 && m_EscapeTimer >= 0.28f)
    {
        game->GetPostProcess()->TriggerBloomPulse(0.92f, 0.52f);
        Input::SetVibration(7, 0.13f);
        m_EscapePhase = 1;
    }
    else if (m_EscapePhase == 1 && m_EscapeTimer >= 0.86f)
    {
        game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.38f);
        Input::SetVibration(10, 0.22f);

        ScreenDustOverlay* crt =
            game->GetObj<ScreenDustOverlay>("CRTNoise");
        if (crt != nullptr)
        {
            crt->SetPower(0.88f);
            crt->SetActive(true);
            crt->SetTimer(0.72f);
        }
        m_EscapePhase = 2;
    }
    else if (m_EscapePhase == 2 && m_EscapeTimer >= 1.48f)
    {
        game->GetPostProcess()->TriggerBloomPulse(2.15f, 0.75f);
        Input::SetVibration(16, 0.30f);
        m_EscapePhase = 3;
    }
    else if (m_EscapePhase == 3 && m_EscapeTimer >= 2.12f)
    {
        m_EscapePhase = 4;
        game->RequestSceneChange(m_NextScene);
    }
}

// 処理内容: ExitTriggerの「Interact」処理を担当します。
void ExitTrigger::Interact(Player& player)
{
    if (m_InteractionEnabled)
    {
        BeginEscape(player);
    }
}

// 処理内容: 処理区間を開始し、必要な状態を設定します。
void ExitTrigger::BeginEscape(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    if (!game->IsPowerRestored() || m_IsEscaping)
    {
        return;
    }

    m_IsEscaping = true;
    m_EscapeTimer = 0.0f;
    m_EscapePhase = 0;
    player.SetCanControl(false);
    game->GetPostProcess()->TriggerHorrorPulse(0.18f, 0.25f);
    Input::SetVibration(6, 0.12f);
}

// 処理内容: 保持している値または参照を取得します。
const char* ExitTrigger::GetInteractionPrompt() const
{
    if (!m_InteractionEnabled || m_IsEscaping)
    {
        return "";
    }

    return Core::Game::GetInstance()->IsPowerRestored()
        ? "出口から移動する"
        : "電力が必要";
}

// 処理内容: 現在の状態に対応する描画命令を発行します。
void ExitTrigger::Draw(Camera* cam)
{
    //これで終了しますか？
}

void ExitTrigger::Uninit()
{}
