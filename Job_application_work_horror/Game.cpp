#include "Game.h"
#include "Renderer.h"
#include "Input.h"

Game* Game::m_Instance;
// コンストラクタ
Game::Game()
{
	m_Scene = nullptr;
}

// デストラクタ
Game::~Game()
{
	delete m_Scene;
	DeleteAllObject();
}

// 初期化
void Game::Init()
{
	m_Instance = new Game;
	// 描画終了処理
	Renderer::Init();
	//入力初期化
	Input::Create();
	// カメラ初期化
	m_Instance->m_Camera.Init();

	m_Instance->m_Scene = new StageScene;
	
}

// 更新
void Game::Update()
{
	//入力処理更新
	Input::Update();

	m_Instance->m_Scene->Update();
	// カメラ更新
	m_Instance->m_Camera.Update();
	// オブジェクト更新
	for (auto& o : m_Instance->m_Objects)
	{
		if (!o->IsDestroy()) 
		{
			o->Update();
		}
	}
	
	// 死活フラグが立っているオブジェクトを一括削除
	std::erase_if(m_Instance->m_Objects, [](const std::unique_ptr<Object>& o) {
		if (o->IsDestroy()) {
			o->Uninit();
			return true;
		}
		return false;
	});
}

// 描画
void Game::Draw()
{
	// 描画前処理
	Renderer::DrawStart();

	// テストオブジェクト描画
	for (auto& o : m_Instance->m_Objects)
	{
		o->Draw(&m_Instance->m_Camera);
	}

	// 描画後処理
	Renderer::DrawEnd();
}

// 終了処理
void Game::Uninit()
{
	// カメラ終了処理
	m_Instance->m_Camera.Uninit();
	// テストオブジェクト終了処理
	for (auto& o : m_Instance->m_Objects)
	{
		o->Uninit();
	}
	//入力処理終了
	Input::Release();
	// 描画終了処理
	Renderer::Uninit();
	delete m_Instance;
}

Game* Game::GetInstance()
{
	return m_Instance;
}

void Game::ChangeScene(SceneName sName)
{
	//読み込み済みのシーンがあれば削除
	int score = 0;
	if (m_Instance->m_Scene != nullptr)
	{
		/*if (Stage1Scene* sObj = dynamic_cast<Stage1Scene*>(m_Instance->m_Scene))
		{
			score = sObj->GetScore();
		}*/
		delete m_Instance->m_Scene;
		m_Instance->m_Scene = nullptr;
	}

	switch (sName)
	{
	case TITLE:
		m_Instance->m_Scene = new TitleScene;
		break;
	case STAGE:
		m_Instance->m_Scene = new StageScene;
		break;
	case RESULT:
		m_Instance->m_Scene = new ResultScene;
		dynamic_cast<ResultScene*>(m_Instance->m_Scene)->SetScore(score);
		break;
	}
}

void Game::DeleteObject(Object* pt)
{
	if (pt == NULL) return;
	pt->Destroy();
}

void Game::DeleteAllObject()
{
	for (auto & o : m_Instance->m_Objects)
	{
		o->Uninit();
	}
	m_Instance->m_Objects.clear();
	m_Instance->m_Objects.shrink_to_fit();
}

