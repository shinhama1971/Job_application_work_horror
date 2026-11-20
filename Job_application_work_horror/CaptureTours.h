// ============================================================================
// ファイルの役割: 自動撮影（--capture・--capture-features・--forest --capture）の道順のデータと、撮影の段取りの時刻。
// 道順の中身（キーフレーム）は CaptureTours.cpp にある。道順をたどって撮る処理は CaptureMode.cpp。
// ============================================================================

#pragma once

#include "MathUtility.h"

#include <SimpleMath.h>
#include <cmath>
#include <vector>

namespace Tools::CaptureTours
{
    // ------------------------------------------------------------------------
    // 道順（キーフレーム）。時刻と時刻の間は、位置と向きを直線で補間している。
    // Shotに名前があるキーフレームの時刻で、スクリーンショットを1枚保存している。
    // ------------------------------------------------------------------------
    struct Keyframe
    {
        // 道順の時刻（シーンが始まってからの秒数）
        float Time;
        DirectX::SimpleMath::Vector3 Position;   // 足元の位置（yは床の高さ）
        float Yaw;          // 水平の向き（+Zが0、+Xがπ/2）
        float Pitch;        // 上下の向き（負で下を向く）
        const wchar_t* Shot;  // スクリーンショットのファイル名（nullptrなら撮らない）
        bool Hide;          // 2面のロッカーに隠れている区間
    };

    // 円周率と、床の高さ（プレイヤーの足元のy）
    constexpr float Pi = MathUtility::Pi;
    constexpr float FloorY = -99.0f;

    // (fromX, fromZ)から(toX, toZ)の方を向くヨー角を求めている
    inline float YawToward(float fromX, float fromZ, float toX, float toZ)
    {
        return std::atan2(toX - fromX, toZ - fromZ);
    }

    // ------------------------------------------------------------------------
    // 撮影の段取り：道順の時刻に合わせて、懐中電灯を消す・異変を出す・ドアを開けるなどをする時刻
    // ------------------------------------------------------------------------
    // 2面の鏡の異変は、この時刻にデバッグの操作（SceneDebugAction::ShowMirrorAnomaly）で出している。
    constexpr float MirrorAnomalyRequestTime = 40.0f;
    // 1面で、この時刻の間だけ懐中電灯を消している（消したときの暗さを写すため）
    constexpr float Stage1FlashlightOffStart = 28.5f;
    constexpr float Stage1FlashlightOffEnd = 29.4f;
    // 2面で、この時刻の間だけ懐中電灯を消している（電球の光だけで、床に落ちる自分の体の影を写すため）
    constexpr float Stage2FlashlightOffStart = 9.6f;
    constexpr float Stage2FlashlightOffEnd = 11.3f;
    // 1面の西棟で、この時刻に天井の人影を出している（水面にだけ映る、天井に逆さまに張り付いた人影を写すため）
    constexpr float CeilingReflectionRequestTime = 59.0f;
    // 1面の最後で、この時刻に突き当たりの最後のドアを開けている（開けた瞬間に開始地点の奥のドアへつながる様子を写すため）
    constexpr float Stage1EndDoorOpenTime = 119.0f;
    // 1面の最後に、この時刻に3周目・ヒューズ3本の状態にしている（建物全体が呼吸する様子を写すため）
    constexpr float BreathingBuildingRequestTime = 141.0f;
    // 濡れた足跡の場面の前に、2周目へ進めている（誰かの足跡は2周目から出る）
    constexpr float SecondLoopRequestTime = 50.5f;
    // 肖像画の場面の前に、肖像画の異変を出している
    constexpr float PortraitAnomalyRequestTime = 62.5f;
    // この時刻からは、電池を25%にして弱った懐中電灯を写している
    constexpr float LowBatteryTime = 73.5f;
    constexpr float LowBatteryEndTime = 77.0f;
    // 偽の扉の場面の前に、偽の扉の異変を出している
    constexpr float FalseDoorRequestTime = 77.5f;
    // 見えない影の場面の前に、見えない影の異変を出している（姿は見えず、照明の下を通ると影だけが落ちる）
    constexpr float UnseenAnomalyRequestTime = 83.0f;
    // 水たまりの場面の前に、水たまりの異変を出している（水面にだけ、水たまりの向こうの人影が映る）
    constexpr float PuddleAnomalyRequestTime = 94.0f;
    // 最後の停電と追跡を始める時刻
    constexpr float FinalSequenceRequestTime = 100.5f;

    // 道順（中身は CaptureTours.cpp）。1面・2面・機能の確認用（--capture-features）・3面・3面の観測区画の回り道・外の森
    extern const std::vector<Keyframe> Stage1Tour;
    extern const std::vector<Keyframe> Stage2Tour;
    extern const std::vector<Keyframe> FeatureStage1;
    extern const std::vector<Keyframe> FeatureStage2;
    extern const std::vector<Keyframe> Stage3Tour;
    extern const std::vector<Keyframe> Stage3ObservationTour;
    extern const std::vector<Keyframe> ForestTour;
}
