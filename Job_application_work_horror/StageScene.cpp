// ============================================================================
// ファイルの役割: 1面のステージ配置、ヒューズ探索、電力復旧、出口までの進行を管理します。
// ============================================================================

#include "StageScene.h"
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

StageScene::StageScene()
{
    Init();
}

StageScene::~StageScene()
{
    Uninit();
}

// 1面で必要な床、壁、照明、アイテム、進行用Triggerをまとめて配置します。
void StageScene::Init()
{
    Core::Game* game = Core::Game::GetInstance();
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetLensDistortionStrength(0.20f);
    game->GetPostProcess()->SetFilmGradeStrength(0.52f);
    game->GetPostProcess()->SetLensDirtStrength(0.10f);
    m_CorridorLoopCount = 0;
    m_LastFuseCount = game->GetItemCount();
    m_FuseNoticeTimer = 0.0f;
    m_FuseWatcherNoticeTimer = 0.0f;
    m_FuseWatcherState = 0;
    m_ChargerNoticeTimer = 0.0f;
    m_ChargerHandled = false;
    m_EvidenceNoticeTimer = 0.0f;
    m_EvidenceHandled = false;
    m_LoopCooldown = 0.0f;
    m_LoopNoticeTimer = 0.0f;
    m_EntranceEventTriggered = false;
    m_EntranceEventTimer = -1.0f;
    m_EntranceEventPhase = -1;
    m_ScareLightTimer = -1.0f;
    m_ScareLightPhase = -1;
    m_ScareMessageTimer = 0.0f;
    m_WasPowerRestored = false;
    m_PowerRestoreTimer = -1.0f;
    m_PowerRestorePhase = -1;
    m_ExitPowerEventTimer = -1.0f;
    m_ExitPowerEventPhase = -1;
    m_ExitPowerSequenceComplete = false;
    m_StageVisualTimer = 0.0f;
    m_ExitOmenTriggered = false;
    m_ExitOmenTimer = 0.0f;
    m_ExitOmenPhase = -1;
    m_ProgressHintTimer = 0.0f;

    // プレイヤー
    Player* player = game->CreateObj<Player>("Player");
    player->SetPosition(Vector3(0.0f, -99.0f, -120.0f));

    // 地面
    Ground* ground = game->CreateObj<Ground>("Ground");
    ground->SetPosition(0.0f, -100.0f, 0.0f);
    ground->SetScale(20.0f, 1.0f, 20.0f);

    // 壁
    Wall* wall1 = game->CreateObj<Wall>("Wall1");
    wall1->SetPosition(0.0f, -74.0f, 340.0f);
    wall1->SetScale(440.0f, 50.0f, 4.0f);

    Wall* wall2 = game->CreateObj<Wall>("Wall2");
    wall2->SetPosition(220.0f, -74.0f, 80.0f);
    wall2->SetScale(4.0f, 50.0f, 520.0f);

    Wall* wall3 = game->CreateObj<Wall>("Wall3");
    wall3->SetPosition(0.0f, -74.0f, -180.0f);
    wall3->SetScale(440.0f, 50.0f, 4.0f);

    Wall* wall4 = game->CreateObj<Wall>("Wall4");
    wall4->SetPosition(-220.0f, -74.0f, 80.0f);
    wall4->SetScale(4.0f, 50.0f, 520.0f);

    Wall* wall5 = game->CreateObj<Wall>("Wall5");
    wall5->SetPosition(-117.5f, -74.0f, 40.0f);
    wall5->SetScale(205.0f, 50.0f, 4.0f);

    Wall* wall6 = game->CreateObj<Wall>("Wall6");
    wall6->SetPosition(117.5f, -74.0f, 40.0f);
    wall6->SetScale(205.0f, 50.0f, 4.0f);

    // Storage-room divider with a wide central passage.
    Wall* wall7 = game->CreateObj<Wall>("Wall7");
    wall7->SetPosition(-135.0f, -74.0f, -70.0f);
    wall7->SetScale(170.0f, 50.0f, 4.0f);

    Wall* wall8 = game->CreateObj<Wall>("Wall8");
    wall8->SetPosition(135.0f, -74.0f, -70.0f);
    wall8->SetScale(170.0f, 50.0f, 4.0f);

    // Short walls divide the rear storage area into three searchable rooms.
    Wall* wall9 = game->CreateObj<Wall>("Wall9");
    wall9->SetPosition(-90.0f, -74.0f, -140.0f);
    wall9->SetScale(4.0f, 50.0f, 80.0f);

    Wall* wall10 = game->CreateObj<Wall>("Wall10");
    wall10->SetPosition(90.0f, -74.0f, -140.0f);
    wall10->SetScale(4.0f, 50.0f, 80.0f);

    // The powered-door side becomes a narrow corridor with a side office.
    Wall* wall11 = game->CreateObj<Wall>("Wall11");
    wall11->SetPosition(-45.0f, -74.0f, 75.0f);
    wall11->SetScale(4.0f, 50.0f, 70.0f);

    Wall* wall12 = game->CreateObj<Wall>("Wall12");
    wall12->SetPosition(-45.0f, -74.0f, 155.0f);
    wall12->SetScale(4.0f, 50.0f, 50.0f);

    Wall* wall13 = game->CreateObj<Wall>("Wall13");
    wall13->SetPosition(45.0f, -74.0f, 110.0f);
    wall13->SetScale(4.0f, 50.0f, 140.0f);

    // Exit-hall divider. The center opening connects to the corridor.
    Wall* wall14 = game->CreateObj<Wall>("Wall14");
    wall14->SetPosition(-132.5f, -74.0f, 180.0f);
    wall14->SetScale(175.0f, 50.0f, 4.0f);

    Wall* wall15 = game->CreateObj<Wall>("Wall15");
    wall15->SetPosition(132.5f, -74.0f, 180.0f);
    wall15->SetScale(175.0f, 50.0f, 4.0f);

    // A narrow L-shaped repeating corridor begins at the center opening.
    // The first section runs north, then turns right behind a blind corner.
    Wall* loopWall1 = game->CreateObj<Wall>("LoopWall1");
    loopWall1->SetPosition(-45.0f, -74.0f, 227.5f);
    loopWall1->SetScale(4.0f, 50.0f, 95.0f);

    Wall* loopWall2 = game->CreateObj<Wall>("LoopWall2");
    loopWall2->SetPosition(45.0f, -74.0f, 207.5f);
    loopWall2->SetScale(4.0f, 50.0f, 55.0f);

    // The south wall starts at the corner, leaving the straight section open.
    Wall* loopWall3 = game->CreateObj<Wall>("LoopWall3");
    loopWall3->SetPosition(122.5f, -74.0f, 235.0f);
    loopWall3->SetScale(155.0f, 50.0f, 4.0f);

    // This long wall closes the forward view and forces the right turn.
    Wall* loopWall4 = game->CreateObj<Wall>("LoopWall4");
    loopWall4->SetPosition(77.5f, -74.0f, 275.0f);
    loopWall4->SetScale(245.0f, 50.0f, 4.0f);


    // Industrial utility details give each area a readable silhouette.
    // Trims and overhead conduits are visual-only; floor equipment blocks movement.
    const auto createStageProp = [game](
        const char* name,
        const Vector3& position,
        const Vector3& scale,
        const Color& diffuse,
        const Color& emission,
        float shininess,
        bool collisionEnabled)
    {
        Wall* prop = game->CreateObj<Wall>(name);
        prop->SetPosition(position.x, position.y, position.z);
        prop->SetScale(scale.x, scale.y, scale.z);
        prop->SetAppearance(diffuse, emission, shininess);
        prop->SetCollisionEnabled(collisionEnabled);
        return prop;
    };

    const Color darkMetal(0.10f, 0.115f, 0.11f, 1.0f);
    const Color cabinetMetal(0.16f, 0.18f, 0.17f, 1.0f);
    const Color trimColor(0.075f, 0.08f, 0.075f, 1.0f);
    const Color noEmission(0.0f, 0.0f, 0.0f, 1.0f);

    Wall* ceiling = createStageProp("PropCeilingMain",
        Vector3(0.0f, -47.0f, 80.0f), Vector3(436.0f, 3.0f, 516.0f),
        Color(0.055f, 0.06f, 0.058f, 1.0f), noEmission, 4.0f, false);
    ceiling->SetCastsShadow(false);

    createStageProp("PropPipeLeft", Vector3(-205.0f, -55.0f, 60.0f),
        Vector3(3.0f, 3.0f, 450.0f), darkMetal, noEmission, 22.0f, false);
    createStageProp("PropPipeRight", Vector3(205.0f, -55.0f, 60.0f),
        Vector3(3.0f, 3.0f, 450.0f), darkMetal, noEmission, 22.0f, false);
    createStageProp("PropPipeCrossDoor", Vector3(-115.0f, -52.5f, 37.0f),
        Vector3(175.0f, 2.5f, 2.5f), darkMetal, noEmission, 22.0f, false);
    createStageProp("PropPipeCrossDoorRight", Vector3(115.0f, -52.5f, 37.0f),
        Vector3(175.0f, 2.5f, 2.5f), darkMetal, noEmission, 22.0f, false);
    createStageProp("PropPipeCrossHall", Vector3(-132.5f, -52.5f, 177.0f),
        Vector3(175.0f, 2.5f, 2.5f), darkMetal, noEmission, 22.0f, false);
    createStageProp("PropPipeCrossHallRight", Vector3(132.5f, -52.5f, 177.0f),
        Vector3(175.0f, 2.5f, 2.5f), darkMetal, noEmission, 22.0f, false);

    createStageProp("PropBaseboardLeft", Vector3(-217.2f, -96.5f, 80.0f),
        Vector3(1.5f, 6.0f, 510.0f), trimColor, noEmission, 5.0f, false);
    createStageProp("PropBaseboardRight", Vector3(217.2f, -96.5f, 80.0f),
        Vector3(1.5f, 6.0f, 510.0f), trimColor, noEmission, 5.0f, false);

    createStageProp("PropCabinetLeft", Vector3(-190.0f, -87.0f, -132.0f),
        Vector3(28.0f, 24.0f, 12.0f), cabinetMetal, noEmission, 14.0f, true);
    createStageProp("PropCabinetRight", Vector3(190.0f, -87.0f, -112.0f),
        Vector3(28.0f, 24.0f, 12.0f), cabinetMetal, noEmission, 14.0f, true);
    createStageProp("PropCabinetBack", Vector3(-145.0f, -88.0f, 326.0f),
        Vector3(36.0f, 22.0f, 14.0f), cabinetMetal, noEmission, 14.0f, true);
    createStageProp("PropServiceBox", Vector3(205.0f, -82.0f, 118.0f),
        Vector3(10.0f, 30.0f, 24.0f), cabinetMetal, noEmission, 12.0f, true);

    createStageProp("PropExitColumnLeft", Vector3(-52.0f, -80.0f, 179.0f),
        Vector3(10.0f, 38.0f, 10.0f), darkMetal, noEmission, 8.0f, true);
    createStageProp("PropExitColumnRight", Vector3(52.0f, -80.0f, 179.0f),
        Vector3(10.0f, 38.0f, 10.0f), darkMetal, noEmission, 8.0f, true);
    createStageProp("PropDoorIndicator", Vector3(58.0f, -68.0f, 177.4f),
        Vector3(10.0f, 5.0f, 1.0f), Color(0.26f, 0.025f, 0.018f, 1.0f),
        Color(0.30f, 0.005f, 0.002f, 1.0f), 28.0f, false);

    const auto createLoopMarker = [&createStageProp, &noEmission](
        const char* name,
        const Vector3& position)
    {
        Wall* marker = createStageProp(name, position,
            Vector3(30.0f, 7.0f, 1.0f),
            Color(0.22f, 0.012f, 0.008f, 1.0f), noEmission, 20.0f, false);
        marker->SetCastsShadow(false);
        marker->SetVisible(false);
    };

    createLoopMarker("PropLoopMarker1", Vector3(-82.0f, -67.0f, -177.4f));
    createLoopMarker("PropLoopMarker2", Vector3(82.0f, -67.0f, -177.4f));
    createLoopMarker("PropLoopMarker3", Vector3(36.0f, -67.0f, 37.4f));
    // Ceiling fixtures communicate the power state visually.
    CeilingLight* light1 = game->CreateObj<CeilingLight>("CeilingLight1");
    light1->SetPosition(0.0f, -50.5f, -140.0f);
    light1->SetScale(24.0f, 2.0f, 11.0f);
    light1->SetEmergencyLight(true, 0.0f);

    CeilingLight* light2 = game->CreateObj<CeilingLight>("CeilingLight2");
    light2->SetPosition(-150.0f, -50.5f, -140.0f);
    light2->SetScale(22.0f, 2.0f, 10.0f);
    light2->SetEmergencyLight(false, 0.8f);

    CeilingLight* light3 = game->CreateObj<CeilingLight>("CeilingLight3");
    light3->SetPosition(150.0f, -50.5f, -140.0f);
    light3->SetScale(22.0f, 2.0f, 10.0f);
    light3->SetEmergencyLight(true, 1.7f);

    CeilingLight* light4 = game->CreateObj<CeilingLight>("CeilingLight4");
    light4->SetPosition(0.0f, -50.5f, -10.0f);
    light4->SetScale(26.0f, 2.0f, 11.0f);
    light4->SetEmergencyLight(true, 2.4f);

    CeilingLight* light5 = game->CreateObj<CeilingLight>("CeilingLight5");
    light5->SetPosition(0.0f, -50.5f, 105.0f);
    light5->SetScale(18.0f, 2.0f, 8.0f);
    light5->SetEmergencyLight(false, 3.1f);

    CeilingLight* light6 = game->CreateObj<CeilingLight>("CeilingLight6");
    light6->SetPosition(-130.0f, -50.5f, 120.0f);
    light6->SetScale(24.0f, 2.0f, 11.0f);
    light6->SetEmergencyLight(true, 3.8f);

    CeilingLight* light7 = game->CreateObj<CeilingLight>("CeilingLight7");
    light7->SetPosition(120.0f, -50.5f, 270.0f);
    light7->SetScale(30.0f, 2.0f, 13.0f);
    light7->SetEmergencyLight(false, 4.5f);

    CeilingLight* light8 = game->CreateObj<CeilingLight>("CeilingLight8");
    light8->SetPosition(0.0f, -50.5f, 215.0f);
    light8->SetScale(20.0f, 2.0f, 8.0f);
    light8->SetEmergencyLight(true, 6.2f);

    // アイテム
    Item* item1 = game->CreateObj<Item>("Item1");
    item1->SetPosition(0.0f, -95.0f, -155.0f);

    Item* item2 = game->CreateObj<Item>("Item2");
    item2->SetPosition(-150.0f, -95.0f, -140.0f);
    item2->SetActive(false);

    Item* item3 = game->CreateObj<Item>("Item3");
    item3->SetPosition(150.0f, -95.0f, -140.0f);
    item3->SetActive(false);

    // ドア
    Door* door = game->CreateObj<Door>("Door");
    door->SetPosition(0.0f, -74.0f, 40.0f);
    FuseBox* fuseBox = game->CreateObj<FuseBox>("FuseBox");
    fuseBox->SetPosition(-180.0f, -90.0f, 35.0f);

    FuseBox* exitPowerPanel =
        game->CreateObj<FuseBox>("ExitPowerPanel");
    exitPowerPanel->SetExitControl(true);
    exitPowerPanel->SetPosition(145.0f, -90.0f, 270.0f);

    FuseBox* emergencyCharger =
        game->CreateObj<FuseBox>("Stage1EmergencyCharger");
    emergencyCharger->SetManualControl("非常用充電器を使う");
    emergencyCharger->SetManualInteractionAllowed(true);
    emergencyCharger->SetPosition(-205.0f, -90.0f, -112.0f);
    emergencyCharger->SetRotation(Vector3(0.0f, 1.5707963f, 0.0f));

    // Optional exploration reward.  It is deliberately away from the
    // critical path so players choose between a faster escape and a full run.
    FuseBox* evidenceTerminal =
        game->CreateObj<FuseBox>("Stage1EvidenceTerminal");
    evidenceTerminal->SetManualControl("残された記録を回収する");
    evidenceTerminal->SetManualInteractionAllowed(true);
    evidenceTerminal->SetPosition(205.0f, -90.0f, -42.0f);
    evidenceTerminal->SetRotation(Vector3(0.0f, -1.5707963f, 0.0f));
    Wall* evidenceMarker = createStageProp(
        "Stage1EvidenceMarker",
        Vector3(217.2f, -65.0f, -42.0f),
        Vector3(1.0f, 5.0f, 18.0f),
        Color(0.025f, 0.11f, 0.09f, 1.0f),
        Color(0.02f, 0.24f, 0.16f, 1.0f),
        36.0f,
        false);
    evidenceMarker->SetCastsShadow(false);


    // Stage 1 exit: the player must operate a visible door and walk through
    // it.  The old invisible trigger could be activated by pressing A while
    // merely walking through the corridor.
    Door* stageExitDoor = game->CreateObj<Door>("Stage1ExitDoor");
    stageExitDoor->SetPosition(202.0f, -74.0f, 307.5f);
    stageExitDoor->SetRotation(Vector3(0.0f, 1.5707963f, 0.0f));
    stageExitDoor->SetScale(Vector3(60.0f, 50.0f, 4.0f));
    stageExitDoor->SetLocked(true);

    Wall* stageExitSign = createStageProp(
        "PropStage1ExitSign",
        Vector3(198.0f, -51.5f, 307.5f),
        Vector3(2.0f, 5.0f, 24.0f),
        Color(0.24f, 0.025f, 0.018f, 1.0f),
        Color(0.30f, 0.005f, 0.002f, 1.0f),
        30.0f,
        false);
    stageExitSign->SetCastsShadow(false);

    ExitTrigger* exit = game->CreateObj<ExitTrigger>("ExitTrigger");
    exit->SetPosition(207.0f, -80.0f, 307.5f);
    exit->SetNextScene(SceneName::Stage2);
    exit->SetInteractionEnabled(false);

    // バッテリー
    BatteryItem* battery = game->CreateObj<BatteryItem>("BatteryItem");
    battery->SetPosition(-130.0f, -95.0f, 120.0f);

    ScreenDustOverlay* crt =
        game->CreateObj<ScreenDustOverlay>("CRTNoise");

    crt->SetPower(0.7f);
    crt->SetActive(false);

    ScareTrigger* corridorScare =
        game->CreateObj<ScareTrigger>("ScareTrigger_Corridor");
    corridorScare->SetPosition(Vector3(0.0f, -90.0f, 65.0f));
    corridorScare->SetSize(Vector3(70.0f, 30.0f, 32.0f));
    corridorScare->SetShadowPosition(Vector3(0.0f, -99.0f, 150.0f));
    corridorScare->SetRequiresPower(true);

    ShadowMan* exitOmen =
        game->CreateObj<ShadowMan>("Stage1ExitOmen");
    exitOmen->SetPosition(92.0f, -99.0f, 278.0f);
    exitOmen->SetDeactivateOnExpire(true);
    exitOmen->SetActive(false);

    ShadowMan* fuseWatcher =
        game->CreateObj<ShadowMan>("Stage1FuseWatcher");
    fuseWatcher->SetDeactivateOnExpire(true);
    fuseWatcher->SetActive(false);

    // Prime the camera and light before the first draw after scene change.
    // This also prevents a black stage if gameplay is paused in ImGui.
    player->Update();


    m_Hud.Init();
}

// ヒューズ数と電力状態を基準に目的表示とイベント段階を更新します。
void StageScene::Update()
{
    Player* player =
        Core::Game::GetInstance()->GetObj<Player>("Player");

    if (player == nullptr || !player->CanControl())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    m_StageVisualTimer += 1.0f / 60.0f;
    m_ProgressHintTimer += 1.0f / 60.0f;
    if (Input::GetKeyTrigger(VK_H) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_ProgressHintTimer = (std::max)(m_ProgressHintTimer, 35.0f);
        Input::SetVibration(2, 0.04f);
    }

    m_FuseNoticeTimer = (std::max)(
        0.0f, m_FuseNoticeTimer - 1.0f / 60.0f);
    m_FuseWatcherNoticeTimer = (std::max)(
        0.0f, m_FuseWatcherNoticeTimer - 1.0f / 60.0f);
    m_ChargerNoticeTimer = (std::max)(
        0.0f, m_ChargerNoticeTimer - 1.0f / 60.0f);
    m_EvidenceNoticeTimer = (std::max)(
        0.0f, m_EvidenceNoticeTimer - 1.0f / 60.0f);
    const int currentFuseCount = game->GetItemCount();
    if (currentFuseCount > m_LastFuseCount)
    {
        m_LastFuseCount = currentFuseCount;
        m_ProgressHintTimer = 0.0f;
        m_FuseNoticeTimer = 2.35f;
        game->GetPostProcess()->TriggerBloomPulse(
            0.48f + static_cast<float>(currentFuseCount) * 0.12f,
            0.28f);
        Input::SetVibration(
            4 + currentFuseCount * 2,
            0.10f + static_cast<float>(currentFuseCount) * 0.025f);
        StartFuseWatcher(currentFuseCount);
    }

    if (game->IsPowerRestored())
    {
        ShadowMan* watcher = game->GetObj<ShadowMan>("Stage1FuseWatcher");
        if (watcher != nullptr) watcher->SetActive(false);
        m_FuseWatcherState = 0;
    }

    FuseBox* emergencyCharger =
        game->GetObj<FuseBox>("Stage1EmergencyCharger");
    if (!m_ChargerHandled && emergencyCharger != nullptr &&
        emergencyCharger->IsActivated())
    {
        m_ChargerHandled = true;
        m_ChargerNoticeTimer = 2.8f;
        player->AddBattery(35.0f);
        game->RegisterChargerUsed();
        StartFuseWatcher(2);

        CeilingLight* chargerLight =
            game->GetObj<CeilingLight>("CeilingLight2");
        if (chargerLight != nullptr)
        {
            chargerLight->TriggerEventFlicker(0.92f, 0.84f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.66f, 0.24f);
        Input::SetVibration(6, 0.14f);
    }

    FuseBox* evidenceTerminal =
        game->GetObj<FuseBox>("Stage1EvidenceTerminal");
    if (!m_EvidenceHandled && evidenceTerminal != nullptr &&
        evidenceTerminal->IsActivated())
    {
        m_EvidenceHandled = true;
        m_EvidenceNoticeTimer = 3.2f;
        game->RegisterEvidenceCollected();
        player->AddBattery(8.0f);
        Wall* evidenceMarker = game->GetObj<Wall>("Stage1EvidenceMarker");
        if (evidenceMarker != nullptr)
        {
            evidenceMarker->SetAppearance(
                Color(0.08f, 0.18f, 0.10f, 1.0f),
                Color(0.16f, 0.52f, 0.22f, 1.0f),
                44.0f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.42f, 0.20f);
        Input::SetVibration(4, 0.09f);
    }
    UpdateCorridorLoop(*player);
    UpdateEntranceThresholdEvent(*player);
    UpdateScareLightSequence();
    UpdatePowerRestoreSequence();
    UpdateExitPowerSequence();
    UpdateExitOmen(*player);

    Door* stageExitDoor = game->GetObj<Door>("Stage1ExitDoor");
    ExitTrigger* stageExit = game->GetObj<ExitTrigger>("ExitTrigger");
    const bool exitPowerReady = m_ExitPowerSequenceComplete;
    if (stageExitDoor != nullptr)
    {
        stageExitDoor->SetLocked(!exitPowerReady);
        const Vector3 exitPosition = player->GetPosition();
        const bool crossedOpenedDoor =
            stageExitDoor->IsOpen() &&
            exitPosition.x >= 199.0f &&
            std::abs(exitPosition.z - 307.5f) <= 34.0f;
        if (crossedOpenedDoor && stageExit != nullptr)
        {
            stageExit->BeginEscape(*player);
        }
    }

    Wall* stageExitSign = game->GetObj<Wall>("PropStage1ExitSign");
    if (stageExitSign != nullptr)
    {
        const float pulse = 0.78f +
            std::sin(m_StageVisualTimer *
                (game->IsPowerRestored() ? 3.2f : 7.4f)) * 0.16f;
        if (exitPowerReady)
        {
            stageExitSign->SetAppearance(
                Color(0.025f, 0.24f, 0.06f, 1.0f),
                Color(0.006f, 0.36f * pulse, 0.025f, 1.0f),
                34.0f);
        }
        else
        {
            stageExitSign->SetAppearance(
                Color(0.24f, 0.025f, 0.018f, 1.0f),
                Color(0.32f * pulse, 0.004f, 0.002f, 1.0f),
                30.0f);
        }
    }

    Wall* exitIndicator = game->GetObj<Wall>("PropDoorIndicator");
    if (exitIndicator != nullptr)
    {
        const float omenRate = m_ExitOmenTriggered
            ? 1.0f - (std::clamp)(m_ExitOmenTimer / 3.2f, 0.0f, 1.0f)
            : 0.0f;
        const float indicatorPulse =
            0.72f + std::sin(m_StageVisualTimer *
                (3.2f + omenRate * 8.0f)) *
                (0.10f + omenRate * 0.14f);
        if (exitPowerReady)
        {
            exitIndicator->SetAppearance(
                Color(0.025f, 0.20f, 0.055f, 1.0f),
                Color(0.005f, 0.24f * indicatorPulse, 0.025f, 1.0f),
                30.0f);
        }
        else
        {
            exitIndicator->SetAppearance(
                Color(0.24f, 0.025f, 0.018f, 1.0f),
                Color(0.28f * indicatorPulse, 0.004f, 0.002f, 1.0f),
                28.0f);
        }
    }

    float lowBattery = (25.0f - player->GetBattery()) / 25.0f;
    if (lowBattery < 0.0f) lowBattery = 0.0f;
    if (lowBattery > 1.0f) lowBattery = 1.0f;

    const float powerBlend = game->IsPowerRestored()
        ? (std::clamp)(m_PowerRestoreTimer / 2.5f, 0.0f, 1.0f)
        : 0.0f;
    const float powerCalm = 0.03f * powerBlend;
    const float sprintStress = player->IsSprinting() ? 1.0f : 0.0f;
    const Vector3 playerPosition = player->GetPosition();

    // The repeating corridor grows subtly oppressive toward its far end.
    // Loop count raises the baseline, while restored power clears the effect.
    const float corridorDepth = (std::clamp)(
        (playerPosition.z - 90.0f) / 190.0f,
        0.0f,
        1.0f);
    const float corridorWidthMask = 1.0f - (std::clamp)(
        (std::abs(playerPosition.x) - 75.0f) / 105.0f,
        0.0f,
        1.0f);
    const float loopTension = (std::min)(
        static_cast<float>(m_CorridorLoopCount) * 0.14f,
        0.42f);
    const float poweredExitDepth = (std::clamp)(
        (playerPosition.z - 190.0f) / 105.0f,
        0.0f,
        1.0f);
    const float corridorTension = game->IsPowerRestored()
        ? poweredExitDepth * 0.38f
        : (std::clamp)(
            corridorDepth * corridorWidthMask * 0.72f + loopTension,
            0.0f,
            1.0f);
    game->GetPostProcess()->SetCorridorTension(corridorTension);

    // Preserve horror darkness while allowing navigation after the player's
    // eyes have had time to adjust without the flashlight.
    const float targetExposure = game->IsPowerRestored()
        ? 1.12f + (1.01f - 1.12f) * powerBlend
        : (player->IsFlashlightOn() ? 1.04f : 1.15f);
    game->GetPostProcess()->SetExposure(targetExposure);
    const float noiseAmount =
        0.18f - powerCalm + lowBattery * 0.18f +
        sprintStress * 0.04f + corridorTension * 0.055f;
    const float vignetteStrength =
        0.55f - powerCalm + lowBattery * 0.20f +
        sprintStress * 0.06f + corridorTension * 0.075f;

    game->GetPostProcess()->SetAtmosphere(
        noiseAmount,
        vignetteStrength);
    game->GetPostProcess()->SetLensDistortionStrength(
        0.20f + corridorTension * 0.22f);
    game->GetPostProcess()->SetFilmGradeStrength(
        0.52f + corridorTension * 0.18f);
    game->GetPostProcess()->SetLensDirtStrength(
        0.10f + corridorTension * 0.12f);

    m_InteractionSystem.Update(*player);
}

void StageScene::UpdateEntranceThresholdEvent(Player& player)
{
    constexpr float deltaTime = 1.0f / 60.0f;
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

        // A physical light reaction replaces the old forced-camera cutaway.
        // Control remains with the player, so noticing the event feels earned.
        m_EntranceEventTriggered = true;
        m_EntranceEventTimer = 0.0f;
        m_EntranceEventPhase = 0;

        CeilingLight* lightBehind =
            game->GetObj<CeilingLight>("CeilingLight4");
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
            game->GetObj<CeilingLight>("CeilingLight5");
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
            game->GetObj<CeilingLight>("CeilingLight4");
        if (lightBehind != nullptr)
        {
            lightBehind->TriggerEventFlicker(0.20f, 0.28f);
        }

        m_EntranceEventTimer = -1.0f;
        m_EntranceEventPhase = 2;
    }
}

void StageScene::UpdateCorridorLoop(Player& player)
{
    constexpr float deltaTime = 1.0f / 60.0f;
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

    // The camera is updated by Player later in the same frame, hiding a scene
    // reload and preserving the direction in which the player was looking.
    player.SetPosition(Vector3(0.0f, -99.0f, -150.0f));
    m_LoopCooldown = 1.0f;
    m_ProgressHintTimer = 0.0f;
    m_LoopNoticeTimer = 2.4f;

    const int loopPhase = m_CorridorLoopCount < 3
        ? m_CorridorLoopCount
        : 3;

    for (int markerIndex = 1; markerIndex <= 3; ++markerIndex)
    {
        const std::string markerName =
            "PropLoopMarker" + std::to_string(markerIndex);
        Wall* marker = game->GetObj<Wall>(markerName);
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

    // Returning to the entrance also restores the corridor door. Reopening
    // the same physical threshold makes each loop feel deliberate.
    Door* loopDoor = game->GetObj<Door>("Door");
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(loopPhase);
    }

    CeilingLight* entranceLight =
        game->GetObj<CeilingLight>("CeilingLight1");
    CeilingLight* middleLight =
        game->GetObj<CeilingLight>("CeilingLight4");
    CeilingLight* cornerLight =
        game->GetObj<CeilingLight>("CeilingLight8");

    if (loopPhase == 1)
    {
        Item* secondFuse = game->GetObj<Item>("Item2");
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
        Item* thirdFuse = game->GetObj<Item>("Item3");
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

        // The warning is literal: a single apparition waits behind the player.
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

    m_ScareLightTimer = 0.0f;
    m_ScareLightPhase = 0;
    m_ScareMessageTimer = 3.8f;

    CeilingLight* entrance =
        game->GetObj<CeilingLight>("CeilingLight1");
    CeilingLight* middle =
        game->GetObj<CeilingLight>("CeilingLight4");
    CeilingLight* hall =
        game->GetObj<CeilingLight>("CeilingLight5");
    CeilingLight* corner =
        game->GetObj<CeilingLight>("CeilingLight8");
    CeilingLight* loopExit =
        game->GetObj<CeilingLight>("CeilingLight7");

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
    constexpr float deltaTime = 1.0f / 60.0f;
    if (m_ScareMessageTimer > 0.0f)
    {
        m_ScareMessageTimer -= deltaTime;
    }

    if (m_ScareLightTimer < 0.0f)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        m_ScareLightTimer = -1.0f;
        m_ScareLightPhase = -1;
        return;
    }

    m_ScareLightTimer += deltaTime;

    CeilingLight* entrance =
        game->GetObj<CeilingLight>("CeilingLight1");
    CeilingLight* middle =
        game->GetObj<CeilingLight>("CeilingLight4");
    CeilingLight* hall =
        game->GetObj<CeilingLight>("CeilingLight5");
    CeilingLight* corner =
        game->GetObj<CeilingLight>("CeilingLight8");
    CeilingLight* loopExit =
        game->GetObj<CeilingLight>("CeilingLight7");

    if (m_ScareLightPhase == 0 && m_ScareLightTimer >= 0.45f)
    {
        if (entrance != nullptr)
        {
            entrance->SetEmergencyLight(false, 0.35f);
        }
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, 0.75f);
        }
        m_ScareLightPhase = 1;
        game->GetPostProcess()->TriggerHorrorPulse(0.13f, 0.18f);
        Input::SetVibration(5, 0.10f);
    }
    else if (m_ScareLightPhase == 1 && m_ScareLightTimer >= 0.90f)
    {
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(false, 0.75f);
        }
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(true, 0.42f);
        }
        m_ScareLightPhase = 2;
        game->GetPostProcess()->TriggerHorrorPulse(0.14f, 0.18f);
        Input::SetVibration(6, 0.13f);
    }
    else if (m_ScareLightPhase == 2 && m_ScareLightTimer >= 1.38f)
    {
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(false, 0.42f);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, 0.18f);
        }
        m_ScareLightPhase = 3;
        game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.20f);
        Input::SetVibration(7, 0.16f);
    }
    else if (m_ScareLightPhase == 3 && m_ScareLightTimer >= 1.92f)
    {
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(false, 0.18f);
        }
        if (loopExit != nullptr)
        {
            loopExit->SetEmergencyLight(true, 0.08f);
        }
        m_ScareLightPhase = 4;
        game->GetPostProcess()->TriggerHorrorPulse(0.19f, 0.22f);
        Input::SetVibration(8, 0.19f);
    }
    else if (m_ScareLightPhase == 4 && m_ScareLightTimer >= 2.65f)
    {
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

        m_ScareLightTimer = -1.0f;
        m_ScareLightPhase = 5;
        Input::SetVibration(4, 0.09f);
    }
}

void StageScene::StartFuseWatcher(int fuseCount)
{
    if (fuseCount < 2 || Core::Game::GetInstance()->IsPowerRestored())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* watcher = game->GetObj<ShadowMan>("Stage1FuseWatcher");
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
                game->GetObj<ShadowMan>("Stage1FuseWatcher");
            if (activeWatcher != nullptr)
            {
                activeWatcher->SetActive(false);
            }

            CeilingLight* reactionLight =
                game->GetObj<CeilingLight>("CeilingLight4");
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
    constexpr float deltaTime = 1.0f / 60.0f;
    Core::Game* game = Core::Game::GetInstance();
    const bool powerRestored = game->IsPowerRestored();

    if (powerRestored && !m_WasPowerRestored)
    {
        m_PowerRestoreTimer = 0.0f;
        m_ProgressHintTimer = 0.0f;
        m_PowerRestorePhase = 0;

        // Power restoration owns the presentation from this point onward.
        m_ScareLightTimer = -1.0f;
        m_ScareLightPhase = -1;
        m_ScareMessageTimer = 0.0f;

        for (int markerIndex = 1; markerIndex <= 3; ++markerIndex)
        {
            const std::string markerName =
                "PropLoopMarker" + std::to_string(markerIndex);
            Wall* marker = game->GetObj<Wall>(markerName);
            if (marker != nullptr)
            {
                marker->SetVisible(false);
            }
        }
    }

    m_WasPowerRestored = powerRestored;
    if (!powerRestored || m_PowerRestoreTimer < 0.0f)
    {
        return;
    }

    m_PowerRestoreTimer += deltaTime;

    if (m_PowerRestorePhase == 0 && m_PowerRestoreTimer >= 0.38f)
    {
        game->GetPostProcess()->TriggerBloomPulse(1.18f, 0.55f);
        Input::SetVibration(9, 0.16f);
        m_PowerRestorePhase = 1;
    }
    else if (m_PowerRestorePhase == 1 && m_PowerRestoreTimer >= 1.15f)
    {
        game->GetPostProcess()->TriggerHorrorPulse(0.12f, 0.22f);
        Input::SetVibration(6, 0.11f);
        m_PowerRestorePhase = 2;
    }
    else if (m_PowerRestorePhase == 2 && m_PowerRestoreTimer >= 2.25f)
    {
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.40f);
        Input::SetVibration(4, 0.07f);
        m_PowerRestorePhase = 3;
    }
}

void StageScene::UpdateExitPowerSequence()
{
    constexpr float deltaTime = 1.0f / 60.0f;
    Core::Game* game = Core::Game::GetInstance();
    FuseBox* panel = game->GetObj<FuseBox>("ExitPowerPanel");
    if (panel == nullptr || !panel->IsActivated())
    {
        return;
    }

    if (m_ExitPowerEventTimer < 0.0f)
    {
        m_ExitPowerEventTimer = 0.0f;
        m_ExitPowerEventPhase = 0;
        m_ProgressHintTimer = 0.0f;

        CeilingLight* corner = game->GetObj<CeilingLight>("CeilingLight7");
        CeilingLight* exitLight = game->GetObj<CeilingLight>("CeilingLight8");
        if (corner != nullptr) corner->SetForcedOff(true);
        if (exitLight != nullptr) exitLight->SetForcedOff(true);
        game->GetPostProcess()->TriggerHorrorPulse(0.30f, 0.28f);
        return;
    }

    if (m_ExitPowerSequenceComplete)
    {
        return;
    }

    m_ExitPowerEventTimer += deltaTime;
    if (m_ExitPowerEventPhase == 0 && m_ExitPowerEventTimer >= 0.28f)
    {
        CeilingLight* corner = game->GetObj<CeilingLight>("CeilingLight7");
        if (corner != nullptr)
        {
            corner->SetForcedOff(false);
            corner->SetEmergencyLight(false, 0.0f);
            corner->TriggerEventFlicker(0.72f, 0.82f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.68f, 0.24f);
        Input::SetVibration(5, 0.11f);
        m_ExitPowerEventPhase = 1;
    }
    else if (m_ExitPowerEventPhase == 1 && m_ExitPowerEventTimer >= 0.78f)
    {
        CeilingLight* exitLight = game->GetObj<CeilingLight>("CeilingLight8");
        if (exitLight != nullptr)
        {
            exitLight->SetForcedOff(false);
            exitLight->SetEmergencyLight(false, 0.0f);
            exitLight->TriggerEventFlicker(0.82f, 0.92f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.92f, 0.30f);
        Input::SetVibration(7, 0.15f);
        m_ExitPowerEventPhase = 2;
    }
    else if (m_ExitPowerEventPhase == 2 && m_ExitPowerEventTimer >= 1.30f)
    {
        m_ExitPowerSequenceComplete = true;
        m_ExitPowerEventPhase = 3;
        game->GetPostProcess()->TriggerBloomPulse(1.18f, 0.42f);
        Input::SetVibration(10, 0.20f);
    }
}

void StageScene::UpdateExitOmen(Player& player)
{
    constexpr float deltaTime = 1.0f / 60.0f;
    m_ExitOmenTimer =
        (std::max)(0.0f, m_ExitOmenTimer - deltaTime);
    if (m_ExitOmenTriggered)
    {
        Core::Game* game = Core::Game::GetInstance();
        if (m_ExitOmenPhase == 0 && m_ExitOmenTimer <= 2.45f)
        {
            CeilingLight* lightBehind =
                game->GetObj<CeilingLight>("CeilingLight8");
            if (lightBehind != nullptr)
            {
                lightBehind->SetForcedOff(true);
            }
            game->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.30f);
            Input::SetVibration(7, 0.16f);
            m_ExitOmenPhase = 1;
        }
        else if (m_ExitOmenPhase == 1 && m_ExitOmenTimer <= 1.35f)
        {
            CeilingLight* exitLight =
                game->GetObj<CeilingLight>("CeilingLight7");
            if (exitLight != nullptr)
            {
                exitLight->SetFaulted(true);
                exitLight->TriggerEventFlicker(1.10f, 0.88f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.44f, 0.20f);
            m_ExitOmenPhase = 2;
        }
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 playerPosition = player.GetPosition();
    if (!m_ExitPowerSequenceComplete ||
        playerPosition.z < 215.0f ||
        playerPosition.x < 28.0f)
    {
        return;
    }

    m_ExitOmenTriggered = true;
    m_ExitOmenTimer = 3.2f;
    m_ExitOmenPhase = 0;

    ShadowMan* shadow =
        game->GetObj<ShadowMan>("Stage1ExitOmen");
    if (shadow != nullptr)
    {
        shadow->SetActive(true);
        shadow->EnableGazeScare(4.2f);
    }

    const char* exitLightNames[] =
    {
        "CeilingLight7", "CeilingLight8"
    };
    for (const char* lightName : exitLightNames)
    {
        CeilingLight* light =
            game->GetObj<CeilingLight>(lightName);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.82f, 0.78f);
        }
    }

    game->GetPostProcess()->TriggerHorrorPulse(0.28f, 0.42f);
    game->GetPostProcess()->TriggerBloomPulse(0.54f, 0.24f);
    Input::SetVibration(10, 0.22f);
}

void StageScene::Draw(Camera* camera)
{
    (void)camera;

    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");

    if (player == nullptr)
    {
        return;
    }

    FuseBox* exitPowerPanel = game->GetObj<FuseBox>("ExitPowerPanel");
    const bool exitPowerActivated =
        exitPowerPanel != nullptr && exitPowerPanel->IsActivated();
    const bool exitPowerReady = m_ExitPowerSequenceComplete;

    const int fuseCount = game->GetItemCount();
    std::string_view objectiveText;
    if (fuseCount <= 0)
    {
        objectiveText = "開始地点の近くでヒューズを探す";
    }
    else if (fuseCount == 1)
    {
        objectiveText = m_CorridorLoopCount < 1
            ? "中央のドアを開けて廊下の奥へ進む"
            : "左側の部屋でヒューズを探す";
    }
    else if (fuseCount == 2)
    {
        objectiveText = m_CorridorLoopCount < 2
            ? "もう一度廊下の奥まで進む"
            : "右側の部屋でヒューズを探す";
    }
    else
    {
        objectiveText = "左の部屋にある配電盤を調べる";
    }
    ExitTrigger* exitTrigger =
        game->GetObj<ExitTrigger>("ExitTrigger");
    if (exitTrigger != nullptr && exitTrigger->IsEscaping())
    {
        objectiveText = "ドアの先へ移動中";
    }
    else if (m_ChargerNoticeTimer > 0.0f)
    {
        objectiveText = m_FuseWatcherState == 1
            ? "充電音で影が現れた ライトを向ける"
            : "バッテリーを充電した";
    }
    else if (m_EvidenceNoticeTimer > 0.0f)
    {
        objectiveText = "残された記録を回収した 1 / 3";
    }
    else if (m_FuseNoticeTimer > 0.0f)
    {
        if (m_FuseWatcherState == 1)
            objectiveText = "影に懐中電灯を向ける";
        else if (fuseCount == 1) objectiveText = "ヒューズを1本入手";
        else if (fuseCount == 2) objectiveText = "ヒューズを2本入手";
        else objectiveText = "ヒューズを3本入手";
    }
    else if (m_FuseWatcherNoticeTimer > 0.0f)
    {
        objectiveText = m_FuseWatcherState == 1
            ? "影を正面から懐中電灯で照らす"
            : "影が光の中へ消えた";
    }
    else if (m_ExitOmenTimer > 0.0f)
    {
        objectiveText = m_ExitOmenTimer > 1.75f
            ? "何かが待っている"
            : "立ち止まらず進む";
    }
    else if (game->IsPowerRestored() &&
        m_PowerRestoreTimer >= 0.0f &&
        m_PowerRestoreTimer < 4.5f)
    {
        objectiveText = m_PowerRestoreTimer < 1.55f
            ? "電力が復旧した"
            : "出口側の非常送電盤へ向かう";
    }
    else if (exitPowerActivated && !exitPowerReady)
    {
        objectiveText = "非常電源を送電中";
    }
    else if (m_ScareMessageTimer > 0.0f)
    {
        objectiveText = m_ScareMessageTimer > 2.65f
            ? "何かに見られている"
            : "点灯した照明をたどる";
    }
    else if (m_LoopNoticeTimer > 0.0f)
    {
        if (m_CorridorLoopCount == 1)
        {
            objectiveText = "廊下の様子が変わった";
        }
        else if (m_CorridorLoopCount == 2)
        {
            objectiveText = "そのまま歩き続ける";
        }
        else
        {
            objectiveText = "後ろを振り返らない";
        }
    }
    else if (m_ProgressHintTimer >= 35.0f)
    {
        if (game->IsPowerRestored() && !exitPowerActivated)
        {
            objectiveText = "ヒント 右奥の赤い送電盤を調べる";
        }
        else if (game->IsPowerRestored())
        {
            objectiveText = "ヒント 右奥の緑色の出口へ向かう";
        }
        else if (fuseCount <= 0)
        {
            objectiveText = "ヒント 最初の壁付近を探す";
        }
        else if (fuseCount == 1 && m_CorridorLoopCount < 1)
        {
            objectiveText = "ヒント 中央のドアを開け廊下の奥へ進む";
        }
        else if (fuseCount == 1)
        {
            objectiveText = "ヒント 左奥の部屋を探す";
        }
        else if (fuseCount == 2 && m_CorridorLoopCount < 2)
        {
            objectiveText = "ヒント もう一度廊下の奥まで進む";
        }
        else if (fuseCount == 2)
        {
            objectiveText = "ヒント 右奥の部屋を探す";
        }
        else
        {
            objectiveText = "ヒント 左の部屋の配電盤を調べる";
        }
    }
    else if (m_ProgressHintTimer >= 18.0f)
    {
        if (game->IsPowerRestored() && !exitPowerActivated)
        {
            objectiveText = "ヒント 出口手前の送電盤へ向かう";
        }
        else if (game->IsPowerRestored())
        {
            objectiveText = "ヒント 緑色の出口灯をたどる";
        }
        else if (fuseCount <= 0)
        {
            objectiveText = "ヒント 開始地点の周囲を探す";
        }
        else if (fuseCount == 1 && m_CorridorLoopCount < 1)
        {
            objectiveText = "ヒント 中央のドアが進行ルート";
        }
        else if (fuseCount == 1)
        {
            objectiveText = "ヒント 左側の部屋を確認する";
        }
        else if (fuseCount == 2 && m_CorridorLoopCount < 2)
        {
            objectiveText = "ヒント 長い廊下をもう一度進む";
        }
        else if (fuseCount == 2)
        {
            objectiveText = "ヒント 右側の部屋を確認する";
        }
        else
        {
            objectiveText = "ヒント 配電盤へ戻る";
        }
    }
    else if (game->IsPowerRestored() && !exitPowerActivated)
    {
        objectiveText = "出口手前の非常送電盤を操作する";
    }
    else if (game->IsPowerRestored())
    {
        Door* stageExitDoor = game->GetObj<Door>("Stage1ExitDoor");
        objectiveText = stageExitDoor != nullptr && stageExitDoor->IsOpen()
            ? "開いた出口ドアを通り抜ける"
            : "右奥の出口ドアを開ける";
    }

    m_Hud.Draw(
        *player,
        fuseCount,
        m_InteractionSystem.GetPrompt(),
        objectiveText);

    if (m_StageVisualTimer >= 4.20f &&
        (exitTrigger == nullptr || !exitTrigger->IsEscaping()))
    {
        Vector3 guideTarget(0.0f, -99.0f, 315.0f);
        if (game->IsPowerRestored() && !exitPowerActivated)
        {
            guideTarget = Vector3(145.0f, -90.0f, 270.0f);
        }
        else if (game->IsPowerRestored())
        {
            guideTarget = Vector3(202.0f, -74.0f, 307.5f);
        }
        else if (fuseCount <= 0)
        {
            guideTarget = Vector3(0.0f, -95.0f, -155.0f);
        }
        else if (fuseCount == 1 && m_CorridorLoopCount >= 1)
        {
            guideTarget = Vector3(-150.0f, -95.0f, -140.0f);
        }
        else if (fuseCount == 2 && m_CorridorLoopCount >= 2)
        {
            guideTarget = Vector3(150.0f, -95.0f, -140.0f);
        }
        else if (fuseCount >= 3)
        {
            guideTarget = Vector3(-180.0f, -90.0f, 35.0f);
        }
        m_Hud.DrawObjectiveGuide(
            *camera,
            player->GetPosition(),
            guideTarget);
    }

    if (m_StageVisualTimer < 0.65f)
    {
        const float fade = 1.0f - m_StageVisualTimer / 0.65f;
        m_Hud.DrawBlink(fade * fade);
    }
    if (m_StageVisualTimer < 4.20f)
    {
        m_Hud.DrawChapterCard(
            "1階", "ヒューズを集めて電力を復旧する", m_StageVisualTimer);
    }

    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->GetPauseSettingIndex(),
            1,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
}

void StageScene::Uninit()
{
    Core::Game::GetInstance()->GetPostProcess()->SetAtmosphere(0.18f, 0.55f);
    Core::Game::GetInstance()->GetPostProcess()->SetExposure(1.0f);
    Core::Game::GetInstance()->GetPostProcess()->SetCorridorTension(0.0f);
    Core::Game::GetInstance()->GetPostProcess()->SetVolumetricLight(false);
    Core::Game::GetInstance()->GetPostProcess()->SetLensDistortionStrength(0.20f);
    Core::Game::GetInstance()->GetPostProcess()->SetFilmGradeStrength(0.55f);
    Core::Game::GetInstance()->GetPostProcess()->SetLensDirtStrength(0.10f);
    m_Hud.Uninit();

    Core::Game* game = Core::Game::GetInstance();

    game->DestroyObj("Player");
    game->DestroyObj("Ground");

    game->DestroyObj("Wall1");
    game->DestroyObj("Wall2");
    game->DestroyObj("Wall3");
    game->DestroyObj("Wall4");
    game->DestroyObj("Wall5");
    game->DestroyObj("Wall6");
    game->DestroyObj("Wall7");
    game->DestroyObj("Wall8");
    game->DestroyObj("Wall9");
    game->DestroyObj("Wall10");
    game->DestroyObj("Wall11");
    game->DestroyObj("Wall12");
    game->DestroyObj("Wall13");
    game->DestroyObj("Wall14");
    game->DestroyObj("Wall15");
    game->DestroyObj("LoopWall1");
    game->DestroyObj("LoopWall2");
    game->DestroyObj("LoopWall3");
    game->DestroyObj("LoopWall4");
    game->DestroyObj("PropCeilingMain");

    game->DestroyObj("PropPipeLeft");
    game->DestroyObj("PropPipeRight");
    game->DestroyObj("PropPipeCrossDoor");
    game->DestroyObj("PropPipeCrossDoorRight");
    game->DestroyObj("PropPipeCrossHall");
    game->DestroyObj("PropPipeCrossHallRight");
    game->DestroyObj("PropBaseboardLeft");
    game->DestroyObj("PropBaseboardRight");
    game->DestroyObj("PropCabinetLeft");
    game->DestroyObj("PropCabinetRight");
    game->DestroyObj("PropCabinetBack");
    game->DestroyObj("PropServiceBox");
    game->DestroyObj("PropExitColumnLeft");
    game->DestroyObj("PropExitColumnRight");
    game->DestroyObj("PropDoorIndicator");
    game->DestroyObj("PropLoopMarker1");
    game->DestroyObj("PropLoopMarker2");
    game->DestroyObj("PropLoopMarker3");

    game->DestroyObj("CeilingLight1");
    game->DestroyObj("CeilingLight2");
    game->DestroyObj("CeilingLight3");
    game->DestroyObj("CeilingLight4");
    game->DestroyObj("CeilingLight5");
    game->DestroyObj("CeilingLight6");
    game->DestroyObj("CeilingLight7");
    game->DestroyObj("CeilingLight8");

    game->DestroyObj("Item1");
    game->DestroyObj("Item2");
    game->DestroyObj("Item3");

    game->DestroyObj("Door");
    game->DestroyObj("Stage1ExitDoor");
    game->DestroyObj("PropStage1ExitSign");
    game->DestroyObj("FuseBox");
    game->DestroyObj("ExitPowerPanel");
    game->DestroyObj("Stage1EmergencyCharger");
    game->DestroyObj("Stage1EvidenceTerminal");
    game->DestroyObj("Stage1EvidenceMarker");
    game->DestroyObj("ExitTrigger");
    game->DestroyObj("BatteryItem");
    game->DestroyObj("ScareTrigger_Corridor");
    game->DestroyObj("Stage1ExitOmen");
    game->DestroyObj("Stage1FuseWatcher");
}
