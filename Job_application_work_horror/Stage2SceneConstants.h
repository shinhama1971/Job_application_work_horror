// ============================================================================
// ファイルの役割: 2面の壁の引っかき傷（文字を形作る小片）の名前と個数を定義します。
// 主な技術: constexpr、配列の要素数の自動計算
// 個数は配置（Stage2Layout）と、傷を少しずつ見せる進行（Stage2Scene）の両方で使います。
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

