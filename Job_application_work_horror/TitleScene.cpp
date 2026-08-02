#include "TitleScene.h"
#include "Game.h"
#include "Input.h"
#include "Texture2D.h"
// コンストラクタ
TitleScene::TitleScene()
{
	Init();
}

// デストラクタ
TitleScene::~TitleScene()
{
	Uninit();
}

// 初期化
void TitleScene::Init()
{
	Texture2D* pt = Core::Game::GetInstance()->AddObject<Texture2D>();
	pt->SetTexture("assets/texture/background1.png");//画像を指定
	pt->SetPosition(0.0f, 0.0f, 1.0f);//位置を指定
	pt->SetRotation(0.0f, 0.0f, 0.0f);//角度を指定
	pt->SetScale(1280.0f, 720.0f, 0.0f);//大きさを指定
	m_MySceneObjects.emplace_back(pt);

	Texture2D* pt1 = Core::Game::GetInstance()->AddObject<Texture2D>();
	pt1->SetTexture("assets/texture/titlerogo.png");//画像を指定
	pt1->SetPosition(0.0f, 0.0f, 0.0f);//位置を指定
	pt1->SetRotation(0.0f, 0.0f, 0.0f);//角度を指定
	pt1->SetScale(580.0f, 360.0f, 0.0f);//大きさを指定
	m_MySceneObjects.emplace_back(pt1);
}

// 更新
void TitleScene::Update()
{
	// エンターキーを押してステージ1へ
	if (Input::GetKeyTrigger(VK_RETURN))
	{
		Core::Game::GetInstance()->RequestSceneChange(STAGE);
	}
}

// 終了処理
void TitleScene::Uninit()
{
	// このシーンのオブジェクトを削除する
	for (auto& o : m_MySceneObjects) {
		Core::Game::GetInstance()->DeleteObject(o);
	}
	m_MySceneObjects.clear();
}
