// ============================================================================
// ファイルの役割: 監視カメラの映像：映像の描画と、映像の中に出す異変を選び、出し、照らしているかを調べている。
// （StageSurveillanceController.cpp から分けた。説明は元のファイルとヘッダーを参照）
// ============================================================================

#include "StageSurveillanceController.h"
#include "Application.h"
#include "CeilingLight.h"
#include "Door.h"
#include "EventFeedback.h"
#include "FuseBox.h"
#include "Game.h"
#include "PostProcess.h"
#include "TiledLighting.h"
#include "Hud.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "Wall.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float PatrolLookDistance = 110.0f;
    constexpr float PatrolLookAlignment = 0.90f;
    // 異常なしの回の割合。いつも異常がある状態では、報告がただの作業になってしまうためである。
    constexpr float PatrolNoAnomalyChance = 0.25f;
    // 映像に映る人影を、現地確認の制限時間よりこの秒数だけ長く残す（見つける前に消えないように）。人影の胸の高さ
    constexpr float FigureExtraSeconds = 60.0f;
    constexpr float FigureChestHeight = 17.0f;
    // 監視カメラの映像：画角（寄ったとき・普段）、縦横比、描く距離、カメラの位置に足す補助の光の範囲
    constexpr float FeedZoomedFieldOfViewDegrees = 32.0f;
    constexpr float FeedFieldOfViewDegrees = 56.0f;
    constexpr float FeedAspect = 16.0f / 9.0f;
    constexpr float FeedNear = 1.0f;
    constexpr float FeedFar = 420.0f;
    constexpr float FeedHelperLightRange = 260.0f;
    // 監視映像の背景の色（何も映らない所）、映像だけに使う環境光、カメラの位置の補助の光の色と強さ（緑がかった監視映像の光）
    const Color FeedClearColor(0.018f, 0.028f, 0.022f, 1.0f);
    const Color FeedAmbient(0.16f, 0.19f, 0.17f, 1.0f);
    const Vector4 FeedHelperLightColor(0.72f, 0.92f, 0.78f, 0.52f);
    // 照らしたかを判定するとき、距離がこれより短ければ向きを計算しない（0で割らないため）
    constexpr float MinimumLookDistance = 0.001f;

    // float[3]をVector3にしている
    Vector3 ToVector3(const float (&value)[3])
    {
        return Vector3(value[0], value[1], value[2]);
    }
}

// 監視映像の画面（ライブか基準映像か・カメラの名前・巡回の進み具合・間違いの数など）をHUDで描いている
void StageSurveillanceController::DrawFeed(Hud& hud)
{
    const int selectedCamera = m_Patrol.GetSelectedCamera();
    hud.DrawSurveillanceFeed(
        m_ShowReference
            ? m_References[selectedCamera].GetSRV()
            : m_Feed.GetSRV(),
        m_Shader,
        m_ViewTimer,
        StageSurveillanceCameras[selectedCamera].Label,
        selectedCamera,
        StageSurveillanceCameraCount,
        m_Patrol.GetRoundsCleared(),
        SurveillancePatrol::RequiredRounds,
        m_Patrol.GetMistakes(),
        SurveillancePatrol::MistakesUntilCaught,
        m_Zoomed,
        m_ShowReference,
        m_ViewTimer >= PatrolInputDelay && !m_ReferenceCapturePending,
        m_WrongTimer > 0.0f);
}

// 異常をランダムに選んでいる（25%は異常なし）
SurveillancePatrol::Anomaly StageSurveillanceController::ChooseAnomaly()
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(m_Random) < PatrolNoAnomalyChance)
    {
        return SurveillancePatrol::Anomaly{};
    }

    std::uniform_int_distribution<int> cameraDistribution(
        0, StageSurveillanceCameraCount - 1);
    SurveillancePatrol::Anomaly anomaly;
    anomaly.camera = cameraDistribution(m_Random);

    // カメラごとに、その場所で起こせる異常だけを候補にしている。
    const StageSurveillanceCamera& camera =
        StageSurveillanceCameras[anomaly.camera];
    // 人影・消灯・開かずの扉の各種類が1件ずつ入る。
    constexpr int MaxCameraAnomalyTypes = 3;
    SurveillancePatrol::AnomalyType candidates[MaxCameraAnomalyTypes] =
    {
        SurveillancePatrol::AnomalyType::Figure
    };
    int candidateCount = 1;
    CeilingLight* anomalyLight = camera.LightNumber > StageSurveillanceTarget::NoLight
        ? m_Objects->CeilingLightAt(camera.LightNumber)
        : nullptr;
    // 停電中にもともと消えている普通の照明を選ぶと映像に差が出ないため、
    // 電力が戻る前は、点いている非常灯だけを消灯の異常の候補にしている。
    if (anomalyLight != nullptr &&
        (Core::Game::GetInstance()->IsPowerRestored() ||
         anomalyLight->IsEmergencyLight()))
    {
        candidates[candidateCount++] = SurveillancePatrol::AnomalyType::LightOut;
    }
    if (camera.SealedDoorIndex > StageSurveillanceTarget::NoSealedDoor)
    {
        candidates[candidateCount++] = SurveillancePatrol::AnomalyType::DoorOpen;
    }
    std::uniform_int_distribution<int> typeDistribution(0, candidateCount - 1);
    anomaly.type = candidates[typeDistribution(m_Random)];
    return anomaly;
}

// 異常の見た目を出したり消したりしている（人影を立たせる・照明を消す・開かずの扉を開ける）
void StageSurveillanceController::SetAnomalyVisible(
    const SurveillancePatrol::Anomaly& anomaly, bool visible)
{
    if (!anomaly.Exists())
    {
        return;
    }

    const StageSurveillanceCamera& camera =
        StageSurveillanceCameras[anomaly.camera];
    switch (anomaly.type)
    {
    case SurveillancePatrol::AnomalyType::Figure:
        if (ShadowMan* figure = m_Objects->evidenceShadow)
        {
            figure->SetActive(false);
            if (visible)
            {
                const Vector3 position = ToVector3(camera.FigurePosition);
                figure->SetPosition(position.x, position.y, position.z);
                figure->SetActive(true);
                // 現地確認の制限時間より長く残し、見つける前に消えないようにしている。
                figure->EnableGazeScare(
                    SurveillancePatrol::DispatchTimeLimit + FigureExtraSeconds);
            }
        }
        break;
    case SurveillancePatrol::AnomalyType::LightOut:
        if (CeilingLight* light = m_Objects->CeilingLightAt(camera.LightNumber))
        {
            light->SetForcedOff(visible);
        }
        break;
    case SurveillancePatrol::AnomalyType::DoorOpen:
        if (Door* door = m_Objects->sealedDoors[
            static_cast<std::size_t>(camera.SealedDoorIndex)])
        {
            if (visible)
            {
                door->ForceOpen();
            }
            else
            {
                door->ResetClosed(0);
                door->SetLocked(true);
            }
        }
        break;
    case SurveillancePatrol::AnomalyType::None:
        break;
    }
}

// ライトを点けて、異常の場所を110以内から視線の中心（内積0.9超）で照らしているかを返している
bool StageSurveillanceController::IsIlluminatingAnomaly(const Player& player) const
{
    // ライトを点けて自分で異常を探す操作を必須にし、現地に着くだけでは完了させていない。
    if (!player.IsFlashlightOn())
    {
        return false;
    }
    const SurveillancePatrol::Anomaly& anomaly = m_Patrol.GetAnomaly();
    if (!anomaly.Exists())
    {
        return false;
    }

    const StageSurveillanceCamera& camera =
        StageSurveillanceCameras[anomaly.camera];
    Vector3 target;
    switch (anomaly.type)
    {
    case SurveillancePatrol::AnomalyType::Figure:
        target = ToVector3(camera.FigurePosition) + Vector3(0.0f, FigureChestHeight, 0.0f);
        break;
    case SurveillancePatrol::AnomalyType::LightOut:
        target = m_Objects->CeilingLightAt(camera.LightNumber)->GetPosition();
        break;
    case SurveillancePatrol::AnomalyType::DoorOpen:
        target = ToVector3(StageSealedDoors[camera.SealedDoorIndex].Position);
        break;
    case SurveillancePatrol::AnomalyType::None:
        return false;
    }

    // 光が届く距離まで近づき、ライトの中心で照らしている場合だけ対処を進めている。
    Camera* viewCamera = Core::Game::GetInstance()->GetCamera();
    Vector3 toTarget = target - viewCamera->GetPosition();
    const float distance = toTarget.Length();
    if (distance > PatrolLookDistance || distance < MinimumLookDistance)
    {
        return false;
    }
    toTarget /= distance;
    return viewCamera->GetForward().Dot(toTarget) > PatrolLookAlignment;
}

// 映像を見ている間だけ、監視カメラの視点で世界を描いている（本描画の前に呼ばれている）
void StageSurveillanceController::RenderFeeds()
{
    if (m_Patrol.GetState() != SurveillancePatrol::State::Viewing)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Camera* camera = game->GetCamera();
    if (camera == nullptr)
    {
        return;
    }

    // 本編のライトと点光源を覚えておき、描き終えたら元に戻している
    const LIGHT previousLight = Renderer::GetLight();
    Effect::TiledLighting* tiledLighting = game->GetTiledLighting();
    const std::vector<ENVIRONMENT_POINT_LIGHT> previousPointLights =
        tiledLighting->GetLights();
    // 1台のカメラの視点で描いている（拡大なら画角32度、普段は56度）
    const auto drawCamera = [&](int index, bool zoomed,
        Graphics::RenderTexture& output)
    {
        const StageSurveillanceCamera& spot = StageSurveillanceCameras[index];
        const Vector3 cameraPosition(
            spot.Position[0], spot.Position[1], spot.Position[2]);
        const Vector3 cameraTarget(
            spot.Target[0], spot.Target[1], spot.Target[2]);
        const Matrix view = Matrix::CreateLookAt(
            cameraPosition, cameraTarget, Vector3::Up);
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            DirectX::XMConvertToRadians(zoomed ? FeedZoomedFieldOfViewDegrees : FeedFieldOfViewDegrees),
            FeedAspect, FeedNear, FeedFar);

        output.SetRenderTarget();
        output.Clear(FeedClearColor.R(), FeedClearColor.G(), FeedClearColor.B(), FeedClearColor.A());
        camera->SetOverrideMatrices(view, projection);

        // 本編の照明は変えず、監視映像だけに補助の環境光を使っている（懐中電灯の光は映さない）。
        LIGHT surveillanceLight = previousLight;
        surveillanceLight.Enable = TRUE;
        surveillanceLight.FlashlightEnabled = FALSE;
        surveillanceLight.Ambient = FeedAmbient;
        Renderer::SetLight(surveillanceLight);

        // 光源の数の上限が大きくなったため、今の照明を押し出さずに補助の光を足せる。
        std::vector<ENVIRONMENT_POINT_LIGHT> surveillanceLights = previousPointLights;
        ENVIRONMENT_POINT_LIGHT helper{};
        helper.PositionRange = Vector4(
            cameraPosition.x, cameraPosition.y, cameraPosition.z, FeedHelperLightRange);
        helper.ColorIntensity = FeedHelperLightColor;
        surveillanceLights.push_back(helper);
        tiledLighting->SetLights(surveillanceLights);

        game->DrawWorldForAuxiliaryCamera(*camera);
    };

    if (m_ReferenceCapturePending)
    {
        // 1フレームに1台ずつ記録し、端末を開いた瞬間の描画の負荷を分散している。全部記録したら異常の見た目を出している。
        drawCamera(m_ReferenceCaptureIndex, false,
            m_References[m_ReferenceCaptureIndex]);
        ++m_ReferenceCaptureIndex;
        if (m_ReferenceCaptureIndex == StageSurveillanceCameraCount)
        {
            SetAnomalyVisible(m_Patrol.GetAnomaly(), true);
            m_ReferenceCapturePending = false;
        }
    }

    // 選んでいるカメラのライブ映像を描いている
    drawCamera(m_Patrol.GetSelectedCamera(), m_Zoomed,
        m_Feed);
    Renderer::SetLight(previousLight);
    tiledLighting->SetLights(previousPointLights);
    camera->ClearOverrideMatrices();
    Renderer::SetBackBufferRenderTarget();
}
