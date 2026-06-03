#pragma once
#include <iostream>
#include "Camera.h"
#include"Renderer.h"
#include"TitleScene.h"
#include"StageScene.h"
#include"ResultScene.h"
#include"sound.h"

enum SceneName
{
	TITLE,
	STAGE,
	RESULT
};

class Game
{
private:
	static Game* m_Instance;//ゲームインスタンス
	Scene* m_Scene;//シーン
	// カメラ
	Camera  m_Camera;
	//オブジェクト配列
	std::vector <std::unique_ptr<Object>>m_Objects;

public:
	Game(); // コンストラクタ
	~Game(); // デストラクタ

static void Init(); // 初期化
static void Update(); // 更新
static void Draw(); // 描画
static void Uninit(); // 終了処理

static Game* GetInstance();

void ChangeScene(SceneName sName);
void DeleteObject(Object* pt);
void DeleteAllObject();

template<typename T>T* AddObject()
{
	T* pt = new T;
	m_Instance->m_Objects.emplace_back(pt);
	pt->Init();
	return pt;
}

template<typename T>std::vector<T*>GetObjects()
{
	std::vector<T*>res;
	for (auto& o : m_Instance->m_Objects)
	{
		if (T* derivedObj = dynamic_cast<T*>(o.get()))
		{
			res.emplace_back(derivedObj);
		}
	}
	return res;
}

public:
	Camera* GetCamera() { return &m_Camera; }
};
