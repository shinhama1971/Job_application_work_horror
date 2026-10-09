// ============================================================================
// ファイルの役割: 1面の暗証番号の扉（入力画面の操作、正解・不正解の反応、手がかりの数字の配置）を管理している。
// 主な技術: 入力画面を開いている間だけ操作を受け取る仕組み、状態だけを持つクラス（KeypadLock）への委任、ランダムな番号と手がかりの連動
// ============================================================================

#include "Stage1KeypadDoor.h"

#include "Door.h"
#include "FlashlightWriting.h"
#include "FuseBox.h"
#include "Game.h"
#include "Hud.h"
#include "Input.h"
#include "Player.h"

#include <string>

// 番号をランダムに決め、壁の手がかりの数字をその番号の画像にしている
void Stage1KeypadDoor::Init(const Parts& parts)
{
    m_Parts = parts;
    m_InputOpen = false;
    m_InputDelayTimer = 0.0f;
    m_WrongTimer = 0.0f;

    // 0は「まだ入力していない桁」と見分けにくいため、1〜9から選んでいる。
    std::uniform_int_distribution<int> digitDistribution(1, 9);
    KeypadLock::Digits code{};
    for (int& digit : code)
    {
        digit = digitDistribution(m_Random);
    }
    m_Lock.SetCode(code);
#ifdef _DEBUG
    // 動作確認用に、Visual Studioの出力ウィンドウへ正解の番号を出している（Debug構成のみ）。
    const std::string message = "[Stage1KeypadDoor] code = " +
        std::to_string(code[0]) + std::to_string(code[1]) +
        std::to_string(code[2]) + std::to_string(code[3]) + "\n";
    OutputDebugStringA(message.c_str());
#endif

    // 各桁の手がかりの文字を、その数字の画像（writing_digit1〜9.png）に差し替えている
    for (int index = 0; index < KeypadLock::DigitCount; ++index)
    {
        FlashlightWriting* writing = m_Parts.digitWritings[static_cast<std::size_t>(index)];
        if (writing != nullptr)
        {
            writing->SetTextures("assets/texture/writing_digit" +
                std::to_string(code[static_cast<std::size_t>(index)]) + ".png");
        }
    }
}

// 入力盤を調べたら入力画面を開き、開いている間は入力を処理している
void Stage1KeypadDoor::Update(Player& player, float deltaTime)
{
    FuseBox* panel = m_Parts.panel;
    if (panel == nullptr || m_Parts.door == nullptr)
    {
        return;
    }

    if (!m_InputOpen)
    {
        // 入力盤を調べると（FuseBoxが作動した状態になると）入力画面を開いている。
        if (!m_Lock.IsSolved() && panel->IsActivated())
        {
            m_InputOpen = true;
            m_InputDelayTimer = InputDelay;
            m_WrongTimer = 0.0f;
            m_Lock.ResetEntry();
            player.SetCanControl(false);
        }
        return;
    }

    m_InputDelayTimer -= deltaTime;
    m_WrongTimer -= deltaTime;
    if (m_InputDelayTimer <= 0.0f)
    {
        HandleInput(player);
    }
}

// 入力を処理している：Q（B）で閉じ、左右で桁を選び、上下で数字を変え、E（A）で決定している
void Stage1KeypadDoor::HandleInput(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();

    if (Input::GetKeyTrigger(VK_Q) || Input::GetButtonTrigger(XINPUT_B))
    {
        Close(player, false);
        return;
    }

    bool changed = false;
    if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A) ||
        Input::GetButtonTrigger(XINPUT_LEFT))
    {
        m_Lock.MoveCursor(-1);
        changed = true;
    }
    else if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D) ||
        Input::GetButtonTrigger(XINPUT_RIGHT))
    {
        m_Lock.MoveCursor(1);
        changed = true;
    }
    else if (Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W) ||
        Input::GetButtonTrigger(XINPUT_UP))
    {
        m_Lock.ChangeDigit(1);
        changed = true;
    }
    else if (Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S) ||
        Input::GetButtonTrigger(XINPUT_DOWN))
    {
        m_Lock.ChangeDigit(-1);
        changed = true;
    }
    else
    {
        // キーボードでは数字キーで直接入力でき、入力すると次の桁へ進んでいる。
        for (int digit = 0; digit <= 9; ++digit)
        {
            if (Input::GetKeyTrigger(static_cast<BYTE>(VK_0 + digit)))
            {
                const int current = m_Lock.GetEntered()[static_cast<std::size_t>(m_Lock.GetCursor())];
                m_Lock.ChangeDigit(digit - current);
                m_Lock.MoveCursor(1);
                changed = true;
                break;
            }
        }
    }
    // 操作するたびに、小さな音と振動で手応えを返している
    if (changed)
    {
        game->PlayAudioCue(SOUND_CUE_FLASHLIGHT, 1.35f);
        Input::SetVibration(1, 0.03f);
        return;
    }

    if (!Input::GetKeyTrigger(VK_E) && !Input::GetButtonTrigger(XINPUT_A))
    {
        return;
    }

    if (m_Lock.Submit() == KeypadLock::SubmitResult::Correct)
    {
        Close(player, true);
        return;
    }

    // 不正解: 入力をやり直させ、扉を向こう側から揺らされたように鳴らしている。
    m_WrongTimer = WrongFeedbackSeconds;
    m_Lock.ResetEntry();
    m_Parts.door->Interact(player);
    if (m_Lock.GetMistakes() >= 3)
    {
        // 何度も（3回以上）間違えると、扉の向こうの気配を強くしている。
        game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, m_Parts.door->GetPosition(), 0.82f, 1.3f);
        game->GetPostProcess()->TriggerHorrorPulse(0.20f, 0.30f);
    }
}

// 入力画面を閉じ、プレイヤーを動けるようにしている
void Stage1KeypadDoor::Close(Player& player, bool solved)
{
    m_InputOpen = false;
    player.SetCanControl(true);

    FuseBox* panel = m_Parts.panel;
    Door* door = m_Parts.door;
    if (!solved)
    {
        // もう一度調べられるよう、入力盤を押していない状態に戻している。
        panel->ResetActivation();
        return;
    }

    // 正解: 入力盤は作動したまま（緑のランプ）にし、扉の鍵を外して開けている。
    panel->SetManualInteractionAllowed(false);
    door->SetLocked(false);
    door->Interact(player);
    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCueAt(SOUND_CUE_POWER, panel->GetPosition(), 1.2f, 0.8f);
    game->GetPostProcess()->TriggerBloomPulse(0.42f, 0.22f);
    Input::SetVibration(4, 0.12f);
}

// 入力画面を開いていれば、入力中の番号・選んでいる桁・不正解の点滅・間違えた回数を渡して描いている
void Stage1KeypadDoor::Draw(Hud& hud) const
{
    if (!m_InputOpen)
    {
        return;
    }
    hud.DrawKeypad(
        m_Lock.GetEntered(),
        m_Lock.GetCursor(),
        m_WrongTimer > 0.0f ? m_WrongTimer / WrongFeedbackSeconds : 0.0f,
        m_Lock.GetMistakes());
}
