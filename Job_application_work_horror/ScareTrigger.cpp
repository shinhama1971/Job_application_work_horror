// ============================================================================
// ファイルの役割: プレイヤーが範囲に入ったとき、一度だけ起きる驚かせる演出の条件と実行を管理している。
// 主な技術: 箱の範囲（AABB）に入ったかの判定、出来事の発生、二度起きないようにする仕組み
// ============================================================================

#include "Game.h"
#include "ScareTrigger.h"
#include "Input.h"
#include "Player.h"
#include "ScreenDustOverlay.h"
#include "ShadowMan.h"

using namespace DirectX::SimpleMath;

// 準備するものはない
void ScareTrigger::Init()
{
}

// まだ起きていなくて、条件を満たし、プレイヤーが範囲に入ったら演出を起こしている
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
    // 影を出している（更新の途中なので、追加は予約にしている）。電力が戻った後の影は、見つめると消える演出にしている
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

    // 強い振動・驚かせる音・画面の乱れ・ブラウン管のようなノイズで驚かせている
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

// 見た目はないので何も描かない
void ScareTrigger::Draw(Camera* camera)
{
    (void)camera;
}

// 解放するものはない
void ScareTrigger::Uninit()
{
}

// プレイヤーの足元が、箱の範囲の中にあるかを調べている
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
