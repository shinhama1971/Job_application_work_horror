// ============================================================================
// ファイルの役割: タイトル画面の入力と表示、ゲーム開始への切り替えを管理している。
// 主な技術: Sceneの継承、2DのUI、入力によるシーンの切り替え
// ============================================================================

#pragma once
#include "Scene.h"
#include "Object.h"
#include "Hud.h"

// TitleSceneクラス：題名と操作の説明を表示し、始める面（1面・2面・外の森（試作））を選んでEnterで始め、Qでゲームを終えている
class TitleScene : public Scene
{
private:
	std::vector<Object*> m_MySceneObjects; // このシーンのObject（今は使っていない）
    // タイトル画面を描くHUD、表示してからの経過秒（文字の明滅に使っている）
    Hud m_Hud;
    float m_TitleTime = 0.0f;
    // 選んでいる面（0 = 1面、1 = 2面、2 = 外の森（試作））。前に選んだ面を、次にタイトルへ戻ったときも選んだままにしている
    static int s_SelectedStage;
    // 左スティックを倒したままのとき、選ぶ面が続けて動かないよう、一度戻すまで待っている
    bool m_StickReleased = true;

	void Init(); // HUDを準備している
	void Uninit(); // HUDを片付けている

public:
	TitleScene(); // コンストラクタ（Initを呼んでいる）
	~TitleScene(); // デストラクタ（Uninitを呼んでいる）

	void Update(); // 入力を受け取り、ゲームの開始・終了をしている
    // タイトル画面を描いている
    void Draw(Camera* camera) override;
};

