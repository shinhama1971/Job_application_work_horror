#pragma once
#include "Scene.h"
#include "Object.h"
#include"sound.h"
#include "Hud.h"
// ResultSceneクラス
class ResultScene : public Scene
{
private:
	std::vector<Object*> m_MySceneObjects; // このシーンのオブジェクト

	void Init(); // 初期化
	void Uninit(); // 終了処理
	Sound m_Sound;//サウンド
	Hud m_Hud;
	float m_ResultTimer = 0.0f;

public:
	ResultScene(); // コンストラクタ
	~ResultScene(); // デストラクタ

	void Update(); // 更新
	void Draw(Camera* camera) override;
	void SetScore(int c); // スコア設定
	static int s_Score;      // スコア保存用
	static int s_NextScene;  // 次のシーンID保存用 (SCENE_ID を int で扱います)
};

