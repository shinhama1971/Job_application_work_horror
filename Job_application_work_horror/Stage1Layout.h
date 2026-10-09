// ============================================================================
// ファイルの役割: 1面の施設（壁・照明・ヒューズ・端末・扉・演出用の人影）を生成して配置します。
// 主な技術: 配置データの分離、生成時のポインタ受け渡し、名前一覧の一元管理
// ゲームの進行は扱わず、「何をどこに置くか」だけを担当します。進行はStageSceneが管理します。
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

// 1面の進行で使うObjectです。Stage1Layout::Buildが生成と同時にポインタを入れて返すため、
// 名前での検索や打ち間違いは起きません。
// 実体はObjectManagerが所有し、どれもSceneの終了まで破棄されないため非所有ポインタで保持します。
struct StageObjects
{
    static constexpr int CeilingLightCount = 8;

    Player* player = nullptr;
    // 建物の壁を上から見た長方形（x,y = 中心のx・z、z,w = 幅と奥行きの半分）。部屋の角の暗がりの計算に使います。
    std::vector<DirectX::SimpleMath::Vector4> wallFootprints;
    ShadowMan* fuseWatcher = nullptr;
    ShadowMan* storageShadow = nullptr;
    ShadowMan* evidenceShadow = nullptr;
    ShadowMan* exitOmen = nullptr;
    FuseBox* emergencyCharger = nullptr;
    FuseBox* evidenceTerminal = nullptr;
    FuseBox* exitPowerPanel = nullptr;
    Wall* evidenceMarker = nullptr;
    Wall* exitSign = nullptr;
    Wall* doorIndicator = nullptr;
    std::array<Wall*, 3> loopMarkers{};
    Door* loopDoor = nullptr;
    Door* exitDoor = nullptr;
    ExitTrigger* exitTrigger = nullptr;
    // ヒューズ。部屋の中の位置はプレイごとに変わるため、案内の矢印はここから位置を読みます。
    Item* firstFuse = nullptr;
    Item* secondFuse = nullptr;
    Item* thirdFuse = nullptr;
    std::array<CeilingLight*, CeilingLightCount> ceilingLights{};
    std::array<Door*, StageSealedDoorCount> sealedDoors{};
    // 懐中電灯で照らすと浮かぶ壁の文字（並びはStage1WallWritings::Index）
    Stage1WallWritings::Writings writings{};
    // 暗証番号の扉・入力盤・番号の手がかり（任意探索）
    Stage1KeypadDoor::Parts keypad;
    // 暗証番号の扉の先の部屋の閉じ込めイベント（鍵・記録端末・影。扉はkeypad.doorと同じ）
    Stage1HiddenRoomEvent::Parts hiddenRoom;
    // 西棟（浸水した機械室）の扉・鍵・3本目のヒューズ。西棟に入らないと先へ進めません。
    Stage1WestWing::Parts westWing;

    // 生成したすべてのObjectの名前。Sceneの終了時にこの一覧で破棄します。
    // 名前での破棄は、すでに破棄されたObjectに対しても安全です。将来、途中で破棄されるObjectを
    // 配置に加えても壊れないよう、ポインタではなく名前で持ちます。
    std::vector<std::string> objectNames;

    // 配置名"CeilingLight1"〜"CeilingLight8"と同じ1始まりの番号で照明を取得します。
    CeilingLight* CeilingLightAt(int number) const
    {
        return ceilingLights[static_cast<std::size_t>(number - 1)];
    }
};

namespace Stage1Layout
{
    // 1面の施設を組み立てます。
    StageObjects Build(Core::Game& game);
}
