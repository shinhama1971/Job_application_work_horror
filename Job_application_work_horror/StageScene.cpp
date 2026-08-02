#include "StageScene.h"
#include "Game.h"
#include "Input.h"

#include "Player.h"
#include "Ground.h"
#include "Wall.h"
#include "Texture2D.h"
#include "Item.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "BatteryItem.h"
#include "MovieTrigger.h"
#include "ScreenDustOverlay.h"
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
    player->SetPosition(Vector3(0.0f, -80.0f, 0.0f));

    // 地面
    Ground* ground = game->CreateObj<Ground>("Ground");
    ground->SetPosition(0.0f, -100.0f, 0.0f);
    ground->SetScale(20.0f, 1.0f, 20.0f);

    // 壁
    Wall* wall1 = game->CreateObj<Wall>("Wall1");
    wall1->SetPosition(0.0f, -90.0f, 200.0f);

    Wall* wall2 = game->CreateObj<Wall>("Wall2");
    wall2->SetPosition(200.0f, -90.0f, 0.0f);

    // アイテム
    Item* item1 = game->CreateObj<Item>("Item1");
    item1->SetPosition(0.0f, -70.0f, -100.0f);

    Item* item2 = game->CreateObj<Item>("Item2");
    item2->SetPosition(60.0f, -95.0f, 140.0f);

    Item* item3 = game->CreateObj<Item>("Item3");
    item3->SetPosition(-60.0f, -95.0f, 200.0f);

    m_movieTrigger = std::make_unique<MovieTrigger>();
    m_movieTrigger->SetPosition(DirectX::SimpleMath::Vector3(0, 0, 30));
    m_movieTrigger->SetSize(DirectX::SimpleMath::Vector3(0.0f, -85.0f, 0.0f));
    // UI
    Texture2D* ui = game->CreateObj<Texture2D>("UI_Back");
    ui->SetTexture("assets/texture/ui_back.png");
    ui->SetPosition(-475.0f, -300.0f, 0.0f);
    ui->SetScale(250.0f, 120.0f, 0.0f);

    // ドア
    Door* door = game->CreateObj<Door>("Door");
    door->SetPosition(0.0f, -100.0f, 40.0f);

    // ゴール
    ExitTrigger* exit = game->CreateObj<ExitTrigger>("ExitTrigger");
    exit->SetPosition(150.0f, -80.0f, 300.0f);

    // バッテリー
    BatteryItem* battery = game->CreateObj<BatteryItem>("BatteryItem");
    battery->SetPosition(100.0f, -95.0f, 100.0f);

    MovieTrigger* movie =
        game->CreateObj<MovieTrigger>("MovieTrigger_01");
    ScreenDustOverlay* crt =
        game->CreateObj<ScreenDustOverlay>("CRTNoise");

    crt->SetPower(0.7f);
    crt->SetActive(false);

    movie->SetPosition(DirectX::SimpleMath::Vector3(
        0.0f,
        -99.0f,
        80.0f
    ));

    movie->SetSize(
        DirectX::SimpleMath::Vector3(10.0f, 10.0f, 10.0f)
    );

    movie->SetPosition(Vector3(0.0f, -80.0f, 20.0f));
    movie->SetSize(Vector3(20.0f, 30.0f, 20.0f));

    movie->SetDuration(2.0f);
    Texture2D* triggerMark =
        game->CreateObj<Texture2D>("MovieTriggerMark");

    triggerMark->SetTexture("assets/texture/ui_back.png");

    // センサーと同じ位置
    triggerMark->SetPosition(0.0f, -90.0f, 80.0f);

    // 大きめに表示
    triggerMark->SetScale(50.0f, 50.0f, 1.0f);
}

void StageScene::Update()
{
    if (Input::GetKeyTrigger(VK_RETURN))
    {
        Core::Game::GetInstance()->RequestSceneChange(RESULT);
        return;
    }

    Player* player =
        Core::Game::GetInstance()->GetObj<Player>("Player");

    if (player != nullptr)
    {
        // ここでプレイヤー情報を使える
    }
}

void StageScene::Uninit()
{
    Core::Game* game = Core::Game::GetInstance();

    game->DestroyObj("Player");
    game->DestroyObj("Ground");

    game->DestroyObj("Wall1");
    game->DestroyObj("Wall2");

    game->DestroyObj("Item1");
    game->DestroyObj("Item2");
    game->DestroyObj("Item3");

    game->DestroyObj("UI_Back");

    game->DestroyObj("Door");
    game->DestroyObj("ExitTrigger");
    game->DestroyObj("BatteryItem");
    game->DestroyObj("MovieTriggerMark");
}
