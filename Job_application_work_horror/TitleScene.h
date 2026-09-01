// ============================================================================
// ファイルの役割: タイトル画面の入力、表示、ゲーム開始への遷移を管理します。
// ============================================================================

#pragma once
#include "Scene.h"
#include "Object.h"
#include "Hud.h"

// TitleSceneクラス
class TitleScene : public Scene
{
private:
	std::vector<Object*> m_MySceneObjects; // このシーンのオブジェクト
    Hud m_Hud;
    float m_TitleTime = 0.0f;

	void Init(); // 初期化
	void Uninit(); // 終了処理

public:
	TitleScene(); // コンストラクタ
	~TitleScene(); // デストラクタ

	void Update(); // 更新
    void Draw(Camera* camera) override;
};

