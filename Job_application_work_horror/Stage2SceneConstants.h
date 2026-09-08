// ============================================================================
// 2面で複数の処理から参照するオブジェクト名と要素数をまとめます。
// 配置名を変更するときは、このファイルだけを修正してください。
// ============================================================================

#pragma once

inline constexpr const char* Stage2ScratchNames[] =
{
    "Stage2Scratch01", "Stage2Scratch02", "Stage2Scratch03",
    "Stage2Scratch04", "Stage2Scratch05", "Stage2Scratch06",
    "Stage2Scratch07", "Stage2Scratch08", "Stage2Scratch09",
    "Stage2Scratch10", "Stage2Scratch11", "Stage2Scratch12",
    "Stage2Scratch13"
};
inline constexpr int Stage2ScratchCount =
    static_cast<int>(sizeof(Stage2ScratchNames) /
        sizeof(Stage2ScratchNames[0]));

inline constexpr const char* Stage2FalseDoorNames[] =
{
    "Stage2FalseDoorPanel",
    "Stage2FalseDoorFrameNear",
    "Stage2FalseDoorFrameFar",
    "Stage2FalseDoorFrameTop",
    "Stage2FalseDoorHandle"
};

inline constexpr const char* Stage2ClockNames[] =
{
    "Stage2ClockFace",
    "Stage2ClockFrameTop",
    "Stage2ClockFrameBottom",
    "Stage2ClockFrameNear",
    "Stage2ClockFrameFar",
    "Stage2ClockHourHand",
    "Stage2ClockMinuteHand"
};

