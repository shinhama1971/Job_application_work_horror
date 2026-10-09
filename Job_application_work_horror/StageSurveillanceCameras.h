// ============================================================================
// ファイルの役割: 1面の監視カメラの設置位置と、各カメラで起こせる異常の場所を定義している。
// 主な技術: constexprの表、データと処理の分離
// 座標はStage1Layoutの壁・照明の配置に合わせている。配置を変えたら、ここも合わせる必要がある。
// ============================================================================

#pragma once

// 監視カメラ1台分の設定
struct StageSurveillanceCamera
{
    const char* Label;          // 映像の左上に出す名前
    float Position[3];          // カメラの位置（天井の近く）
    float Target[3];            // カメラが向いている点
    int LightNumber;            // 消灯の異常に使う照明（CeilingLightN）。0なら無し
    float FigurePosition[3];    // 人影を立たせる床の位置
    int SealedDoorIndex;        // 開く異常に使う開かずの扉の番号。-1なら無し
};

// 4台の監視カメラ（右の倉庫・左の倉庫・中央ホール・端末の前）
inline constexpr StageSurveillanceCamera StageSurveillanceCameras[] =
{
    { "CAM 01   右側倉庫",
        { 150.0f, -57.0f, -82.0f }, { 150.0f, -82.0f, -150.0f },
        3, { 150.0f, -99.0f, -150.0f }, -1 },
    { "CAM 02   左側倉庫",
        { -150.0f, -57.0f, -82.0f }, { -150.0f, -82.0f, -150.0f },
        2, { -165.0f, -99.0f, -155.0f }, 0 },
    { "CAM 03   中央ホール",
        { -60.0f, -57.0f, -60.0f }, { 20.0f, -85.0f, 20.0f },
        4, { -30.0f, -99.0f, 0.0f }, -1 },
    // 自分が操作している端末の背後を映すカメラ（映像に自分の後ろが映る怖さを狙っている）。
    { "CAM 04   端末前",
        { 150.0f, -57.0f, 20.0f }, { 200.0f, -88.0f, -40.0f },
        0, { 185.0f, -99.0f, -15.0f }, 1 },
};

// 監視カメラの台数
inline constexpr int StageSurveillanceCameraCount =
    static_cast<int>(sizeof(StageSurveillanceCameras) /
        sizeof(StageSurveillanceCameras[0]));

// 開かずの扉の配置。壁の手前に置くため、開いても先は壁で通り抜けられない。
struct StageSealedDoor
{
    const char* Name;
    float Position[3];
};

inline constexpr StageSealedDoor StageSealedDoors[] =
{
    { "Stage1SealedDoorStorage", { -130.0f, -74.0f, -176.0f } },  // 左の倉庫の奥の壁
    { "Stage1SealedDoorHall", { 150.0f, -74.0f, 36.0f } },        // 中央ホールの南の壁
};

// 開かずの扉の数
inline constexpr int StageSealedDoorCount =
    static_cast<int>(sizeof(StageSealedDoors) / sizeof(StageSealedDoors[0]));
