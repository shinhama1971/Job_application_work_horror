#include "TitleScene.h"
#include "Game.h"
#include "Input.h"
#include "Texture2D.h"
#include "Application.h"
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
    // Do not create the two golf prototype background objects below.
    // The current portfolio title is rendered through the code-based Hud.
    m_TitleTime = 0.0f;
    m_Hud.Init();
    return;

	Texture2D* pt = Core::Game::GetInstance()->AddObject<Texture2D>();
	pt->SetTexture("assets/texture/background1.png");//画像を指定
	pt->SetPosition(0.0f, 0.0f, 1.0f);//位置を指定
	pt->SetRotation(0.0f, 0.0f, 0.0f);//角度を指定
	pt->SetScale(
		static_cast<float>(Application::GetWidth()),
		static_cast<float>(Application::GetHeight()),
		0.0f);//大きさを指定
	m_MySceneObjects.emplace_back(pt);

	Texture2D* pt1 = Core::Game::GetInstance()->AddObject<Texture2D>();
	pt1->SetTexture("assets/texture/background1.png");//画像を指定
	pt1->SetPosition(0.0f, 0.0f, 0.0f);//位置を指定
	pt1->SetRotation(0.0f, 0.0f, 0.0f);//角度を指定
	const float titleScale = static_cast<float>(Application::GetHeight()) / 720.0f;
	pt1->SetScale(580.0f * titleScale, 360.0f * titleScale, 0.0f);//大きさを指定
	m_MySceneObjects.emplace_back(pt1);
}

// 更新
void TitleScene::Update()
{
    m_TitleTime += 1.0f / 60.0f;
	// エンターキーを押してステージ1へ
	if (Input::GetKeyTrigger(VK_RETURN) ||
	Input::GetButtonTrigger(XINPUT_A) ||
	Input::GetButtonTrigger(XINPUT_START))
	{
		Core::Game::GetInstance()->RequestSceneChange(SceneName::Stage);
	}
}

void TitleScene::Draw(Camera* camera)
{
    (void)camera;
    m_Hud.DrawTitle(m_TitleTime);
}

// 終了処理
void TitleScene::Uninit()
{
	// このシーンのオブジェクトを削除する
    m_Hud.Uninit();
    return;

	for (auto& o : m_MySceneObjects) {
		Core::Game::GetInstance()->DeleteObject(o);
	}
	m_MySceneObjects.clear();
}
