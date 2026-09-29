// ============================================================================
// ファイルの役割: 1面の監視カメラ巡回（映像の確認・報告・現地での対処・捕獲）を進めます。
// 主な技術: 有限状態機械、RenderTextureによる別視点描画、入力と演出の同期
// 端末で監視映像を確認して異常のあるカメラを報告し、現地で懐中電灯を当てて対処します。
// 誤った報告や時間切れが続くと捕獲され、端末の前へ戻されます。
// ============================================================================

#include "StageSurveillanceController.h"

#include "Application.h"
#include "CeilingLight.h"
#include "Door.h"
#include "FuseBox.h"
#include "Game.h"
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
    // 映像を開いてから操作を受け付けるまでの秒数です（開いた直後の誤入力を防ぎます）。
    constexpr float PatrolInputDelay = 0.65f;
    constexpr float PatrolWrongFeedbackSeconds = 1.35f;
    constexpr float PatrolWarningTime = 12.0f;
    constexpr float PatrolLookDistance = 110.0f;
    constexpr float PatrolLookAlignment = 0.90f;
    // 異常なしの回の割合です。常に異常がある状態では報告が作業になってしまうためです。
    constexpr float PatrolNoAnomalyChance = 0.25f;

    Vector3 ToVector3(const float (&value)[3])
    {
        return Vector3(value[0], value[1], value[2]);
    }

    const char* GetAnomalyLabel(SurveillancePatrol::AnomalyType type)
    {
        switch (type)
        {
        case SurveillancePatrol::AnomalyType::Figure:
            return "人影";
        case SurveillancePatrol::AnomalyType::LightOut:
            return "消えた照明";
        case SurveillancePatrol::AnomalyType::DoorOpen:
            return "開いた扉";
        case SurveillancePatrol::AnomalyType::None:
            return "異常";
        }
        return "異常";
    }
}

void StageSurveillanceController::Init(const StageObjects& objects)
{
    m_Objects = &objects;
    m_Patrol.Reset();
    m_Caught.Reset();
    m_ViewTimer = 0.0f;
    m_Zoomed = false;
    m_ShowReference = false;
    m_ReferenceCapturePending = false;
    m_ReferenceCaptureIndex = 0;
    m_WrongTimer = 0.0f;
    m_WarningCooldown = 0.0f;
    m_NoticeTimer = 0.0f;
    m_NoticeText = "";
    m_CompletedThisFrame = false;

    m_Shader.Create(
        "shader/unlitTextureVS.hlsl",
        "shader/surveillanceFeedPS.hlsl");
    // 大きく表示しても異常の輪郭が潰れない解像度を確保します。
    m_Feed.Init(960, 540);
    for (Graphics::RenderTexture& reference : m_References)
    {
        reference.Init(960, 540);
    }
}

void StageSurveillanceController::Uninit()
{
    m_Feed.Uninit();
    for (Graphics::RenderTexture& reference : m_References)
    {
        reference.Uninit();
    }
}

bool StageSurveillanceController::Update(Player& player, float deltaTime)
{
    m_CompletedThisFrame = false;
    UpdateState(player, deltaTime);
    return m_CompletedThisFrame;
}

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

void StageSurveillanceController::FillObjectiveInput(
    Stage1ObjectiveInput& input, const Player& player) const
{
    input.patrolDispatched =
        m_Patrol.GetState() == SurveillancePatrol::State::Dispatched;
    if (input.patrolDispatched)
    {
        const SurveillancePatrol::Anomaly& anomaly = m_Patrol.GetAnomaly();
        input.patrolCameraLabel = StageSurveillanceCameras[anomaly.camera].Label;
        input.patrolAnomalyLabel = GetAnomalyLabel(anomaly.type);
        input.patrolRemainingSeconds = static_cast<int>(
            std::ceil(m_Patrol.GetRemainingTime()));
        input.patrolConfirmPercent = static_cast<int>(
            std::round(m_Patrol.GetConfirmRate() * 100.0f));
    }
    input.flashlightOn = player.IsFlashlightOn();
    input.patrolNotice = m_NoticeTimer > 0.0f;
    input.patrolNoticeText = m_NoticeText;
}

void StageSurveillanceController::UpdateState(Player& player, float deltaTime)
{
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);

    if (m_Caught.IsActive())
    {
        UpdateCaught(player, deltaTime);
        return;
    }

    switch (m_Patrol.GetState())
    {
    case SurveillancePatrol::State::Idle:
    {
        FuseBox* terminal = m_Objects->evidenceTerminal;
        if (terminal == nullptr || !terminal->IsActivated())
        {
            return;
        }

        // 基準映像を記録してから異常を出すため、ここでは種類だけ確定します。
        const SurveillancePatrol::Anomaly anomaly = ChooseAnomaly();
        m_Patrol.BeginViewing(anomaly);
        m_ViewTimer = 0.0f;
        m_Zoomed = false;
        m_ShowReference = false;
        m_ReferenceCapturePending = true;
        m_ReferenceCaptureIndex = 0;
        m_WrongTimer = 0.0f;
        player.SetCanControl(false);

        Core::Game* game = Core::Game::GetInstance();
        game->PlayAudioCue(SOUND_CUE_POWER, 0.72f);
        game->GetPostProcess()->TriggerHorrorPulse(0.12f, 0.20f);
        break;
    }
    case SurveillancePatrol::State::Viewing:
        m_ViewTimer += deltaTime;
        UpdateViewing(player);
        break;
    case SurveillancePatrol::State::Dispatched:
        UpdateDispatch(player, deltaTime);
        break;
    case SurveillancePatrol::State::Completed:
        break;
    }
}

void StageSurveillanceController::UpdateViewing(Player& player)
{
    m_WrongTimer = (std::max)(
        0.0f, m_WrongTimer - Application::GetDeltaTime());
    if (m_ReferenceCapturePending ||
        m_ViewTimer < PatrolInputDelay || m_WrongTimer > 0.0f)
    {
        return;
    }

    if (Input::GetKeyTrigger(VK_LEFT) ||
        Input::GetButtonTrigger(XINPUT_LEFT) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_Patrol.SelectCamera(-1, StageSurveillanceCameraCount);
        Input::SetVibration(1, 0.03f);
    }
    else if (Input::GetKeyTrigger(VK_RIGHT) ||
        Input::GetButtonTrigger(XINPUT_RIGHT) ||
        Input::GetButtonTrigger(XINPUT_RIGHT_SHOULDER))
    {
        m_Patrol.SelectCamera(1, StageSurveillanceCameraCount);
        Input::SetVibration(1, 0.03f);
    }

    if (Input::GetKeyTrigger(VK_C) || Input::GetButtonTrigger(XINPUT_Y))
    {
        m_ShowReference = !m_ShowReference;
        // 記録映像は標準画角なので、切替時にライブも同じ画角へ戻します。
        m_Zoomed = false;
        Input::SetVibration(1, 0.03f);
    }

    // 広い画角で場所を把握し、必要なときだけ中央を拡大して小さな変化を調べます。
    if (!m_ShowReference &&
        (Input::GetKeyTrigger(VK_Z) || Input::GetButtonTrigger(XINPUT_X)))
    {
        m_Zoomed = !m_Zoomed;
        Input::SetVibration(1, 0.03f);
    }

    // 基準映像からの誤報告を防ぐため、報告はライブ画面でのみ受け付けます。
    if (m_ShowReference)
    {
        return;
    }

    SurveillancePatrol::ReportResult result =
        SurveillancePatrol::ReportResult::Ignored;
    if (Input::GetKeyTrigger(VK_E) || Input::GetButtonTrigger(XINPUT_A))
    {
        result = m_Patrol.ReportSelectedCamera();
    }
    else if (Input::GetKeyTrigger(VK_Q) || Input::GetButtonTrigger(XINPUT_B))
    {
        result = m_Patrol.ReportNoAnomaly();
    }

    Core::Game* game = Core::Game::GetInstance();
    switch (result)
    {
    case SurveillancePatrol::ReportResult::Ignored:
        break;
    case SurveillancePatrol::ReportResult::Dispatched:
        m_WarningCooldown = 0.0f;
        EndViewing(player);
        game->PlayAudioCue(SOUND_CUE_DOOR, 0.62f);
        break;
    case SurveillancePatrol::ReportResult::ClearedNoAnomaly:
        EndViewing(player);
        if (m_Patrol.IsCompleted())
        {
            Complete(player);
        }
        else
        {
            ShowNotice("異常なしを確認した", 2.2f);
        }
        break;
    case SurveillancePatrol::ReportResult::Wrong:
        game->RegisterPuzzleMistake();
        m_WrongTimer = PatrolWrongFeedbackSeconds;
        game->PlayAudioCue(SOUND_CUE_DOOR, 0.54f);
        game->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.24f);
        Input::SetVibration(5, 0.11f);
        break;
    case SurveillancePatrol::ReportResult::Caught:
        game->RegisterPuzzleMistake();
        StartCaught(player);
        break;
    }
}

void StageSurveillanceController::UpdateDispatch(Player& player, float deltaTime)
{
    const SurveillancePatrol::Anomaly anomaly = m_Patrol.GetAnomaly();
    const SurveillancePatrol::DispatchResult result =
        m_Patrol.UpdateDispatch(deltaTime, IsIlluminatingAnomaly(player));

    Core::Game* game = Core::Game::GetInstance();
    switch (result)
    {
    case SurveillancePatrol::DispatchResult::None:
        // 残り時間が少ないほど、端末から離れている不安を画面と振動で強めます。
        m_WarningCooldown -= deltaTime;
        if (m_Patrol.GetRemainingTime() <= PatrolWarningTime &&
            m_WarningCooldown <= 0.0f)
        {
            m_WarningCooldown = 3.0f;
            game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.30f);
            Input::SetVibration(4, 0.08f);
        }
        break;
    case SurveillancePatrol::DispatchResult::Resolved:
        SetAnomalyVisible(anomaly, false);
        game->RegisterAnomalyHandled();
        game->GetPostProcess()->TriggerBloomPulse(0.36f, 0.18f);
        Input::SetVibration(4, 0.09f);
        if (m_Patrol.IsCompleted())
        {
            Complete(player);
        }
        else
        {
            ShowNotice("異常を確認した 端末へ戻る", 2.6f);
            if (FuseBox* terminal = m_Objects->evidenceTerminal)
            {
                terminal->SetManualInteractionAllowed(true);
            }
        }
        break;
    case SurveillancePatrol::DispatchResult::TimedOut:
        SetAnomalyVisible(anomaly, false);
        game->RegisterPuzzleMistake();
        ShowNotice("映像の反応が途絶えた 端末へ戻る", 2.8f);
        game->PlayAudioCue(SOUND_CUE_SCARE, 0.62f);
        game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.30f);
        if (FuseBox* terminal = m_Objects->evidenceTerminal)
        {
            terminal->SetManualInteractionAllowed(true);
        }
        break;
    case SurveillancePatrol::DispatchResult::Caught:
        SetAnomalyVisible(anomaly, false);
        game->RegisterPuzzleMistake();
        StartCaught(player);
        break;
    }
}

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

    // カメラごとに、その場所で起こせる異常だけを候補にします。
    const StageSurveillanceCamera& camera =
        StageSurveillanceCameras[anomaly.camera];
    SurveillancePatrol::AnomalyType candidates[3] =
    {
        SurveillancePatrol::AnomalyType::Figure
    };
    int candidateCount = 1;
    CeilingLight* anomalyLight = camera.LightNumber > 0
        ? m_Objects->CeilingLightAt(camera.LightNumber)
        : nullptr;
    // 停電中に元から消えている通常照明を選ぶと映像に差が出ないため、
    // 通電前は点灯している非常灯だけを消灯異常の候補にします。
    if (anomalyLight != nullptr &&
        (Core::Game::GetInstance()->IsPowerRestored() ||
         anomalyLight->IsEmergencyLight()))
    {
        candidates[candidateCount++] = SurveillancePatrol::AnomalyType::LightOut;
    }
    if (camera.SealedDoorIndex >= 0)
    {
        candidates[candidateCount++] = SurveillancePatrol::AnomalyType::DoorOpen;
    }
    std::uniform_int_distribution<int> typeDistribution(0, candidateCount - 1);
    anomaly.type = candidates[typeDistribution(m_Random)];
    return anomaly;
}

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
                // 現地確認の制限時間より長く残し、見つける前に消えないようにします。
                figure->EnableGazeScare(
                    SurveillancePatrol::DispatchTimeLimit + 60.0f);
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

bool StageSurveillanceController::IsIlluminatingAnomaly(const Player& player) const
{
    // ライトを点けて自分で異常を探す操作を必須にし、現地到着だけでは完了させません。
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
        target = ToVector3(camera.FigurePosition) + Vector3(0.0f, 17.0f, 0.0f);
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

    // 光が届く距離まで近づき、ライトの中心で照らしている場合だけ対処を進めます。
    Camera* viewCamera = Core::Game::GetInstance()->GetCamera();
    Vector3 toTarget = target - viewCamera->GetPosition();
    const float distance = toTarget.Length();
    if (distance > PatrolLookDistance || distance < 0.001f)
    {
        return false;
    }
    toTarget /= distance;
    return viewCamera->GetForward().Dot(toTarget) > PatrolLookAlignment;
}

void StageSurveillanceController::EndViewing(Player& player)
{
    player.SetCanControl(true);
    if (FuseBox* terminal = m_Objects->evidenceTerminal)
    {
        terminal->ResetActivation();
        // 現地確認中は端末を操作できないようにし、確認後に戻ってから次の映像を見せます。
        terminal->SetManualInteractionAllowed(
            m_Patrol.GetState() == SurveillancePatrol::State::Idle);
    }
}

void StageSurveillanceController::StartCaught(Player& player)
{
    SetAnomalyVisible(m_Patrol.GetAnomaly(), false);
    if (!m_Caught.Start(CaughtSequence::Reason::FinalPursuit))
    {
        return;
    }

    player.SetCanControl(false);
    Core::Game* game = Core::Game::GetInstance();
    game->RegisterCaught();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    game->GetPostProcess()->TriggerHorrorPulse(1.0f, 0.72f);
    Input::SetVibration(24, 0.72f);
}

void StageSurveillanceController::UpdateCaught(Player& player, float deltaTime)
{
    m_Caught.Advance(deltaTime);
    if (!m_Caught.IsReadyToRecover())
    {
        return;
    }

    // 暗転中に端末の前へ戻し、巡回はやり直せる状態にします（対処済みの回数は保持）。
    m_Caught.Complete();
    player.SetPosition(Vector3(190.0f, -99.0f, -42.0f));
    player.SetCanControl(true);
    if (ShadowMan* figure = m_Objects->evidenceShadow)
    {
        figure->SetActive(false);
    }
    if (FuseBox* terminal = m_Objects->evidenceTerminal)
    {
        terminal->ResetActivation();
        terminal->SetManualInteractionAllowed(true);
    }
    ShowNotice("気がつくと端末の前にいた", 3.0f);
}

void StageSurveillanceController::Complete(Player& player)
{
    // 記録端末の通知はSceneが出すため、完了したことだけを伝えます。
    m_CompletedThisFrame = true;
    player.AddBattery(8.0f);

    if (FuseBox* terminal = m_Objects->evidenceTerminal)
    {
        terminal->SetManualInteractionAllowed(false);
    }
    if (Wall* marker = m_Objects->evidenceMarker)
    {
        marker->SetAppearance(
            Color(0.08f, 0.18f, 0.10f, 1.0f),
            Color(0.16f, 0.52f, 0.22f, 1.0f),
            44.0f);
    }
    if (CeilingLight* roomLight = m_Objects->CeilingLightAt(3))
    {
        roomLight->TriggerEventFlicker(0.84f, 0.92f);
    }

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterEvidenceCollected();
    game->PlayAudioCue(SOUND_CUE_SCARE, 0.76f);
    game->GetPostProcess()->TriggerHorrorPulse(0.34f, 0.32f);
    game->GetPostProcess()->TriggerBloomPulse(0.42f, 0.20f);
    Input::SetVibration(9, 0.19f);
}

void StageSurveillanceController::ShowNotice(const char* text, float seconds)
{
    m_NoticeText = text;
    m_NoticeTimer = seconds;
}
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

    const LIGHT previousLight = Renderer::GetLight();
    Effect::TiledLighting* tiledLighting = game->GetTiledLighting();
    const std::vector<ENVIRONMENT_POINT_LIGHT> previousPointLights =
        tiledLighting->GetLights();
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
            DirectX::XMConvertToRadians(zoomed ? 32.0f : 56.0f),
            16.0f / 9.0f, 1.0f, 420.0f);

        output.SetRenderTarget();
        output.Clear(0.018f, 0.028f, 0.022f, 1.0f);
        camera->SetOverrideMatrices(view, projection);

        // 本編の照明を変更せず、監視映像だけに補助環境光を使います。
        LIGHT surveillanceLight = previousLight;
        surveillanceLight.Enable = TRUE;
        surveillanceLight.FlashlightEnabled = FALSE;
        surveillanceLight.Ambient = Color(0.16f, 0.19f, 0.17f, 1.0f);
        Renderer::SetLight(surveillanceLight);

        // 光源数の上限が大きくなったため、既存の照明を押し出さずに補助光を足せます。
        std::vector<ENVIRONMENT_POINT_LIGHT> surveillanceLights = previousPointLights;
        ENVIRONMENT_POINT_LIGHT helper{};
        helper.PositionRange = Vector4(
            cameraPosition.x, cameraPosition.y, cameraPosition.z, 260.0f);
        helper.ColorIntensity = Vector4(0.72f, 0.92f, 0.78f, 0.52f);
        surveillanceLights.push_back(helper);
        tiledLighting->SetLights(surveillanceLights);

        game->DrawWorldForAuxiliaryCamera(*camera);
    };

    if (m_ReferenceCapturePending)
    {
        // 1フレームに1台ずつ記録し、端末を開いた瞬間の描画負荷を分散します。
        drawCamera(m_ReferenceCaptureIndex, false,
            m_References[m_ReferenceCaptureIndex]);
        ++m_ReferenceCaptureIndex;
        if (m_ReferenceCaptureIndex == StageSurveillanceCameraCount)
        {
            SetAnomalyVisible(m_Patrol.GetAnomaly(), true);
            m_ReferenceCapturePending = false;
        }
    }

    drawCamera(m_Patrol.GetSelectedCamera(), m_Zoomed,
        m_Feed);
    Renderer::SetLight(previousLight);
    tiledLighting->SetLights(previousPointLights);
    camera->ClearOverrideMatrices();
    Renderer::SetBackBufferRenderTarget();
}
