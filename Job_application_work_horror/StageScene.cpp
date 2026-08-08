#include "StageScene.h"
#include "Game.h"
#include "Input.h"

#include "Player.h"
#include "Ground.h"
#include "Wall.h"
#include "Texture2D.h"
#include "Item.h"
#include "Door.h"
#include "FuseBox.h"
#include "CeilingLight.h"
#include "ExitTrigger.h"
#include "BatteryItem.h"
#include "MovieTrigger.h"
#include "ScreenDustOverlay.h"
#include "ScareTrigger.h"
#include "ShadowMan.h"
#include <SimpleMath.h>
#include <cmath>

using namespace DirectX::SimpleMath;

StageScene::StageScene()
{
    Init();
}

StageScene::~StageScene()
{
    Uninit();
}

void StageScene::Init()
{
    Core::Game* game = Core::Game::GetInstance();
    game->GetPostProcess()->SetVolumetricLight(true);
    m_CorridorLoopCount = 0;
    m_LoopCooldown = 0.0f;
    m_LoopNoticeTimer = 0.0f;
    m_ScareLightTimer = -1.0f;
    m_ScareLightPhase = -1;

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


    // ゴール
    ExitTrigger* exit = game->CreateObj<ExitTrigger>("ExitTrigger");
    exit->SetPosition(150.0f, -80.0f, 300.0f);

    // バッテリー
    BatteryItem* battery = game->CreateObj<BatteryItem>("BatteryItem");
    battery->SetPosition(-130.0f, -95.0f, 120.0f);

    MovieTrigger* movie =
        game->CreateObj<MovieTrigger>("MovieTrigger_01");
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


    movie->SetPosition(Vector3(0.0f, -90.0f, 90.0f));
    movie->SetSize(Vector3(50.0f, 30.0f, 24.0f));
    movie->SetCameraEnd(
        Vector3(0.0f, -92.0f, 150.0f),
        Vector3(0.0f, -92.0f, 185.0f)
    );
    movie->SetDuration(1.2f);
    Texture2D* triggerMark =
        game->CreateObj<Texture2D>("MovieTriggerMark");

    triggerMark->SetTexture("assets/texture/ui_back.png");

    // センサーと同じ位置
    triggerMark->SetPosition(0.0f, -90.0f, 90.0f);

    // 大きめに表示
    triggerMark->SetScale(50.0f, 50.0f, 1.0f);

    m_Hud.Init();
}

void StageScene::Update()
{
    Player* player =
        Core::Game::GetInstance()->GetObj<Player>("Player");

    if (player == nullptr || !player->CanControl())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    UpdateCorridorLoop(*player);
    UpdateScareLightSequence();

    float lowBattery = (25.0f - player->GetBattery()) / 25.0f;
    if (lowBattery < 0.0f) lowBattery = 0.0f;
    if (lowBattery > 1.0f) lowBattery = 1.0f;

    const float powerCalm = game->IsPowerRestored() ? 0.03f : 0.0f;
    const float sprintStress = player->IsSprinting() ? 1.0f : 0.0f;

    // Preserve horror darkness while allowing navigation after the player's
    // eyes have had time to adjust without the flashlight.
    const float targetExposure = game->IsPowerRestored()
        ? 1.01f
        : (player->IsFlashlightOn() ? 1.04f : 1.15f);
    game->GetPostProcess()->SetExposure(targetExposure);
    const float noiseAmount =
        0.18f - powerCalm + lowBattery * 0.18f + sprintStress * 0.04f;
    const float vignetteStrength =
        0.55f - powerCalm + lowBattery * 0.20f + sprintStress * 0.06f;

    game->GetPostProcess()->SetAtmosphere(
        noiseAmount,
        vignetteStrength);

    m_InteractionSystem.Update(*player);
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
    m_LoopNoticeTimer = 2.4f;

    const int loopPhase = m_CorridorLoopCount < 3
        ? m_CorridorLoopCount
        : 3;
    game->GetPostProcess()->TriggerHorrorPulse(
        0.24f + static_cast<float>(loopPhase) * 0.13f,
        0.42f + static_cast<float>(loopPhase) * 0.10f);
    Input::SetVibration(7 + loopPhase * 3, 0.18f + loopPhase * 0.04f);

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

    CeilingLight* entrance =
        game->GetObj<CeilingLight>("CeilingLight1");
    CeilingLight* middle =
        game->GetObj<CeilingLight>("CeilingLight4");
    CeilingLight* corner =
        game->GetObj<CeilingLight>("CeilingLight8");

    if (entrance != nullptr)
    {
        entrance->SetEmergencyLight(true, 0.35f);
    }
    if (middle != nullptr)
    {
        middle->SetEmergencyLight(false, 8.2f);
    }
    if (corner != nullptr)
    {
        corner->SetEmergencyLight(false, 4.3f);
    }
}

void StageScene::UpdateScareLightSequence()
{
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

    constexpr float deltaTime = 1.0f / 60.0f;
    m_ScareLightTimer += deltaTime;

    CeilingLight* entrance =
        game->GetObj<CeilingLight>("CeilingLight1");
    CeilingLight* middle =
        game->GetObj<CeilingLight>("CeilingLight4");
    CeilingLight* corner =
        game->GetObj<CeilingLight>("CeilingLight8");

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
    }
    else if (m_ScareLightPhase == 1 && m_ScareLightTimer >= 1.05f)
    {
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(false, 0.75f);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, 0.15f);
        }
        m_ScareLightPhase = 2;
        game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.20f);
    }
    else if (m_ScareLightPhase == 2 && m_ScareLightTimer >= 1.85f)
    {
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, 2.6f);
        }
        m_ScareLightTimer = -1.0f;
        m_ScareLightPhase = -1;
    }
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

    std::string_view objectiveText = "FIND 3 FUSES";
    if (m_LoopNoticeTimer > 0.0f)
    {
        if (m_CorridorLoopCount == 1)
        {
            objectiveText = "SOMETHING CHANGED";
        }
        else if (m_CorridorLoopCount == 2)
        {
            objectiveText = "KEEP WALKING";
        }
        else
        {
            objectiveText = "DON'T LOOK BACK";
        }
    }
    else if (game->IsPowerRestored())
    {
        objectiveText = "ESCAPE";
    }
    else if (game->GetItemCount() >= 3)
    {
        objectiveText = "RESTORE POWER";
    }

    m_Hud.Draw(
        *player,
        game->GetItemCount(),
        m_InteractionSystem.GetPrompt(),
        objectiveText);
}

void StageScene::Uninit()
{
    Core::Game::GetInstance()->GetPostProcess()->SetAtmosphere(0.18f, 0.55f);
    Core::Game::GetInstance()->GetPostProcess()->SetExposure(1.0f);
    Core::Game::GetInstance()->GetPostProcess()->SetVolumetricLight(false);
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
    game->DestroyObj("FuseBox");
    game->DestroyObj("ExitTrigger");
    game->DestroyObj("BatteryItem");
    game->DestroyObj("MovieTriggerMark");
    game->DestroyObj("ScareTrigger_Corridor");
}
