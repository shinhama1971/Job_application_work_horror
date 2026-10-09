// ============================================================================
// ファイルの役割: タイトル画面の入力と表示、ゲーム開始への切り替えを管理している。
// 主な技術: Sceneの継承、2DのUI、入力によるシーンの切り替え
// ============================================================================

#pragma once
#include "Scene.h"
#include "Object.h"
#include "Hud.h"

// TitleSceneクラス：題名と操作の説明を表示し、Enterで1面を始め、Qでゲームを終えている
class TitleScene : public Scene
{
private:
	std::vector<Object*> m_MySceneObjects; // このシーンのObject（今は使っていない）
    // タイトル画面を描くHUD、表示してからの経過秒（文字の明滅に使っている）
    Hud m_Hud;
    float m_TitleTime = 0.0f;

	void Init(); // HUDを準備している
	void Uninit(); // HUDを片付けている

public:
	TitleScene(); // コンストラクタ（Initを呼んでいる）
	~TitleScene(); // デストラクタ（Uninitを呼んでいる）

	void Update(); // 入力を受け取り、ゲームの開始・終了をしている
    // タイトル画面を描いている
    void Draw(Camera* camera) override;
};

