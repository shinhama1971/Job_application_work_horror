#include "StageScene.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "Ground.h"
#include "Wall.h"
#include "Texture2D.h"
#include "Item.h"
#include "Renderer.h"
#include "Door.h"
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
    // プレイヤー
    Player* player = Game::GetInstance()->AddObject<Player>();
    m_MySceneObjects.emplace_back(player);

    // 床
    Ground* ground = Game::GetInstance()->AddObject<Ground>();
    ground->SetPosition(0.0f, -100.0f, 0.0f);
    ground->SetScale(20.0f, 1.0f, 20.0f);
    m_MySceneObjects.emplace_back(ground);

    // 壁：仮配置
    Wall* wall1 = Game::GetInstance()->AddObject<Wall>();
    wall1->SetPosition(0.0f, -90.0f, 200.0f);
    m_MySceneObjects.emplace_back(wall1);

    Wall* wall2 = Game::GetInstance()->AddObject<Wall>();
    wall2->SetPosition(200.0f, -90.0f, 0.0f);
    m_MySceneObjects.emplace_back(wall2);

    Item* item1 = Game::GetInstance()->AddObject<Item>();
    item1->SetPosition(0.0f, -70.0f, -100.0f);
    m_MySceneObjects.emplace_back(item1);

    Item* item2 = Game::GetInstance()->AddObject<Item>();
    item2->SetPosition(60.0f, -95.0f, 140.0f);
    m_MySceneObjects.emplace_back(item2);

    Item* item3 = Game::GetInstance()->AddObject<Item>();
    item3->SetPosition(-60.0f, -95.0f, 200.0f);
    m_MySceneObjects.emplace_back(item3);

    // UI例
    Texture2D* ui = Game::GetInstance()->AddObject<Texture2D>();
    ui->SetTexture("assets/texture/ui_back.png");
    ui->SetPosition(-475.0f, -300.0f, 0.0f);
    ui->SetScale(250.0f, 120.0f, 0.0f);
    m_MySceneObjects.emplace_back(ui);

    Door* door =
        Game::GetInstance()->AddObject<Door>();

    door->SetPosition(
        0.0f,
       -100.0f,
        40.0f);
}

void StageScene::Update()
{
    // テスト用：Enterでタイトルへ戻る
    if (Input::GetKeyTrigger(VK_RETURN))
    {
        Game::GetInstance()->ChangeScene(RESULT);
    }
}

void StageScene::Uninit()
{
    for (auto& o : m_MySceneObjects)
    {
        Game::GetInstance()->DeleteObject(o);
    }

    m_MySceneObjects.clear();
}