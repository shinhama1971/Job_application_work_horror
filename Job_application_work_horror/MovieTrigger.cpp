// ============================================================================
// ファイルの役割: 指定範囲への侵入を検知し、ホラー演出イベントを開始します。
// ============================================================================

#include "MovieTrigger.h"
#include "Game.h"
#include "Player.h"
#include "Camera.h"
#include "ScreenDustOverlay.h"

using namespace DirectX::SimpleMath;

void MovieTrigger::Init()
{
    m_Position = Vector3(0.0f, 0.0f, 0.0f);

    m_CameraEndPos = Vector3(0.0f, 0.0f, 0.0f);
    m_CameraEndTarget = Vector3(0.0f, 0.0f, 1.0f);

    m_Duration = 2.0f;
    m_IsPlayed = false;
}

void MovieTrigger::Update()
{
    Player* player =
        Core::Game::GetInstance()->GetObj<Player>("Player");

    Camera* camera =
        Core::Game::GetInstance()->GetCamera();

    // まだ再生していない時だけセンサー判定
    if (!m_IsPlayed)
    {
        if (CheckPlayerInside())
        {
            m_IsPlayed = true;

            if (player != nullptr)
            {
                player->SetCanControl(false);
            }

            if (camera != nullptr)
            {
                camera->StartMovieLook(
                    m_CameraEndPos,
                    m_CameraEndTarget,
                    m_Duration
                );
            }

            // CRTノイズON
            ScreenDustOverlay* crt =
                Core::Game::GetInstance()->GetObj<ScreenDustOverlay>("CRTNoise");

            if (crt != nullptr)
            {
                crt->SetActive(true);
                crt->SetTimer(2.0f);
            }
        }

        return;
    }

    // カメラ演出が終わったら操作を戻す
    if (camera != nullptr && !camera->IsMovie())
    {
        if (player != nullptr)
        {
            player->SetCanControl(true);
        }
    }
}

void MovieTrigger::Draw(Camera* cam)
{
    // センサーなので描画しない
}

void MovieTrigger::Uninit()
{}

bool MovieTrigger::CheckPlayerInside()
{
    Player* player =
        Core::Game::GetInstance()->GetObj<Player>("Player");

    if (player == nullptr)
    {
        return false;
    }

    Vector3 p = player->GetPosition();

    return
        p.x >= m_Position.x - m_Size.x * 0.5f &&
        p.x <= m_Position.x + m_Size.x * 0.5f &&
        p.y >= m_Position.y - m_Size.y * 0.5f &&
        p.y <= m_Position.y + m_Size.y * 0.5f &&
        p.z >= m_Position.z - m_Size.z * 0.5f &&
        p.z <= m_Position.z + m_Size.z * 0.5f;
}
