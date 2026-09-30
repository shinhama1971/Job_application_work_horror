// ============================================================================
// ファイルの役割: 2面のループ廊下に置くObject（壁・照明・端末・異変用の小物）を生成して配置します。
// 主な技術: 配置データの分離、生成時のポインタ受け渡し、名前一覧の一元管理
// ゲームの進行は扱わず、「何をどこに置くか」だけを担当します。進行はStage2Sceneが管理します。
// ============================================================================

#pragma once

#include "Stage2SceneConstants.h"

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

// 2面の廊下照明。演出テーブルは名前文字列ではなくこの値で照明を指定します。
enum class Stage2Light
{
    Light1,         // 入口側（"Stage2Light1"）
    Light2,         // 中央（"Stage2Light2"）
    Light3,         // 奥（"Stage2Light3"）
    DoorLight,      // 出口扉の上（"CeilingLight4"）
    Count
};

// 廊下の奥行き（z）に一番近い天井照明です。照明はz=-112, -38, 38, 112に並んでいます。
inline Stage2Light Stage2NearestLight(float z)
{
    if (z < -75.0f) return Stage2Light::Light1;
    if (z < 0.0f) return Stage2Light::Light2;
    if (z < 75.0f) return Stage2Light::Light3;
    return Stage2Light::DoorLight;
}

// 2面の進行で使うObjectです。Stage2Layout::Buildが生成と同時にポインタを入れて返すため、
// 名前での検索や打ち間違いは起きません。
// 実体はObjectManagerが所有し、どれもSceneの終了まで破棄されないため非所有ポインタで保持します。
struct Stage2Objects
{
    Player* player = nullptr;
    ExitTrigger* exit = nullptr;
    Door* door = nullptr;
    ShadowMan* shadow = nullptr;
    ShadowMan* noiseShadow = nullptr;
    ShadowMan* presence = nullptr;      // 背後の気配
    FuseBox* confirmationPanel = nullptr;
    FuseBox* emergencyCharger = nullptr;
    BatteryItem* battery = nullptr;
    Wall* doorIndicator = nullptr;
    Wall* portrait = nullptr;
    Wall* loopMark = nullptr;
    Wall* clockFace = nullptr;
    Wall* clockHourHand = nullptr;
    Wall* clockMinuteHand = nullptr;
    Wall* falseDoorPanel = nullptr;
    Wall* falseDoorFrameNear = nullptr;
    Wall* falseDoorFrameFar = nullptr;
    Wall* falseDoorFrameTop = nullptr;
    Wall* falseDoorHandle = nullptr;
    std::array<CeilingLight*, static_cast<std::size_t>(Stage2Light::Count)> lights{};
    std::array<Wall*, 3> puddles{};
    std::array<FuseBox*, 2> evidenceTerminals{};
    std::array<Wall*, 2> evidenceMarkers{};
    std::array<Wall*, 2> portraitEyes{};
    std::array<Wall*, 3> cycleMarks{};
    std::array<FuseBox*, 3> signalTerminals{};
    std::array<Wall*, 3> signalMarkers{};
    std::array<Wall*, Stage2ScratchCount> scratches{};

    // 生成したすべてのObjectの名前。Sceneの終了時にこの一覧で破棄します。
    // 名前での破棄は、すでに破棄されたObjectに対しても安全です。将来、途中で破棄されるObjectを
    // 配置に加えても壊れないよう、ポインタではなく名前で持ちます。
    std::vector<std::string> objectNames;

    CeilingLight* Light(Stage2Light light) const
    {
        return lights[static_cast<std::size_t>(light)];
    }
};

namespace Stage2Layout
{
    // 2面の廊下を組み立てます。時計の針は、最初の周回の時刻に合わせて置きます。
    Stage2Objects Build(Core::Game& game, float clockHourAngle, float clockMinuteAngle);
}
