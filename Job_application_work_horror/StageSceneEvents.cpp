// ============================================================================
// ファイルの役割: 1面の入口演出、廊下ループ、電力復旧、出口イベントを管理します。
// 主な技術: イベント駆動、有限状態機械、カメラ・照明・音の同期
// ============================================================================

#include "StageScene.h"
#include "Application.h"
#include "Game.h"
#include "Input.h"

#include "Player.h"
#include "Ground.h"
#include "Wall.h"
#include "Item.h"
#include "Door.h"
#include "FuseBox.h"
#include "CeilingLight.h"
#include "ExitTrigger.h"
#include "BatteryItem.h"
#include "ScreenDustOverlay.h"
#include "ScareTrigger.h"
#include "ShadowMan.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

void StageScene::UpdateEntranceThresholdEvent(Player& player)
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();

    if (!m_EntranceEventTriggered)
    {
        const Vector3 position = player.GetPosition();
        const bool crossedDoorThreshold =
            !game->IsPowerRestored() &&
            position.z > 62.0f &&
            std::abs(position.x) < 58.0f;
        if (!crossedDoorThreshold)
        {
            return;
        }

        // 強制カメラ演出の代わりに、空間内の照明変化で出来事を見せます。
        // 操作を奪わず、プレイヤー自身が異変へ気付ける演出にします。
        m_EntranceEventTriggered = true;
        m_EntranceEventTimer = 0.0f;
        m_EntranceEventPhase = 0;

        CeilingLight* lightBehind =
            m_Objects.CeilingLightAt(4);
        if (lightBehind != nullptr)
        {
            lightBehind->TriggerEventFlicker(0.38f, 0.52f);
        }

        game->GetPostProcess()->TriggerHorrorPulse(0.075f, 0.14f);
        Input::SetVibration(3, 0.07f);
        return;
    }

    if (m_EntranceEventTimer < 0.0f)
    {
        return;
    }
    if (game->IsPowerRestored())
    {
        m_EntranceEventTimer = -1.0f;
        m_EntranceEventPhase = -1;
        return;
    }

    m_EntranceEventTimer += deltaTime;
    if (m_EntranceEventPhase == 0 && m_EntranceEventTimer >= 0.38f)
    {
        CeilingLight* lightAhead =
            m_Objects.CeilingLightAt(5);
        if (lightAhead != nullptr)
        {
            lightAhead->TriggerEventFlicker(0.62f, 0.72f);
        }

        game->GetPostProcess()->TriggerHorrorPulse(0.10f, 0.18f);
        Input::SetVibration(4, 0.09f);
        m_EntranceEventPhase = 1;
    }
    else if (m_EntranceEventPhase == 1 &&
        m_EntranceEventTimer >= 1.12f)
    {
        CeilingLight* lightBehind =
            m_Objects.CeilingLightAt(4);
        if (lightBehind != nullptr)
        {
            lightBehind->TriggerEventFlicker(0.20f, 0.28f);
        }

        m_EntranceEventTimer = -1.0f;
        m_EntranceEventPhase = 2;
    }
}

void StageScene::UpdateStorageScare(Player& player)
{
    if (m_StorageScarePhase >= 2)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 position = player.GetPosition();
    if (game->IsPowerRestored() || game->GetItemCount() >= 2)
    {
        m_StorageScarePhase = 3;
        return;
    }

    if (m_StorageScarePhase == 0)
    {
        // 二周目の左倉庫へ踏み込んだとき、先に物音と照明で背後を意識させます。
        if (m_CorridorLoopCount < 1 || game->GetItemCount() != 1 ||
            position.x > -110.0f || position.z > -108.0f)
        {
            return;
        }

        m_StorageScarePhase = 1;
        m_StorageScareTimer = 0.0f;
        m_StorageScareNoticeTimer = 2.1f;
        game->PlayAudioCue(SOUND_CUE_DOOR, 0.68f);
        CeilingLight* light = m_Objects.CeilingLightAt(2);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.42f, 0.62f);
        }
        return;
    }

    m_StorageScareTimer += Application::GetDeltaTime();
    if (m_StorageScareTimer < 0.8f)
    {
        return;
    }
    m_StorageScarePhase = 2;

    // 倉庫から離れた場合は出現させず、視界の外に突然残る人影を防ぎます。
    if (position.x > -100.0f || position.z > -90.0f)
    {
        m_StorageScarePhase = 3;
        return;
    }

    ShadowMan* shadow = m_Objects.storageShadow;
    if (shadow == nullptr)
    {
        m_StorageScarePhase = 3;
        return;
    }
    shadow->SetPosition(-155.0f, -99.0f, -88.0f);
    shadow->SetActive(true);
    shadow->EnableGazeScare(5.0f);
    m_StorageScareNoticeTimer = 2.0f;
    shadow->SetOnObserved([this]()
    {
        Core::Game* currentGame = Core::Game::GetInstance();
        currentGame->RegisterAnomalyHandled();
        m_StorageScarePhase = 3;
        m_StorageScareNoticeTimer = 1.8f;
        CeilingLight* light =
            m_Objects.CeilingLightAt(2);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.75f, 0.88f);
        }
        currentGame->PlayAudioCue(SOUND_CUE_SCARE, 0.82f);
        currentGame->GetPostProcess()->TriggerHorrorPulse(0.24f, 0.26f);
        Input::SetVibration(7, 0.15f);
    });
}

// ----------------------------------------------------------------------------
// 監視カメラ巡回
// 端末で監視映像を確認して異常のあるカメラを報告し、現地で懐中電灯を当てて対処します。
// 誤った報告や時間切れが続くと捕獲され、端末の前へ戻されます。
// ----------------------------------------------------------------------------
namespace
{
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
}

void StageScene::UpdateSurveillancePatrol(Player& player, float deltaTime)
{
    m_PatrolNoticeTimer = (std::max)(0.0f, m_PatrolNoticeTimer - deltaTime);

    if (m_PatrolCaught.IsActive())
    {
        UpdatePatrolCaught(player, deltaTime);
        return;
    }

    switch (m_Patrol.GetState())
    {
    case SurveillancePatrol::State::Idle:
    {
        FuseBox* terminal = m_Objects.evidenceTerminal;
        if (terminal == nullptr || !terminal->IsActivated())
        {
            return;
        }

        // 基準映像を記録してから異常を出すため、ここでは種類だけ確定します。
        const SurveillancePatrol::Anomaly anomaly = ChoosePatrolAnomaly();
        m_Patrol.BeginViewing(anomaly);
        m_PatrolViewTimer = 0.0f;
        m_PatrolZoomed = false;
        m_PatrolShowReference = false;
        m_PatrolReferenceCapturePending = true;
        m_PatrolReferenceCaptureIndex = 0;
        m_PatrolWrongTimer = 0.0f;
        player.SetCanControl(false);

        Core::Game* game = Core::Game::GetInstance();
        game->PlayAudioCue(SOUND_CUE_POWER, 0.72f);
        game->GetPostProcess()->TriggerHorrorPulse(0.12f, 0.20f);
        break;
    }
    case SurveillancePatrol::State::Viewing:
        m_PatrolViewTimer += deltaTime;
        UpdatePatrolViewing(player);
        break;
    case SurveillancePatrol::State::Dispatched:
        UpdatePatrolDispatch(player, deltaTime);
        break;
    case SurveillancePatrol::State::Completed:
        break;
    }
}

void StageScene::UpdatePatrolViewing(Player& player)
{
    m_PatrolWrongTimer = (std::max)(
        0.0f, m_PatrolWrongTimer - Application::GetDeltaTime());
    if (m_PatrolReferenceCapturePending ||
        m_PatrolViewTimer < PatrolInputDelay || m_PatrolWrongTimer > 0.0f)
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
        m_PatrolShowReference = !m_PatrolShowReference;
        // 記録映像は標準画角なので、切替時にライブも同じ画角へ戻します。
        m_PatrolZoomed = false;
        Input::SetVibration(1, 0.03f);
    }

    // 広い画角で場所を把握し、必要なときだけ中央を拡大して小さな変化を調べます。
    if (!m_PatrolShowReference &&
        (Input::GetKeyTrigger(VK_Z) || Input::GetButtonTrigger(XINPUT_X)))
    {
        m_PatrolZoomed = !m_PatrolZoomed;
        Input::SetVibration(1, 0.03f);
    }

    // 基準映像からの誤報告を防ぐため、報告はライブ画面でのみ受け付けます。
    if (m_PatrolShowReference)
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
        m_PatrolWarningCooldown = 0.0f;
        EndPatrolViewing(player);
        game->PlayAudioCue(SOUND_CUE_DOOR, 0.62f);
        break;
    case SurveillancePatrol::ReportResult::ClearedNoAnomaly:
        EndPatrolViewing(player);
        if (m_Patrol.IsCompleted())
        {
            CompletePatrol(player);
        }
        else
        {
            ShowPatrolNotice("異常なしを確認した", 2.2f);
        }
        break;
    case SurveillancePatrol::ReportResult::Wrong:
        game->RegisterPuzzleMistake();
        m_PatrolWrongTimer = PatrolWrongFeedbackSeconds;
        game->PlayAudioCue(SOUND_CUE_DOOR, 0.54f);
        game->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.24f);
        Input::SetVibration(5, 0.11f);
        break;
    case SurveillancePatrol::ReportResult::Caught:
        game->RegisterPuzzleMistake();
        StartPatrolCaught(player);
        break;
    }
}

void StageScene::UpdatePatrolDispatch(Player& player, float deltaTime)
{
    const SurveillancePatrol::Anomaly anomaly = m_Patrol.GetAnomaly();
    const SurveillancePatrol::DispatchResult result =
        m_Patrol.UpdateDispatch(deltaTime, IsIlluminatingPatrolAnomaly(player));

    Core::Game* game = Core::Game::GetInstance();
    switch (result)
    {
    case SurveillancePatrol::DispatchResult::None:
        // 残り時間が少ないほど、端末から離れている不安を画面と振動で強めます。
        m_PatrolWarningCooldown -= deltaTime;
        if (m_Patrol.GetRemainingTime() <= PatrolWarningTime &&
            m_PatrolWarningCooldown <= 0.0f)
        {
            m_PatrolWarningCooldown = 3.0f;
            game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.30f);
            Input::SetVibration(4, 0.08f);
        }
        break;
    case SurveillancePatrol::DispatchResult::Resolved:
        SetPatrolAnomalyVisible(anomaly, false);
        game->RegisterAnomalyHandled();
        game->GetPostProcess()->TriggerBloomPulse(0.36f, 0.18f);
        Input::SetVibration(4, 0.09f);
        if (m_Patrol.IsCompleted())
        {
            CompletePatrol(player);
        }
        else
        {
            ShowPatrolNotice("異常を確認した 端末へ戻る", 2.6f);
            if (FuseBox* terminal = m_Objects.evidenceTerminal)
            {
                terminal->SetManualInteractionAllowed(true);
            }
        }
        break;
    case SurveillancePatrol::DispatchResult::TimedOut:
        SetPatrolAnomalyVisible(anomaly, false);
        game->RegisterPuzzleMistake();
        ShowPatrolNotice("映像の反応が途絶えた 端末へ戻る", 2.8f);
        game->PlayAudioCue(SOUND_CUE_SCARE, 0.62f);
        game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.30f);
        if (FuseBox* terminal = m_Objects.evidenceTerminal)
        {
            terminal->SetManualInteractionAllowed(true);
        }
        break;
    case SurveillancePatrol::DispatchResult::Caught:
        SetPatrolAnomalyVisible(anomaly, false);
        game->RegisterPuzzleMistake();
        StartPatrolCaught(player);
        break;
    }
}

SurveillancePatrol::Anomaly StageScene::ChoosePatrolAnomaly()
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(m_PatrolRandom) < PatrolNoAnomalyChance)
    {
        return SurveillancePatrol::Anomaly{};
    }

    std::uniform_int_distribution<int> cameraDistribution(
        0, StageSurveillanceCameraCount - 1);
    SurveillancePatrol::Anomaly anomaly;
    anomaly.camera = cameraDistribution(m_PatrolRandom);

    // カメラごとに、その場所で起こせる異常だけを候補にします。
    const StageSurveillanceCamera& camera =
        StageSurveillanceCameras[anomaly.camera];
    SurveillancePatrol::AnomalyType candidates[3] =
    {
        SurveillancePatrol::AnomalyType::Figure
    };
    int candidateCount = 1;
    CeilingLight* anomalyLight = camera.LightNumber > 0
        ? m_Objects.CeilingLightAt(camera.LightNumber)
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
    anomaly.type = candidates[typeDistribution(m_PatrolRandom)];
    return anomaly;
}

void StageScene::SetPatrolAnomalyVisible(
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
        if (ShadowMan* figure = m_Objects.evidenceShadow)
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
        if (CeilingLight* light = m_Objects.CeilingLightAt(camera.LightNumber))
        {
            light->SetForcedOff(visible);
        }
        break;
    case SurveillancePatrol::AnomalyType::DoorOpen:
        if (Door* door = m_Objects.sealedDoors[
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

bool StageScene::IsIlluminatingPatrolAnomaly(const Player& player) const
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
        target = m_Objects.CeilingLightAt(camera.LightNumber)->GetPosition();
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

void StageScene::EndPatrolViewing(Player& player)
{
    player.SetCanControl(true);
    if (FuseBox* terminal = m_Objects.evidenceTerminal)
    {
        terminal->ResetActivation();
        // 現地確認中は端末を操作できないようにし、確認後に戻ってから次の映像を見せます。
        terminal->SetManualInteractionAllowed(
            m_Patrol.GetState() == SurveillancePatrol::State::Idle);
    }
}

void StageScene::StartPatrolCaught(Player& player)
{
    SetPatrolAnomalyVisible(m_Patrol.GetAnomaly(), false);
    if (!m_PatrolCaught.Start(CaughtSequence::Reason::FinalPursuit))
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

void StageScene::UpdatePatrolCaught(Player& player, float deltaTime)
{
    m_PatrolCaught.Advance(deltaTime);
    if (!m_PatrolCaught.IsReadyToRecover())
    {
        return;
    }

    // 暗転中に端末の前へ戻し、巡回はやり直せる状態にします（対処済みの回数は保持）。
    m_PatrolCaught.Complete();
    player.SetPosition(Vector3(190.0f, -99.0f, -42.0f));
    player.SetCanControl(true);
    if (ShadowMan* figure = m_Objects.evidenceShadow)
    {
        figure->SetActive(false);
    }
    if (FuseBox* terminal = m_Objects.evidenceTerminal)
    {
        terminal->ResetActivation();
        terminal->SetManualInteractionAllowed(true);
    }
    ShowPatrolNotice("気がつくと端末の前にいた", 3.0f);
}

void StageScene::CompletePatrol(Player& player)
{
    m_EvidenceHandled = true;
    m_EvidenceNoticeTimer = 3.2f;
    player.AddBattery(8.0f);

    if (FuseBox* terminal = m_Objects.evidenceTerminal)
    {
        terminal->SetManualInteractionAllowed(false);
    }
    if (Wall* marker = m_Objects.evidenceMarker)
    {
        marker->SetAppearance(
            Color(0.08f, 0.18f, 0.10f, 1.0f),
            Color(0.16f, 0.52f, 0.22f, 1.0f),
            44.0f);
    }
    if (CeilingLight* roomLight = m_Objects.CeilingLightAt(3))
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

void StageScene::ShowPatrolNotice(const char* text, float seconds)
{
    m_PatrolNoticeText = text;
    m_PatrolNoticeTimer = seconds;
}

void StageScene::UpdateCorridorLoop(Player& player)
{
    const float deltaTime = Application::GetDeltaTime();
    if (m_LoopCooldown > 0.0f)
    {
        m_LoopCooldown -= deltaTime;
    }
    if (m_LoopNoticeTimer > 0.0f)
    {
        m_LoopNoticeTimer -= deltaTime;
    }

    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored() || m_LoopCooldown > 0.0f)
    {
        return;
    }

    const Vector3 position = player.GetPosition();
    const bool insideLoopExit =
        position.x > 178.0f &&
        position.z > 237.0f && position.z < 273.0f;
    if (insideLoopExit)
    {
        AdvanceCorridorLoop(player);
    }
}

void StageScene::AdvanceCorridorLoop(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    ++m_CorridorLoopCount;

    // 同じフレーム後半でPlayerがカメラを更新するため、再配置の瞬間を隠しつつ
    // プレイヤーが見ていた方向を維持できます。
    player.SetPosition(Vector3(0.0f, -99.0f, -150.0f));
    m_LoopCooldown = 1.0f;
    m_ProgressHintTimer = 0.0f;
    m_LoopNoticeTimer = 2.4f;

    const int loopPhase = m_CorridorLoopCount < 3
        ? m_CorridorLoopCount
        : 3;

    for (int markerIndex = 1; markerIndex <= 3; ++markerIndex)
    {
        Wall* marker = m_Objects.loopMarkers[static_cast<std::size_t>(markerIndex - 1)];
        if (marker != nullptr)
        {
            marker->SetVisible(markerIndex <= loopPhase);
            const float intensity =
                0.14f + static_cast<float>(markerIndex) * 0.055f;
            marker->SetAppearance(
                Color(0.26f, 0.012f, 0.008f, 1.0f),
                Color(intensity, 0.002f, 0.001f, 1.0f),
                24.0f);
        }
    }
    game->GetPostProcess()->TriggerHorrorPulse(
        0.24f + static_cast<float>(loopPhase) * 0.13f,
        0.42f + static_cast<float>(loopPhase) * 0.10f);
    Input::SetVibration(7 + loopPhase * 3, 0.18f + loopPhase * 0.04f);

    // 入口へ戻すときに廊下の扉も復元します。同じ境界を再び開けさせることで、
    // 各周回が意図的な反復として感じられるようにします。
    Door* loopDoor = m_Objects.loopDoor;
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(loopPhase);
    }

    CeilingLight* entranceLight =
        m_Objects.CeilingLightAt(1);
    CeilingLight* middleLight =
        m_Objects.CeilingLightAt(4);
    CeilingLight* cornerLight =
        m_Objects.CeilingLightAt(8);

    if (loopPhase == 1)
    {
        Item* secondFuse = m_Objects.secondFuse;
        if (secondFuse != nullptr && !secondFuse->IsCollected())
        {
            secondFuse->SetActive(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(false, 2.4f);
        }
    }
    else if (loopPhase == 2)
    {
        Item* thirdFuse = m_Objects.thirdFuse;
        if (thirdFuse != nullptr && !thirdFuse->IsCollected())
        {
            thirdFuse->SetActive(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, 5.8f);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(true, 1.1f);
        }

        game->RequestAddObject<ShadowMan>(
            [](ShadowMan& shadow)
            {
                shadow.SetPosition(0.0f, -99.0f, -25.0f);
            });
    }
    else
    {
        if (entranceLight != nullptr)
        {
            entranceLight->SetEmergencyLight(false, 0.0f);
        }
        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, 8.2f);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(false, 4.3f);
        }

        // 警告文どおり、プレイヤーの背後に一度だけ人影を出現させます。
        if (m_CorridorLoopCount == 3)
        {
            game->RequestAddObject<ShadowMan>(
                [this](ShadowMan& shadow)
                {
                    shadow.SetPosition(0.0f, -99.0f, -170.0f);
                    shadow.EnableGazeScare();
                    shadow.SetOnObserved(
                        [this]()
                        {
                            StartScareLightSequence();
                        });
                });
        }
    }
}

void StageScene::StartScareLightSequence()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        return;
    }

    m_ScareLightSequence.Start();

    CeilingLight* entrance =
        m_Objects.CeilingLightAt(1);
    CeilingLight* middle =
        m_Objects.CeilingLightAt(4);
    CeilingLight* hall =
        m_Objects.CeilingLightAt(5);
    CeilingLight* corner =
        m_Objects.CeilingLightAt(8);
    CeilingLight* loopExit =
        m_Objects.CeilingLightAt(7);

    if (entrance != nullptr)
    {
        entrance->SetEmergencyLight(true, 0.35f);
    }
    if (middle != nullptr)
    {
        middle->SetEmergencyLight(false, 8.2f);
    }
    if (hall != nullptr)
    {
        hall->SetEmergencyLight(false, 3.1f);
    }
    if (corner != nullptr)
    {
        corner->SetEmergencyLight(false, 4.3f);
    }
    if (loopExit != nullptr)
    {
        loopExit->SetEmergencyLight(false, 4.5f);
    }
}

void StageScene::UpdateScareLightSequence()
{
    const float deltaTime = Application::GetDeltaTime();
    m_ScareLightSequence.UpdateNotice(deltaTime);

    if (!m_ScareLightSequence.IsActive())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        m_ScareLightSequence.Cancel();
        return;
    }

    m_ScareLightSequence.Advance(deltaTime);

    CeilingLight* entrance =
        m_Objects.CeilingLightAt(1);
    CeilingLight* middle =
        m_Objects.CeilingLightAt(4);
    CeilingLight* hall =
        m_Objects.CeilingLightAt(5);
    CeilingLight* corner =
        m_Objects.CeilingLightAt(8);
    CeilingLight* loopExit =
        m_Objects.CeilingLightAt(7);

    switch (m_ScareLightSequence.ConsumePendingBeat())
    {
    case 0:
        if (entrance != nullptr)
        {
            entrance->SetEmergencyLight(false, 0.35f);
        }
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, 0.75f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.13f, 0.18f);
        Input::SetVibration(5, 0.10f);
        break;
    case 1:
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(false, 0.75f);
        }
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(true, 0.42f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.14f, 0.18f);
        Input::SetVibration(6, 0.13f);
        break;
    case 2:
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(false, 0.42f);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, 0.18f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.20f);
        Input::SetVibration(7, 0.16f);
        break;
    case 3:
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(false, 0.18f);
        }
        if (loopExit != nullptr)
        {
            loopExit->SetEmergencyLight(true, 0.08f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.19f, 0.22f);
        Input::SetVibration(8, 0.19f);
        break;
    case 4:
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, 2.6f);
        }
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(true, 3.1f);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, 4.3f);
        }
        Input::SetVibration(4, 0.09f);
        break;
    default:
        break;
    }
}

void StageScene::StartFuseWatcher(int fuseCount)
{
    if (fuseCount < 2 || Core::Game::GetInstance()->IsPowerRestored())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* watcher = m_Objects.fuseWatcher;
    if (watcher == nullptr)
    {
        return;
    }

    watcher->SetActive(false);
    watcher->SetPosition(
        0.0f,
        -99.0f,
        fuseCount == 2 ? -22.0f : 118.0f);
    watcher->SetActive(true);
    watcher->EnableGazeScare(fuseCount == 2 ? 5.8f : 7.2f);
    watcher->SetOnObserved(
        [this]()
        {
            m_FuseWatcherState = 2;
            m_FuseWatcherNoticeTimer = 1.8f;

            Core::Game* game = Core::Game::GetInstance();
            game->RegisterAnomalyHandled();
            ShadowMan* activeWatcher =
                m_Objects.fuseWatcher;
            if (activeWatcher != nullptr)
            {
                activeWatcher->SetActive(false);
            }

            CeilingLight* reactionLight =
                m_Objects.CeilingLightAt(4);
            if (reactionLight != nullptr)
            {
                reactionLight->TriggerEventFlicker(0.72f, 0.74f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.42f, 0.18f);
            Input::SetVibration(7, 0.16f);
        });

    m_FuseWatcherState = 1;
    m_FuseWatcherNoticeTimer = fuseCount == 2 ? 5.8f : 7.2f;
    game->GetPostProcess()->TriggerHorrorPulse(
        fuseCount == 2 ? 0.20f : 0.32f,
        0.30f);
}

void StageScene::UpdatePowerRestoreSequence()
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();
    const bool powerRestored = game->IsPowerRestored();

    if (m_PowerSequence.ObservePowerState(powerRestored))
    {
        m_ProgressHintTimer = 0.0f;

        // ここから先の画面・照明・音の演出は通電シーケンス側で一括管理します。
        m_ScareLightSequence.Cancel();
        m_ScareLightSequence.ClearNotice();

        for (Wall* marker : m_Objects.loopMarkers)
        {
            marker->SetVisible(false);
        }
    }

    if (!powerRestored || !m_PowerSequence.IsRestoreActive())
    {
        return;
    }

    m_PowerSequence.AdvanceRestore(deltaTime);

    switch (m_PowerSequence.ConsumeRestoreBeat())
    {
    case 0:
        game->GetPostProcess()->TriggerBloomPulse(1.18f, 0.55f);
        Input::SetVibration(9, 0.16f);
        break;
    case 1:
        game->GetPostProcess()->TriggerHorrorPulse(0.12f, 0.22f);
        Input::SetVibration(6, 0.11f);
        break;
    case 2:
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.40f);
        Input::SetVibration(4, 0.07f);
        break;
    default:
        break;
    }
}

void StageScene::UpdateExitPowerSequence()
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();
    FuseBox* panel = m_Objects.exitPowerPanel;
    if (panel == nullptr || !panel->IsActivated())
    {
        return;
    }

    if (m_PowerSequence.BeginExitIfNeeded())
    {
        m_ProgressHintTimer = 0.0f;

        CeilingLight* corner = m_Objects.CeilingLightAt(7);
        CeilingLight* exitLight = m_Objects.CeilingLightAt(8);
        if (corner != nullptr) corner->SetForcedOff(true);
        if (exitLight != nullptr) exitLight->SetForcedOff(true);
        game->GetPostProcess()->TriggerHorrorPulse(0.30f, 0.28f);
        return;
    }

    if (m_PowerSequence.IsExitComplete())
    {
        return;
    }

    m_PowerSequence.AdvanceExit(deltaTime);
    switch (m_PowerSequence.ConsumeExitBeat())
    {
    case 0:
    {
        CeilingLight* corner = m_Objects.CeilingLightAt(7);
        if (corner != nullptr)
        {
            corner->SetForcedOff(false);
            corner->SetEmergencyLight(false, 0.0f);
            corner->TriggerEventFlicker(0.72f, 0.82f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.68f, 0.24f);
        Input::SetVibration(5, 0.11f);
        break;
    }
    case 1:
    {
        CeilingLight* exitLight = m_Objects.CeilingLightAt(8);
        if (exitLight != nullptr)
        {
            exitLight->SetForcedOff(false);
            exitLight->SetEmergencyLight(false, 0.0f);
            exitLight->TriggerEventFlicker(0.82f, 0.92f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.92f, 0.30f);
        Input::SetVibration(7, 0.15f);
        break;
    }
    case 2:
        game->GetPostProcess()->TriggerBloomPulse(1.18f, 0.42f);
        Input::SetVibration(10, 0.20f);
        break;
    default:
        break;
    }
}

void StageScene::UpdateExitOmen(Player& player)
{
    const float deltaTime = Application::GetDeltaTime();
    m_ExitOmenSequence.Update(deltaTime);
    if (m_ExitOmenSequence.IsTriggered())
    {
        Core::Game* game = Core::Game::GetInstance();
        switch (m_ExitOmenSequence.ConsumePendingBeat())
        {
        case 0:
        {
            CeilingLight* lightBehind =
                m_Objects.CeilingLightAt(8);
            if (lightBehind != nullptr)
            {
                lightBehind->SetForcedOff(true);
            }
            game->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.30f);
            Input::SetVibration(7, 0.16f);
            break;
        }
        case 1:
        {
            CeilingLight* exitLight =
                m_Objects.CeilingLightAt(7);
            if (exitLight != nullptr)
            {
                exitLight->SetFaulted(true);
                exitLight->TriggerEventFlicker(1.10f, 0.88f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.44f, 0.20f);
            break;
        }
        default:
            break;
        }
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 playerPosition = player.GetPosition();
    if (!m_PowerSequence.IsExitComplete() ||
        playerPosition.z < 215.0f ||
        playerPosition.x < 28.0f)
    {
        return;
    }

    m_ExitOmenSequence.Start();

    ShadowMan* shadow =
        m_Objects.exitOmen;
    if (shadow != nullptr)
    {
        shadow->SetActive(true);
        shadow->EnableGazeScare(4.2f);
    }

    for (int lightNumber : { 7, 8 })
    {
        CeilingLight* light = m_Objects.CeilingLightAt(lightNumber);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.82f, 0.78f);
        }
    }

    game->GetPostProcess()->TriggerHorrorPulse(0.28f, 0.42f);
    game->GetPostProcess()->TriggerBloomPulse(0.54f, 0.24f);
    Input::SetVibration(10, 0.22f);
}
