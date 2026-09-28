// ============================================================================
// ファイルの役割: 1面の監視カメラの設置位置と、各カメラで起こせる異常の場所を定義します。
// 主な技術: constexprテーブル、データとロジックの分離
// 座標はStageScene::Initの壁・照明配置に合わせています。配置を変えたらここも合わせてください。
// ============================================================================

#pragma once

struct StageSurveillanceCamera
{
    const char* Label;          // 映像左上に出す名前
    float Position[3];          // カメラ位置（天井付近）
    float Target[3];            // 注視点
    int LightNumber;            // 消灯の異常に使う照明（CeilingLightN）。0なら無し
    float FigurePosition[3];    // 人影を立たせる床位置
    int SealedDoorIndex;        // 開く異常に使う開かずの扉。-1なら無し
};

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
    // 自分が操作している端末の背後を映すカメラです。
    { "CAM 04   端末前",
        { 150.0f, -57.0f, 20.0f }, { 200.0f, -88.0f, -40.0f },
        0, { 185.0f, -99.0f, -15.0f }, 1 },
};

inline constexpr int StageSurveillanceCameraCount =
    static_cast<int>(sizeof(StageSurveillanceCameras) /
        sizeof(StageSurveillanceCameras[0]));

// 開かずの扉の配置。壁の手前に置くため、開いても先は壁で通り抜けられません。
struct StageSealedDoor
{
    const char* Name;
    float Position[3];
};

inline constexpr StageSealedDoor StageSealedDoors[] =
{
    { "Stage1SealedDoorStorage", { -130.0f, -74.0f, -176.0f } },  // 左側倉庫の奥の壁
    { "Stage1SealedDoorHall", { 150.0f, -74.0f, 36.0f } },        // 中央ホール南の壁
};

inline constexpr int StageSealedDoorCount =
    static_cast<int>(sizeof(StageSealedDoors) / sizeof(StageSealedDoors[0]));
