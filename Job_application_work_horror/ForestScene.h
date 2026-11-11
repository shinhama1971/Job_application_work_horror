// ============================================================================
// ファイルの役割: 外の森の場面（1面の前）。南の小道から、雨の森を歩いて、森の奥に建つ2階建ての研究所に入る。
// 主な技術: 配置ファイル（assets/layout/forest_props.txt）から木・岩・研究所・灯りを作る、Debug版の読み直し
// タイトルで「1面」を選んだとき、ゲームを --forest で起動したとき、デバッグ画面（F1）のボタンから開く。
// 歩いている間に研究棟からの無線（ForestRadio）が届き、研究所の玄関（LabEntrance）を調べると、目的の画面（IntroScene）を挟んで1面が始まる。
// 木や研究所の置き場所は、Unity のマップエディター（Map Layout → 森を読み込む）で動かして書き出せる。
// 森の世界の作り方（BuildWorld）は、タイトル画面の後ろに映す森でも使っている。
// ============================================================================

#pragma once

#include "Scene.h"
#include "Hud.h"
#include "ForestEvents.h"
#include "ForestRadio.h"
#include "InteractionSystem.h"
#include "PropLayoutFile.h"

#include <string>
#include <vector>

class Player;
class ForestGrass;
class Ground;
class SceneLayoutBuilder;
class LabEntrance;
class ForestLightning;

class ForestScene final : public Scene
{
public:
    // 森の配置ファイル
    static constexpr const char* PropLayoutPath = "assets/layout/forest_props.txt";
    // 森に雨を降らせるか（雨粒・地面のしぶき・水たまりの雨の輪・雨音）。false にすると、雨のない夜の森になる。
    // 雨の仕組み（ForestRain・Ground::AddRainRipples・雨音）はそのまま残しているので、true に戻せばまた降る
    static constexpr bool RainEnabled = true;
    // 雨の強さ（0〜1。1 は土砂降り）。BuildWorld が ForestRain（雨粒としぶきの数・落ちる速さ）、Ground（水たまりの雨の輪の数）に、
    // Game::ChangeScene が雨音の大きさに、ForestRain が岩・樹皮・外壁の濡れ具合（Renderer::SetRainWetness）に使っている。
    // 0.35 は、ライトの中に雨の筋がまばらに見え、足音や物音が雨音に埋もれないくらいの、少し弱い雨
    static constexpr float RainStrength = 0.35f;
    // ゲームを --forest で起動したか（タイトルの代わりに、森から始める）
    static bool IsRequestedByCommandLine();

    // 森の世界（空・プレイヤー・地面・木や研究所などの小物・草）。森の場面と、タイトル画面の後ろの森（TitleScene）で共通に使っている
    struct World
    {
        Player* PlayerObject = nullptr;
        Ground* GroundObject = nullptr;
        ForestGrass* Grass = nullptr;
        PropLayoutFile::SpawnedProps Props;
    };
    // 森の配置ファイルを読んでいる（読めなければ、理由を出して終了している）
    static PropLayout LoadLayout();
    // 配置から、森の世界を作っている
    static World BuildWorld(SceneLayoutBuilder& builder, const PropLayout& layout);
    // 夜の森の画面効果（暗さ・霧・光の筋）にしている。RestoreAtmosphere で普段の値に戻す
    static void ApplyAtmosphere();
    static void RestoreAtmosphere();

    ForestScene();
    ~ForestScene() override;

    void Update() override;
    // 目的・調べる操作・無線の字幕と、玄関から入るときの暗転を描いている
    void Draw(Camera* camera) override;

private:
    // 配置のファイルを読み直し、木・岩・研究所などと、地面・開始位置以外の置き場所を置き直している（Debug版の F5 と、ファイルが書き換わったとき）
    void ReloadPropLayout();
    void UpdateLightning(float deltaTime, const DirectX::SimpleMath::Vector3& forward);

    // 作ったObjectの名前（終わるときに、この一覧で破棄する）、配置ファイルから作った物、置き場所の置き直し方、ファイルの見張り
    std::vector<std::string> m_ObjectNames;
    PropLayoutFile::SpawnedProps m_LayoutProps;
    PropLayoutFile::PlacementBindings m_PlacementBindings;
    PropLayoutFile::FileChangeWatcher m_LayoutWatcher;
    Player* m_Player = nullptr;
    ForestGrass* m_Grass = nullptr;
    Hud m_Hud;
    // 小道の街灯と研究所の灯りの演出と、前のフレームの恐怖の画面効果の強さ（強まった瞬間に心拍の脈を出すため）
    ForestEvents m_Events;
    float m_LastFear = 0.0f;
    // 研究棟からの無線の字幕、調べる操作、研究所の玄関、玄関から入ってからの秒数（-1 ならまだ入っていない）と暗転の秒数
    ForestRadio m_Radio;
    InteractionSystem m_InteractionSystem;
    LabEntrance* m_Entrance = nullptr;
    ForestLightning* m_Lightning = nullptr;
    float m_LightningTime = -1.0f;
    float m_EnterTimer = -1.0f;
    bool m_SceneChangeRequested = false;
    static constexpr float EnterFadeSeconds = 1.4f;
    // 画面上部に出す知らせと、出す残り秒数
    std::string m_Notice;
    float m_NoticeTimer = 0.0f;
};
