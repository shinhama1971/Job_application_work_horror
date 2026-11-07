// ============================================================================
// ファイルの役割: 外の森（試作）の場面。南の小道から、森の奥に建つ2階建ての研究所まで歩ける。
// 主な技術: 配置ファイル（assets/layout/forest_props.txt）から木・岩・研究所・灯りを作る、Debug版の読み直し
// タイトルで「外の森（試作）」を選んだとき、ゲームを --forest で起動したとき、デバッグ画面（F1）のボタンから開く。1面にはまだつながらない。
// 木や研究所の置き場所は、Unity のマップエディター（Map Layout → 森を読み込む）で動かして書き出せる。
// 森の世界の作り方（BuildWorld）は、タイトル画面の後ろに映す森でも使っている。
// ============================================================================

#pragma once

#include "Scene.h"
#include "Hud.h"
#include "ForestEvents.h"
#include "PropLayoutFile.h"

#include <string>
#include <vector>

class Player;
class ForestGrass;
class Ground;
class SceneLayoutBuilder;

class ForestScene final : public Scene
{
public:
    // 森の配置ファイル
    static constexpr const char* PropLayoutPath = "assets/layout/forest_props.txt";
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
    // 画面上部に、場面の名前と、配置を読み直したときの知らせを出している
    void Draw(Camera* camera) override;

private:
    // 配置のファイルを読み直し、木・岩・研究所などと、地面・開始位置以外の置き場所を置き直している（Debug版の F5 と、ファイルが書き換わったとき）
    void ReloadPropLayout();

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
    // 画面上部に出す知らせと、出す残り秒数
    std::string m_Notice;
    float m_NoticeTimer = 0.0f;
};
