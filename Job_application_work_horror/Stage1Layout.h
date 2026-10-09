// ============================================================================
// ファイルの役割: 1面の施設（壁・照明・ヒューズ・端末・扉・演出用の人影）を作って配置している。
// 主な技術: 配置のデータを進行から分ける設計、作るときにポインタを受け取る仕組み、名前の一覧の一元管理
// ゲームの進行は扱わず、「何をどこに置くか」だけを担当している。進行はStageSceneが管理している。
// ============================================================================

#pragma once

#include "Stage1HiddenRoomEvent.h"
#include "Stage1KeypadDoor.h"
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

// 1面の進行で使うObject。Stage1Layout::Buildが作ると同時にポインタを入れて返すため、
// 名前での検索や、名前の打ち間違いは起きない。
// 実体はObjectManagerが所有し、どれもSceneが終わるまで破棄されないため、所有しないポインタで持っている。
struct StageObjects
{
    // 天井照明の数
    static constexpr int CeilingLightCount = 8;

    // プレイヤー
    Player* player = nullptr;
    // 建物の壁を上から見た長方形（x,y = 中心のx・z、z,w = 幅と奥行きの半分）。部屋の角の暗がりの計算に使っている。
    std::vector<DirectX::SimpleMath::Vector4> wallFootprints;
    // 演出用の人影（ヒューズを拾った後に廊下に立つ影・左の倉庫の影・監視カメラの確認で現れる影・出口の前兆の影）
    ShadowMan* fuseWatcher = nullptr;
    ShadowMan* storageShadow = nullptr;
    ShadowMan* evidenceShadow = nullptr;
    ShadowMan* exitOmen = nullptr;
    // スイッチ類（非常用充電器・監視カメラの端末・出口の送電盤）
    FuseBox* emergencyCharger = nullptr;
    FuseBox* evidenceTerminal = nullptr;
    FuseBox* exitPowerPanel = nullptr;
    // 光る目印（監視端末の目印・出口の表示・扉のランプ）と、ループ廊下で出たり消えたりする目印
    Wall* evidenceMarker = nullptr;
    Wall* exitSign = nullptr;
    Wall* doorIndicator = nullptr;
    std::array<Wall*, 3> loopMarkers{};
    // ループ廊下へ続く中央の扉、出口の扉、出口（調べると2面へ）
    Door* loopDoor = nullptr;
    Door* exitDoor = nullptr;
    ExitTrigger* exitTrigger = nullptr;
    // ヒューズ。部屋の中の位置はプレイごとに変わるため、案内の矢印はここから位置を読んでいる。
    Item* firstFuse = nullptr;
    Item* secondFuse = nullptr;
    Item* thirdFuse = nullptr;
    // 天井照明と、監視カメラの異常で使う開かずの扉
    std::array<CeilingLight*, CeilingLightCount> ceilingLights{};
    std::array<Door*, StageSealedDoorCount> sealedDoors{};
    // 懐中電灯で照らすと浮かぶ壁の文字（並びはStage1WallWritings::Index）
    Stage1WallWritings::Writings writings{};
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
    // 1面の施設を組み立て、進行で使うObjectのポインタをまとめて返している。
    StageObjects Build(Core::Game& game);
}
