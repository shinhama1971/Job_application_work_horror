// ============================================================================
// ファイルの役割: 1面のシーンの初期化と、毎フレームの進行の中心（各演出・仕組みの更新を順に呼ぶ）を担当している。
// 主な技術: Sceneの処理を複数のファイルに分ける構成、進行の状態の管理、周りの物で物語を伝える演出
// ============================================================================

#include "StageScene.h"
#include "Stage1FuseProgress.h"
#include "Stage1CeilingGeometry.h"
#include "MathUtility.h"
#include "Application.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"
#include "Renderer.h"

#include "Player.h"
#include "Wall.h"
#include "Ground.h"
#include "Item.h"
#include "Door.h"
#include "EventFeedback.h"
#include "FuseBox.h"
#include "CeilingLight.h"
#include "ExitTrigger.h"
#include "ShadowMan.h"
#include "SceneLayoutBuilder.h"
#include "PropLayoutFile.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

namespace
{
    // デバッグで「4周目以降のドア」を見せるときの周回の数（StageSceneEvents の StartDoorHauntedGapLoop と同じ）と、
    // ドアの開き方の段階（Door の一番最後の段階）
    constexpr int DebugHauntedDoorLoop = 4;
    // 撮影で、建物全体が呼吸している状態にするときの周回（3周目）
    constexpr int DebugBreathingBuildingLoop = 3;
    constexpr int DebugHauntedDoorPhase = 3;
    constexpr float RoomOcclusionFloorY = -100.0f;
    // 天井の下面（stage1_props.txt の天井の板 PropCeiling* の下の端。11月19日に -48.5 から 12 上げた）
    constexpr float RoomOcclusionCeilingY = Stage1CeilingGeometry::UndersideY;
    constexpr float RoomOcclusionStrength = 0.55f;

    // --- 1面の進行の値
    // 最初に開始地点のドアを叩く音が鳴るまでの秒数
    constexpr float StartDoorFirstKnockSeconds = 4.0f;
    constexpr float EvidenceNoticeSeconds = 3.2f;
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;
    // 1面の壁の傷み（パネルの継ぎ目・ひび・水の垂れた跡・カビ）の強さ
    constexpr float Stage1WallWeathering = 1.0f;
    // 看板やランプが周りを照らす光（届く距離と強さ）：ドアの上の表示、扉のランプと監視カメラの目印、周回の目印
    constexpr float DoorSignGlowRange = 60.0f;
    constexpr float DoorSignGlowIntensity = 2.2f;
    constexpr float IndicatorGlowRange = 45.0f;
    constexpr float IndicatorGlowIntensity = 2.0f;
    constexpr float LoopMarkerGlowRange = 40.0f;
    constexpr float LoopMarkerGlowIntensity = 1.8f;
    // 西棟の天井の人影：水面に映った人影に気づいたとき（真上から降る息づかい）
    constexpr EventFeedback::SoundSpec CeilingFigureBreath{ 0.74f, 0.42f };
    constexpr PulseSpec CeilingFigureSeenPulse{ 0.16f, 0.30f };
    constexpr RumbleSpec CeilingFigureSeenRumble{ 3, 0.10f };
    // 見上げると消えていたとき（真下の水面に落ちる滴と波紋の大きさ・強さ）
    constexpr float CeilingFigureRippleRadius = 2.0f;
    constexpr float CeilingFigureRippleStrength = -1.5f;
    constexpr EventFeedback::SoundSpec CeilingFigureDrop{ 0.95f, 0.80f };
    constexpr PulseSpec CeilingFigureVanishedPulse{ 0.26f, 0.36f };
    constexpr RumbleSpec CeilingFigureVanishedRumble{ 5, 0.14f };
}

// 作るときに初期化している
StageScene::StageScene()
{
    Init();
}

// 壊すときに後片付けをしている
StageScene::~StageScene()
{
    Uninit();
}

// 1面で必要な床・壁・照明・アイテム・進行用のObjectをまとめて配置し、各仕組みを準備している。
void StageScene::Init()
{
    Core::Game* game = Core::Game::GetInstance();
    // 1面は長く放置された施設なので、壁にパネルの継ぎ目・ひび・水の垂れた跡・カビを出している。
    Renderer::SetWallWeathering(Stage1WallWeathering);
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetLensDistortionStrength(DistortionBase);
    game->GetPostProcess()->SetFilmGradeStrength(FilmGradeBase);
    game->GetPostProcess()->SetLensDirtStrength(LensDirtBase);
    ResetProgressState();
    SetupStageSystems();
}

// 進行と演出の状態をすべて最初に戻している（世界の時計・カメラの傾き・音で浮かぶ輪も戻す）
void StageScene::ResetProgressState()
{
    Core::Game* game = Core::Game::GetInstance();
    m_CorridorLoopCount = 0;
    m_LastFuseCount = game->GetItemCount();
    m_FuseNoticeTimer = 0.0f;
    m_FuseWatcherNoticeTimer = 0.0f;
    m_FuseWatcherState = FuseWatcherState::None;
    m_ChargerNoticeTimer = 0.0f;
    m_ChargerHandled = false;
    m_EvidenceNoticeTimer = 0.0f;
    m_LoopCooldown = 0.0f;
    m_LoopNoticeTimer = 0.0f;
    m_DarkWritingReadTime = 0.0f;
    m_DarkWritingTriggered = false;
    m_EmptyFuseCaseNoticeTimer = 0.0f;
    m_NearEmptyFuseCase = false;
    m_KeypadFuseReturned = false;
    m_WasThirdFuseStage = false;
    m_StartDoorKnockTimer = StartDoorFirstKnockSeconds;
    m_StartDoorKnocksLeft = 0;
    m_StartDoorKnockEvents = 0;
    m_EntranceEventTriggered = false;
    m_EntranceEventTimer = -1.0f;
    m_EntranceEventPhase = EntranceEventPhase::NotStarted;
    m_StorageScarePhase = StorageScarePhase::Waiting;
    m_StorageScareTimer = 0.0f;
    m_StorageScareNoticeTimer = 0.0f;
    m_ScareLightSequence.Reset();
    m_PowerSequence.Reset();
    m_StageVisualTimer = 0.0f;
    m_ExitOmenSequence.Reset();
    m_AmbientSounds.Reset();
    m_CeilingStalker.Reset();
    m_CorridorStretch.Reset();
    m_CorridorFlood.Reset();
    m_CorridorFloodCues.clear();
    m_CorridorFloodLights.clear();
    m_CorridorFloodOwnsWater = false;
    m_CorridorFloodRumbleTimer = 0.0f;
    m_FloodStainMinZ = Stage1CorridorFlood::CorridorEndZ;
    m_FloodStainRedness = 0.0f;
    m_FloodStain = 0.0f;
    Renderer::SetFloodWallStain(Vector4::Zero);
    m_TimeStopRoom.Reset();
    m_TimeStopCues.clear();
    m_TiltRoom.Reset();
    m_EchoRoom.Reset();
    m_HandCorruption = 0.0f;
    m_EchoRoomCues.clear();
    m_EchoRoomLights.clear();
    Renderer::SetEchoPulses(nullptr, nullptr, 0);
    Application::SetWorldTimeScale(1.0f);
    game->GetCamera()->SetViewRoll(0.0f);
    game->GetCamera()->SetWorldStretchZ(1.0f);
    m_FuseScareTimer = -1.0f;
    m_FuseScareStepsPlayed = 0;
    m_FuseScareLights.clear();
    m_CeilingStalkerWait = 0.0f;
    m_TensionPulse.Reset();
    m_ProgressHintTimer = 0.0f;
}

// 壁・照明・ヒューズ・扉などを配置し、配置した Object を壁の文字・扉・西棟・呼吸する壁・影などの仕組みに渡して準備している
void StageScene::SetupStageSystems()
{
    Core::Game* game = Core::Game::GetInstance();
    // 壁・照明・ヒューズ・扉などの配置はStage1Layoutが担当し、使うObjectのポインタをまとめて返している。
    m_Objects = Stage1Layout::Build(*game);
    // 壁の文字・暗証番号の扉・隠し部屋・西棟の仕組みに、配置したObjectを渡している
    m_WallWritings.Init(m_Objects.writings);
    m_KeypadDoor.Init(m_Objects.keypad);
    m_HiddenRoom.Init(m_Objects.hiddenRoom);
    m_WestWing.Init(m_Objects.westWing);
    m_BreathingWallTime = 0.0f;
    m_BreathingGroupAmplitudes.fill(0.0f);
    m_BreathingWallCycle = 0;
    SetupBreathingWall();
    m_CeilingScare.Reset();
    Renderer::SetPuddleFigure(Vector3::Zero, 0.0f);
    m_SafeLights.Init(m_Objects.safeZones);
    // 照らすと止まる影の判定と押し戻しに使う壁と扉を、一度だけ集めている（毎フレーム全Objectを調べないため）
    m_ArchiveStalker = Stage1LightStalker{};
    m_FootprintFollower.Reset();
    m_FootprintFollowerUsed = false;
    m_RadioStatic.Reset();
    m_Transmissions.Reset();
    m_StalkerWalls = game->GetObjects<Wall>();
    m_StalkerDoors = game->GetObjects<Door>();
    m_ArchiveStalkerNoticeTimer = 0.0f;
    // 部屋の角の暗がり。床（y=-100）と天井の下面（y≒-48.5）の高さと、建物の壁の形を渡している。
    Renderer::SetRoomOcclusion(
        m_Objects.wallFootprints.data(),
        static_cast<unsigned int>(m_Objects.wallFootprints.size()),
        RoomOcclusionFloorY, RoomOcclusionCeilingY, RoomOcclusionStrength);

    // シーンを変えた後の最初の描画の前に、カメラとライトを更新している。
    // ImGuiでゲームを止めた場合も、面全体が黒くなるのを防いでいる。
    m_Objects.player->Update();

    m_Hud.Init();
    // 監視カメラの巡回は配置したObjectを使うため、配置の後に準備している。
    m_Surveillance.Init(m_Objects);
    SetupPracticalLights();
    m_ActiveRoute = static_cast<std::size_t>(Stage1RouteLight::Count);
    m_RouteFixtureIndex = 0;
    UpdateRouteLights();
}

// 看板やランプが、自分の光る色で周りの床と壁を照らすようにしている。
// 天井照明だけだった頃は、光源の数の上限（8個）のせいで足せなかった小さな光。
void StageScene::SetupPracticalLights()
{
    m_Objects.exitSign->SetGlowLight(DoorSignGlowRange, DoorSignGlowIntensity);
    m_Objects.startDoorSign->SetGlowLight(DoorSignGlowRange, DoorSignGlowIntensity);
    m_Objects.doorIndicator->SetGlowLight(IndicatorGlowRange, IndicatorGlowIntensity);
    m_Objects.evidenceMarker->SetGlowLight(IndicatorGlowRange, IndicatorGlowIntensity);
    for (Wall* marker : m_Objects.loopMarkers)
    {
        marker->SetGlowLight(LoopMarkerGlowRange, LoopMarkerGlowIntensity);
    }
}

// 配置のファイルを読み直し、今の小物と、扉・照明などの置き場所を置き直している（Debug版の F5 と、ファイルが書き換わったとき）。
// 書き間違いがあれば置き直さず、何行目が読めなかったかを画面の左上に出している。
bool StageScene::ReloadPropLayout()
{
    const PropLayout layout = PropLayoutFile::Load(Stage1Layout::PropLayoutPath);
    if (!layout.IsValid())
    {
        m_LayoutNotice = "配置のファイルの" + std::to_string(layout.ErrorLine) + "行目が読めない " + layout.Error;
        m_LayoutNoticeTimer = PropLayoutFile::LayoutErrorNoticeSeconds;
        return false;
    }
    Core::Game& game = *Core::Game::GetInstance();
    // 読み直す前に元の位置へ戻し、呼吸の変位を新しい基準位置に混ぜない。
    UpdateBreathingFixtures(true);
    SceneLayoutBuilder builder(game, m_Objects.objectNames);
    if (!PropLayoutFile::Reload(builder, layout, m_Objects.layoutProps, m_LayoutNotice))
    {
        m_LayoutNoticeTimer = PropLayoutFile::LayoutErrorNoticeSeconds;
        return false;
    }
    // 読み直しで増えた壁も、影の視線と移動を遮る。
    m_StalkerWalls = game.GetObjects<Wall>();
    // 扉・照明・電池など place 行で置いた物も置き直し、部屋の形（room の壁）を部屋の角の暗がりへ渡し直している。
    // プレイヤーの位置と、プレイごとに選ぶヒューズ・鍵の場所（spot）は、遊んでいる途中の状態なので変えない
    const int placed = PropLayoutFile::ReapplyPlacements(layout, m_Objects.placementBindings);
    SetupBreathingWall();
    m_Objects.wallFootprints = PropLayoutFile::CollectRoomFootprints(layout);
    Renderer::SetRoomOcclusion(
        m_Objects.wallFootprints.data(),
        static_cast<unsigned int>(m_Objects.wallFootprints.size()),
        RoomOcclusionFloorY, RoomOcclusionCeilingY, RoomOcclusionStrength);
    m_PropLayoutEditor.SetLayout(layout);
    m_LayoutNotice = "配置のファイルを読み直した（小物 " + std::to_string(layout.Props.size()) + "個・置き場所 " +
        std::to_string(placed) + "個）";
    m_LayoutNoticeTimer = PropLayoutFile::LayoutSuccessNoticeSeconds;
    return true;
}
// 自動撮影から、最後のドアを調べたのと同じように開けている（開けた瞬間の周回の切り替えは、次のUpdateで普段どおり行われる）
void StageScene::RequestDebugAction(SceneDebugAction action)
{
    if (action == SceneDebugAction::ShowCeilingReflection)
    {
        m_CeilingScare.ForceArm();
        return;
    }
    if (action == SceneDebugAction::ShowHauntedStartDoor && m_Objects.startDoor != nullptr)
    {
        m_CorridorLoopCount = DebugHauntedDoorLoop;
        m_Objects.startDoor->ResetClosed(DebugHauntedDoorPhase);
        m_Objects.startDoor->SetLocked(true);
        return;
    }
    if (action == SceneDebugAction::StartEchoRoom)
    {
        m_EchoRoom.ArmOnLoopArrival(m_CorridorLoopCount, true);
        return;
    }
    if (action == SceneDebugAction::StartCorridorFlood)
    {
        m_CorridorFlood.ForceStart();
        return;
    }
    if (action == SceneDebugAction::ShowBreathingBuilding)
    {
        m_CorridorLoopCount = (std::max)(m_CorridorLoopCount, DebugBreathingBuildingLoop);
        Core::Game* game = Core::Game::GetInstance();
        while (game->GetItemCount() < Core::GameState::RequiredFuseCount)
        {
            game->AddItemCount();
        }
        return;
    }
    Door* endDoor = m_Objects.exitDoor;
    Player* player = m_Objects.player;
    if (action == SceneDebugAction::OpenStage1EndDoor &&
        endDoor != nullptr && player != nullptr && endDoor->IsInteractionEnabled())
    {
        endDoor->Interact(*player);
    }
}

// 西棟に入ってしばらくすると、少し先の天井に黒い人影が逆さまに張り付く。目では見えず、足元の水面をのぞいたときだけ
// 水面に映る（床のシェーダーのレイ反射で、逆さまにした人影の距離場として描いている）。見た後に見上げると何もおらず、
// その真下の水面に滴が落ちて波紋が広がる。1回のプレイで一度だけ。
void StageScene::UpdateCeilingReflection(const Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    const Camera* camera = game->GetCamera();
    Ground* ground = m_Objects.westWing.ground;
    const auto isInWater = [ground](const Vector3& position)
    {
        return ground != nullptr && ground->IsInsidePuddle(position);
    };
    const CeilingReflectionScare::Event scareEvent = m_CeilingScare.Update(
        deltaTime, Stage1WestWing::IsInside(player.GetPosition()), camera->GetPosition(), camera->GetForward(),
        Vector2(Stage1WestWing::MinX, Stage1WestWing::MinZ), Vector2(Stage1WestWing::MaxX, Stage1WestWing::MaxZ),
        isInWater);
    const Vector3 position = m_CeilingScare.GetPosition();
    Renderer::SetPuddleFigure(position, m_CeilingScare.GetVisibility(), true);

    if (scareEvent == CeilingReflectionScare::Event::Seen)
    {
        // 水面の人影に気づいた：真上から、かすかな息づかいが降ってくる
        game->PlayAudioCueAt(SOUND_CUE_BREATH, position - Vector3(0.0f, CeilingReflectionScare::HeadDrop, 0.0f),
            CeilingFigureBreath.Pitch, CeilingFigureBreath.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), CeilingFigureSeenPulse);
        EventFeedback::Rumble(CeilingFigureSeenRumble);
    }
    else if (scareEvent == CeilingReflectionScare::Event::Vanished)
    {
        // 見上げると何もいない：人影がいた真下の水面に滴が落ち、波紋が広がる
        const Vector3 below(position.x, CeilingReflectionScare::WaterY, position.z);
        if (ground != nullptr)
        {
            ground->AddRippleImpulse(below, CeilingFigureRippleRadius, CeilingFigureRippleStrength);
        }
        game->PlayAudioCueAt(SOUND_CUE_WATER_DROP, below, CeilingFigureDrop.Pitch, CeilingFigureDrop.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), CeilingFigureVanishedPulse);
        EventFeedback::Rumble(CeilingFigureVanishedRumble);
    }
}

// ヒューズの数と電力の状態をもとに、目的表示と演出の段階を更新している。段階ごとの中身は、この下の関数に分けている
void StageScene::Update()
{
    Player* player =
        m_Objects.player;

    if (player == nullptr)
    {
        return;
    }

    // 映像の確認中と捕まっている間は操作できないので、操作できるかの判定より前に更新している。
    if (m_Surveillance.Update(*player, Application::GetDeltaTime()))
    {
        // 巡回をすべて終えたら、記録端末の完了の知らせを出している。
        m_EvidenceNoticeTimer = EvidenceNoticeSeconds;
    }
    // 暗証番号の入力画面も、操作できない間に入力を受け取るため、操作できるかの判定より前に更新している。
    m_KeypadDoor.Update(*player, Application::GetDeltaTime());
    // 操作できない間（映像の確認・捕まっている間）も呼び、途中の物音を打ち切れるようにしている。
    UpdateAmbientSounds();
    if (!player->CanControl())
    {
        UpdateRouteLights();
        return;
    }

    const float deltaTime = Application::GetDeltaTime();
    UpdateTimersAndHints(*player, deltaTime);
    UpdateFusePickup();
    UpdateEmergencyCharger(*player);
    UpdateStageEvents(*player, deltaTime);
    UpdateExitDoor(*player);
    UpdateExitSigns();
    UpdateScreenAtmosphere(*player);

    // 調べる対象を選び、Eキー（A）で調べている
    m_InteractionSystem.Update(*player);
}

// 描画の設定と画面効果を普段の値に戻し、配置で作ったObjectを破棄している
void StageScene::Uninit()
{
    // ほかの面の壁は前の見た目に戻している（シーンを切り替えるときは、古いSceneの破棄が先に行われる）。
    Renderer::SetWallWeathering(0.0f);
    Renderer::SetRoomOcclusion(nullptr, 0, 0.0f, 0.0f, 0.0f);
    Renderer::SetPuddleFigure(Vector3::Zero, 0.0f);
    // ループ廊下の伸びた見え方・傾く部屋の傾き・時間が止まった部屋の時計を元に戻している（シーンをまたいで使い続けるため）
    Core::Game::GetInstance()->GetCamera()->SetWorldStretchZ(1.0f);
    Core::Game::GetInstance()->GetCamera()->SetViewRoll(0.0f);
    Application::SetWorldTimeScale(1.0f);
    Renderer::SetEchoPulses(nullptr, nullptr, 0);
    Renderer::SetFloodWallStain(DirectX::SimpleMath::Vector4::Zero);
    Effect::PostProcess* postProcess = Core::Game::GetInstance()->GetPostProcess();
    postProcess->SetAtmosphere(Effect::PostProcess::DefaultNoiseAmount, Effect::PostProcess::DefaultVignetteStrength);
    postProcess->SetExposure(Effect::PostProcess::DefaultExposure);
    postProcess->SetCorridorTension(0.0f);
    postProcess->SetVolumetricLight(false);
    postProcess->SetLensDistortionStrength(Effect::PostProcess::DefaultLensDistortionStrength);
    postProcess->SetFilmGradeStrength(Effect::PostProcess::DefaultFilmGradeStrength);
    postProcess->SetLensDirtStrength(Effect::PostProcess::DefaultLensDirtStrength);
    m_Surveillance.Uninit();
    m_Hud.Uninit();

    Core::Game* game = Core::Game::GetInstance();

    // Stage1Layoutが作るときに記録した名前の一覧で破棄している（名前を書く場所を1か所にするため）。
    for (const std::string& name : m_Objects.objectNames)
    {
        game->DestroyObj(name);
    }
}
