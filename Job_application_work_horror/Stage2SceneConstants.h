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

// --- 廊下の高さと入口
// 床の高さと、人影・プレイヤーの足もとの高さ（床のわずか上）
inline constexpr float Stage2FloorY = -100.0f;
inline constexpr float Stage2FootY = -99.0f;
// 入口の z（周回を進めたときと、足音の影に捕まったときに戻す位置。stage2_props.txt の place Player と同じ。x は0）
inline constexpr float Stage2EntranceZ = -125.0f;
// 偽の扉：最初は左の壁（x=-38.3 の取っ手、z=70）に出し、見た後で右の壁の入口寄り（z=-88）へ移す
inline constexpr float Stage2FalseDoorHandleX = 38.3f;
inline constexpr float Stage2FalseDoorLeftZ = 70.0f;
inline constexpr float Stage2FalseDoorRightZ = -88.0f;

// --- 周回の進行
// 廊下を繰り返す回数。この回数まで扉を抜けると廊下の始めへ戻り、達したら信号盤のパズルと最後の停電へ進む
inline constexpr int Stage2FinalLoopCount = 3;
// 出口の扉（中心 z=140）を抜けたとみなす z。周回を進める判定と、最後のループで階段を上り始める判定に使っている
inline constexpr float Stage2LoopDoorPassZ = 146.0f;
// 信号盤が直った後、廊下の中央（この z）より奥へ来たら、最後の停電を始めている
inline constexpr float Stage2FinalSequenceStartZ = -8.0f;
// 周回が切り替わるときの演出の長さ（秒）
inline constexpr float Stage2LoopTransitionSeconds = 4.2f;
// 進めないまま、この秒数がたつと、探すべき異変の近くの照明を揺らして知らせる。その間隔
inline constexpr float Stage2GuidanceHintSeconds = 15.0f;
inline constexpr float Stage2GuidancePulseInterval = 2.8f;
// H（LB）でヒントを頼んだときに、進めていない時間をこの秒数まで進め、すぐに強いヒントを出している
inline constexpr float Stage2RequestedHintSeconds = 30.0f;
// 知らせの文字を出す秒数。普段は2.8秒、短い補足は2.4秒、段階が変わる知らせは3.0秒、
// 読ませたい大事な知らせ（記録の回収・最後の扉へ向かうときなど）は3.2秒
inline constexpr float Stage2ShortNoticeSeconds = 2.4f;
inline constexpr float Stage2NoticeSeconds = 2.8f;
inline constexpr float Stage2LongNoticeSeconds = 3.0f;
inline constexpr float Stage2ImportantNoticeSeconds = 3.2f;
// 非常用充電器：電池を回復する代わりに、危険度をこの値まで上げ、警告をこの秒数出す
inline constexpr float Stage2ChargerBattery = 30.0f;
inline constexpr float Stage2ChargerThreat = 0.76f;
inline constexpr float Stage2ChargerWarningSeconds = 3.2f;
inline constexpr float Stage2ChargerEventCooldown = 0.12f;
// 残された記録を回収したとき：電池を少し回復し、危険度を下げる
inline constexpr float Stage2EvidenceBattery = 6.0f;
inline constexpr float Stage2EvidenceThreatRelief = 0.18f;

// 出口の扉（中心 z=140）の先の階段室。寸法は assets/layout/stage2_props.txt の Stage2Stair* と合わせている。
// 段は奥行き7・高さ4で10段（z=150〜220）、その先が上の踊り場（床は y=-60）。
// 最後のループで扉を抜けたら（周回の判定と同じ位置）、階段を上る演出を始めている
inline constexpr float Stage2StairAutoStartZ = Stage2LoopDoorPassZ;
inline constexpr float Stage2StairStartZ = 150.0f;
inline constexpr float Stage2StairStepRun = 7.0f;
inline constexpr float Stage2StairStepRise = 4.0f;
inline constexpr int Stage2StairStepCount = 10;
