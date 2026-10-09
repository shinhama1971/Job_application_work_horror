// ============================================================================
// ファイルの役割: 出口を調べたときの判定と、演出の後に次のシーンへ移る要求を管理している。
// 主な技術: 調べる操作、電力の状態による条件、時間をずらしたシーンの切り替え
// ============================================================================

#include "ExitTrigger.h"
#include "Application.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "ScreenDustOverlay.h"

// 脱出していない状態から始め、次のシーンはリザルト画面にしている
void ExitTrigger::Init()
{
    m_IsEscaping = false;
    m_EscapeTimer = 0.0f;
    m_EscapePhase = 0;
    m_NextScene = SceneName::Result;
    m_InteractionEnabled = true;
}

// 脱出の演出を時間で進めている：光・画面のノイズ・振動を段階的に強め、2.12秒で次のシーンへ移っている
void ExitTrigger::Update()
{
    if (!m_IsEscaping)
    {
        return;
    }

    const float deltaTime = Application::GetDeltaTime();
    m_EscapeTimer += deltaTime;

    Core::Game* game = Core::Game::GetInstance();
    // 0.28秒：画面を明るく光らせ、振動させている
    if (m_EscapePhase == 0 && m_EscapeTimer >= 0.28f)
    {
        game->GetPostProcess()->TriggerBloomPulse(0.92f, 0.52f);
        Input::SetVibration(7, 0.13f);
        m_EscapePhase = 1;
    }
    // 0.86秒：画面を乱し、ブラウン管のようなノイズを0.72秒出している
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
    // 1.48秒：最も強く光らせ、強く振動させている
    else if (m_EscapePhase == 2 && m_EscapeTimer >= 1.48f)
    {
        game->GetPostProcess()->TriggerBloomPulse(2.15f, 0.75f);
        Input::SetVibration(16, 0.30f);
        m_EscapePhase = 3;
    }
    // 2.12秒：次のシーンへの切り替えを要求している（実際の切り替えはフレームの終わりに安全に行われる）
    else if (m_EscapePhase == 3 && m_EscapeTimer >= 2.12f)
    {
        m_EscapePhase = 4;
        game->RequestSceneChange(m_NextScene);
    }
}

// 調べたとき、出口が使える状態なら脱出を始めている
void ExitTrigger::Interact(Player& player)
{
    if (m_InteractionEnabled)
    {
        BeginEscape(player);
    }
}

// 電力が戻っていれば脱出を始めている。プレイヤーは操作できなくしている
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

// 調べるときに表示する文章（電力が戻っていなければ「電力が必要」）
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

void ExitTrigger::Draw(Camera* /*camera*/)
{
    // 出口自体には見た目がないので、何も描いていない
}

// 解放するものはない
void ExitTrigger::Uninit()
{}
