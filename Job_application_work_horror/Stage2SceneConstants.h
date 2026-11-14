// ============================================================================
// ファイルの役割: 2面の壁の引っかき傷（文字の形を作る小片）の名前と数、ノックの音の出どころ、出口の先の階段の寸法を定義している。
// 主な技術: constexpr、配列の要素数の自動計算
// 傷の数は、配置（Stage2Layout）と、傷を少しずつ見せる進行（Stage2Scene）の両方で使っている。
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
// 傷の小片の数（名前の配列の要素数から計算している）
inline constexpr int Stage2ScratchCount =
    static_cast<int>(sizeof(Stage2ScratchNames) /
        sizeof(Stage2ScratchNames[0]));

// 「壁の向こうのノック」の異変の音の出どころ（KnockingAnomalyが周回ごとに1つ選んでいる）。
// 廊下の壁（x=±42、厚さ4）の裏側に置き、壁越しにこもった音になるようにしている。
// さえぎる物の判定は音源の6手前までの線分で行うため（Game::ComputeSoundOcclusion）、線分が壁を
// 必ず通るよう、壁から少し離した x=±50 にしている。
// 偽の扉・時計・肖像画・端末と重ならない高さ・位置を選んでいる。
inline constexpr float Stage2KnockSpots[4][3] =
{
    { -50.0f, -78.0f, -60.0f },
    { 50.0f, -78.0f, 15.0f },
    { -50.0f, -78.0f, 40.0f },
    { 50.0f, -78.0f, 85.0f }
};

// 出口の扉（中心 z=140）の先の階段室。寸法は assets/layout/stage2_props.txt の Stage2Stair* と合わせている。
// 段は奥行き7・高さ4で10段（z=150〜220）、その先が上の踊り場（床は y=-60）。
// 最後のループで z=146 を越えたら（周回の判定と同じ位置）、階段を上る演出を始めている
inline constexpr float Stage2StairAutoStartZ = 146.0f;
inline constexpr float Stage2StairStartZ = 150.0f;
inline constexpr float Stage2StairStepRun = 7.0f;
inline constexpr float Stage2StairStepRise = 4.0f;
inline constexpr int Stage2StairStepCount = 10;
