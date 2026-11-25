// ============================================================================
// ファイルの役割: 2面の毎フレームの進行：時間、進む先の明滅、進み具合、異変、ロッカー、非常用の充電器、証拠品、確認パネル。
// （Stage2Scene.cpp から分けた。説明は元のファイルとヘッダーを参照）
// ============================================================================

#include "Stage2Scene.h"
#include "Locker.h"
#include "Application.h"
#include "BatteryItem.h"
#include "Camera.h"
#include "CaptureMode.h"
#include "CeilingLight.h"
#include "Door.h"
#include "EventFeedback.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"
#include "Player.h"
#include "Renderer.h"
#include "SceneLayoutBuilder.h"
#include "ShadowMan.h"
#include "Wall.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string_view>
#include "Stage2SceneConstants.h"

using namespace DirectX::SimpleMath;

namespace
{

    using EventFeedback::FlickerSpec;
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;

    // --- ヒントと案内：H（LB）でヒントを頼んだときの振動、しばらく進めていないときに異変の近くの照明を揺らす明滅と光
    constexpr RumbleSpec HintRequestRumble{ 2, 0.04f };
    constexpr FlickerSpec GuidanceFlicker{ 0.72f, 0.58f };
    constexpr PulseSpec GuidanceBloom{ 0.30f, 0.16f };
    // ロッカーの中で、すべての位置付きの音をこもらせる量（扉越しに聞こえる）
    constexpr float LockerListenerMuffle = 0.45f;

    // --- 非常用充電器を使ったとき
    constexpr FlickerSpec ChargerFlicker{ 1.10f, 0.88f };
    constexpr PulseSpec ChargerPulse{ 0.28f, 0.30f };
    constexpr RumbleSpec ChargerRumble{ 7, 0.16f };
    // --- 残された記録を回収したとき：目印を緑に変える色と光沢、光と振動
    const Color EvidenceCollectedDiffuse(0.06f, 0.14f, 0.08f, 1.0f);
    const Color EvidenceCollectedEmission(0.08f, 0.30f, 0.12f, 1.0f);
    constexpr float EvidenceCollectedShininess = 44.0f;
    constexpr PulseSpec EvidenceBloom{ 0.38f, 0.18f };
    constexpr RumbleSpec EvidenceRumble{ 4, 0.08f };
    // --- 異常確認のスイッチを押して扉の鍵が外れたとき
    constexpr FlickerSpec ConfirmationFlicker{ 0.90f, 0.78f };
    constexpr PulseSpec ConfirmationBloom{ 0.76f, 0.28f };
    constexpr RumbleSpec ConfirmationRumble{ 7, 0.16f };
}

// 周回の切り替えの演出・ヒント・知らせ・各タイマーを進め、心拍と呼吸を更新している
void Stage2Scene::UpdateTimers(Player& player, float deltaTime)
{
    // 周回の切り替えの演出は Stage2LoopTransitionSeconds で終えている
    m_VisualTimer += deltaTime;
    if (m_LoopTransitionTimer >= 0.0f)
    {
        m_LoopTransitionTimer += deltaTime;
        if (m_LoopTransitionTimer >= Stage2LoopTransitionSeconds)
        {
            m_LoopTransitionTimer = -1.0f;
        }
    }
    // 進行が止まっている時間を数え、H（LB）が押されたら、すぐに強いヒントを出している
    m_ProgressHintTimer += deltaTime;
    if (Input::GetKeyTrigger(VK_H) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_ProgressHintTimer = (std::max)(m_ProgressHintTimer, Stage2RequestedHintSeconds);
        EventFeedback::Rumble(HintRequestRumble);
    }

    // 各タイマーと知らせの残り時間を減らし、心拍・呼吸を更新している
    m_LoopCooldown = (std::max)(0.0f, m_LoopCooldown - deltaTime);
    m_Notices.Tick(deltaTime);
    m_ObservedScareSequence.UpdateNoticeTimer(deltaTime);
    m_ScratchAnomaly.UpdateNoticeTimer(deltaTime);
    m_PortraitAnomaly.UpdateNoticeTimer(deltaTime);
    m_KnockingAnomaly.UpdateNoticeTimer(deltaTime);
    m_UnseenPresence.UpdateNoticeTimer(deltaTime);
    m_FalseDoorAnomaly.UpdateNoticeTimer(deltaTime);
    m_ClockAnomaly.UpdateNoticeTimer(deltaTime);
    m_PuzzleFeedback.Update(deltaTime);
    m_NoiseThreatSystem.UpdateTimers(deltaTime);
    UpdateTensionPulse(player, deltaTime);

    // 水たまりの上の足音は、濡れた床（Ground）の判定で Player が水音にしている（水たまりの反射・波も1面と同じ）
    m_LoopBlinkTimer =
        (std::max)(0.0f, m_LoopBlinkTimer - deltaTime);
    m_FinalSequence.UpdateCountdowns(deltaTime);
    m_GuidancePulseCooldown = (std::max)(
        0.0f, m_GuidancePulseCooldown - deltaTime);
}

// しばらく進めていないとき、探すべき異変の近くの照明を揺らしている（偽の扉は奥寄り、時計と肖像画は中央付近、
// ノックは音の出どころの近く、血の手形は最後に現れた手形の近く、鏡は鏡の真上、見えない影はその影の真上、水たまりは中央の水たまりの真上）。
void Stage2Scene::UpdateGuidancePulse(Door* corridorDoor)
{
    if (corridorDoor == nullptr || !corridorDoor->IsLocked() ||
        m_ProgressHintTimer < Stage2GuidanceHintSeconds ||
        m_GuidancePulseCooldown > 0.0f)
    {
        return;
    }
    const Stage2Anomaly requiredAnomaly = m_AnomalyPlan.GetRequired(m_LoopCount);
    CeilingLight* guideLight = m_Objects.Light(
        requiredAnomaly == Stage2Anomaly::FalseDoor ? Stage2Light::Light3 :
        requiredAnomaly == Stage2Anomaly::Mirror ? Stage2NearestLight(MirrorAnomaly::CenterZ) :
        requiredAnomaly == Stage2Anomaly::Puddle ? Stage2NearestLight(PuddleReflectionAnomaly::PuddleCenterZ) :
        requiredAnomaly == Stage2Anomaly::Handprint
            ? Stage2NearestLight(m_HandprintAnomaly.GetLatestPosition().z) :
        requiredAnomaly == Stage2Anomaly::Knocking
            ? Stage2NearestLight(GetKnockListenPoint().z) :
        requiredAnomaly == Stage2Anomaly::Unseen
            ? Stage2NearestLight(m_UnseenPresence.GetPosition().z)
            : Stage2Light::Light2);
    if (guideLight != nullptr)
    {
        EventFeedback::Flicker(*guideLight, GuidanceFlicker);
    }
    EventFeedback::BloomPulse(*Core::Game::GetInstance()->GetPostProcess(), GuidanceBloom);
    m_GuidancePulseCooldown = Stage2GuidancePulseInterval;
}

// 廊下の扉を開けて通った後に周回を進め、信号盤が直って準備ができていれば、廊下の中央まで来たときに最後の停電を始めている
void Stage2Scene::UpdateProgression(Player& player)
{
    if (m_LoopCount < Stage2FinalLoopCount && m_LoopCooldown <= 0.0f &&
        // 扉の中心の座標はz=140。
        player.GetPosition().z > Stage2LoopDoorPassZ)
    {
        AdvanceLoop(player);
    }

    if (m_SignalPuzzle.IsComplete() && m_FinalSequenceArmed &&
        !m_FinalSequence.IsSequenceActive() &&
        player.GetPosition().z > Stage2FinalSequenceStartZ)
    {
        StartFinalSequence();
    }
}

// 照明の区画・各異変・時計を更新している
void Stage2Scene::UpdateAnomalies(Player& player, float deltaTime)
{
    UpdateLightZones(player);
    UpdateScratchMessage(player, deltaTime);
    UpdatePortraitAnomaly(player, deltaTime);
    UpdatePortraitEyes(deltaTime);
    UpdateHandprintAnomaly(player, deltaTime);
    UpdateMirrorAnomaly(player, deltaTime);
    UpdatePuddleAnomaly(player, deltaTime);
    UpdateFootprintFollower(player, deltaTime);
    // ロッカーの中では、扉越しに聞こえるよう、すべての位置付きの音をこもらせている
    Core::Game::GetInstance()->SetListenerMuffle(player.IsHiding() ? LockerListenerMuffle : 0.0f);
    UpdateKnockingAnomaly(player, deltaTime);
    UpdateUnseenPresence(player, deltaTime);
    UpdateFalseDoorAnomaly(player);
    UpdateClock(deltaTime);
    UpdateClockObservation();
}

// ロッカーは周回中だけ使える（信号盤パズルと最後の追跡では、隠れずに対処させるため）。
void Stage2Scene::UpdateLockers()
{
    const bool lockersUsable = m_LoopCount < Stage2FinalLoopCount &&
        !m_FinalSequence.IsSequenceActive() && !m_FinalSequence.IsPursuitActive();
    for (Locker* locker : m_Objects.lockers)
    {
        if (locker != nullptr)
        {
            locker->SetUsable(lockersUsable);
        }
    }
}

// 非常用充電器を使ったら、電池を回復する代わりに、音で危険度を大きく上げている
void Stage2Scene::UpdateEmergencyCharger(Player& player)
{
    FuseBox* emergencyCharger = m_Objects.emergencyCharger;
    if (m_ChargerHandled || emergencyCharger == nullptr || !emergencyCharger->IsActivated())
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    m_ChargerHandled = true;
    m_Notices.charger = Stage2NoticeSeconds;
    m_NoiseThreatSystem.SetWarningTimer(Stage2ChargerWarningSeconds);
    m_NoiseThreatSystem.SetThreat((std::max)(
        m_NoiseThreatSystem.GetThreat(), Stage2ChargerThreat));
    m_NoiseThreatSystem.SetEventCooldown(Stage2ChargerEventCooldown);
    player.AddBattery(Stage2ChargerBattery);
    game->RegisterChargerUsed();

    CeilingLight* startLight = m_Objects.Light(Stage2Light::Light1);
    if (startLight != nullptr)
    {
        EventFeedback::Flicker(*startLight, ChargerFlicker);
    }
    EventFeedback::HorrorPulse(*game->GetPostProcess(), ChargerPulse);
    EventFeedback::Rumble(ChargerRumble);
}

// 残された記録を回収したら、電池を少し回復し、危険度を下げ、目印を緑に変えている
void Stage2Scene::UpdateEvidence(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    for (std::size_t evidenceIndex = 0; evidenceIndex < m_Objects.evidenceTerminals.size(); ++evidenceIndex)
    {
        FuseBox* evidence = m_Objects.evidenceTerminals[evidenceIndex];
        if (m_EvidenceHandled[evidenceIndex] || evidence == nullptr || !evidence->IsActivated())
        {
            continue;
        }
        m_EvidenceHandled[evidenceIndex] = true;
        m_Notices.evidence = Stage2ImportantNoticeSeconds;
        game->RegisterEvidenceCollected();
        player.AddBattery(Stage2EvidenceBattery);
        m_NoiseThreatSystem.SetThreat((std::max)(
            0.0f, m_NoiseThreatSystem.GetThreat() - Stage2EvidenceThreatRelief));
        Wall* marker = m_Objects.evidenceMarkers[evidenceIndex];
        if (marker != nullptr)
        {
            marker->SetAppearance(EvidenceCollectedDiffuse, EvidenceCollectedEmission, EvidenceCollectedShininess);
        }
        EventFeedback::BloomPulse(*game->GetPostProcess(), EvidenceBloom);
        EventFeedback::Rumble(EvidenceRumble);
    }
}

// この周回の異変を見つけたら、異常確認のスイッチを押せるようにし、押したら扉の鍵を外している
void Stage2Scene::UpdateConfirmationPanel(Door* corridorDoor)
{
    FuseBox* confirmationPanel = m_Objects.confirmationPanel;
    if (confirmationPanel == nullptr)
    {
        return;
    }
    const bool evidenceConfirmed = IsRequiredAnomalyFound();
    confirmationPanel->SetManualInteractionAllowed(evidenceConfirmed);
    if (!evidenceConfirmed || !confirmationPanel->IsActivated() || m_ConfirmationHandledThisLoop)
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    m_ConfirmationHandledThisLoop = true;
    game->RegisterAnomalyHandled();
    m_Notices.loop = Stage2NoticeSeconds;
    m_FalseDoorAnomaly.ClearNotice();
    m_ClockAnomaly.ClearNotice();
    m_ProgressHintTimer = 0.0f;
    if (corridorDoor != nullptr)
    {
        corridorDoor->SetLocked(false);
    }

    CeilingLight* doorLight = m_Objects.Light(Stage2Light::DoorLight);
    if (doorLight != nullptr)
    {
        EventFeedback::Flicker(*doorLight, ConfirmationFlicker);
    }
    EventFeedback::BloomPulse(*game->GetPostProcess(), ConfirmationBloom);
    EventFeedback::Rumble(ConfirmationRumble);
}
