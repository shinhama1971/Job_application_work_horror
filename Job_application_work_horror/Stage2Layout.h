// ============================================================================
// ファイルの役割: 2面のループ廊下に置くObject（壁・照明・端末・異変用の小物）を作って配置している。
// 主な技術: 配置のデータを進行から分ける設計、作るときにポインタを受け取る仕組み、名前の一覧の一元管理
// ゲームの進行は扱わず、「何をどこに置くか」だけを担当している。進行はStage2Sceneが管理している。
// ============================================================================

#pragma once

#include "Stage2SceneConstants.h"
#include "PropLayoutFile.h"

#include <SimpleMath.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Core
{
    class Game;
}
class Player;
class ExitTrigger;
class Door;
class ShadowMan;
class FuseBox;
class Wall;
class BatteryItem;
class CeilingLight;
class Locker;
class MirrorSelf;
class Ground;
class HangingBulb;

// 2面の廊下の照明。演出の表では、名前の文字列ではなくこの値で照明を指定している。
enum class Stage2Light
{
    Light1,         // 入口側（"Stage2Light1"）
    Light2,         // 中央（"Stage2Light2"）
    Light3,         // 奥（"Stage2Light3"）
    DoorLight,      // 出口扉の上（"CeilingLight4"）
    // 照明の数
    Count
};

// 廊下の奥行き（z）に一番近い天井照明を返している。照明はz=-112, -38, 38, 112に並んでいる。
inline Stage2Light Stage2NearestLight(float z)
{
    if (z < -75.0f) return Stage2Light::Light1;
    if (z < 0.0f) return Stage2Light::Light2;
    if (z < 75.0f) return Stage2Light::Light3;
    return Stage2Light::DoorLight;
}

// 2面の進行で使うObject。Stage2Layout::Buildが作ると同時にポインタを入れて返すため、
// 名前での検索や、名前の打ち間違いは起きない。
// 実体はObjectManagerが所有し、どれもSceneが終わるまで破棄されないため、所有しないポインタで持っている。
struct Stage2Objects
{
    // プレイヤー、出口、奥の扉
    Player* player = nullptr;
    ExitTrigger* exit = nullptr;
    Door* door = nullptr;
    // 謎解きと最後の追跡で使う影、足音を聞きつけて追ってくる影
    ShadowMan* shadow = nullptr;
    ShadowMan* noiseShadow = nullptr;
    ShadowMan* presence = nullptr;      // 背後の気配
    // 左の壁の鏡（どの周回にもある。鏡の異変の人影は、物として置かず鏡のシェーダーが描く）
    Wall* mirror = nullptr;
    // 鏡にだけ映る自分の姿
    MirrorSelf* mirrorSelf = nullptr;
    // 濡れた床（水たまり・濡れた足跡）
    Ground* ground = nullptr;
    // 天井から吊るした、揺れる裸電球（P.T. の廊下の電球）
    std::array<HangingBulb*, 2> hangingBulbs{};
    // 隠れられるロッカー（左の壁と右の壁に1つずつ）
    std::array<Locker*, 2> lockers{};
    // 異常確認のスイッチ、非常用充電器、棚の上の電池
    FuseBox* confirmationPanel = nullptr;
    FuseBox* emergencyCharger = nullptr;
    BatteryItem* battery = nullptr;
    // 扉のランプ、肖像画、周回の印、時計の文字盤（針も文字盤のシェーダーが描く）
    Wall* doorIndicator = nullptr;
    Wall* portrait = nullptr;
    Wall* loopMark = nullptr;
    Wall* clockFace = nullptr;
    // 偽の扉（板・手前の枠・奥の枠・上の枠・取っ手）
    Wall* falseDoorPanel = nullptr;
    Wall* falseDoorFrameNear = nullptr;
    Wall* falseDoorFrameFar = nullptr;
    Wall* falseDoorFrameTop = nullptr;
    Wall* falseDoorHandle = nullptr;
    // 照明、残された記録の端末とその目印、周回を数える傷の印
    std::array<CeilingLight*, static_cast<std::size_t>(Stage2Light::Count)> lights{};
    std::array<FuseBox*, 2> evidenceTerminals{};
    std::array<Wall*, 2> evidenceMarkers{};
    std::array<Wall*, 3> cycleMarks{};
    // 信号盤とその色の目印、壁の引っかき傷
    std::array<FuseBox*, 3> signalTerminals{};
    std::array<Wall*, 3> signalMarkers{};
    std::array<Wall*, Stage2ScratchCount> scratches{};
    // 部屋の角の暗がりの計算に使う、廊下の壁を上から見た長方形（中心x・z、半分の幅・奥行き）
    std::vector<DirectX::SimpleMath::Vector4> wallFootprints;
    // 配置ファイルから作った建物（名前とWall）と、place 行で置いた物の置き直し方（配置ファイルを読み直したときに使う）
    PropLayoutFile::SpawnedProps layoutProps;
    PropLayoutFile::PlacementBindings placementBindings;

    // 作ったすべてのObjectの名前。Sceneの終了時にこの一覧で破棄している。
    // 名前での破棄は、すでに破棄されたObjectに対しても安全。将来、途中で破棄されるObjectを
    // 配置に加えても壊れないよう、ポインタではなく名前で持っている。
    std::vector<std::string> objectNames;

    // 照明を、名前ではなく列挙値で取得している
    CeilingLight* Light(Stage2Light light) const
    {
        return lights[static_cast<std::size_t>(light)];
    }
};

namespace Stage2Layout
{
    // 2面の建物（廊下の壁・天井・縁取り・配管）の配置のファイル（実行ファイルの作業フォルダから見た場所）
    inline constexpr const char* PropLayoutPath = "assets/layout/stage2_props.txt";

    // 2面の廊下を組み立てている。時計の針は、最初の周回の時刻に合わせて置いている。
    Stage2Objects Build(Core::Game& game, float clockHourAngle, float clockMinuteAngle);
}
