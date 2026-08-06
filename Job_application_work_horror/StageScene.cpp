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
#include <SimpleMath.h>

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

    // アイテム
    Item* item1 = game->CreateObj<Item>("Item1");
    item1->SetPosition(0.0f, -95.0f, -155.0f);

    Item* item2 = game->CreateObj<Item>("Item2");
    item2->SetPosition(-150.0f, -95.0f, -140.0f);

    Item* item3 = game->CreateObj<Item>("Item3");
    item3->SetPosition(150.0f, -95.0f, -140.0f);

    // UI
    Texture2D* ui = game->CreateObj<Texture2D>("UI_Back");
    ui->SetTexture("assets/texture/ui_back.png");
    ui->SetPosition(-475.0f, -300.0f, 0.0f);
    ui->SetScale(250.0f, 120.0f, 0.0f);

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

    m_InteractionSystem.Update(*player);
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
    if (game->IsPowerRestored())
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

    game->DestroyObj("CeilingLight1");
    game->DestroyObj("CeilingLight2");
    game->DestroyObj("CeilingLight3");
    game->DestroyObj("CeilingLight4");
    game->DestroyObj("CeilingLight5");
    game->DestroyObj("CeilingLight6");
    game->DestroyObj("CeilingLight7");

    game->DestroyObj("Item1");
    game->DestroyObj("Item2");
    game->DestroyObj("Item3");

    game->DestroyObj("UI_Back");

    game->DestroyObj("Door");
    game->DestroyObj("FuseBox");
    game->DestroyObj("ExitTrigger");
    game->DestroyObj("BatteryItem");
    game->DestroyObj("MovieTriggerMark");
    game->DestroyObj("ScareTrigger_Corridor");
}
