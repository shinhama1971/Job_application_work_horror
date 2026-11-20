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

    // --- 廊下の画面効果（UpdateCorridorAtmosphere）。各値は「基本＋周回の進み具合1・暗さ1・危険度1のときに足す分」
    // 周回の色調が追いつく速さ（毎秒）、照明の明るさが届く廊下の向きの距離、緊張のゆっくりした揺れ（速さ・大きさ）
    constexpr float LoopDepthFollowSpeed = 2.5f;
    constexpr float FixtureLightReach = 105.0f;
    constexpr float TensionPulseSpeed = 1.7f;
    constexpr float TensionPulseAmount = 0.025f;
    constexpr float TensionBase = 0.38f;
    constexpr float TensionPerLoop = 0.42f;
    constexpr float TensionPerThreat = 0.14f;
    constexpr float TensionMax = 0.92f;
    constexpr float NoiseBase = 0.20f;
    constexpr float NoisePerLoop = 0.10f;
    constexpr float NoisePerDarkness = 0.018f;
    constexpr float NoisePerThreat = 0.025f;
    constexpr float VignetteBase = 0.62f;
    constexpr float VignettePerLoop = 0.12f;
    constexpr float VignettePerDarkness = 0.035f;
    constexpr float VignettePerThreat = 0.045f;
    // 目が慣れたような露出（ライトを点けているとき・消しているとき）
    constexpr float FlashlightOnExposure = 1.00f;
    constexpr float FlashlightOnExposurePerDarkness = 0.035f;
    constexpr float FlashlightOffExposure = 1.055f;
    constexpr float FlashlightOffExposurePerDarkness = 0.090f;
    constexpr float DistortionBase = 0.20f;
    constexpr float DistortionPerLoop = 0.34f;
    constexpr float FilmGradeBase = 0.58f;
    constexpr float FilmGradePerLoop = 0.20f;
    constexpr float LensDirtBase = 0.14f;
    constexpr float LensDirtPerLoop = 0.08f;
    // 2面の始めの露出（電力が戻った、少し暗い廊下）と、2面を出るときに戻すレンズの歪み（作った直後と同じ強さ）
    constexpr float StartExposure = 0.95f;
    constexpr float ExitLensDistortionStrength = Effect::PostProcess::StartupLensDistortionStrength;
    // 信号の乱れ：信号盤の数、足音の影が近いときに乱れ始める危険度と強まり方と上限、信号盤パズルの間の乱れ
    constexpr float StalkerInterferenceStartThreat = 0.52f;
    constexpr float StalkerInterferencePerThreat = 0.72f;
    constexpr float StalkerInterferenceMax = 0.30f;
    constexpr float SignalInterferenceBase = 0.10f;
    constexpr float SignalInterferencePerThreat = 0.62f;
    constexpr float SignalInterferenceUnresolved = 0.16f;
    constexpr float SignalInterferenceMax = 0.88f;
    constexpr float VolumetricBase = 0.38f;
    constexpr float VolumetricPerLoop = 0.20f;
    constexpr float VolumetricPerDarkness = 0.055f;
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

    // --- 扉のランプ：鍵がかかっている間は赤くゆっくり、外れたら緑で速く点滅させる（点滅の速さ、色、光沢）
    constexpr float LockedIndicatorPulseSpeed = 3.4f;
    constexpr float OpenIndicatorPulseSpeed = 6.2f;
    const Color LockedIndicatorDiffuse(0.24f, 0.012f, 0.008f, 1.0f);
    constexpr float LockedIndicatorRedBase = 0.13f;
    constexpr float LockedIndicatorRedPulse = 0.08f;
    constexpr float LockedIndicatorGreen = 0.001f;
    constexpr float LockedIndicatorShininess = 24.0f;
    const Color OpenIndicatorDiffuse(0.04f, 0.18f, 0.035f, 1.0f);
    constexpr float OpenIndicatorRed = 0.015f;
    constexpr float OpenIndicatorGreenBase = 0.16f;
    constexpr float OpenIndicatorGreenPulse = 0.10f;
    constexpr float OpenIndicatorBlue = 0.008f;
    constexpr float OpenIndicatorShininess = 28.0f;
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
    game->GetPostProcess()->SetCorridorTension(TensionBase);
    game->GetPostProcess()->SetExposure(StartExposure);
    game->GetPostProcess()->SetAtmosphere(NoiseBase, VignetteBase);
    game->GetPostProcess()->SetLensDistortionStrength(DistortionBase);
    game->GetPostProcess()->SetFilmGradeStrength(FilmGradeBase);
    game->GetPostProcess()->SetSignalInterference(0.0f);
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetVolumetricIntensity(VolumetricBase);

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

// 画面効果を、周回の進み具合・近くの照明の明るさ・危険度に合わせて変えている
void Stage2Scene::UpdateCorridorAtmosphere(Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    const float loopRate = static_cast<float>(m_LoopCount) / static_cast<float>(Stage2FinalLoopCount);
    const float threat = m_NoiseThreatSystem.GetThreat();
    // P.T. のように、周回が進むほど廊下の色調を赤黒く沈め、霧を濃くしている（扉を抜けた直後の暗転の間に、約1秒で変わる）
    m_LoopDepth += (loopRate - m_LoopDepth) * (std::min)(deltaTime * LoopDepthFollowSpeed, 1.0f);
    Renderer::SetLoopDepth(m_LoopDepth);
    // 一番明るく照らしている近くの照明（廊下の向きの距離で弱める）から、今いる所の暗さを求めている
    float localFixtureLight = 0.0f;
    for (CeilingLight* fixture : m_Objects.lights)
    {
        if (fixture == nullptr)
        {
            continue;
        }

        const float longitudinalDistance = std::fabs(
            fixture->GetPosition().z - player.GetPosition().z);
        const float proximity = 1.0f - (std::min)(
            longitudinalDistance / FixtureLightReach, 1.0f);
        localFixtureLight = (std::max)(
            localFixtureLight,
            fixture->GetBrightness() * proximity);
    }
    const float localDarkness =
        1.0f - (std::clamp)(localFixtureLight, 0.0f, 1.0f);
    const float pulse = std::sin(m_VisualTimer * TensionPulseSpeed) * TensionPulseAmount;
    game->GetPostProcess()->SetCorridorTension(
        (std::clamp)(TensionBase + loopRate * TensionPerLoop +
            threat * TensionPerThreat + pulse, 0.0f, TensionMax));
    game->GetPostProcess()->SetAtmosphere(
        NoiseBase + loopRate * NoisePerLoop + localDarkness * NoisePerDarkness + threat * NoisePerThreat,
        VignetteBase + loopRate * VignettePerLoop + localDarkness * VignettePerDarkness + threat * VignettePerThreat);
    // ライトを消しているときと暗い場所では、目が慣れたように露出を上げている
    const float adaptedExposure = player.IsFlashlightOn()
        ? FlashlightOnExposure + localDarkness * FlashlightOnExposurePerDarkness
        : FlashlightOffExposure + localDarkness * FlashlightOffExposurePerDarkness;
    game->GetPostProcess()->SetExposure(adaptedExposure);
    game->GetPostProcess()->SetLensDistortionStrength(DistortionBase + loopRate * DistortionPerLoop);
    game->GetPostProcess()->SetFilmGradeStrength(FilmGradeBase + loopRate * FilmGradePerLoop);
    game->GetPostProcess()->SetLensDirtStrength(LensDirtBase + loopRate * LensDirtPerLoop);
    // 信号盤パズルの間と、足音の影が近いときは、監視映像のような信号の乱れを出している
    const bool signalRestorationActive =
        m_LoopCount >= Stage2FinalLoopCount && !m_SignalPuzzle.IsComplete();
    const float unresolvedSignalRate =
        1.0f - static_cast<float>(m_SignalPuzzle.GetStep()) / static_cast<float>(Stage2SignalTerminalCount);
    ShadowMan* activeNoiseShadow = m_Objects.noiseShadow;
    const float stalkerInterference =
        activeNoiseShadow != nullptr && activeNoiseShadow->IsActive()
            ? (std::clamp)((threat - StalkerInterferenceStartThreat) * StalkerInterferencePerThreat,
                0.0f, StalkerInterferenceMax)
            : 0.0f;
    game->GetPostProcess()->SetSignalInterference(
        signalRestorationActive
            ? (std::clamp)(SignalInterferenceBase + threat * SignalInterferencePerThreat +
                unresolvedSignalRate * SignalInterferenceUnresolved, 0.0f, SignalInterferenceMax)
            : stalkerInterference);
    game->GetPostProcess()->SetVolumetricLight(player.IsFlashlightOn());
    game->GetPostProcess()->SetVolumetricIntensity(
        VolumetricBase + loopRate * VolumetricPerLoop + localDarkness * VolumetricPerDarkness);
}

// 扉のランプ：鍵がかかっている間は赤く、外れたら緑でゆっくり点滅させている
void Stage2Scene::UpdateDoorIndicator(Door* corridorDoor)
{
    Wall* doorIndicator = m_Objects.doorIndicator;
    if (doorIndicator == nullptr || m_FinalDoorReady)
    {
        return;
    }
    const bool locked = corridorDoor != nullptr && corridorDoor->IsLocked();
    const float indicatorPulse = std::sin(
        m_VisualTimer * (locked ? LockedIndicatorPulseSpeed : OpenIndicatorPulseSpeed)) * 0.5f + 0.5f;
    if (locked)
    {
        doorIndicator->SetAppearance(
            LockedIndicatorDiffuse,
            Color(LockedIndicatorRedBase + indicatorPulse * LockedIndicatorRedPulse,
                LockedIndicatorGreen, 0.0f, 1.0f), LockedIndicatorShininess);
    }
    else
    {
        doorIndicator->SetAppearance(
            OpenIndicatorDiffuse,
            Color(OpenIndicatorRed, OpenIndicatorGreenBase + indicatorPulse * OpenIndicatorGreenPulse,
                OpenIndicatorBlue, 1.0f), OpenIndicatorShininess);
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
