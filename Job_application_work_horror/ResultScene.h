// ============================================================================
// ファイルの役割: クリアした後の評価を表示するリザルト画面を管理している。
// 主な技術: Sceneの継承、成績の集計、2DのUI、入力によるシーンの切り替え
// ============================================================================

#pragma once
#include "Scene.h"
#include "Object.h"
#include"sound.h"
#include "Hud.h"

#include <string>
// ResultSceneクラス：クリアタイム・捕まった回数・今回の発見などを表示し、タイトルへ戻るかもう一度遊ぶかを選ばせている
class ResultScene : public Scene
{
private:
	std::vector<Object*> m_MySceneObjects; // このシーンのObject（今は使っていない）

	void Init(); // HUDを準備し、画面効果をリザルト用の落ち着いた設定にしている
	void Uninit(); // 画面効果の設定を普段の値に戻している
	Sound m_Sound; // 音（今は使っていない。音はGameのSoundで鳴らしている）
	// リザルト画面を描くHUD、画面を開いてからの経過秒
	Hud m_Hud;
	float m_ResultTimer = 0.0f;
	// 2階で今回出た異変の名前（「時計・壁のノック」など）。始めるときに一度だけ作っている。
	std::string m_Stage2AnomalyText;

public:
	ResultScene(); // コンストラクタ（Initを呼んでいる）
	~ResultScene(); // デストラクタ（Uninitを呼んでいる）

	void Update(); // 少し待ってから、もう一度遊ぶ・タイトルへ戻る入力を受け付けている
	// 成績をHUDで描いている
	void Draw(Camera* camera) override;
	void SetScore(int c); // （宣言だけで、どこからも使っていない）
	static int s_Score;      // （宣言だけで、どこからも使っていない）
	static int s_NextScene;  // （宣言だけで、どこからも使っていない）
};

