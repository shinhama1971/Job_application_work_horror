// ============================================================================
// ファイルの役割: 2面のシーンの初期化と、毎フレームの進行の中心（各異変・パズル・演出の更新を順に呼ぶ）を担当している。
// 主な技術: Sceneの処理を複数のファイルに分ける構成、有限状態機械、よく見て気づく謎解き、追われる演出
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


// 作るときに初期化している
Stage2Scene::Stage2Scene()
{
    Init();
}

// 壊すときに後片付けをしている
Stage2Scene::~Stage2Scene()
{
    Uninit();
}

// デバッグ画面に、周回・パズルの段階・間違えた回数・危険度・最後のイベントの状態・今回の異変を渡している
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

// この周回で見つけるべき異変を、それぞれの状態クラスに問い合わせて調べている
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
    // 操作できるフレームのUpdateで1回だけ実行している。
    m_PendingDebugAction = action;
}

// ループ廊下の基本の形と、周回によって表示を切り替える異変のObjectを準備している。
void Stage2Scene::Init()
{
    Core::Game* game = Core::Game::GetInstance();
    // 2面は電力が戻った状態から始め、画面効果を2面の雰囲気（少し暗く、光の筋あり）にしている
    game->SetPowerRestored(true);
    game->GetPostProcess()->SetCorridorTension(0.38f);
    game->GetPostProcess()->SetExposure(1.02f);
    game->GetPostProcess()->SetAtmosphere(0.20f, 0.62f);
    game->GetPostProcess()->SetLensDistortionStrength(0.20f);
    game->GetPostProcess()->SetFilmGradeStrength(0.58f);
    game->GetPostProcess()->SetSignalInterference(0.0f);
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetVolumetricIntensity(0.38f);

    // 進行の状態をすべて最初に戻している
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
    // 1周目・2周目に探させる異変を、偽の扉・時計・肖像画・壁のノックから毎回ランダムに2つ選んでいる。
    m_AnomalyPlan.Randomize(m_PresenceRandom);
    // 今回出る異変は、リザルト画面の「今回の発見」に出している。
    game->SetStage2Anomalies(
        static_cast<int>(m_AnomalyPlan.GetRequired(1)),
        static_cast<int>(m_AnomalyPlan.GetRequired(2)));
    m_PuzzleFeedback.Reset();
    m_NoiseThreatSystem.Reset();
    m_TensionPulse.Reset();
    // 最初の気配は、周回に慣れた頃（34秒後）に出している。
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

    // 壁・照明・端末などの配置はStage2Layoutが担当し、使うObjectのポインタをまとめて返している。
    m_Objects = Stage2Layout::Build(
        *game, m_ClockAnomaly.GetHourAngle(), m_ClockAnomaly.GetMinuteAngle());

    // カメラの位置を最初から合わせるため、プレイヤーを一度更新している
    m_Objects.player->Update();
    m_Hud.Init();
    SetupPracticalLights();
}

// 信号盤の目印や扉のランプ、肖像画の目が、自分の光る色で廊下を照らすようにしている。
// 色の変化（正解で緑になる、異変で赤く光るなど）が、そのまま周りの光の色になる。
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

// 周回の数、視線、足音、信号盤パズル、追われる演出を同時に見て、進行を更新している。
void Stage2Scene::Update()
{
    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    if (player == nullptr)
    {
        return;
    }

    // 周回中は歩くだけにし、最後の追跡が始まったときだけ走れるようにしている。
    // 静かに歩き続ける緊張と、追跡での解放感を分けるためである。
    player->SetSprintAllowed(
        m_FinalSequence.IsSequenceActive() || m_FinalSequence.IsPursuitActive());

    // 出口から脱出している間は、追跡をやめ、影をすべて消して何もしない
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
    // 捕まった演出の最中は、その処理だけを行っている
    if (m_CaughtSequence.IsActive())
    {
        UpdateCaughtSequence(*player, deltaTime);
        return;
    }

    if (!player->CanControl())
    {
        return;
    }

    // デバッグ画面から頼まれた操作（周回を進める・最後の停電・照明の演出）を実行している
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

    // 周回の切り替えの演出は4.2秒で終えている
    m_VisualTimer += deltaTime;
    if (m_LoopTransitionTimer >= 0.0f)
    {
        m_LoopTransitionTimer += deltaTime;
        if (m_LoopTransitionTimer >= 4.20f)
        {
            m_LoopTransitionTimer = -1.0f;
        }
    }
    // 進行が止まっている時間を数え、H（LB）が押されたら、すぐに強いヒントを出している
    m_ProgressHintTimer += deltaTime;
    if (Input::GetKeyTrigger(VK_H) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_ProgressHintTimer = (std::max)(m_ProgressHintTimer, 30.0f);
        Input::SetVibration(2, 0.04f);
    }

    // 各タイマーと知らせの残り時間を減らし、心拍・呼吸を更新している
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
    UpdateTensionPulse(*player, deltaTime);

    // 水たまりの上にいるかを調べ、足音を水音にしている（3つの水たまりの範囲は配置と同じ）
    const Vector3 playerPosition = player->GetPosition();
    const bool onWetSurface =
        (std::abs(playerPosition.x + 9.0f) <= 20.0f &&
            std::abs(playerPosition.z + 82.0f) <= 11.0f) ||
        (std::abs(playerPosition.x - 10.0f) <= 19.0f &&
            std::abs(playerPosition.z - 12.0f) <= 12.5f) ||
        (std::abs(playerPosition.x + 7.0f) <= 22.0f &&
            std::abs(playerPosition.z - 92.0f) <= 10.0f);
    player->SetWetSurface(onWetSurface);

    // 濡れた面は止まった絵にせず、細かな反射の揺れと、危険なときの照明の反射を加えている。
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
        // しばらく進めていないとき、探すべき異変の近くの照明を揺らしている（偽の扉は奥寄り、時計と肖像画は中央付近、
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
        // プレイヤーが廊下の扉を開けて通った後だけ、周回の状態を変えている。
        // 扉の中心の座標はz=140。
        player->GetPosition().z > 146.0f)
    {
        AdvanceLoop(*player);
    }

    // 信号盤が直り、準備ができていれば、廊下の中央（z>-8）まで来たときに最後の停電を始めている
    if (m_SignalPuzzle.IsComplete() && m_FinalSequenceArmed &&
        !m_FinalSequence.IsSequenceActive() &&
        player->GetPosition().z > -8.0f)
    {
        StartFinalSequence();
    }
    // 照明の区画・各異変・時計を更新している
    UpdateLightZones(*player);
    UpdateScratchMessage(*player, deltaTime);
    UpdatePortraitAnomaly(*player, deltaTime);
    UpdateKnockingAnomaly(*player, deltaTime);
    UpdateFalseDoorAnomaly(*player);
    UpdateClock(deltaTime);
    UpdateClockObservation();
    // ロッカーは周回中だけ使える（信号盤パズルと最後の追跡では、隠れずに対処させるため）。
    const bool lockersUsable = m_LoopCount < 3 &&
        !m_FinalSequence.IsSequenceActive() && !m_FinalSequence.IsPursuitActive();
    for (Locker* locker : m_Objects.lockers)
    {
        if (locker != nullptr)
        {
            locker->SetUsable(lockersUsable);
        }
    }
    // 足音の危険度と背後の気配を更新している
    UpdateNoiseThreat(*player, deltaTime);
    UpdateBehindPresence(*player, deltaTime);

    // 非常用充電器を使ったら、電池を回復する代わりに、音で危険度を大きく上げている
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

    // 残された記録を回収したら、電池を少し回復し、危険度を下げ、目印を緑に変えている
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

    // 信号盤パズルと、その間に背後から来る影を更新している
    UpdateSignalPuzzle();
    UpdateSignalStalker();

    // この周回の異変を見つけたら、異常確認のスイッチを押せるようにし、押したら扉の鍵を外している
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

    // 画面効果を、周回の進み具合・近くの照明の明るさ・危険度に合わせて変えている
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
    // ライトを消しているときと暗い場所では、目が慣れたように露出を上げている
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
    // 信号盤パズルの間と、足音の影が近いときは、監視映像のような信号の乱れを出している
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
    // 演出の更新（影を見たときの照明・最後の追跡・最後の停電）
    UpdateObservedScare(deltaTime);
    UpdateFinalPursuit(deltaTime);
    UpdateFinalSequence(deltaTime);

    // 扉のランプ：鍵がかかっている間は赤く、外れたら緑でゆっくり点滅させている
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

    // 調べる対象を選び、Eキー（A）で調べている
    m_InteractionSystem.Update(*player);
}




// 画面効果を普段の値に戻し、配置で作ったObjectを破棄している
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

    // Stage2Layoutが作るときに記録した名前の一覧で破棄している（名前を書く場所を1か所にするため）。
    for (const std::string& name : m_Objects.objectNames)
    {
        game->DestroyObj(name);
    }
    m_Hud.Uninit();
}
