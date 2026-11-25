// ============================================================================
// ファイルの役割: 2面のシーンの初期化と、毎フレームの進行の中心（各異変・パズル・演出の更新を順に呼ぶ）を担当している。
// 主な技術: Sceneの処理を複数のファイルに分ける構成、有限状態機械、よく見て気づく謎解き、追われる演出
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

using namespace DirectX::SimpleMath;

#include "Stage2SceneConstants.h"

namespace
{
    constexpr float RoomOcclusionCeilingY = -48.5f;
    constexpr float RoomOcclusionStrength = 0.55f;
    constexpr float ExitLensDistortionStrength = Effect::PostProcess::StartupLensDistortionStrength;
    // 壁と天井の古さの強さ。血の手形や引っかき傷は2面では異変として出すので、決まった跡（ホラーの跡）は描かない
    constexpr float WallWeathering = 0.85f;
    constexpr float WallHorrorMarks = 0.0f;
    // 看板やランプが周りを照らす光（届く距離と強さ）：扉のランプ、信号盤と記録の端末の目印、肖像画の赤く光る目
    constexpr float DoorIndicatorGlowRange = 45.0f;
    constexpr float DoorIndicatorGlowIntensity = 2.0f;
    constexpr float MarkerGlowRange = 40.0f;
    constexpr float MarkerGlowIntensity = 1.8f;
    constexpr float PortraitGlowRange = 30.0f;
    constexpr float PortraitGlowIntensity = 2.4f;

}

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
    info.secondAnomaly = Stage2AnomalyPlan::GetName(m_AnomalyPlan.GetRequired(Stage2SecondLoop));
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
    case Stage2Anomaly::Handprint:
        return m_HandprintAnomaly.WasFound();
    case Stage2Anomaly::Mirror:
        return m_MirrorAnomaly.WasFound();
    case Stage2Anomaly::Unseen:
        return m_UnseenPresence.WasFound();
    case Stage2Anomaly::Puddle:
        return m_PuddleAnomaly.WasFound();
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
    InitializeCorridorAtmosphere();

    // 進行の状態をすべて最初に戻している
    m_LoopCount = 0;
    m_LoopDepth = 0.0f;
    Renderer::SetLoopDepth(0.0f);
    m_LoopCooldown = 0.0f;
    m_Notices.Reset();
    m_Notices.loop = Stage2NoticeSeconds;
    m_VisualTimer = 0.0f;
    m_ObservedScareSequence.Reset();
    m_FinalSequence.Reset();
    m_ScratchAnomaly.Reset();
    m_PortraitAnomaly.Reset();
    m_HandprintAnomaly.Reset();
    m_MirrorAnomaly.Reset();
    m_MirrorFigureVisibility = 0.0f;
    Renderer::SetMirrorFigure(Vector3::Zero, 0.0f);
    m_PuddleAnomaly.Reset();
    Renderer::SetPuddleFigure(Vector3::Zero, 0.0f);
    m_FootprintFollower.Reset();
    m_PortraitEyeOpen = 0.0f;
    m_PortraitEyeGlow = 0.0f;
    m_FootprintFollowerUsedThisLoop = false;
    m_RadioStatic.Reset();
    m_KnockingAnomaly.Reset();
    m_FalseDoorAnomaly.Reset();
    m_UnseenPresence.Reset();
    m_UnseenHasStep = false;
    m_UnseenWetSteps = 0;
    m_UnseenShadowVisibility = 0.0f;
    Renderer::SetUnseenFigure(Vector3::Zero, 0.0f);
    m_ClockAnomaly.Reset();
    // 1周目・2周目に探させる異変を、8つの候補（Stage2AnomalyPlan）から毎回ランダムに2つ選んでいる。
    m_AnomalyPlan.Randomize(m_PresenceRandom);
    // 今回出る異変は、リザルト画面の「今回の発見」に出している。
    game->SetStage2Anomalies(
        static_cast<int>(m_AnomalyPlan.GetRequired(1)),
        static_cast<int>(m_AnomalyPlan.GetRequired(Stage2SecondLoop)));
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
    // 壁と天井の古さ（継ぎ目・ひび・水の跡・カビ・天井板）と、部屋の角の暗がりを1面と同じ仕組みで描いている。
    // 血の手形や引っかき傷は2面では異変として出すため、決まった跡は描かない（ホラーの跡の濃さを0にしている）。
    Renderer::SetWallWeathering(WallWeathering, WallHorrorMarks);
    Renderer::SetRoomOcclusion(
        m_Objects.wallFootprints.data(),
        static_cast<unsigned int>(m_Objects.wallFootprints.size()),
        Stage2FloorY, RoomOcclusionCeilingY, RoomOcclusionStrength);

    // 2面は、廊下の入口から北（+z）の奥を向いて始めている。
    // カメラは前のシーンから持ち越されるので、1面の最後に向いていた向き（階段を上った東向きなど）のままにならないよう、ここで決めている
    game->GetCamera()->SetCameraDirection(0.0f);
    game->GetCamera()->SetCameraPitch(0.0f);
    // カメラの位置を最初から合わせるため、プレイヤーを一度更新している
    m_Objects.player->Update();
    m_Hud.Init();
    SetupPracticalLights();

    // 1面の最後の階段を上ってきた続きとして、下の段から3段上がって廊下に出る演出で始めている
    // （少し見上げた視点で、目の高さを12低い所から段ごとに上げる。約1.6秒で普段の操作に戻る）。
    // 自動撮影・計測は決めた位置から撮るので、この演出は出さない
    if (!Tools::CaptureMode::IsActive())
    {
        Camera* camera = game->GetCamera();
        const float yaw = camera->GetCameraDirection();
        camera->SetCameraPitch(Player::ArrivalStartPitch);
        m_Objects.player->StartStairClimb(
            Player::MakeArrivalClimb(Vector3(std::sin(yaw), 0.0f, std::cos(yaw))));
    }
}

// 扉の先の階段を、上の踊り場の奥（x=0、z=250）へ向けて歩かせ、段ごとに目の高さを上げている。
// 斜めに歩くときは、段の奥行きを向きで割り、z の方向に7進むごとに1段上がるようにしている
void Stage2Scene::StartExitStairClimb(Player& player)
{
    const Vector3 position = player.GetPosition();
    const Vector3 landing(0.0f, position.y,
        Stage2StairStartZ + Stage2StairStepRun * static_cast<float>(Stage2StairStepCount) +
            Player::ExitClimbLandingDepth);
    player.StartStairClimb(Player::MakeExitClimb(position, Vector3(0.0f, 0.0f, 1.0f), Stage2StairStartZ,
        Stage2StairStepRun, Stage2StairStepRise, Stage2StairStepCount, landing));
}

// 信号盤の目印や扉のランプ、肖像画の目が、自分の光る色で廊下を照らすようにしている。
// 色の変化（正解で緑になる、異変で赤く光るなど）が、そのまま周りの光の色になる。
void Stage2Scene::SetupPracticalLights()
{
    m_Objects.doorIndicator->SetGlowLight(DoorIndicatorGlowRange, DoorIndicatorGlowIntensity);
    for (Wall* marker : m_Objects.signalMarkers)
    {
        marker->SetGlowLight(MarkerGlowRange, MarkerGlowIntensity);
    }
    for (Wall* marker : m_Objects.evidenceMarkers)
    {
        marker->SetGlowLight(MarkerGlowRange, MarkerGlowIntensity);
    }
    // 肖像画は、目が開いて赤く光ったとき（自ら光る色が付いたとき）だけ、周りを赤く照らす
    m_Objects.portrait->SetGlowLight(PortraitGlowRange, PortraitGlowIntensity);
}

// 周回の数、視線、足音、信号盤パズル、追われる演出を同時に見て、進行を更新している。
void Stage2Scene::ReloadPropLayout()
{
    const PropLayout layout = PropLayoutFile::Load(Stage2Layout::PropLayoutPath);
    if (!layout.IsValid())
    {
        m_LayoutNotice = "配置のファイルの" + std::to_string(layout.ErrorLine) + "行目が読めない " + layout.Error;
        m_LayoutNoticeTimer = PropLayoutFile::LayoutErrorNoticeSeconds;
        return;
    }
    SceneLayoutBuilder builder(*Core::Game::GetInstance(), m_Objects.objectNames);
    if (!PropLayoutFile::Reload(builder, layout, m_Objects.layoutProps, m_LayoutNotice))
    {
        m_LayoutNoticeTimer = PropLayoutFile::LayoutErrorNoticeSeconds;
        return;
    }
    // プレイヤーの位置は、遊んでいる途中の状態なので変えない
    const int placed = PropLayoutFile::ReapplyPlacements(layout, m_Objects.placementBindings);
    m_Objects.wallFootprints = PropLayoutFile::CollectRoomFootprints(layout);
    Renderer::SetRoomOcclusion(
        m_Objects.wallFootprints.data(),
        static_cast<unsigned int>(m_Objects.wallFootprints.size()),
        Stage2FloorY, RoomOcclusionCeilingY, RoomOcclusionStrength);
    m_LayoutNotice = "配置のファイルを読み直した（建物 " + std::to_string(layout.Props.size()) + "個・置き場所 " +
        std::to_string(placed) + "個）";
    m_LayoutNoticeTimer = PropLayoutFile::LayoutSuccessNoticeSeconds;
}

// 1フレーム分の2面の進行。段階ごとの中身は、この下の関数に分けている
void Stage2Scene::Update()
{
    Player* player = m_Objects.player;
    if (player == nullptr)
    {
        return;
    }

    m_LayoutNoticeTimer = (std::max)(0.0f, m_LayoutNoticeTimer - Application::GetDeltaTime());
#if defined(_DEBUG)
    // Debug版だけ：F5 を押したとき、または配置のファイル（assets/layout/stage2_props.txt）が書き換わったとき
    // （Unity のマップエディターで書き出したときなど）に読み直し、ゲームを動かしたまま置き直している
    if (Input::GetKeyTrigger(VK_F5) || m_LayoutWatcher.HasChanged(Stage2Layout::PropLayoutPath, Application::GetDeltaTime()))
    {
        ReloadPropLayout();
    }
#endif

    // 周回中は歩くだけにし、最後の追跡が始まったときだけ走れるようにしている。
    // 静かに歩き続ける緊張と、追跡での解放感を分けるためである。
    player->SetSprintAllowed(
        m_FinalSequence.IsSequenceActive() || m_FinalSequence.IsPursuitActive());

    if (UpdateExitEscape(*player))
    {
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

    RunPendingDebugAction(*player);
    UpdateTimers(*player, deltaTime);

    Door* corridorDoor = m_Objects.door;
    UpdateGuidancePulse(corridorDoor);
    UpdateProgression(*player);
    UpdateAnomalies(*player, deltaTime);
    UpdateLockers();
    // 足音の危険度と背後の気配を更新している
    UpdateNoiseThreat(*player, deltaTime);
    UpdateBehindPresence(*player, deltaTime);
    UpdateEmergencyCharger(*player);
    UpdateEvidence(*player);

    // 信号盤パズルと、その間に背後から来る影を更新している
    UpdateSignalPuzzle();
    UpdateSignalStalker();
    UpdateConfirmationPanel(corridorDoor);
    UpdateCorridorAtmosphere(*player, deltaTime);

    // 演出の更新（影を見たときの照明・最後の追跡・最後の停電）
    UpdateObservedScare(deltaTime);
    UpdateFinalPursuit(deltaTime);
    UpdateFinalSequence(deltaTime);
    UpdateDoorIndicator(corridorDoor);

    // 調べる対象を選び、Eキー（A）で調べている
    m_InteractionSystem.Update(*player);
}

// 最後のループで出口の扉を抜けたら（扉の中心はz=140）、扉の先の階段を自動で上らせている。
// 出口を扉の手前から調べたときも、同じように上らせる。上り切る頃に、出口の演出が3面へ移す。
// 脱出している間は、追跡をやめ、影をすべて消して何もしない（そのときは true を返している）
bool Stage2Scene::UpdateExitEscape(Player& player)
{
    ExitTrigger* exit = m_Objects.exit;
    if (exit == nullptr)
    {
        return false;
    }
    if (exit->IsStairClimb())
    {
        if (!exit->IsEscaping() && exit->IsInteractionEnabled() && player.CanControl() &&
            player.GetPosition().z > Stage2StairAutoStartZ)
        {
            exit->BeginEscape(player);
        }
        if (exit->IsEscaping() && !player.IsClimbingStairs())
        {
            StartExitStairClimb(player);
        }
    }
    if (!exit->IsEscaping())
    {
        return false;
    }

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
    return true;
}

// 異変を探す周回（1周目か2周目）にし、その周回の異変を anomaly にしている。
// 周回を進める前に異変を決めておき、ほかの異変が出ないようにしている。異変を決め直せたら true を返している
bool Stage2Scene::ForceAnomalyLoop(Player& player, Stage2Anomaly anomaly)
{
    if (m_LoopCount < 1)
    {
        m_AnomalyPlan.ForceLoop(1, anomaly);
        AdvanceLoop(player);
    }
    if (m_LoopCount > Stage2AnomalyPlan::PlannedLoopCount)
    {
        return false;
    }
    m_AnomalyPlan.ForceLoop(m_LoopCount, anomaly);
    return true;
}

// デバッグ画面から頼まれた操作（周回を進める・最後の停電・照明の演出・異変を出す）を実行している
void Stage2Scene::RunPendingDebugAction(Player& player)
{
    if (!m_PendingDebugAction.has_value())
    {
        return;
    }
    const SceneDebugAction debugAction = *m_PendingDebugAction;
    m_PendingDebugAction.reset();
    switch (debugAction)
    {
    case SceneDebugAction::AdvanceProgression:
        if (m_LoopCount < Stage2FinalLoopCount)
        {
            AdvanceLoop(player);
        }
        break;
    case SceneDebugAction::PlayFinalSequence:
        while (m_LoopCount < Stage2FinalLoopCount)
        {
            AdvanceLoop(player);
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
    case SceneDebugAction::ShowMirrorAnomaly:
        if (ForceAnomalyLoop(player, Stage2Anomaly::Mirror))
        {
            m_MirrorAnomaly.ResetProgressForLoop();
        }
        break;
    case SceneDebugAction::ShowPortraitAnomaly:
        if (ForceAnomalyLoop(player, Stage2Anomaly::Portrait))
        {
            m_PortraitAnomaly.ResetProgressForLoop();
        }
        break;
    case SceneDebugAction::ShowFalseDoorAnomaly:
        // 偽の扉は、左の壁に出し直している
        if (ForceAnomalyLoop(player, Stage2Anomaly::FalseDoor))
        {
            m_FalseDoorAnomaly.ResetProgressForLoop();
            SetFalseDoorState(true, false);
        }
        break;
    case SceneDebugAction::ShowUnseenAnomaly:
        // 見えない影は、廊下の奥から歩き直させている
        if (ForceAnomalyLoop(player, Stage2Anomaly::Unseen))
        {
            m_UnseenPresence.ResetProgressForLoop();
        }
        break;
    case SceneDebugAction::ShowPuddleAnomaly:
        if (ForceAnomalyLoop(player, Stage2Anomaly::Puddle))
        {
            m_PuddleAnomaly.ResetProgressForLoop();
        }
        break;
    }
}

// 画面効果を普段の値に戻し、配置で作ったObjectを破棄している
void Stage2Scene::Uninit()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }

    // 血の手形の異変で壁に浮かべた手形を消している（ほかの面の壁に残らないように）
    Renderer::SetWallHandprints(nullptr, 0);
    Renderer::SetPuddleFigure(DirectX::SimpleMath::Vector3::Zero, 0.0f);
    Renderer::SetUnseenFigure(DirectX::SimpleMath::Vector3::Zero, 0.0f);
    Renderer::SetMirrorFigure(DirectX::SimpleMath::Vector3::Zero, 0.0f);
    Renderer::SetWallWeathering(0.0f);
    Renderer::SetLoopDepth(0.0f);
    Renderer::SetRoomOcclusion(nullptr, 0, 0.0f, 0.0f, 0.0f);
    game->GetPostProcess()->SetCorridorTension(0.0f);
    game->GetPostProcess()->SetAtmosphere(Effect::PostProcess::DefaultNoiseAmount, Effect::PostProcess::DefaultVignetteStrength);
    game->GetPostProcess()->SetExposure(Effect::PostProcess::DefaultExposure);
    game->GetPostProcess()->SetLensDistortionStrength(ExitLensDistortionStrength);
    game->GetPostProcess()->SetFilmGradeStrength(Effect::PostProcess::DefaultFilmGradeStrength);
    game->GetPostProcess()->SetSignalInterference(0.0f);
    game->GetPostProcess()->SetVolumetricLight(false);
    game->GetPostProcess()->SetVolumetricIntensity(Effect::PostProcess::DefaultVolumetricIntensity);

    // Stage2Layoutが作るときに記録した名前の一覧で破棄している（名前を書く場所を1か所にするため）。
    for (const std::string& name : m_Objects.objectNames)
    {
        game->DestroyObj(name);
    }
    m_Hud.Uninit();
}
