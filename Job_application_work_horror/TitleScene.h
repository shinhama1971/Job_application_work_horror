// ============================================================================
// ファイルの役割: タイトル画面の入力と表示、ゲーム開始への切り替えを管理している。
// 主な技術: Sceneの継承、2DのUI、入力によるシーンの切り替え、後ろに映す3Dの森（ForestScene::BuildWorld）と、ゆっくり動くカメラの場面の切り替え
//
// ・タイトルの後ろでは、ゲームと同じ描画で外の森を映している（霧・草・水たまりの反射・懐中電灯・街灯のちらつき）。
//   プレイヤーは入力を読まない状態（Player::SetAttractMode）にし、位置と視点の向きをこのシーンから動かしている。
// ・いくつかの場面（小道の入口・街灯と水たまり・草むらを何かが通る・研究所の窓）を順に、ゆっくり進むカメラで映し、
//   場面が変わるたびに、電波が途切れたようなノイズ（画面の乱れと、題名の文字のずれ）を入れている。
// ・場面ごとに森の演出（ForestEvents）を作り直すので、街灯が消える・窓の明かりが消える・草むらを何かが通る、が毎回また起きる。
// ============================================================================

#pragma once
#include "Scene.h"
#include "Hud.h"
#include "ForestEvents.h"
#include "PropLayoutFile.h"

#include <SimpleMath.h>

#include <string>
#include <vector>

class Player;
class ForestGrass;

// TitleSceneクラス：題名と操作の説明を表示し、始める面（1面・2面・外の森（試作））を選んでEnterで始め、Qでゲームを終えている
class TitleScene : public Scene
{
private:
    // タイトル画面を描くHUD、表示してからの経過秒（文字の明滅とノイズに使っている）
    Hud m_Hud;
    float m_TitleTime = 0.0f;
    // 選んでいる面（0 = 1面、1 = 2面、2 = 外の森（試作））。前に選んだ面を、次にタイトルへ戻ったときも選んだままにしている
    static int s_SelectedStage;
    // 左スティックを倒したままのとき、選ぶ面が続けて動かないよう、一度戻すまで待っている
    bool m_StickReleased = true;

    // 後ろに映す森：作ったObjectの名前（終わるときに破棄する）、プレイヤー（カメラと懐中電灯）、配置ファイルから作った小物、森の演出
    std::vector<std::string> m_ObjectNames;
    Player* m_Player = nullptr;
    ForestGrass* m_Grass = nullptr;
    PropLayoutFile::SpawnedProps m_Props;
    ForestEvents m_Events;
    // 今映している場面の番号と、その場面に入ってからの秒数。場面が変わった瞬間のノイズの強さ（1から0へ弱める）
    int m_ShotIndex = 0;
    float m_ShotTime = 0.0f;
    float m_CutNoise = 1.0f;

    void Init(); // HUDと後ろの森を準備している
    void Uninit(); // HUDを片付けている
    // 今の場面の、カメラの位置と向きを決めている（場面の終わりで次の場面へ切り替える）
    void UpdateCamera(float deltaTime);
    // 題名の文字と画面に掛けるノイズの強さ（0〜1）。場面の切り替えと、ときどき来る短い乱れから決めている
    float GetNoise() const;

public:
    TitleScene(); // コンストラクタ（Initを呼んでいる）
    ~TitleScene(); // デストラクタ（Uninitを呼んでいる）

    void Update(); // 入力を受け取り、ゲームの開始・終了をしている
    // タイトル画面を描いている
    void Draw(Camera* camera) override;
};
