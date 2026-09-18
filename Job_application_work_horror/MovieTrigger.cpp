// ============================================================================
// ファイルの役割: 指定範囲への侵入を検知し、ホラー演出イベントを開始します。
// 主な技術: トリガー領域、ワンショットイベント、状態フラグ
// 読み方: 上位処理から呼ばれる順に、初期化・更新・描画・解放を追うと流れを確認できます。
// ============================================================================

#include "MovieTrigger.h"
#include "Game.h"
#include "Player.h"
#include "Camera.h"
#include "ScreenDustOverlay.h"

using namespace DirectX::SimpleMath;

// 処理内容: 必要な状態とGPU・音声リソースを初期化します。
void MovieTrigger::Init()
{
    m_Position = Vector3(0.0f, 0.0f, 0.0f);

    m_CameraEndPos = Vector3(0.0f, 0.0f, 0.0f);
    m_CameraEndTarget = Vector3(0.0f, 0.0f, 1.0f);

    m_Duration = 2.0f;
    m_IsPlayed = false;
}

// 処理内容: 経過時間と入力を使い、このフレームの状態を更新します。
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

// 処理内容: 現在の状態に対応する描画命令を発行します。
void MovieTrigger::Draw(Camera* cam)
{
    // センサーなので描画しない
}

void MovieTrigger::Uninit()
{}

// 処理内容: 幾何条件または進行条件を判定します。
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
