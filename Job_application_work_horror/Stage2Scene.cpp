// ============================================================================
// ファイルの役割: 2面のループ廊下、謎解き、段階的な異変とクリア条件を管理します。
// 主な技術: シーン分割、有限状態機械、観察型パズル、追跡演出
// ============================================================================

#include "Stage2Scene.h"
#include "Locker.h"
#include "Application.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace DirectX::SimpleMath;

#include "Stage2SceneConstants.h"


Stage2Scene::Stage2Scene()
{
    Init();
}

Stage2Scene::~Stage2Scene()
{
    Uninit();
}

bool Stage2Scene::TryGetDebugInfo(SceneDebugInfo& info) const
{
    info.progressionStep = m_LoopCount;
    info.puzzleStep = m_SignalPuzzle.GetStep();
    info.puzzleMistakeCount = m_PuzzleFeedback.GetMistakeCount();
    info.threatLevel = m_NoiseThreatSystem.GetThreat();
    info.finalSequenceArmed = m_FinalSequenceArmed;
    info.exitReady = m_FinalDoorReady;
    info.firstAnomaly = Stage2AnomalyPlan::GetName(m_AnomalyPlan.GetRequired(1));
    info.secondAnomaly = Stage2AnomalyPlan::GetName(m_AnomalyPlan.GetRequired(2));
    return true;
}

bool Stage2Scene::IsRequiredAnomalyFound() const
{
    switch (m_AnomalyPlan.GetRequired(m_LoopCount))
    {
    case Stage2Anomaly::FalseDoor:
        return m_FalseDoorAnomaly.HasMoved();
    case Stage2Anomaly::Clock:
        return m_ClockAnomaly.WasObservedThisLoop();
    case Stage2Anomaly::Portrait:
        return m_PortraitAnomaly.HasChangedThisLoop();
    case Stage2Anomaly::Knocking:
        return m_KnockingAnomaly.WasFound();
    case Stage2Anomaly::None:
        break;
    }
    return false;
}

void Stage2Scene::RequestDebugAction(SceneDebugAction action)
{
    // 操作可能なフレームのUpdateで1回だけ実行します。
    m_PendingDebugAction = action;
}

// ループ廊下の基本形と、周回によって表示を切り替える異変Objectを準備します。
void Stage2Scene::Init()
{
    Core::Game* game = Core::Game::GetInstance();
    game->SetPowerRestored(true);
    game->GetPostProcess()->SetCorridorTension(0.38f);
    game->GetPostProcess()->SetExposure(1.02f);
    game->GetPostProcess()->SetAtmosphere(0.20f, 0.62f);
    game->GetPostProcess()->SetLensDistortionStrength(0.20f);
    game->GetPostProcess()->SetFilmGradeStrength(0.58f);
    game->GetPostProcess()->SetSignalInterference(0.0f);
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetVolumetricIntensity(0.38f);

    m_LoopCount = 0;
    m_LoopCooldown = 0.0f;
    m_Notices.Reset();
    m_Notices.loop = 2.8f;
    m_VisualTimer = 0.0f;
    m_ObservedScareSequence.Reset();
    m_FinalSequence.Reset();
    m_ScratchAnomaly.Reset();
    m_PortraitAnomaly.Reset();
    m_KnockingAnomaly.Reset();
    m_FalseDoorAnomaly.Reset();
    m_ClockAnomaly.Reset();
    // 1周目・2周目に探させる異変を、偽ドア・時計・肖像画から毎回ランダムに選びます。
    m_AnomalyPlan.Randomize(m_PresenceRandom);
    m_PuzzleFeedback.Reset();
    m_NoiseThreatSystem.Reset();
    // 最初の気配は周回に慣れた頃に出します。
    m_BehindPresence.Reset(BehindPresence::MaxInterval);
    m_LoopBlinkTimer = 0.0f;
    m_LoopTransitionTimer = -1.0f;
    m_CaughtSequence.Reset();
    m_ProgressHintTimer = 0.0f;
    m_GuidancePulseCooldown = 0.0f;
    m_LightZoneProgress.Reset();
    m_ConfirmationHandledThisLoop = false;
    m_ChargerHandled = false;
    m_EvidenceHandled[0] = false;
    m_EvidenceHandled[1] = false;
    m_SignalPuzzle.Reset();
    m_FinalSequenceArmed = false;
    m_FinalDoorReady = false;
    m_PendingDebugAction.reset();

    // 壁・照明・端末などの配置はStage2Layoutが担当し、使うObjectのポインタをまとめて返します。
    m_Objects = Stage2Layout::Build(
        *game, m_ClockAnomaly.GetHourAngle(), m_ClockAnomaly.GetMinuteAngle());

    m_Objects.player->Update();
    m_Hud.Init();
    SetupPracticalLights();
}

// 信号盤のマーカーや扉の表示灯、肖像の目が、自分の発光色で廊下を照らすようにします。
// 色の変化（正解で緑になる、異変で赤く光るなど）がそのまま周囲の光の色になります。
void Stage2Scene::SetupPracticalLights()
{
    m_Objects.doorIndicator->SetGlowLight(45.0f, 2.0f);
    for (Wall* marker : m_Objects.signalMarkers)
    {
        marker->SetGlowLight(40.0f, 1.8f);
    }
    for (Wall* marker : m_Objects.evidenceMarkers)
    {
        marker->SetGlowLight(40.0f, 1.8f);
    }
    for (Wall* eye : m_Objects.portraitEyes)
    {
        eye->SetGlowLight(30.0f, 2.4f);
    }
}

// 周回数、視線、騒音、信号パズル、追跡演出を同時に監視して進行を更新します。
void Stage2Scene::Update()
{
    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    if (player == nullptr)
    {
        return;
    }

    // 周回中は歩くだけにし、最後の追跡が始まったときだけ走れるようにします。
    // 静かに歩き続ける緊張と、追跡での解放感を分けるためです。
    player->SetSprintAllowed(
        m_FinalSequence.IsSequenceActive() || m_FinalSequence.IsPursuitActive());

    ExitTrigger* exit = m_Objects.exit;
    if (exit != nullptr && exit->IsEscaping())
    {
        m_FinalSequence.StopPursuit();
        ShadowMan* shadow = m_Objects.shadow;
        if (shadow != nullptr)
        {
            shadow->SetActive(false);
        }
        ShadowMan* noiseShadow = m_Objects.noiseShadow;
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        m_Objects.presence->SetActive(false);
        return;
    }

    const float deltaTime = Application::GetDeltaTime();
    if (m_CaughtSequence.IsActive())
    {
        UpdateCaughtSequence(*player, deltaTime);
        return;
    }

    if (!player->CanControl())
    {
        return;
    }

    if (m_PendingDebugAction.has_value())
    {
        const SceneDebugAction debugAction = *m_PendingDebugAction;
        m_PendingDebugAction.reset();
        switch (debugAction)
        {
        case SceneDebugAction::AdvanceProgression:
            if (m_LoopCount < 3)
            {
                AdvanceLoop(*player);
            }
            break;
        case SceneDebugAction::PlayFinalSequence:
            while (m_LoopCount < 3)
            {
                AdvanceLoop(*player);
            }
            m_SignalPuzzle.ForceComplete();
            m_FinalSequenceArmed = true;
            if (!m_FinalSequence.IsSequenceActive())
            {
                StartFinalSequence();
            }
            break;
        case SceneDebugAction::PlayLightingEvent:
            StartObservedScare();
            break;
        }
    }

    m_VisualTimer += deltaTime;
    if (m_LoopTransitionTimer >= 0.0f)
    {
        m_LoopTransitionTimer += deltaTime;
        if (m_LoopTransitionTimer >= 4.20f)
        {
            m_LoopTransitionTimer = -1.0f;
        }
    }
    m_ProgressHintTimer += deltaTime;
    if (Input::GetKeyTrigger(VK_H) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_ProgressHintTimer = (std::max)(m_ProgressHintTimer, 30.0f);
        Input::SetVibration(2, 0.04f);
    }

    m_LoopCooldown = (std::max)(0.0f, m_LoopCooldown - deltaTime);
    m_Notices.Tick(deltaTime);
    m_ObservedScareSequence.UpdateNoticeTimer(deltaTime);
    m_ScratchAnomaly.UpdateNoticeTimer(deltaTime);
    m_PortraitAnomaly.UpdateNoticeTimer(deltaTime);
    m_KnockingAnomaly.UpdateNoticeTimer(deltaTime);
    m_FalseDoorAnomaly.UpdateNoticeTimer(deltaTime);
    m_ClockAnomaly.UpdateNoticeTimer(deltaTime);
    m_PuzzleFeedback.Update(deltaTime);
    m_NoiseThreatSystem.UpdateTimers(deltaTime);

    const Vector3 playerPosition = player->GetPosition();
    const bool onWetSurface =
        (std::abs(playerPosition.x + 9.0f) <= 20.0f &&
            std::abs(playerPosition.z + 82.0f) <= 11.0f) ||
        (std::abs(playerPosition.x - 10.0f) <= 19.0f &&
            std::abs(playerPosition.z - 12.0f) <= 12.5f) ||
        (std::abs(playerPosition.x + 7.0f) <= 22.0f &&
            std::abs(playerPosition.z - 92.0f) <= 10.0f);
    player->SetWetSurface(onWetSurface);

    // 濡れ面は静止画にせず、微細な反射の揺れと危険時の照明反射を与えます。
    for (int puddleIndex = 0; puddleIndex < static_cast<int>(m_Objects.puddles.size()); ++puddleIndex)
    {
        Wall* puddle = m_Objects.puddles[puddleIndex];
        if (puddle == nullptr)
        {
            continue;
        }
        const float shimmer = std::sin(
            m_VisualTimer * (1.25f + puddleIndex * 0.17f) + puddleIndex * 2.1f)
            * 0.5f + 0.5f;
        const float dangerReflection =
            m_NoiseThreatSystem.GetThreat() * 0.055f;
        puddle->SetAppearance(
            Color(0.020f + shimmer * 0.010f,
                0.045f + shimmer * 0.014f,
                0.052f + shimmer * 0.018f, 0.76f),
            Color(0.035f + dangerReflection,
                0.078f + dangerReflection * 0.55f,
                0.095f + shimmer * 0.025f, 1.0f),
            96.0f + shimmer * 28.0f);
    }
    m_LoopBlinkTimer =
        (std::max)(0.0f, m_LoopBlinkTimer - deltaTime);
    m_FinalSequence.UpdateCountdowns(deltaTime);
    m_GuidancePulseCooldown = (std::max)(
        0.0f, m_GuidancePulseCooldown - deltaTime);

    Door* corridorDoor = m_Objects.door;
    if (corridorDoor != nullptr && corridorDoor->IsLocked() &&
        m_ProgressHintTimer >= 15.0f &&
        m_GuidancePulseCooldown <= 0.0f)
    {
        // 探すべき異変の近くの照明を揺らします（偽ドアは奥寄り、時計と肖像画は中央付近、
        // ノックは音の出どころの近く）。
        const Stage2Anomaly requiredAnomaly = m_AnomalyPlan.GetRequired(m_LoopCount);
        CeilingLight* guideLight = m_Objects.Light(
            requiredAnomaly == Stage2Anomaly::FalseDoor ? Stage2Light::Light3 :
            requiredAnomaly == Stage2Anomaly::Knocking
                ? Stage2NearestLight(GetKnockListenPoint().z)
                : Stage2Light::Light2);
        if (guideLight != nullptr)
        {
            guideLight->TriggerEventFlicker(0.72f, 0.58f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.30f, 0.16f);
        m_GuidancePulseCooldown = 2.8f;
    }

    if (m_LoopCount < 3 && m_LoopCooldown <= 0.0f &&
        // プレイヤーが廊下の扉を開けて通過した後だけ周回状態を変更します。
        // 扉の中心座標はz=140です。
        player->GetPosition().z > 146.0f)
    {
        AdvanceLoop(*player);
    }

    if (m_SignalPuzzle.IsComplete() && m_FinalSequenceArmed &&
        !m_FinalSequence.IsSequenceActive() &&
        player->GetPosition().z > -8.0f)
    {
        StartFinalSequence();
    }
    UpdateLightZones(*player);
    UpdateScratchMessage(*player, deltaTime);
    UpdatePortraitAnomaly(*player, deltaTime);
    UpdateKnockingAnomaly(*player, deltaTime);
    UpdateFalseDoorAnomaly(*player);
    UpdateClock(deltaTime);
    UpdateClockObservation();
    // ロッカーは周回中だけ使えます（信号盤パズルと最後の追跡では、隠れずに対処させるため）。
    const bool lockersUsable = m_LoopCount < 3 &&
        !m_FinalSequence.IsSequenceActive() && !m_FinalSequence.IsPursuitActive();
    for (Locker* locker : m_Objects.lockers)
    {
        if (locker != nullptr)
        {
            locker->SetUsable(lockersUsable);
        }
    }
    UpdateNoiseThreat(*player, deltaTime);
    UpdateBehindPresence(*player, deltaTime);

    FuseBox* emergencyCharger =
        m_Objects.emergencyCharger;
    if (!m_ChargerHandled && emergencyCharger != nullptr &&
        emergencyCharger->IsActivated())
    {
        m_ChargerHandled = true;
        m_Notices.charger = 2.8f;
        m_NoiseThreatSystem.SetWarningTimer(3.2f);
        m_NoiseThreatSystem.SetThreat((std::max)(
            m_NoiseThreatSystem.GetThreat(), 0.76f));
        m_NoiseThreatSystem.SetEventCooldown(0.12f);
        player->AddBattery(30.0f);
        game->RegisterChargerUsed();

        CeilingLight* startLight =
            m_Objects.Light(Stage2Light::Light1);
        if (startLight != nullptr)
        {
            startLight->TriggerEventFlicker(1.10f, 0.88f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.28f, 0.30f);
        Input::SetVibration(7, 0.16f);
    }

    for (int evidenceIndex = 0; evidenceIndex < 2; ++evidenceIndex)
    {
        FuseBox* evidence = m_Objects.evidenceTerminals[evidenceIndex];
        if (!m_EvidenceHandled[evidenceIndex] && evidence != nullptr &&
            evidence->IsActivated())
        {
            m_EvidenceHandled[evidenceIndex] = true;
            m_Notices.evidence = 3.2f;
            game->RegisterEvidenceCollected();
            player->AddBattery(6.0f);
            m_NoiseThreatSystem.SetThreat((std::max)(
                0.0f, m_NoiseThreatSystem.GetThreat() - 0.18f));
            Wall* marker = m_Objects.evidenceMarkers[evidenceIndex];
            if (marker != nullptr)
            {
                marker->SetAppearance(
                    Color(0.08f, 0.18f, 0.10f, 1.0f),
                    Color(0.16f, 0.52f, 0.22f, 1.0f),
                    44.0f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.38f, 0.18f);
            Input::SetVibration(4, 0.08f);
        }
    }

    UpdateSignalPuzzle();
    UpdateSignalStalker();

    FuseBox* confirmationPanel =
        m_Objects.confirmationPanel;
    const bool evidenceConfirmed = IsRequiredAnomalyFound();
    if (confirmationPanel != nullptr)
    {
        confirmationPanel->SetManualInteractionAllowed(evidenceConfirmed);
        if (evidenceConfirmed && confirmationPanel->IsActivated() &&
            !m_ConfirmationHandledThisLoop)
        {
            m_ConfirmationHandledThisLoop = true;
            game->RegisterAnomalyHandled();
            m_Notices.loop = 2.8f;
            m_FalseDoorAnomaly.ClearNotice();
            m_ClockAnomaly.ClearNotice();
            m_ProgressHintTimer = 0.0f;
            if (corridorDoor != nullptr)
            {
                corridorDoor->SetLocked(false);
            }

            CeilingLight* doorLight =
                m_Objects.Light(Stage2Light::DoorLight);
            if (doorLight != nullptr)
            {
                doorLight->TriggerEventFlicker(0.90f, 0.78f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.76f, 0.28f);
            Input::SetVibration(7, 0.16f);
        }
    }

    const float loopRate = static_cast<float>(m_LoopCount) / 3.0f;
    float localFixtureLight = 0.0f;
    for (CeilingLight* fixture : m_Objects.lights)
    {
        if (fixture == nullptr)
        {
            continue;
        }

        const float longitudinalDistance = std::fabs(
            fixture->GetPosition().z - player->GetPosition().z);
        const float proximity = 1.0f - (std::min)(
            longitudinalDistance / 105.0f, 1.0f);
        localFixtureLight = (std::max)(
            localFixtureLight,
            fixture->GetBrightness() * proximity);
    }
    const float localDarkness =
        1.0f - (std::clamp)(localFixtureLight, 0.0f, 1.0f);
    const float pulse = std::sin(m_VisualTimer * 1.7f) * 0.025f;
    game->GetPostProcess()->SetCorridorTension(
        (std::clamp)(0.38f + loopRate * 0.42f +
            m_NoiseThreatSystem.GetThreat() * 0.14f + pulse, 0.0f, 0.92f));
    game->GetPostProcess()->SetAtmosphere(
        0.20f + loopRate * 0.10f + localDarkness * 0.018f +
            m_NoiseThreatSystem.GetThreat() * 0.025f,
        0.62f + loopRate * 0.12f + localDarkness * 0.035f +
            m_NoiseThreatSystem.GetThreat() * 0.045f);
    const float adaptedExposure = player->IsFlashlightOn()
        ? 1.00f + localDarkness * 0.035f
        : 1.055f + localDarkness * 0.090f;
    game->GetPostProcess()->SetExposure(adaptedExposure);
    game->GetPostProcess()->SetLensDistortionStrength(
        0.20f + loopRate * 0.34f);
    game->GetPostProcess()->SetFilmGradeStrength(
        0.58f + loopRate * 0.20f);
    game->GetPostProcess()->SetLensDirtStrength(
        0.14f + loopRate * 0.08f);
    const bool signalRestorationActive =
        m_LoopCount >= 3 && !m_SignalPuzzle.IsComplete();
    const float unresolvedSignalRate =
        1.0f - static_cast<float>(m_SignalPuzzle.GetStep()) / 3.0f;
    ShadowMan* activeNoiseShadow =
        m_Objects.noiseShadow;
    const float stalkerInterference =
        activeNoiseShadow != nullptr && activeNoiseShadow->IsActive()
            ? (std::clamp)((m_NoiseThreatSystem.GetThreat() - 0.52f) * 0.72f,
                0.0f, 0.30f)
            : 0.0f;
    game->GetPostProcess()->SetSignalInterference(
        signalRestorationActive
            ? (std::clamp)(0.10f + m_NoiseThreatSystem.GetThreat() * 0.62f +
                unresolvedSignalRate * 0.16f, 0.0f, 0.88f)
            : stalkerInterference);
    game->GetPostProcess()->SetVolumetricLight(player->IsFlashlightOn());
    game->GetPostProcess()->SetVolumetricIntensity(
        0.38f + loopRate * 0.20f + localDarkness * 0.055f);
    UpdateObservedScare(deltaTime);
    UpdateFinalPursuit(deltaTime);
    UpdateFinalSequence(deltaTime);

    Wall* doorIndicator = m_Objects.doorIndicator;
    if (doorIndicator != nullptr && !m_FinalDoorReady)
    {
        const bool locked = corridorDoor != nullptr && corridorDoor->IsLocked();
        const float indicatorPulse = std::sin(
            m_VisualTimer * (locked ? 3.4f : 6.2f)) * 0.5f + 0.5f;
        if (locked)
        {
            doorIndicator->SetAppearance(
                Color(0.24f, 0.012f, 0.008f, 1.0f),
                Color(0.13f + indicatorPulse * 0.08f,
                    0.001f, 0.0f, 1.0f), 24.0f);
        }
        else
        {
            doorIndicator->SetAppearance(
                Color(0.04f, 0.18f, 0.035f, 1.0f),
                Color(0.015f, 0.16f + indicatorPulse * 0.10f,
                    0.008f, 1.0f), 28.0f);
        }
    }

    m_InteractionSystem.Update(*player);
}




void Stage2Scene::Uninit()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }

    game->GetPostProcess()->SetCorridorTension(0.0f);
    game->GetPostProcess()->SetAtmosphere(0.18f, 0.55f);
    game->GetPostProcess()->SetExposure(1.0f);
    game->GetPostProcess()->SetLensDistortionStrength(0.32f);
    game->GetPostProcess()->SetFilmGradeStrength(0.55f);
    game->GetPostProcess()->SetSignalInterference(0.0f);
    game->GetPostProcess()->SetVolumetricLight(false);
    game->GetPostProcess()->SetVolumetricIntensity(0.58f);

    // Stage2Layoutが生成時に記録した名前の一覧で破棄します（名前を書く場所を1か所にするため）。
    for (const std::string& name : m_Objects.objectNames)
    {
        game->DestroyObj(name);
    }
    m_Hud.Uninit();
}
