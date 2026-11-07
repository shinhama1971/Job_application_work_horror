// ============================================================================
// ファイルの役割: 1面の施設（壁・照明・ヒューズ・端末・扉・演出用の人影）を作って配置している。
// 主な技術: 配置のデータを進行から分ける設計、作るときにポインタを受け取る仕組み、名前の一覧の一元管理
// ゲームの進行は扱わず、「何をどこに置くか」だけを担当している。進行はStageSceneが管理している。
// ============================================================================

#pragma once

#include "Stage1HiddenRoomEvent.h"
#include "Stage1KeypadDoor.h"
#include "PropLayoutFile.h"
#include "Stage1SafeLights.h"
#include "Stage1WallWritings.h"
#include "Stage1WestWing.h"
#include "StageSurveillanceCameras.h"

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
class ShadowMan;
class FuseBox;
class Wall;
class Door;
class ExitTrigger;
class Item;
class CeilingLight;
class Ground;

// 進む先を示す天井照明の候補の組（進み具合ごと）。その組の1灯だけが、切れかけの電球のように不規則にちかちかする。
enum class Stage1RouteLight : std::size_t
{
    StartSearch, HallEntrance, CorridorDoor, LeftStorage, RightStorage,
    WestWing, MainPower, PumpRoom, ExitPower, ExitDoor, Count
};

// ループ廊下の突き当たりのドア（1面の最後のドア）の位置。廊下（z=237〜273）をふさぐ向きに置いている。
// 電力が戻る前に開けて通ると開始地点へ戻って次の周回が始まり、出口の送電が終わった後に開けて通ると2面へ進む。
constexpr float Stage1EndDoorX = 196.0f;
constexpr float Stage1EndDoorZ = 255.0f;
constexpr float Stage1EndDoorHalfWidth = 18.0f;
// 開始地点の奥の壁（南の外壁）のドア。最後のドアと同じ形で、裏側（壁の外）には最後の廊下の突き当たりと同じ幅の通路を作っている。
// 電力が戻る前に最後のドアを開けると、その瞬間に、このドアの裏側の同じ位置・同じ向きへ移し、このドアを開け始めている。
// 開いたドアの向こうに開始地点が見えるので、P.T.のように「ドアの先が最初の場所にそのまま続いている」ように見える。
constexpr float Stage1StartDoorX = 0.0f;
constexpr float Stage1StartDoorZ = -180.0f;

// 1面の進行で使うObject。Stage1Layout::Buildが作ると同時にポインタを入れて返すため、
// 名前での検索や、名前の打ち間違いは起きない。
// 実体はObjectManagerが所有し、どれもSceneが終わるまで破棄されないため、所有しないポインタで持っている。
struct StageObjects
{
    // 天井照明の数
    static constexpr int CeilingLightCount = 8;

    // プレイヤー
    Player* player = nullptr;
    // 濡れた床（水たまり・西棟の浸水・濡れた足跡）
    Ground* ground = nullptr;
    // 建物の壁を上から見た長方形（x,y = 中心のx・z、z,w = 幅と奥行きの半分）。部屋の角の暗がりの計算に使っている。
    std::vector<DirectX::SimpleMath::Vector4> wallFootprints;
    // 演出用の人影（ヒューズを拾った後に廊下に立つ影・左の倉庫の影・監視カメラの確認で現れる影・出口の前兆の影）
    ShadowMan* fuseWatcher = nullptr;
    ShadowMan* storageShadow = nullptr;
    ShadowMan* evidenceShadow = nullptr;
    ShadowMan* exitOmen = nullptr;
    // 書類保管室の「照らすと止まる影」（位置はStage1LightStalkerが決めている）
    ShadowMan* archiveStalker = nullptr;
    // スイッチ類（非常用充電器・監視カメラの端末・出口の送電盤）
    FuseBox* emergencyCharger = nullptr;
    FuseBox* evidenceTerminal = nullptr;
    FuseBox* exitPowerPanel = nullptr;
    // 光る目印（監視端末の目印・出口の表示・扉のランプ）と、ループ廊下で出たり消えたりする目印
    Wall* evidenceMarker = nullptr;
    Wall* exitSign = nullptr;
    // 開始地点の奥のドアの裏側にある、最後のドアの上と同じ赤い表示（移った瞬間に同じ景色に見せるため）
    Wall* startDoorSign = nullptr;
    Wall* doorIndicator = nullptr;
    std::array<Wall*, 3> loopMarkers{};
    // 進み具合ごとの、進む先の天井照明の候補（手前から奥の順）
    std::array<std::vector<CeilingLight*>, static_cast<std::size_t>(Stage1RouteLight::Count)> routeLights{};
    // 光の安全地帯（停電しても消えない、暖かい色の電球の下）の場所と広さ
    std::vector<Stage1SafeLights::Zone> safeZones;
    // 小物のファイル（assets/layout/stage1_props.txt）から作った小物（名前とWall）。Debug版で F5 を押すと、ファイルを読み直して置き直している
    PropLayoutFile::SpawnedProps layoutProps;
    // place 行で置いた物の置き直し方（配置ファイルを読み直したときに使う）
    PropLayoutFile::PlacementBindings placementBindings;
    // ループ廊下へ続く中央の扉、ループ廊下の突き当たりの最後のドア、開始地点の奥のドア、出口（通ると2面へ）
    Door* loopDoor = nullptr;
    Door* exitDoor = nullptr;
    Door* startDoor = nullptr;
    ExitTrigger* exitTrigger = nullptr;
    // ヒューズ。部屋の中の位置はプレイごとに変わるため、案内の矢印はここから位置を読んでいる。
    Item* firstFuse = nullptr;
    Item* secondFuse = nullptr;
    Item* thirdFuse = nullptr;
    // 3本目のもう1つの手に入れ方：暗証番号の扉の先（閉じ込められる部屋）の奥にあるヒューズ。
    // 3本目を探す段階になると現れ、西棟のヒューズとどちらか一方を取ればよい（取った方ではない方は消える。StageScene::UpdateThirdFuseRoutes）
    Item* keypadRoomFuse = nullptr;
    // 天井照明と、監視カメラの異常で使う開かずの扉
    std::array<CeilingLight*, CeilingLightCount> ceilingLights{};
    std::array<Door*, StageSealedDoorCount> sealedDoors{};
    // 懐中電灯で照らすと浮かぶ壁の文字（並びはStage1WallWritings::Index）
    Stage1WallWritings::Writings writings{};
    // 突き当たりのドアを通って開始地点へ戻るたびに、開始地点の部屋に1つずつ浮かぶ文字（1周目・2周目・3周目）
    std::array<FlashlightWriting*, 3> lapWritings{};
    // 暗証番号の扉・入力盤・番号の手がかり（任意で探索）
    Stage1KeypadDoor::Parts keypad;
    // 暗証番号の扉の先の部屋に閉じ込められるイベント（鍵・記録端末・影。扉はkeypad.doorと同じ）
    Stage1HiddenRoomEvent::Parts hiddenRoom;
    // 西棟（浸水した機械室）の扉・鍵・3本目のヒューズ。西棟に入らないと先へ進めない。
    Stage1WestWing::Parts westWing;

    // 作ったすべてのObjectの名前。Sceneの終了時にこの一覧で破棄している。
    // 名前での破棄は、すでに破棄されたObjectに対しても安全。将来、途中で破棄されるObjectを
    // 配置に加えても壊れないよう、ポインタではなく名前で持っている。
    std::vector<std::string> objectNames;

    // 配置名"CeilingLight1"〜"CeilingLight8"と同じ、1から始まる番号で照明を取得している。
    CeilingLight* CeilingLightAt(int number) const
    {
        return ceilingLights[static_cast<std::size_t>(number - 1)];
    }
};

namespace Stage1Layout
{
    // 1面の小物の配置のファイル（実行ファイルの作業フォルダから見た場所）
    inline constexpr const char* PropLayoutPath = "assets/layout/stage1_props.txt";

    // 1面の施設を組み立て、進行で使うObjectのポインタをまとめて返している。
    StageObjects Build(Core::Game& game);
}
