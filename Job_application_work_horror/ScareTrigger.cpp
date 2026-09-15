// ============================================================================
// ファイルの役割: 一度だけ発生する驚かせ演出の条件と進行を管理します。
// 主な技術: AABBトリガー、イベント発火、再実行防止
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "Game.h"
#include "ScareTrigger.h"
#include "Input.h"
#include "Player.h"
#include "ScreenDustOverlay.h"
#include "ShadowMan.h"

using namespace DirectX::SimpleMath;

// 処理内容: 必要な状態とGPU・音声リソースを初期化します。
void ScareTrigger::Init()
{
}

// 処理内容: 経過時間と入力を使い、このフレームの状態を更新します。
void ScareTrigger::Update()
{
    Core::Game* game = Core::Game::GetInstance();
    if (m_HasTriggered ||
        (m_RequiresPower && !game->IsPowerRestored()) ||
        !IsPlayerInside())
    {
        return;
    }

    m_HasTriggered = true;
    const Vector3 shadowPosition = m_ShadowPosition;
    const bool makePersistentScare = m_RequiresPower;
    game->RequestAddObject<ShadowMan>(
        [shadowPosition, makePersistentScare](ShadowMan& shadow)
        {
            shadow.SetPosition(
                shadowPosition.x,
                shadowPosition.y,
                shadowPosition.z);

            if (makePersistentScare)
            {
                shadow.EnableGazeScare(5.0f);
            }
        });

    Input::SetVibration(14, 0.34f);
    game->PlayAudioCue(SOUND_CUE_SCARE);
    game->GetPostProcess()->TriggerHorrorPulse(1.0f, 0.65f);

    ScreenDustOverlay* crt =
        game->GetObj<ScreenDustOverlay>("CRTNoise");
    if (crt != nullptr)
    {
        crt->SetPower(0.82f);
        crt->SetActive(true);
        crt->SetTimer(0.55f);
    }
}

// 処理内容: 現在の状態に対応する描画命令を発行します。
void ScareTrigger::Draw(Camera* camera)
{
    (void)camera;
}

// 処理内容: 所有するリソースを依存関係の逆順で解放します。
void ScareTrigger::Uninit()
{
}

// 処理内容: 現在の状態が条件を満たすか返します。
bool ScareTrigger::IsPlayerInside() const
{
    const std::vector<Player*> players =
        Core::Game::GetInstance()->GetObjects<Player>();
    if (players.empty() || players.front() == nullptr)
    {
        return false;
    }

    const Vector3 playerPosition = players.front()->GetPosition();
    return
        playerPosition.x >= m_Position.x - m_Size.x * 0.5f &&
        playerPosition.x <= m_Position.x + m_Size.x * 0.5f &&
        playerPosition.y >= m_Position.y - m_Size.y * 0.5f &&
        playerPosition.y <= m_Position.y + m_Size.y * 0.5f &&
        playerPosition.z >= m_Position.z - m_Size.z * 0.5f &&
        playerPosition.z <= m_Position.z + m_Size.z * 0.5f;
}
