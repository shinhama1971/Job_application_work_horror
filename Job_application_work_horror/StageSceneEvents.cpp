// ============================================================================
// ファイルの役割: 1面の演出（入口・左の倉庫・ループ廊下・照明の連鎖・ヒューズの後の影・電力の復旧・出口・物音・壁の文字・心拍・録音と無線）を担当している。
// 主な技術: 出来事をきっかけにした処理、有限状態機械、照明・音・画面効果のタイミング合わせ
// ============================================================================

#include "StageScene.h"
#include "CaptureMode.h"
#include "Stage1FuseProgress.h"
#include "ShadowFigureGeometry.h"
#include "MathUtility.h"
#include "Application.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"

#include "Player.h"
#include "Ground.h"
#include "Wall.h"
#include "Item.h"
#include "Door.h"
#include "FuseBox.h"
#include "CeilingLight.h"
#include "ExitTrigger.h"
#include "BatteryItem.h"
#include "ScreenDustOverlay.h"
#include "ScareTrigger.h"
#include "ShadowMan.h"
#include "FlashlightWriting.h"
#include "Camera.h"
#include "TensionPulseFeedback.h"
#include "VoiceRecorder.h"
#include "EventFeedback.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

namespace
{
    // 懐中電灯で照らしているかを調べるとき、これより近い物は向きを計算しない（0で割らないため）
    constexpr float MinimumLitDistance = 0.001f;
}

namespace
{
    // 人影の足もとの高さ（床 y=-100 のわずか上）
    constexpr float FootY = -99.0f;

    // --- 入口の演出：中央の扉の先（この z より奥、廊下の幅の中）へ初めて入ったら、前と後ろの照明を順に明滅させる
    constexpr float EntranceThresholdZ = 62.0f;
    constexpr float EntranceHalfWidth = 58.0f;
    constexpr float EntranceAheadFlickerSeconds = 0.38f;
    constexpr float EntranceBehindFlickerSeconds = 1.12f;

    // --- 左の倉庫の演出：2回目に倉庫の奥（x・z がこれより小さい所）へ入ったら、物音の後に影を出す
    constexpr int StorageScareEndsAtFuseCount = 2;
    constexpr float StorageScareEnterMaxX = -110.0f;
    constexpr float StorageScareEnterMaxZ = -108.0f;
    // 物音から影を出すまでの秒数と、そのときまだ倉庫の中（x・z がこれより小さい所）にいるか
    constexpr float StorageScareDelaySeconds = 0.8f;
    constexpr float StorageScareStayMaxX = -100.0f;
    constexpr float StorageScareStayMaxZ = -90.0f;
    const Vector3 StorageShadowPosition(-155.0f, FootY, -88.0f);
    constexpr float StorageShadowGazeSeconds = 5.0f;
    // 知らせの秒数（物音のとき・影が出たとき・影を見たとき）
    constexpr float StorageNoiseNoticeSeconds = 2.1f;
    constexpr float StorageShadowNoticeSeconds = 2.0f;
    constexpr float StorageObservedNoticeSeconds = 1.8f;

    // --- 開始地点の奥のドア：通ってきて、部屋へこれだけ入り、ドアに背を向けたら（前の向きの z がこれより大きい）閉める
    constexpr float StartDoorCloseWalkIn = 30.0f;
    constexpr float StartDoorCloseFacingZ = 0.2f;
    // ドアを叩く音（この周回から）、取っ手が揺れる（この周回から）、ドアの隙間が開いて見える（この周回から）
    constexpr int StartDoorKnockLoop = 2;
    constexpr int StartDoorRattleLoop = 3;
    constexpr int StartDoorHauntedGapLoop = 4;
    // 開始地点に戻ったとき、背後に一度だけ人影を出す周回（壁の文字の警告どおり）
    constexpr int BehindShadowLoop = 3;
    // 開始地点の部屋の範囲（z がこれより小さく、x の絶対値がこれより小さい）
    constexpr float StartRoomMaxZ = -70.0f;
    constexpr float StartRoomHalfWidth = 90.0f;
    // 叩く間隔：部屋に戻ってきてから最初まで、続けて叩く間、叩き終えた後の間（基本＋回ごとにずらす量を周期で折り返す）
    constexpr float StartDoorKnockReturnDelay = 2.5f;
    constexpr float StartDoorKnockBurstInterval = 0.38f;
    constexpr float StartDoorKnockPauseBase = 6.5f;
    constexpr float StartDoorKnockPauseStep = 2.7f;
    constexpr float StartDoorKnockPauseRange = 4.0f;

    // 突き当たりの最後のドアを抜けたとみなす、ドアの幅からのゆとり
    constexpr float EndDoorPassMargin = 2.0f;
    // ループ廊下を1周進めたときの知らせの秒数
    constexpr float CorridorLoopNoticeSeconds = 2.4f;
    // 1周目に中央の廊下へ出す影の位置と、3周目に開始地点のドアの奥へ出す影の、ドアからの距離
    const Vector3 CorridorShadowPosition(0.0f, FootY, -25.0f);
    constexpr float StartDoorShadowBehind = 70.0f;

    // --- ヒューズの後に出る影：2本目は中央の廊下（z=-22）、3本目は出口の前（z=118）に出し、見せる秒数を変える
    // 見張りは2本目から出す。出現位置と通知も取得順で切り替える。
    constexpr int FirstFuseWatcherPickup = 2;
    constexpr float FuseWatcherSecondZ = -22.0f;
    constexpr float FuseWatcherThirdZ = 118.0f;
    constexpr float FuseWatcherSecondSeconds = 5.8f;
    constexpr float FuseWatcherThirdSeconds = 7.2f;
    constexpr float FuseWatcherObservedNoticeSeconds = 1.8f;

    // --- 出口の前兆：送電の後、出口の前（z と x がこれ以上の所）へ来たら影を出す
    constexpr float ExitOmenMinZ = 215.0f;
    constexpr float ExitOmenMinX = 28.0f;
    constexpr float ExitOmenGazeSeconds = 4.2f;
    // 電力が戻ってから、この秒数は周りの物音を鳴らさない
    constexpr float PowerRestoreQuietSeconds = 6.0f;

    // --- 心拍：影が近いほど強める範囲（この距離から、この幅で遠ざかるほど弱く）。隠し部屋に閉じ込められている間の弱い心拍
    constexpr float HeartbeatNearDistance = 14.0f;
    constexpr float HeartbeatRange = 72.0f;
    constexpr float HiddenRoomHeartbeat = 0.30f;

    // --- 懐中電灯で照らしているか：光が届く距離（約275）より少し手前まで、光の円錐の中（向きとの内積）、
    // 遮る物を調べる線分を目標のこれだけ手前で止める
    constexpr float FlashlightLitDistance = 240.0f;
    constexpr float FlashlightLitFacing = 0.927f;
    constexpr float FlashlightLitEndMargin = 4.0f;

    // --- 書類保管室の照らすと止まる影：照らされたかを調べる胸の高さ、音を鳴らす高さ、知らせの秒数、電池を奪っても残す量
    constexpr float ArchiveStalkerKnockHeight = 20.0f;
    constexpr float ArchiveStalkerStepHeight = 4.0f;
    constexpr float ArchiveStalkerFirstNoticeSeconds = 4.5f;
    constexpr float ArchiveStalkerNoticeSeconds = 2.6f;
    constexpr float ArchiveStalkerCaughtNoticeSeconds = 2.8f;
    constexpr float ArchiveStalkerBatteryFloor = 5.0f;

    // ------------------------------------------------------------------------
    // 演出の強さと長さ（照明の明滅・画面の脈打ち・振動・音）。出来事の流れの順に並べている
    // ------------------------------------------------------------------------
    using EventFeedback::FlickerSpec;
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;
    using EventFeedback::SoundSpec;

    // 1面の天井の照明の番号（StageObjects::CeilingLightAt）

    // 入口：踏み込んだ瞬間に後ろの照明、少し後に前の照明、最後にもう一度後ろの照明を弱く明滅させる
    constexpr FlickerSpec EntranceFirstFlicker{ 0.38f, 0.52f };
    constexpr PulseSpec EntranceFirstPulse{ 0.075f, 0.14f };
    constexpr RumbleSpec EntranceFirstRumble{ 3, 0.07f };
    constexpr FlickerSpec EntranceAheadFlicker{ 0.62f, 0.72f };
    constexpr PulseSpec EntranceAheadPulse{ 0.10f, 0.18f };
    constexpr RumbleSpec EntranceAheadRumble{ 4, 0.09f };
    constexpr FlickerSpec EntranceBehindFlicker{ 0.20f, 0.28f };

    // 左の倉庫：先に低い扉の物音と照明で背後を意識させ、影を見たら強めに反応する
    constexpr SoundSpec StorageNoiseDoorSound{ 0.68f, 1.0f };
    constexpr FlickerSpec StorageNoiseFlicker{ 0.42f, 0.62f };
    constexpr FlickerSpec StorageObservedFlicker{ 0.75f, 0.88f };
    constexpr SoundSpec StorageObservedScareSound{ 0.82f, 1.0f };
    constexpr PulseSpec StorageObservedPulse{ 0.24f, 0.26f };
    constexpr RumbleSpec StorageObservedRumble{ 7, 0.15f };

    // 開始地点の奥のドアを叩く音：1回に叩く数（最小＋叩いた回数で巡る分）、叩くほど少しずつ高くなる音、音量
    constexpr int StartDoorKnockMinCount = 2;
    constexpr int StartDoorKnockCountCycle = 3;
    constexpr float StartDoorKnockBasePitch = 0.58f;
    constexpr float StartDoorKnockPitchStep = 0.03f;
    constexpr float StartDoorKnockVolume = 1.5f;

    // ループ廊下：周回の段階の上限（これより先は同じ見た目）と、開始地点へ移った後に次の判定をするまでの秒数
    constexpr int MaxCorridorLoopPhase = 3;
    constexpr float CorridorLoopCooldownSeconds = 1.0f;
    // 周回の数だけ出す壁の赤い印：地の色、自己発光の赤（印ごとに少しずつ強く）、光沢
    const Color LoopMarkerDiffuse(0.26f, 0.012f, 0.008f, 1.0f);
    constexpr float LoopMarkerGlowBase = 0.14f;
    constexpr float LoopMarkerGlowStep = 0.055f;
    constexpr float LoopMarkerGlowGreen = 0.002f;
    constexpr float LoopMarkerGlowBlue = 0.001f;
    constexpr float LoopMarkerShininess = 24.0f;
    // 周回を進めたときの画面の脈打ちと振動（基本＋周回の段階ごとに強める量）
    constexpr PulseSpec CorridorLoopPulseBase{ 0.24f, 0.42f };
    constexpr PulseSpec CorridorLoopPulseStep{ 0.13f, 0.10f };
    constexpr RumbleSpec CorridorLoopRumbleBase{ 7, 0.18f };
    constexpr RumbleSpec CorridorLoopRumbleStep{ 3, 0.04f };

    // 非常灯の点滅の周期を、器具ごとにずらす値（同じ値だとそろって点滅し、機械的に見えるため）
    // 周回ごとの配置
    constexpr float MiddleLoop1EmergencyOffset = 2.4f;
    constexpr float MiddleLoop2EmergencyOffset = 5.8f;
    constexpr float CornerLoop2EmergencyOffset = 1.1f;
    // 3周目以降と、照明の連鎖が始まる前の配置
    constexpr float StartRoomEmergencyOffset = 0.35f;
    constexpr float MiddleIdleEmergencyOffset = 8.2f;
    constexpr float HallIdleEmergencyOffset = 3.1f;
    constexpr float CornerIdleEmergencyOffset = 4.3f;
    constexpr float LoopExitIdleEmergencyOffset = 4.5f;
    // 照明の連鎖で赤い非常灯が移っていく間（すぐに点くよう、小さくずらしている）
    constexpr float MiddleChaseEmergencyOffset = 0.75f;
    constexpr float HallChaseEmergencyOffset = 0.42f;
    constexpr float CornerChaseEmergencyOffset = 0.18f;
    constexpr float LoopExitChaseEmergencyOffset = 0.08f;
    // 連鎖の最後に廊下を赤い非常灯に戻すとき
    constexpr float MiddleFinalEmergencyOffset = 2.6f;

    // 照明の連鎖の1拍ごとの画面の脈打ちと振動（出口へ近づくほど強める）
    constexpr PulseSpec ScareChasePulse1{ 0.13f, 0.18f };
    constexpr RumbleSpec ScareChaseRumble1{ 5, 0.10f };
    constexpr PulseSpec ScareChasePulse2{ 0.14f, 0.18f };
    constexpr RumbleSpec ScareChaseRumble2{ 6, 0.13f };
    constexpr PulseSpec ScareChasePulse3{ 0.16f, 0.20f };
    constexpr RumbleSpec ScareChaseRumble3{ 7, 0.16f };
    constexpr PulseSpec ScareChasePulse4{ 0.19f, 0.22f };
    constexpr RumbleSpec ScareChaseRumble4{ 8, 0.19f };
    constexpr RumbleSpec ScareChaseFinalRumble{ 4, 0.09f };

    // ヒューズの後の影：見たときの反応と、出たときの画面の脈打ち（3本目のほうが強い）
    constexpr FlickerSpec FuseWatcherObservedFlicker{ 0.72f, 0.74f };
    constexpr PulseSpec FuseWatcherObservedBloom{ 0.42f, 0.18f };
    constexpr RumbleSpec FuseWatcherObservedRumble{ 7, 0.16f };
    constexpr PulseSpec FuseWatcherSecondPulse{ 0.20f, 0.30f };
    constexpr PulseSpec FuseWatcherThirdPulse{ 0.32f, 0.30f };

    // 電力が戻る演出の3拍：強い光、短い乱れ、余韻の光
    constexpr PulseSpec PowerRestoreFlashBloom{ 1.18f, 0.55f };
    constexpr RumbleSpec PowerRestoreFlashRumble{ 9, 0.16f };
    constexpr PulseSpec PowerRestoreJoltPulse{ 0.12f, 0.22f };
    constexpr RumbleSpec PowerRestoreJoltRumble{ 6, 0.11f };
    constexpr PulseSpec PowerRestoreAfterglowBloom{ 0.48f, 0.40f };
    constexpr RumbleSpec PowerRestoreAfterglowRumble{ 4, 0.07f };

    // 出口へ送電する演出：照明を消した瞬間と、角・出口の照明を順に点け直す拍、最後の光
    constexpr PulseSpec ExitPowerCutPulse{ 0.30f, 0.28f };
    constexpr FlickerSpec ExitPowerFirstFlicker{ 0.72f, 0.82f };
    constexpr PulseSpec ExitPowerFirstBloom{ 0.68f, 0.24f };
    constexpr RumbleSpec ExitPowerFirstRumble{ 5, 0.11f };
    constexpr FlickerSpec ExitPowerSecondFlicker{ 0.82f, 0.92f };
    constexpr PulseSpec ExitPowerSecondBloom{ 0.92f, 0.30f };
    constexpr RumbleSpec ExitPowerSecondRumble{ 7, 0.15f };
    constexpr PulseSpec ExitPowerFinalBloom{ 1.18f, 0.42f };
    constexpr RumbleSpec ExitPowerFinalRumble{ 10, 0.20f };

    // 出口の前兆：影が出た瞬間と、その後の2拍（背後の照明が消える・出口の照明が壊れる）
    constexpr FlickerSpec ExitOmenFlicker{ 0.82f, 0.78f };
    constexpr PulseSpec ExitOmenPulse{ 0.28f, 0.42f };
    constexpr PulseSpec ExitOmenBloom{ 0.54f, 0.24f };
    constexpr RumbleSpec ExitOmenRumble{ 10, 0.22f };
    constexpr PulseSpec ExitOmenLightOffPulse{ 0.22f, 0.30f };
    constexpr RumbleSpec ExitOmenLightOffRumble{ 7, 0.16f };
    constexpr FlickerSpec ExitOmenFaultFlicker{ 1.10f, 0.88f };
    constexpr PulseSpec ExitOmenFaultBloom{ 0.44f, 0.20f };

    // 暖かい灯りの下にいる度合いが、これ以上なら物音を鳴らさない
    constexpr float SafeLightQuietCalm = 0.5f;

    // --- 振り返ると廊下が伸びて見える（Stage1CorridorStretch）
    // 伸び始めたときの音（扉のきしみを低く遅くして、建物がきしむ音にしている）は、南のドアの方から鳴らしている。
    // 鳴らす所は、プレイヤーからこの距離だけ南
    constexpr float CorridorStretchSoundDistance = 60.0f;
    constexpr EventFeedback::SoundSpec CorridorStretchSound{ 0.38f, 0.9f };
    // 伸び始めの小さな画面の脈打ちと振動
    constexpr PulseSpec CorridorStretchPulse{ 0.16f, 0.6f };
    constexpr RumbleSpec CorridorStretchRumble{ 10, 0.08f };

    // --- 呼吸する壁（ループ廊下の東の区間の南の壁）
    // 配置ファイルの壁の名前と、膨らませる面（+z の面＝北の廊下側）、格子の細かさ、縁から動かさない幅
    constexpr const char* BreathingWallName = "LoopWall3";
    constexpr bool BreathingWallPositiveZFace = true;
    constexpr int BreathingWallColumns = 72;
    constexpr int BreathingWallRows = 24;
    constexpr float BreathingWallEdgeWidth = 14.0f;
    // 一番大きく膨らむ量（1周目・2周目・3周目以降。0周目は動かない）と、その量へ近づく速さ（1秒あたり）
    constexpr float BreathingWallFirstLoopAmplitude = 4.0f;
    constexpr float BreathingWallSecondLoopAmplitude = 5.5f;
    constexpr float BreathingWallLaterLoopAmplitude = 7.0f;
    constexpr float BreathingWallAmplitudeRate = 1.0f;
    // 息の音：壁のこの範囲（x）と高さで、プレイヤーに一番近い所から鳴らす。この距離より遠いと鳴らさない
    constexpr float BreathingWallSoundMinX = 50.0f;
    constexpr float BreathingWallSoundMaxX = 190.0f;
    constexpr float BreathingWallSoundY = -70.0f;
    constexpr float BreathingWallSoundZ = 238.0f;
    constexpr float BreathingWallSoundDistance = 90.0f;
    // 懐中電灯を壁へ向けているとみなす向き（視線と、音の所への向きの内積がこれより大きい）。照らしている間は息をしない
    constexpr float BreathingWallLitFacing = 0.9f;
    constexpr EventFeedback::SoundSpec BreathingWallSound{ 0.55f, 0.75f };

    // --- 天井裏の足音が、照明を消しながら迫ってくる（Stage1CeilingStalker）
    // 電力が戻ってから、この秒数が過ぎた後に起きる（照明が全部点き、落ち着いた頃）
    constexpr float CeilingStalkerDelaySeconds = 25.0f;
    // 足音が通る線の上の照明（南北に一直線に並ぶ、中央の廊下・中央ホール・北の角）
    constexpr int CeilingStalkerLights[] =
    {
        Stage1LightNumber::MiddleCorridor,
        Stage1LightNumber::CentralHall,
        Stage1LightNumber::NorthCorner
    };
    // 足音が真下を通った照明が消えるときの音（懐中電灯のスイッチの音を低く）、画面の脈打ち、振動
    constexpr EventFeedback::SoundSpec CeilingStalkerLightOffSound{ 0.45f, 1.3f };
    constexpr PulseSpec CeilingStalkerLightOffPulse{ 0.18f, 0.24f };
    constexpr RumbleSpec CeilingStalkerLightOffRumble{ 4, 0.12f };
    // 追いつかれたとき：真上の大きな衝撃音、強い画面の脈打ち、振動
    constexpr EventFeedback::SoundSpec CeilingStalkerCaughtSound{ 0.70f, 1.0f };
    constexpr PulseSpec CeilingStalkerCaughtPulse{ 0.85f, 0.70f };
    constexpr RumbleSpec CeilingStalkerCaughtRumble{ 18, 0.55f };
    // 照明が点き直すときの明滅（秒・強さ）
    constexpr FlickerSpec CeilingStalkerRestoreFlicker{ 0.55f, 0.80f };

    // --- ヒューズを拾った瞬間の演出（1本目・2本目・3本目の順。本数が多いほど強い）
    struct FuseScareSetting
    {
        float Radius;
        float BlackoutSeconds;
        float SputterSeconds;
        std::size_t StepCount;
    };
    constexpr FuseScareSetting FuseScares[] =
    {
        { 90.0f, 1.2f, 0.9f, 0 },
        { 120.0f, 1.8f, 1.4f, 1 },
        { 160.0f, 2.4f, 1.9f, 2 },
    };
    constexpr float FuseScareStepTimes[] = { 0.65f, 1.05f };
    static_assert(std::size(FuseScares) == Core::GameState::RequiredFuseCount);
    static_assert(std::ranges::all_of(FuseScares, [](const FuseScareSetting& setting)
    {
        return setting.StepCount <= std::size(FuseScareStepTimes);
    }));
    // 足音を鳴らす所（拾った瞬間に向いていた方の、真後ろのこの距離。床の高さ）と、重い足音の高さ・音量
    constexpr float FuseScareBehindDistance = 5.0f;
    constexpr float FuseScareStepPitch = 0.62f;
    constexpr float FuseScareStepVolume = 2.2f;
    // 照明が消える瞬間の音（懐中電灯のスイッチの音を低く）、足音と一緒に出す画面の脈打ちと振動
    constexpr EventFeedback::SoundSpec FuseScareCutSound{ 0.42f, 1.4f };
    constexpr PulseSpec FuseScareStepPulse{ 0.36f, 0.40f };
    constexpr RumbleSpec FuseScareStepRumble{ 8, 0.22f };
    // 照明が点き直すときの明滅（秒・強さ）
    constexpr FlickerSpec FuseScareRestoreFlicker{ 0.50f, 0.85f };

    // --- 影のまわりの空間のゆがみ（PostProcess::SetPresenceWarps）
    // ゆがみ始める距離と、一番強くなる距離（これより近いと最大）
    constexpr float PresenceWarpRange = 140.0f;
    constexpr float PresenceWarpNearDistance = 18.0f;
    // ゆがみの中心にする高さ（影の胸。ShadowMan の視線の判定と同じ）と、ゆがむ範囲の体のまわりの半径（ワールドの長さ）
    constexpr float PresenceWarpWorldRadius = 28.0f;
    // 画面の上でのゆがむ範囲の半径の下限と上限（縦の高さを1とした長さ）
    constexpr float PresenceWarpMinScreenRadius = 0.08f;
    constexpr float PresenceWarpMaxScreenRadius = 0.6f;
    // 画面の外へこれだけ出た影までは、端にかかるゆがみを出す（画面の位置 0〜1 の外側）
    constexpr float PresenceWarpScreenMargin = 0.25f;
    // カメラの後ろとみなす w の下限
    constexpr float PresenceWarpMinClipW = 0.001f;
    // 残像（PostProcess::SetEcho）を出し始める緊張（影の近さ）と、一番強くなる緊張
    constexpr float EchoThreatStart = 0.45f;
    constexpr float EchoThreatFull = 0.95f;

    // 書き換わった壁の文字を読んだとき
    constexpr PulseSpec ChangedWritingPulse{ 0.22f, 0.36f };
    // 最初の通過では普通の廊下を覚え、次に通ったときだけ暗がりの変化を発見できる。
    constexpr int DarkWritingFirstCompletedLoop = 1;
    constexpr float DarkWritingReadSeconds = 0.45f;
    constexpr PulseSpec DarkWritingPulse{ 0.13f, 0.65f };
    constexpr RumbleSpec ChangedWritingRumble{ 5, 0.16f };

    // 書類保管室の影：現れたときの棚の奥の音、近づく足音、触れられたとき
    constexpr SoundSpec ArchiveStalkerKnockSound{ 0.55f, 1.4f };
    constexpr SoundSpec ArchiveStalkerStepSound{ 0.74f, 1.6f };
    constexpr SoundSpec ArchiveStalkerCaughtSound{ 0.9f, 1.0f };
    constexpr PulseSpec ArchiveStalkerCaughtPulse{ 0.72f, 0.55f };
    constexpr RumbleSpec ArchiveStalkerCaughtRumble{ 14, 0.36f };

    // 誰かの濡れた足跡：水音、照らして止めたとき、すぐ後ろまで来たときの息づかい（聞き手の後ろのこの距離から鳴らす）
    constexpr SoundSpec FollowerWaterStepSound{ 0.78f, 0.55f };
    constexpr PulseSpec FollowerLitPulse{ 0.14f, 0.26f };
    constexpr RumbleSpec FollowerLitRumble{ 2, 0.08f };
    constexpr float FollowerBreathBehindDistance = 10.0f;
    constexpr SoundSpec FollowerBreathSound{ 0.85f, 0.45f };
    constexpr PulseSpec FollowerReachedPulse{ 0.30f, 0.40f };
    constexpr RumbleSpec FollowerReachedRumble{ 5, 0.14f };
}

// 入口の演出：中央の扉の先へ初めて入ったとき、前と後ろの照明を順に明滅させている（電力が戻るまで）
void StageScene::UpdateEntranceThresholdEvent(Player& player)
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();

    if (!m_EntranceEventTriggered)
    {
        const Vector3 position = player.GetPosition();
        const bool crossedDoorThreshold =
            !game->IsPowerRestored() &&
            position.z > EntranceThresholdZ &&
            std::abs(position.x) < EntranceHalfWidth;
        if (!crossedDoorThreshold)
        {
            return;
        }

        // カメラを無理に動かす演出の代わりに、空間の中の照明の変化で出来事を見せている。
        // 操作を奪わず、プレイヤー自身が異変に気づける演出にしている。
        m_EntranceEventTriggered = true;
        m_EntranceEventTimer = 0.0f;
        m_EntranceEventPhase = EntranceEventPhase::Started;

        CeilingLight* lightBehind =
            m_Objects.CeilingLightAt(Stage1LightNumber::MiddleCorridor);
        if (lightBehind != nullptr)
        {
            EventFeedback::Flicker(*lightBehind, EntranceFirstFlicker);
        }

        EventFeedback::HorrorPulse(*game->GetPostProcess(), EntranceFirstPulse);
        EventFeedback::Rumble(EntranceFirstRumble);
        return;
    }

    if (m_EntranceEventTimer < 0.0f)
    {
        return;
    }
    if (game->IsPowerRestored())
    {
        m_EntranceEventTimer = -1.0f;
        m_EntranceEventPhase = EntranceEventPhase::NotStarted;
        return;
    }

    // 0.38秒で前の照明、1.12秒で後ろの照明を明滅させて終えている
    m_EntranceEventTimer += deltaTime;
    if (m_EntranceEventPhase == EntranceEventPhase::Started && m_EntranceEventTimer >= EntranceAheadFlickerSeconds)
    {
        CeilingLight* lightAhead =
            m_Objects.CeilingLightAt(Stage1LightNumber::CentralHall);
        if (lightAhead != nullptr)
        {
            EventFeedback::Flicker(*lightAhead, EntranceAheadFlicker);
        }

        EventFeedback::HorrorPulse(*game->GetPostProcess(), EntranceAheadPulse);
        EventFeedback::Rumble(EntranceAheadRumble);
        m_EntranceEventPhase = EntranceEventPhase::AheadFlickered;
    }
    else if (m_EntranceEventPhase == EntranceEventPhase::AheadFlickered &&
        m_EntranceEventTimer >= EntranceBehindFlickerSeconds)
    {
        CeilingLight* lightBehind =
            m_Objects.CeilingLightAt(Stage1LightNumber::MiddleCorridor);
        if (lightBehind != nullptr)
        {
            EventFeedback::Flicker(*lightBehind, EntranceBehindFlicker);
        }

        m_EntranceEventTimer = -1.0f;
        m_EntranceEventPhase = EntranceEventPhase::Finished;
    }
}

// 左の倉庫の演出：2本目のヒューズを探しに入ると、物音の後に背後へ影を出している
void StageScene::UpdateStorageScare(Player& player)
{
    if (m_StorageScarePhase >= StorageScarePhase::ShadowShown)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 position = player.GetPosition();
    if (game->IsPowerRestored() || game->GetItemCount() >= StorageScareEndsAtFuseCount)
    {
        m_StorageScarePhase = StorageScarePhase::Finished;
        return;
    }

    if (m_StorageScarePhase == StorageScarePhase::Waiting)
    {
        // 2回目に左の倉庫へ踏み込んだとき、先に物音と照明で背後を意識させている。
        if (m_CorridorLoopCount < 1 || game->GetItemCount() != 1 ||
            position.x > StorageScareEnterMaxX || position.z > StorageScareEnterMaxZ)
        {
            return;
        }

        m_StorageScarePhase = StorageScarePhase::NoiseHeard;
        m_StorageScareTimer = 0.0f;
        m_StorageScareNoticeTimer = StorageNoiseNoticeSeconds;
        game->PlayAudioCue(SOUND_CUE_DOOR, StorageNoiseDoorSound.Pitch, StorageNoiseDoorSound.Volume);
        CeilingLight* light = m_Objects.CeilingLightAt(Stage1LightNumber::LeftStorage);
        if (light != nullptr)
        {
            EventFeedback::Flicker(*light, StorageNoiseFlicker);
        }
        return;
    }

    m_StorageScareTimer += Application::GetDeltaTime();
    if (m_StorageScareTimer < StorageScareDelaySeconds)
    {
        return;
    }
    m_StorageScarePhase = StorageScarePhase::ShadowShown;

    // 倉庫から離れた場合は出さず、視界の外に人影が突然残るのを防いでいる。
    if (position.x > StorageScareStayMaxX || position.z > StorageScareStayMaxZ)
    {
        m_StorageScarePhase = StorageScarePhase::Finished;
        return;
    }

    ShadowMan* shadow = m_Objects.storageShadow;
    if (shadow == nullptr)
    {
        m_StorageScarePhase = StorageScarePhase::Finished;
        return;
    }
    shadow->SetPosition(StorageShadowPosition.x, StorageShadowPosition.y, StorageShadowPosition.z);
    shadow->SetActive(true);
    shadow->EnableGazeScare(StorageShadowGazeSeconds);
    m_StorageScareNoticeTimer = StorageShadowNoticeSeconds;
    // 影を見たら、異変に対処した数を増やして、照明と音で反応している
    shadow->SetOnObserved([this]()
    {
        Core::Game* currentGame = Core::Game::GetInstance();
        currentGame->RegisterAnomalyHandled();
        m_StorageScarePhase = StorageScarePhase::Finished;
        m_StorageScareNoticeTimer = StorageObservedNoticeSeconds;
        CeilingLight* light =
            m_Objects.CeilingLightAt(Stage1LightNumber::LeftStorage);
        if (light != nullptr)
        {
            EventFeedback::Flicker(*light, StorageObservedFlicker);
        }
        currentGame->PlayAudioCue(SOUND_CUE_SCARE, StorageObservedScareSound.Pitch, StorageObservedScareSound.Volume);
        EventFeedback::HorrorPulse(*currentGame->GetPostProcess(), StorageObservedPulse);
        EventFeedback::Rumble(StorageObservedRumble);
    });
}

// ループ廊下：電力が戻る前に、突き当たりの最後のドアを開けたら、その瞬間に開始地点の奥のドアの裏側へ移して次の周回を始めている
void StageScene::UpdateCorridorLoop(Player& player)
{
    const float deltaTime = Application::GetDeltaTime();
    if (m_LoopCooldown > 0.0f)
    {
        m_LoopCooldown -= deltaTime;
    }
    if (m_LoopNoticeTimer > 0.0f)
    {
        m_LoopNoticeTimer -= deltaTime;
    }

    Core::Game* game = Core::Game::GetInstance();

    // 開始地点の奥のドア：通ってきたプレイヤーが部屋へ入り、ドアに背を向けたら、音を立てて閉めて鍵をかけている
    Door* startDoor = m_Objects.startDoor;
    if (startDoor != nullptr && startDoor->IsOpen())
    {
        const bool walkedIntoRoom = player.GetPosition().z > Stage1StartDoorZ + StartDoorCloseWalkIn;
        const bool facingAway = game->GetCamera()->GetForward().z > StartDoorCloseFacingZ;
        if (walkedIntoRoom && facingAway)
        {
            startDoor->ResetClosed(0);
            startDoor->SetLocked(true);
            game->PlayAudioCueAt(SOUND_CUE_DOOR, startDoor->GetPosition());
        }
    }

    if (startDoor != nullptr)
    {
        // 既存の遮蔽判定を使い、壁越しにライトを向けただけでは閉じない。
        const bool lit = IsLitByFlashlight(player, startDoor->GetPosition());
        startDoor->SetHauntedGap(m_CorridorLoopCount >= StartDoorHauntedGapLoop && !game->IsPowerRestored(), lit);
    }
    if (game->IsPowerRestored() || m_LoopCooldown > 0.0f)
    {
        return;
    }

    // 2周目以降：開始地点の奥のドアが閉まった後、部屋にいる間だけ、ドアの向こうから誰かが叩く（2〜4回続けて叩き、間を空けてまた叩く）。
    // 3周目からは、叩く合間に取っ手がガタガタ揺れる（通ってきたドアの向こうに、何かがついて来ている）。
    if (m_CorridorLoopCount >= StartDoorKnockLoop && startDoor != nullptr && startDoor->IsLocked())
    {
        const Vector3 position = player.GetPosition();
        const bool inStartRoom = position.z < StartRoomMaxZ && std::abs(position.x) < StartRoomHalfWidth;
        if (!inStartRoom)
        {
            // 部屋の外では鳴らさず、戻ってきてから少し間を置いて鳴らしている
            m_StartDoorKnocksLeft = 0;
            m_StartDoorKnockTimer = (std::max)(m_StartDoorKnockTimer, StartDoorKnockReturnDelay);
        }
        else if ((m_StartDoorKnockTimer -= deltaTime) <= 0.0f)
        {
            if (m_StartDoorKnocksLeft <= 0)
            {
                ++m_StartDoorKnockEvents;
                const bool rattle = m_CorridorLoopCount >= StartDoorRattleLoop && (m_StartDoorKnockEvents % 2) == 0;
                if (rattle)
                {
                    // 鍵のかかったドアを調べたときと同じく、取っ手が揺れて音が鳴り、画面がわずかに脈打つ
                    startDoor->Interact(player);
                    m_StartDoorKnockTimer = StartDoorKnockPauseBase + std::fmod(static_cast<float>(m_StartDoorKnockEvents) * StartDoorKnockPauseStep, StartDoorKnockPauseRange);
                }
                else
                {
                    m_StartDoorKnocksLeft = StartDoorKnockMinCount + m_StartDoorKnockEvents % StartDoorKnockCountCycle;
                }
            }
            if (m_StartDoorKnocksLeft > 0)
            {
                // 拳で木の扉を叩くような鈍い音。続けて叩く間は短く、叩き終えたら長く間を空けている
                --m_StartDoorKnocksLeft;
                game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, startDoor->GetPosition(),
                    StartDoorKnockBasePitch + static_cast<float>(m_StartDoorKnocksLeft) * StartDoorKnockPitchStep,
                    StartDoorKnockVolume);
                m_StartDoorKnockTimer = m_StartDoorKnocksLeft > 0
                    ? StartDoorKnockBurstInterval
                    : StartDoorKnockPauseBase + std::fmod(static_cast<float>(m_StartDoorKnockEvents) * StartDoorKnockPauseStep, StartDoorKnockPauseRange);
            }
        }
    }

    Door* endDoor = m_Objects.exitDoor;
    if (endDoor != nullptr && endDoor->IsOpening())
    {
        AdvanceCorridorLoop(player);
    }
}

// 突き当たりの最後のドアが開いていて、プレイヤーがドアの位置を越えて向こう側へ出たか
bool StageScene::IsThroughEndDoor(const Vector3& position) const
{
    const Door* endDoor = m_Objects.exitDoor;
    return endDoor != nullptr &&
        endDoor->IsOpen() &&
        position.x > Stage1EndDoorX &&
        std::abs(position.z - Stage1EndDoorZ) < Stage1EndDoorHalfWidth + EndDoorPassMargin;
}

// ループ廊下を1周進めている：開始地点の奥のドアの裏側へ移し、周回の数に応じて印・扉・照明・ヒューズ・影を変えている
void StageScene::AdvanceCorridorLoop(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    // 最後のドアは、前の周回の開き方で開き始めている。奥のドアも同じ開き方で開けて、続きに見せている。
    const int openingPhase = (std::min)(m_CorridorLoopCount, MaxCorridorLoopPhase);
    ++m_CorridorLoopCount;

    if (m_CorridorLoopCount == 1 && m_Objects.facilityMap != nullptr)
    {
        m_Objects.facilityMap->ShowAlternate();
    }

    // 最後のドアに対する立ち位置と向きを、そのまま奥のドアの裏側へ写している。
    // 最後のドアを通る向き（+x）が奥のドアを通る向き（+z）になるよう、水平に90度回している（(x, z) → (-z, x)）。
    // 同じフレームの後半でPlayerがカメラを更新するため、移った瞬間も1フレームもずれない。
    const Vector3 position = player.GetPosition();
    const float alongDoor = position.x - Stage1EndDoorX;
    const float acrossDoor = position.z - Stage1EndDoorZ;
    player.SetPosition(Vector3(
        Stage1StartDoorX - acrossDoor, position.y, Stage1StartDoorZ + alongDoor));
    Camera* camera = game->GetCamera();
    camera->SetCameraDirection(camera->GetCameraDirection() - DirectX::XM_PIDIV2);

    // 奥のドアを開け始めている。開いた向こうに開始地点が見える。
    Door* startDoor = m_Objects.startDoor;
    if (startDoor != nullptr)
    {
        startDoor->ResetClosed(openingPhase);
        startDoor->StartOpeningQuietly();
        game->PlayAudioCueAt(SOUND_CUE_DOOR, startDoor->GetPosition());
    }
    m_LoopCooldown = CorridorLoopCooldownSeconds;
    m_ProgressHintTimer = 0.0f;
    m_LoopNoticeTimer = CorridorLoopNoticeSeconds;
    // 周回が進むほど、振り返った廊下が大きく伸びる
    m_CorridorStretch.SetLoopCount(m_CorridorLoopCount);

    const int loopPhase = m_CorridorLoopCount < MaxCorridorLoopPhase
        ? m_CorridorLoopCount
        : MaxCorridorLoopPhase;

    UpdateCorridorLoopMarks(loopPhase);

    // 開始地点へ戻すときに、廊下の扉も閉じた状態に戻している。同じ境目をもう一度開けさせることで、
    // 各周回が意図された繰り返しとして感じられるようにしている。
    Door* loopDoor = m_Objects.loopDoor;
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(loopPhase);
    }
    // 最後のドアは閉じた状態に戻し、周回に合わせた開き方に変えている（次の周回でもう一度開けさせる）
    Door* endDoor = m_Objects.exitDoor;
    if (endDoor != nullptr)
    {
        endDoor->ResetClosed(loopPhase);
    }

    ApplyCorridorLoopChanges(loopPhase);
}

// 開始地点の部屋の壁の文字と、周回の数だけの壁の赤い印を出し、周回が進むほど強い演出をしている
void StageScene::UpdateCorridorLoopMarks(int loopPhase)
{
    Core::Game* game = Core::Game::GetInstance();
    // 開始地点の部屋に、この周回の壁の文字をじわりと浮かばせている（戻ってくるたびに、部屋の文字が1つずつ増えていく）
    if (m_CorridorLoopCount >= 1 && m_CorridorLoopCount <= static_cast<int>(m_Objects.lapWritings.size()))
    {
        FlashlightWriting* lapWriting = m_Objects.lapWritings[static_cast<std::size_t>(m_CorridorLoopCount - 1)];
        if (lapWriting != nullptr)
        {
            lapWriting->SetTargetPresence(1.0f);
        }
    }

    // 周回の数だけ、壁の赤い印を出している
    for (int markerIndex = 1; markerIndex <= MaxCorridorLoopPhase; ++markerIndex)
    {
        Wall* marker = m_Objects.loopMarkers[static_cast<std::size_t>(markerIndex - 1)];
        if (marker != nullptr)
        {
            marker->SetVisible(markerIndex <= loopPhase);
            const float intensity =
                LoopMarkerGlowBase + static_cast<float>(markerIndex) * LoopMarkerGlowStep;
            marker->SetAppearance(
                LoopMarkerDiffuse,
                Color(intensity, LoopMarkerGlowGreen, LoopMarkerGlowBlue, 1.0f),
                LoopMarkerShininess);
        }
    }
    const float phase = static_cast<float>(loopPhase);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), PulseSpec{
        CorridorLoopPulseBase.Strength + phase * CorridorLoopPulseStep.Strength,
        CorridorLoopPulseBase.Seconds + phase * CorridorLoopPulseStep.Seconds });
    EventFeedback::Rumble(RumbleSpec{
        CorridorLoopRumbleBase.Frames + loopPhase * CorridorLoopRumbleStep.Frames,
        CorridorLoopRumbleBase.Strength + loopPhase * CorridorLoopRumbleStep.Strength });
}

// 周回ごとに廊下を変えている（1周目：2本目のヒューズ、2周目：西棟と影、3周目以降：照明の配置と背後の人影）
void StageScene::ApplyCorridorLoopChanges(int loopPhase)
{
    Core::Game* game = Core::Game::GetInstance();
    CeilingLight* entranceLight =
        m_Objects.CeilingLightAt(Stage1LightNumber::StartRoom);
    CeilingLight* middleLight =
        m_Objects.CeilingLightAt(Stage1LightNumber::MiddleCorridor);
    CeilingLight* cornerLight =
        m_Objects.CeilingLightAt(Stage1LightNumber::NorthCorner);

    // 1周目：左の倉庫に2本目のヒューズを出している
    if (loopPhase == 1)
    {
        Item* secondFuse = m_Objects.secondFuse;
        if (secondFuse != nullptr && !secondFuse->IsCollected())
        {
            secondFuse->SetActive(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(false, MiddleLoop1EmergencyOffset);
        }
    }
    else if (loopPhase == Stage1FuseProgress::LoopsToUnlockLastFuse)
    {
        // 2周目：3本目のヒューズは西棟の奥にある。右の倉庫に西棟の鍵を出し、鍵で西棟の扉を開けさせている。
        // ヒューズは、西棟の排水バルブで水を抜いたときにStage1WestWingが出している。
        m_WestWing.Activate();

        // 開始地点の部屋の照明が、この周回から故障したように不規則に消えるようになる
        if (entranceLight != nullptr)
        {
            entranceLight->SetFaulted(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, MiddleLoop2EmergencyOffset);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(true, CornerLoop2EmergencyOffset);
        }

        // 中央の廊下に影を出している
        game->RequestAddObject<ShadowMan>(
            [](ShadowMan& shadow)
            {
                shadow.SetPosition(CorridorShadowPosition.x, CorridorShadowPosition.y, CorridorShadowPosition.z);
            });
    }
    // 3周目以降：照明の配置を変えている
    else
    {
        if (entranceLight != nullptr)
        {
            entranceLight->SetEmergencyLight(false, 0.0f);
        }
        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, MiddleIdleEmergencyOffset);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(false, CornerIdleEmergencyOffset);
        }

        // 3周目だけ、壁の文字の警告どおり、プレイヤーの背後に一度だけ人影を出している（見ると照明の連鎖が始まる）。
        if (m_CorridorLoopCount == BehindShadowLoop)
        {
            game->RequestAddObject<ShadowMan>(
                [this](ShadowMan& shadow)
                {
                    shadow.SetPosition(Stage1StartDoorX, FootY, Stage1StartDoorZ - StartDoorShadowBehind);
                    shadow.EnableGazeScare();
                    shadow.SetOnObserved(
                        [this]()
                        {
                            StartScareLightSequence();
                        });
                });
        }
    }
}

// 照明が次々に明滅する演出を始めている（電力が戻った後は起こさない）
void StageScene::StartScareLightSequence()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        return;
    }

    m_ScareLightSequence.Start();

    CeilingLight* entrance =
        m_Objects.CeilingLightAt(Stage1LightNumber::StartRoom);
    CeilingLight* middle =
        m_Objects.CeilingLightAt(Stage1LightNumber::MiddleCorridor);
    CeilingLight* hall =
        m_Objects.CeilingLightAt(Stage1LightNumber::CentralHall);
    CeilingLight* corner =
        m_Objects.CeilingLightAt(Stage1LightNumber::NorthCorner);
    CeilingLight* loopExit =
        m_Objects.CeilingLightAt(Stage1LightNumber::LoopExitCorridor);

    if (entrance != nullptr)
    {
        entrance->SetEmergencyLight(true, StartRoomEmergencyOffset);
    }
    if (middle != nullptr)
    {
        middle->SetEmergencyLight(false, MiddleIdleEmergencyOffset);
    }
    if (hall != nullptr)
    {
        hall->SetEmergencyLight(false, HallIdleEmergencyOffset);
    }
    if (corner != nullptr)
    {
        corner->SetEmergencyLight(false, CornerIdleEmergencyOffset);
    }
    if (loopExit != nullptr)
    {
        loopExit->SetEmergencyLight(false, LoopExitIdleEmergencyOffset);
    }
}

// 照明の演出を進めている：赤い非常灯を、入口から廊下の出口へ向かって1つずつ移している
void StageScene::UpdateScareLightSequence()
{
    const float deltaTime = Application::GetDeltaTime();
    m_ScareLightSequence.UpdateNotice(deltaTime);

    if (!m_ScareLightSequence.IsActive())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        m_ScareLightSequence.Cancel();
        return;
    }

    m_ScareLightSequence.Advance(deltaTime);

    CeilingLight* entrance =
        m_Objects.CeilingLightAt(Stage1LightNumber::StartRoom);
    CeilingLight* middle =
        m_Objects.CeilingLightAt(Stage1LightNumber::MiddleCorridor);
    CeilingLight* hall =
        m_Objects.CeilingLightAt(Stage1LightNumber::CentralHall);
    CeilingLight* corner =
        m_Objects.CeilingLightAt(Stage1LightNumber::NorthCorner);
    CeilingLight* loopExit =
        m_Objects.CeilingLightAt(Stage1LightNumber::LoopExitCorridor);

    switch (m_ScareLightSequence.ConsumePendingBeat())
    {
    case ScareLightSequence::Beat::MiddleCorridor:
        if (entrance != nullptr)
        {
            entrance->SetEmergencyLight(false, StartRoomEmergencyOffset);
        }
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, MiddleChaseEmergencyOffset);
        }
        EventFeedback::HorrorPulse(*game->GetPostProcess(), ScareChasePulse1);
        EventFeedback::Rumble(ScareChaseRumble1);
        break;
    case ScareLightSequence::Beat::CentralHall:
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(false, MiddleChaseEmergencyOffset);
        }
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(true, HallChaseEmergencyOffset);
        }
        EventFeedback::HorrorPulse(*game->GetPostProcess(), ScareChasePulse2);
        EventFeedback::Rumble(ScareChaseRumble2);
        break;
    case ScareLightSequence::Beat::NorthCorner:
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(false, HallChaseEmergencyOffset);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, CornerChaseEmergencyOffset);
        }
        EventFeedback::HorrorPulse(*game->GetPostProcess(), ScareChasePulse3);
        EventFeedback::Rumble(ScareChaseRumble3);
        break;
    case ScareLightSequence::Beat::LoopExit:
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(false, CornerChaseEmergencyOffset);
        }
        if (loopExit != nullptr)
        {
            loopExit->SetEmergencyLight(true, LoopExitChaseEmergencyOffset);
        }
        EventFeedback::HorrorPulse(*game->GetPostProcess(), ScareChasePulse4);
        EventFeedback::Rumble(ScareChaseRumble4);
        break;
    // 最後の段階で、廊下の照明を赤い非常灯に戻している
    case ScareLightSequence::Beat::RestoreEmergency:
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, MiddleFinalEmergencyOffset);
        }
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(true, HallIdleEmergencyOffset);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, CornerIdleEmergencyOffset);
        }
        EventFeedback::Rumble(ScareChaseFinalRumble);
        break;
    case ScareLightSequence::Beat::None:
        break;
    }
}

// ヒューズ取得直後に周囲を暗くして、懐中電灯と背後の足音で注意を引く。
void StageScene::StartFuseScare(int fuseCount)
{
    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    if (player == nullptr || fuseCount <= 0 || game->IsPowerRestored())
    {
        return;
    }
    // 前の演出が終わっていなければ、照明を戻してからやり直している
    EndFuseScare();
    m_FuseScareLevel = (std::min)(fuseCount, Core::GameState::RequiredFuseCount) - 1;
    m_FuseScareTimer = 0.0f;
    m_FuseScareStepsPlayed = 0;

    // 近くの天井の照明を一斉に消している（停電のように。暖かい色の電球の安全地帯は消さない）
    const Vector3 position = player->GetPosition();
    const float radius = FuseScares[m_FuseScareLevel].Radius;
    for (CeilingLight* light : m_Objects.ceilingLights)
    {
        if (light == nullptr || light->IsForcedOff())
        {
            continue;
        }
        Vector3 offset = light->GetPosition() - position;
        offset.y = 0.0f;
        if (offset.LengthSquared() <= radius * radius)
        {
            light->SetForcedOff(true);
            m_FuseScareLights.push_back(light);
            game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, light->GetPosition(),
                FuseScareCutSound.Pitch, FuseScareCutSound.Volume);
        }
    }
    // 懐中電灯もちらつかせている（消えるたびに、電圧が下がったときと同じ脈打ちと振動が出る）
    player->SputterFlashlight(FuseScares[m_FuseScareLevel].SputterSeconds);

    // 足音は、拾った瞬間に向いていた方の真後ろで鳴らす（振り向いても、そこには何もいない）
    Vector3 forward = game->GetCamera()->GetForward();
    forward.y = 0.0f;
    if (forward.LengthSquared() > 0.0f)
    {
        forward.Normalize();
    }
    m_FuseScareBehind = Vector3(position.x - forward.x * FuseScareBehindDistance, FootY,
        position.z - forward.z * FuseScareBehindDistance);
}

void StageScene::UpdateFuseScare(float deltaTime)
{
    if (m_FuseScareTimer < 0.0f)
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    m_FuseScareTimer += deltaTime;
    const auto& scare = FuseScares[m_FuseScareLevel];
    if (m_FuseScareStepsPlayed < scare.StepCount && m_FuseScareTimer >= FuseScareStepTimes[m_FuseScareStepsPlayed])
    {
        game->PlayAudioCueAt(SOUND_CUE_FOOTSTEP, m_FuseScareBehind, FuseScareStepPitch, FuseScareStepVolume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FuseScareStepPulse);
        EventFeedback::Rumble(FuseScareStepRumble);
        ++m_FuseScareStepsPlayed;
    }
    if (m_FuseScareTimer >= scare.BlackoutSeconds)
    {
        EndFuseScare();
    }
}

void StageScene::EndFuseScare()
{
    // 消した照明を、明滅させながら点け直している
    for (CeilingLight* light : m_FuseScareLights)
    {
        light->SetForcedOff(false);
        EventFeedback::Flicker(*light, FuseScareRestoreFlicker);
    }
    m_FuseScareLights.clear();
    m_FuseScareTimer = -1.0f;
}

void StageScene::StartFuseWatcher(int fuseCount)
{
    if (fuseCount < FirstFuseWatcherPickup || Core::Game::GetInstance()->IsPowerRestored())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* watcher = m_Objects.fuseWatcher;
    if (watcher == nullptr)
    {
        return;
    }

    watcher->SetActive(false);
    watcher->SetPosition(
        0.0f,
        FootY,
        fuseCount == FirstFuseWatcherPickup ? FuseWatcherSecondZ : FuseWatcherThirdZ);
    watcher->SetActive(true);
    watcher->EnableGazeScare(fuseCount == FirstFuseWatcherPickup ? FuseWatcherSecondSeconds : FuseWatcherThirdSeconds);
    watcher->SetOnObserved(
        [this]()
        {
            m_FuseWatcherState = FuseWatcherState::Observed;
            m_FuseWatcherNoticeTimer = FuseWatcherObservedNoticeSeconds;

            Core::Game* game = Core::Game::GetInstance();
            game->RegisterAnomalyHandled();
            ShadowMan* activeWatcher =
                m_Objects.fuseWatcher;
            if (activeWatcher != nullptr)
            {
                activeWatcher->SetActive(false);
            }

            CeilingLight* reactionLight =
                m_Objects.CeilingLightAt(Stage1LightNumber::MiddleCorridor);
            if (reactionLight != nullptr)
            {
                EventFeedback::Flicker(*reactionLight, FuseWatcherObservedFlicker);
            }
            EventFeedback::BloomPulse(*game->GetPostProcess(), FuseWatcherObservedBloom);
            EventFeedback::Rumble(FuseWatcherObservedRumble);
        });

    m_FuseWatcherState = FuseWatcherState::Watching;
    m_FuseWatcherNoticeTimer = fuseCount == FirstFuseWatcherPickup ? FuseWatcherSecondSeconds : FuseWatcherThirdSeconds;
    EventFeedback::HorrorPulse(*game->GetPostProcess(),
        fuseCount == FirstFuseWatcherPickup ? FuseWatcherSecondPulse : FuseWatcherThirdPulse);
}

// 電力が戻る演出：戻った瞬間に照明の演出と周回の印を消し、0.38・1.15・2.25秒に画面の光と振動を出している
void StageScene::UpdatePowerRestoreSequence()
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();
    const bool powerRestored = game->IsPowerRestored();

    if (m_PowerSequence.ObservePowerState(powerRestored))
    {
        m_ProgressHintTimer = 0.0f;

        // ここから先の画面・照明・音の演出は、電力が戻る演出の側でまとめて管理している。
        m_ScareLightSequence.Cancel();
        m_ScareLightSequence.ClearNotice();

        for (Wall* marker : m_Objects.loopMarkers)
        {
            marker->SetVisible(false);
        }
    }

    if (!powerRestored || !m_PowerSequence.IsRestoreActive())
    {
        return;
    }

    m_PowerSequence.AdvanceRestore(deltaTime);

    switch (m_PowerSequence.ConsumeRestoreBeat())
    {
    case StagePowerSequence::RestoreBeat::Flash:
        EventFeedback::BloomPulse(*game->GetPostProcess(), PowerRestoreFlashBloom);
        EventFeedback::Rumble(PowerRestoreFlashRumble);
        break;
    case StagePowerSequence::RestoreBeat::Jolt:
        EventFeedback::HorrorPulse(*game->GetPostProcess(), PowerRestoreJoltPulse);
        EventFeedback::Rumble(PowerRestoreJoltRumble);
        break;
    case StagePowerSequence::RestoreBeat::Afterglow:
        EventFeedback::BloomPulse(*game->GetPostProcess(), PowerRestoreAfterglowBloom);
        EventFeedback::Rumble(PowerRestoreAfterglowRumble);
        break;
    case StagePowerSequence::RestoreBeat::None:
        break;
    }
}

// 出口へ送電する演出：送電盤を操作したら出口側の照明を消し、時間をずらして順に点け直している
void StageScene::UpdateExitPowerSequence()
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();
    FuseBox* panel = m_Objects.exitPowerPanel;
    if (panel == nullptr || !panel->IsActivated())
    {
        return;
    }

    if (m_PowerSequence.BeginExitIfNeeded())
    {
        m_ProgressHintTimer = 0.0f;

        CeilingLight* corner = m_Objects.CeilingLightAt(Stage1LightNumber::LoopExitCorridor);
        CeilingLight* exitLight = m_Objects.CeilingLightAt(Stage1LightNumber::NorthCorner);
        if (corner != nullptr) corner->SetForcedOff(true);
        if (exitLight != nullptr) exitLight->SetForcedOff(true);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), ExitPowerCutPulse);
        return;
    }

    if (m_PowerSequence.IsExitComplete())
    {
        return;
    }

    m_PowerSequence.AdvanceExit(deltaTime);
    switch (m_PowerSequence.ConsumeExitBeat())
    {
    case StagePowerSequence::ExitBeat::RestoreCorridor:
    {
        CeilingLight* corner = m_Objects.CeilingLightAt(Stage1LightNumber::LoopExitCorridor);
        if (corner != nullptr)
        {
            corner->SetForcedOff(false);
            corner->SetEmergencyLight(false, 0.0f);
            EventFeedback::Flicker(*corner, ExitPowerFirstFlicker);
        }
        EventFeedback::BloomPulse(*game->GetPostProcess(), ExitPowerFirstBloom);
        EventFeedback::Rumble(ExitPowerFirstRumble);
        break;
    }
    case StagePowerSequence::ExitBeat::RestoreExit:
    {
        CeilingLight* exitLight = m_Objects.CeilingLightAt(Stage1LightNumber::NorthCorner);
        if (exitLight != nullptr)
        {
            exitLight->SetForcedOff(false);
            exitLight->SetEmergencyLight(false, 0.0f);
            EventFeedback::Flicker(*exitLight, ExitPowerSecondFlicker);
        }
        EventFeedback::BloomPulse(*game->GetPostProcess(), ExitPowerSecondBloom);
        EventFeedback::Rumble(ExitPowerSecondRumble);
        break;
    }
    case StagePowerSequence::ExitBeat::Finish:
        EventFeedback::BloomPulse(*game->GetPostProcess(), ExitPowerFinalBloom);
        EventFeedback::Rumble(ExitPowerFinalRumble);
        break;
    case StagePowerSequence::ExitBeat::None:
        break;
    }
}

// 出口の前兆：送電が終わった後、出口の近く（z>215・x>28）まで来たら、背後の照明を消して影を出している
void StageScene::UpdateExitOmen(Player& player)
{
    const float deltaTime = Application::GetDeltaTime();
    m_ExitOmenSequence.Update(deltaTime);
    if (m_ExitOmenSequence.IsTriggered())
    {
        Core::Game* game = Core::Game::GetInstance();
        switch (m_ExitOmenSequence.ConsumePendingBeat())
        {
        case ExitOmenSequence::Beat::CutLightBehind:
        {
            CeilingLight* lightBehind =
                m_Objects.CeilingLightAt(Stage1LightNumber::NorthCorner);
            if (lightBehind != nullptr)
            {
                lightBehind->SetForcedOff(true);
            }
            EventFeedback::HorrorPulse(*game->GetPostProcess(), ExitOmenLightOffPulse);
            EventFeedback::Rumble(ExitOmenLightOffRumble);
            break;
        }
        case ExitOmenSequence::Beat::FlickerExit:
        {
            CeilingLight* exitLight =
                m_Objects.CeilingLightAt(Stage1LightNumber::LoopExitCorridor);
            if (exitLight != nullptr)
            {
                exitLight->SetFaulted(true);
                EventFeedback::Flicker(*exitLight, ExitOmenFaultFlicker);
            }
            EventFeedback::BloomPulse(*game->GetPostProcess(), ExitOmenFaultBloom);
            break;
        }
        case ExitOmenSequence::Beat::None:
            break;
        }
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 playerPosition = player.GetPosition();
    if (!m_PowerSequence.IsExitComplete() ||
        playerPosition.z < ExitOmenMinZ ||
        playerPosition.x < ExitOmenMinX)
    {
        return;
    }

    m_ExitOmenSequence.Start();

    ShadowMan* shadow =
        m_Objects.exitOmen;
    if (shadow != nullptr)
    {
        shadow->SetActive(true);
        shadow->EnableGazeScare(ExitOmenGazeSeconds);
    }

    for (int lightNumber : { Stage1LightNumber::LoopExitCorridor, Stage1LightNumber::NorthCorner })
    {
        CeilingLight* light = m_Objects.CeilingLightAt(lightNumber);
        if (light != nullptr)
        {
            EventFeedback::Flicker(*light, ExitOmenFlicker);
        }
    }

    EventFeedback::HorrorPulse(*game->GetPostProcess(), ExitOmenPulse);
    EventFeedback::BloomPulse(*game->GetPostProcess(), ExitOmenBloom);
    EventFeedback::Rumble(ExitOmenRumble);
}

// ----------------------------------------------------------------------------
// 姿の見えない物音
// 台本の演出（照明の連鎖・倉庫の気配・出口の前兆など）の最中や、監視映像を見ている間、暖かい灯りの下（光の安全地帯）にいる間は鳴らしていない。
// 演出の驚きを物音で打ち消さず、何も起きていない静かな時間にだけ不安を足すためである。
// ----------------------------------------------------------------------------
bool StageScene::IsAmbientSoundAllowed() const
{
    const Player* player = m_Objects.player;
    const ExitTrigger* exit = m_Objects.exitTrigger;
    const bool powerRestoring =
        m_PowerSequence.IsRestoreActive() && m_PowerSequence.GetRestoreTimer() < PowerRestoreQuietSeconds;
    return player != nullptr &&
        player->CanControl() &&
        !m_Surveillance.IsViewing() &&
        !m_Surveillance.IsCaughtActive() &&
        !m_ScareLightSequence.IsActive() &&
        !m_HiddenRoom.IsTrapped() &&
        m_EntranceEventTimer < 0.0f &&
        m_StorageScarePhase != StorageScarePhase::NoiseHeard &&
        m_FuseWatcherState != FuseWatcherState::Watching &&
        m_ExitOmenSequence.GetTimer() <= 0.0f &&
        !m_CeilingStalker.IsActive() &&
        !powerRestoring &&
        m_SafeLights.GetCalm() < SafeLightQuietCalm &&
        (exit == nullptr || !exit->IsEscaping());
}

void StageScene::UpdateCorridorStretch(const Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    Camera* camera = game->GetCamera();
    const Vector3 position = player.GetPosition();
    if (m_CorridorStretch.Update(deltaTime, position, camera->GetForward(), IsAmbientSoundAllowed()))
    {
        game->PlayAudioCueAt(SOUND_CUE_DOOR, position - Vector3(0.0f, 0.0f, CorridorStretchSoundDistance),
            CorridorStretchSound.Pitch, CorridorStretchSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), CorridorStretchPulse);
        EventFeedback::Rumble(CorridorStretchRumble);
    }
    camera->SetWorldStretchZ(m_CorridorStretch.GetStretchZ());
}

void StageScene::SetupBreathingWall()
{
    m_BreathingWall = nullptr;
    for (const auto& [name, wall] : m_Objects.layoutProps)
    {
        if (name == BreathingWallName && wall != nullptr)
        {
            wall->EnableBreathing(BreathingWallPositiveZFace, BreathingWallColumns, BreathingWallRows, BreathingWallEdgeWidth);
            m_BreathingWall = wall;
            break;
        }
    }
}

void StageScene::UpdateBreathingWall(const Player& player, float deltaTime)
{
    if (m_BreathingWall == nullptr)
    {
        return;
    }
    // 1周するまでは普通の壁。周回が進むほど大きく膨らむ（急に動き出さないよう、少しずつ近づけている）
    const float target =
        m_CorridorLoopCount <= 0 ? 0.0f
        : m_CorridorLoopCount == 1 ? BreathingWallFirstLoopAmplitude
        : m_CorridorLoopCount == 2 ? BreathingWallSecondLoopAmplitude
        : BreathingWallLaterLoopAmplitude;
    const float step = BreathingWallAmplitudeRate * deltaTime;
    m_BreathingWallAmplitude = std::clamp(target, m_BreathingWallAmplitude - step, m_BreathingWallAmplitude + step);
    m_BreathingWallTime += deltaTime;
    m_BreathingWall->SetBreathing(m_BreathingWallTime, m_BreathingWallAmplitude);

    // 息を吐き始める所（呼吸の1回の始まり）ごとに、壁の中から息の音を1回鳴らしている
    const int cycle = static_cast<int>(m_BreathingWallTime / SIGNAL_LOST_BREATHING_WALL_CYCLE_SECONDS);
    const bool newCycle = cycle != m_BreathingWallCycle;
    m_BreathingWallCycle = cycle;
    if (!newCycle || m_BreathingWallAmplitude <= 0.0f || !IsAmbientSoundAllowed())
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    const Vector3 playerPosition = player.GetPosition();
    const Vector3 soundPosition(
        std::clamp(playerPosition.x, BreathingWallSoundMinX, BreathingWallSoundMaxX),
        BreathingWallSoundY, BreathingWallSoundZ);
    Vector3 toSound = soundPosition - game->GetCamera()->GetPosition();
    const float distance = toSound.Length();
    if (distance > BreathingWallSoundDistance)
    {
        return;
    }
    toSound /= (std::max)(distance, 0.001f);
    const bool lit = player.IsFlashlightOn() && game->GetCamera()->GetForward().Dot(toSound) > BreathingWallLitFacing;
    if (!lit)
    {
        game->PlayAudioCueAt(SOUND_CUE_BREATH, soundPosition, BreathingWallSound.Pitch, BreathingWallSound.Volume);
    }
}

void StageScene::UpdateCeilingStalker(const Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        m_CeilingStalkerWait += deltaTime;
    }
    // 出口へ送電した後や出口の前兆が始まった後は、北の角の照明をそちらの演出が使うので、足音では触らない
    const FuseBox* exitPanel = m_Objects.exitPowerPanel;
    const bool exitLightsBusy = (exitPanel != nullptr && exitPanel->IsActivated()) || m_ExitOmenSequence.IsTriggered();

    if (!m_CeilingStalker.IsActive() && m_CeilingStalkerWait >= CeilingStalkerDelaySeconds && IsAmbientSoundAllowed())
    {
        std::vector<Stage1CeilingStalker::Fixture> fixtures;
        for (int number : CeilingStalkerLights)
        {
            const CeilingLight* light = m_Objects.CeilingLightAt(number);
            if (light == nullptr || light->IsForcedOff() ||
                (number == Stage1LightNumber::NorthCorner && exitLightsBusy))
            {
                continue;
            }
            fixtures.push_back({ number, light->GetPosition() });
        }
        m_CeilingStalker.TryStart(player.GetPosition(), game->GetCamera()->GetForward(), fixtures);
    }

    m_CeilingStalkerCues.clear();
    m_CeilingStalker.Update(deltaTime, player.GetPosition(), m_CeilingStalkerCues);
    for (const Stage1CeilingStalker::Cue& cue : m_CeilingStalkerCues)
    {
        CeilingLight* light = cue.LightNumber > 0 ? m_Objects.CeilingLightAt(cue.LightNumber) : nullptr;
        switch (cue.Type)
        {
        case Stage1CeilingStalker::CueType::Step:
            // 人より重く低い足音。天井裏なので、壁がなくてもこもらせている
            game->PlayAudioCueAt(SOUND_CUE_FOOTSTEP, cue.Position, cue.Pitch,
                Stage1CeilingStalker::StepVolume, Stage1CeilingStalker::StepMinimumOcclusion);
            break;
        case Stage1CeilingStalker::CueType::LightOff:
            if (light != nullptr)
            {
                light->SetForcedOff(true);
            }
            game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, cue.Position,
                CeilingStalkerLightOffSound.Pitch, CeilingStalkerLightOffSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), CeilingStalkerLightOffPulse);
            EventFeedback::Rumble(CeilingStalkerLightOffRumble);
            break;
        case Stage1CeilingStalker::CueType::Caught:
            game->PlayAudioCueAt(SOUND_CUE_SCARE, cue.Position,
                CeilingStalkerCaughtSound.Pitch, CeilingStalkerCaughtSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), CeilingStalkerCaughtPulse);
            EventFeedback::Rumble(CeilingStalkerCaughtRumble);
            break;
        case Stage1CeilingStalker::CueType::LightOn:
            // 足音の後に出口の演出が北の角の照明を使い始めていたら、点け直さずに任せる
            if (light != nullptr && !(cue.LightNumber == Stage1LightNumber::NorthCorner && exitLightsBusy))
            {
                light->SetForcedOff(false);
                EventFeedback::Flicker(*light, CeilingStalkerRestoreFlicker);
            }
            break;
        }
    }
}

// 物音の予定を進め、鳴らす時間が来た音を立体音響で鳴らしている
void StageScene::UpdateAmbientSounds()
{
    Core::Game* game = Core::Game::GetInstance();
    const Camera* camera = game->GetCamera();

    m_AmbientCues.clear();
    m_AmbientSounds.Update(
        Application::GetDeltaTime(),
        IsAmbientSoundAllowed(),
        game->IsPowerRestored(),
        camera->GetPosition(),
        camera->GetForward(),
        m_AmbientCues);
    for (const AmbientSoundCue& cue : m_AmbientCues)
    {
        game->PlayAudioCueAt(
            cue.Label, cue.Position, cue.Pitch, cue.Volume, cue.MinimumOcclusion);
    }
}

// ----------------------------------------------------------------------------
// 懐中電灯で照らすと浮かぶ壁の文字
// ループ廊下の「ふりかえるな」は、文字に背を向けた隙に「ふりかえったな」へ書き換わる（横を見ただけでは変わらない）。
// それを読んだ瞬間、背後の天井裏を何かが歩いていき、振り返らせる流れを作っている。
// ----------------------------------------------------------------------------
void StageScene::UpdateWallWritings(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    const Camera* camera = game->GetCamera();
    const bool changedWritingRead = m_WallWritings.Update(
        Application::GetDeltaTime(),
        camera->GetPosition(),
        camera->GetForward(),
        player.IsFlashlightOn(),
        game->IsPowerRestored());
    // 読んだ数は、リザルト画面の「壁の文字」に出している。
    game->SetWallWritingsRead(m_WallWritings.GetReadCount());
    UpdateDarkCorridorWriting(player);
    if (!changedWritingRead)
    {
        return;
    }

    m_AmbientSounds.StartCeilingStepsNow(
        game->IsPowerRestored(), camera->GetPosition(), camera->GetForward());
    EventFeedback::HorrorPulse(*game->GetPostProcess(), ChangedWritingPulse);
    EventFeedback::Rumble(ChangedWritingRumble);
}

// 光を消して文字を探す間に、何かが背後へ近づく。進行に必要な手がかりには数えない。
void StageScene::UpdateDarkCorridorWriting(const Player& player)
{
    FlashlightWriting* writing = m_Objects.darkCorridorWriting;
    if (writing == nullptr)
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    const bool flashlightOn = player.IsFlashlightOn();
    if (flashlightOn || m_CorridorLoopCount < DarkWritingFirstCompletedLoop || game->IsPowerRestored())
    {
        // 電池のちらつきでも読める状態になるのを避け、点灯したらその場で文字を消す。
        writing->SetPresenceImmediately(0.0f);
        m_DarkWritingReadTime = 0.0f;
        return;
    }

    writing->SetTargetPresence(1.0f);
    const Camera* camera = game->GetCamera();
    if (m_DarkWritingTriggered || !IsAmbientSoundAllowed() ||
        !writing->IsBeingRead(camera->GetPosition(), camera->GetForward(), flashlightOn))
    {
        m_DarkWritingReadTime = 0.0f;
        return;
    }

    m_DarkWritingReadTime += Application::GetDeltaTime();
    if (m_DarkWritingReadTime >= DarkWritingReadSeconds)
    {
        m_DarkWritingTriggered = true;
        m_AmbientSounds.StartCeilingStepsNow(false, camera->GetPosition(), camera->GetForward());
        EventFeedback::HorrorPulse(*game->GetPostProcess(), DarkWritingPulse);
    }
}

// 今の危険度（0〜1）を求めている
float StageScene::ComputeThreatRate(const Player& player) const
{
    // 出ている影のうち、一番近いものほど危険としている（2面の足音の影と同じ距離の感じ方）。
    const ShadowMan* shadows[] =
    {
        m_Objects.fuseWatcher,
        m_Objects.storageShadow,
        m_Objects.evidenceShadow,
        m_Objects.exitOmen,
        m_Objects.hiddenRoom.shadow,
        m_Objects.archiveStalker,
    };
    float threatRate = 0.0f;
    for (const ShadowMan* shadow : shadows)
    {
        if (shadow == nullptr || !shadow->IsActive())
        {
            continue;
        }
        Vector3 toShadow = shadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        const float danger = MathUtility::Proximity(toShadow.Length(), HeartbeatNearDistance, HeartbeatRange);
        threatRate = (std::max)(threatRate, danger);
    }
    // 天井裏の足音が迫っている間と、追いつかれた直後も、近さに合わせて心拍を速めている
    threatRate = (std::max)(threatRate, m_CeilingStalker.GetThreat());
    // 隠し部屋に閉じ込められている間は、影が出ていなくても小さく心拍が聞こえるようにしている（息は荒くしない）。
    if (m_HiddenRoom.IsTrapped())
    {
        threatRate = (std::max)(threatRate, HiddenRoomHeartbeat);
    }
    // 暖かい灯りの下（光の安全地帯）にいる間は、心拍と息が静まっていく
    return threatRate * (1.0f - m_SafeLights.GetCalm());
}

void StageScene::UpdatePresenceWarp(const Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    std::array<Vector4, Graphics::FullScreenQuad::PresenceWarpCount> warps{};
    // 監視映像を見ている間は、画面に施設の景色が映っていないので出さない
    if (m_Surveillance.IsViewing())
    {
        game->GetPostProcess()->SetPresenceWarps(warps);
        return;
    }
    const ShadowMan* shadows[] =
    {
        m_Objects.fuseWatcher,
        m_Objects.storageShadow,
        m_Objects.evidenceShadow,
        m_Objects.exitOmen,
        m_Objects.hiddenRoom.shadow,
        m_Objects.archiveStalker,
    };
    Matrix view;
    Matrix projection;
    game->GetCamera()->GetMainMatrices(view, projection);
    const Matrix viewProjection = view * projection;
    const Vector3 cameraRight = Vector3::TransformNormal(Vector3::UnitX, view.Invert());
    const float aspect = static_cast<float>(Application::GetWidth()) / static_cast<float>(Application::GetHeight());
    // 暖かい灯りの下（光の安全地帯）にいる間は、ゆがみも静まっていく
    const float calm = 1.0f - m_SafeLights.GetCalm();

    const auto toScreen = [&viewProjection](const Vector3& position, Vector2& uv)
    {
        const Vector4 clip = Vector4::Transform(Vector4(position.x, position.y, position.z, 1.0f), viewProjection);
        if (clip.w <= PresenceWarpMinClipW)
        {
            return false;
        }
        uv = Vector2(clip.x / clip.w * 0.5f + 0.5f, -clip.y / clip.w * 0.5f + 0.5f);
        return true;
    };

    for (const ShadowMan* shadow : shadows)
    {
        if (shadow == nullptr || !shadow->IsActive())
        {
            continue;
        }
        Vector3 toShadow = shadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        const float strength = MathUtility::Proximity(toShadow.Length(), PresenceWarpNearDistance, PresenceWarpRange) * calm;
        if (strength <= 0.0f)
        {
            continue;
        }
        // 影の胸の位置と、そこから体の半径だけ横へずらした位置を画面へ写し、画面の上での位置と大きさにしている
        const Vector3 chest = shadow->GetPosition() + Vector3(0.0f, ShadowFigureGeometry::ChestHeight, 0.0f);
        Vector2 center;
        Vector2 side;
        if (!toScreen(chest, center) || !toScreen(chest + cameraRight * PresenceWarpWorldRadius, side) ||
            center.x < -PresenceWarpScreenMargin || center.x > 1.0f + PresenceWarpScreenMargin ||
            center.y < -PresenceWarpScreenMargin || center.y > 1.0f + PresenceWarpScreenMargin)
        {
            continue;
        }
        const float radius = std::clamp(((side - center) * Vector2(aspect, 1.0f)).Length(),
            PresenceWarpMinScreenRadius, PresenceWarpMaxScreenRadius);
        // 強い方から2つまで残している
        Vector4 warp(center.x, center.y, radius, strength);
        for (Vector4& slot : warps)
        {
            if (warp.w > slot.w)
            {
                std::swap(slot, warp);
            }
        }
    }
    game->GetPostProcess()->SetPresenceWarps(warps);
}

void StageScene::UpdateTensionPulse(const Player& player, float deltaTime)
{
    // 1面には隠れる場所がないため、隠れている扱いにはしていない。
    const float threatRate = ComputeThreatRate(player);
    // 撮影・計測では両効果を省く。影の位置を画面に投影する計算も不要になる。
    if (!Tools::CaptureMode::IsActive())
    {
        UpdatePresenceWarp(player);
        // 影が近いほど残像を強くする。監視映像を開いている間は強さを0へ戻す。
        Core::Game::GetInstance()->GetPostProcess()->SetEcho(m_Surveillance.IsViewing() ? 0.0f :
            MathUtility::SmoothStep(EchoThreatStart, EchoThreatFull, threatRate));
    }
    const TensionPulse::Output pulse = m_TensionPulse.Update(deltaTime, threatRate, false);
    PlayTensionPulse(pulse);
    // 緊張が高いほど、画面の端から視界が狭まり、心拍に合わせて脈打つ
    ShowFearVision(m_TensionPulse, pulse);
    // 影が近いほど、携帯ラジオの雑音が大きくなる（監視映像を見ている間は鳴らさない）。
    // 無線が届いている間と録音を流している間も、その強さまでは鳴らしている
    m_RadioStatic.Update(deltaTime, (std::max)(threatRate, m_Transmissions.GetStaticLevel()), !m_Surveillance.IsViewing());
}

// ----------------------------------------------------------------------------
// ボイスレコーダーの録音と、1面の途中で届く無線
// 字幕の順番と無線の嘘の進み具合は Stage1Transmissions に任せ、ここでは調べられた録音機と今の状態を渡している。
// ----------------------------------------------------------------------------
void StageScene::UpdateTransmissions(const Player& player, float deltaTime, bool archiveStalkerLit)
{
    Core::Game* game = Core::Game::GetInstance();
    for (VoiceRecorder* recorder : m_Objects.recorders)
    {
        if (recorder != nullptr && recorder->TakePlayRequest())
        {
            m_Transmissions.PlayRecording(recorder->GetRecordingIndex());
            // 聞いた数は、リザルト画面の「録音」に出している
            game->RegisterRecordingPlayed();
        }
    }

    Stage1Transmissions::FrameInput input;
    input.DeltaTime = deltaTime;
    input.PlayerPosition = player.GetPosition();
    input.FuseCount = game->GetItemCount();
    input.PowerRestored = game->IsPowerRestored();
    // 閉じ込められている間・照明が次々に明滅する演出の間・監視映像を見ている間は、新しい無線を予約しない
    input.RadioAllowed = !m_HiddenRoom.IsTrapped() && !m_ScareLightSequence.IsActive() && !m_Surveillance.IsViewing();
    input.ArchiveStalkerLit = archiveStalkerLit;
    m_Transmissions.Update(input);
}

// 点が懐中電灯で照らされているかを返している。
// 光の円（外側29度）の縁は暗いため、視線の中心から22度以内を「照らしている」としている。
bool StageScene::IsLitByFlashlight(const Player& player, const Vector3& target) const
{
    if (!player.IsFlashlightOn())
    {
        return false;
    }

    Camera* camera = Core::Game::GetInstance()->GetCamera();
    const Vector3 eye = camera->GetPosition();
    Vector3 toTarget = target - eye;
    const float distance = toTarget.Length();
    // 懐中電灯の光が届く距離（約275）より少し手前までにしている
    if (distance < MinimumLitDistance || distance > FlashlightLitDistance)
    {
        return false;
    }
    toTarget /= distance;
    Vector3 forward = camera->GetForward();
    forward.Normalize();
    if (forward.Dot(toTarget) < FlashlightLitFacing)
    {
        return false;
    }

    // 間に壁・棚・閉じた扉があれば、光は届いていない。
    // 線分の終点は目標の少し手前にし、目標のすぐ近くの棚の面に当たって照らされない扱いになるのを防いでいる。
    const Vector3 end = eye + toTarget * (distance - FlashlightLitEndMargin);
    float hitDistance = 0.0f;
    for (const Wall* wall : m_StalkerWalls)
    {
        if (wall->IntersectsInteractionSegment(eye, end, hitDistance))
        {
            return false;
        }
    }
    for (const Door* door : m_StalkerDoors)
    {
        if (door->BlocksSoundSegment(eye, end))
        {
            return false;
        }
    }
    return true;
}

// 書類保管室の「照らすと止まる影」を1フレーム進めている。
// 動きのルールはStage1LightStalkerが決め、ここでは照らされているかの判定と、表示・音・電池の変化を行っている。
bool StageScene::UpdateArchiveStalker(Player& player, float deltaTime)
{
    ShadowMan* shadow = m_Objects.archiveStalker;
    if (shadow == nullptr)
    {
        return false;
    }
    Core::Game* game = Core::Game::GetInstance();
    m_ArchiveStalkerNoticeTimer = (std::max)(0.0f, m_ArchiveStalkerNoticeTimer - deltaTime);

    Stage1LightStalker::FrameInput input;
    input.PlayerPosition = player.GetPosition();
    // 電力が戻った後と、隠し部屋に閉じ込められている間は出さない
    input.Enabled = !game->IsPowerRestored() && !m_HiddenRoom.IsTrapped();
    // 視線判定と同じ胸の位置を使い、影の見た目に合わせる。
    input.Lit = m_ArchiveStalker.IsActive() &&
        IsLitByFlashlight(player, shadow->GetPosition() + Vector3(0.0f, ShadowFigureGeometry::ChestHeight, 0.0f));
    input.DeltaTime = deltaTime;

    // 影は棚・壁・閉じた扉を通り抜けず、押し戻されて沿うように回り込む
    const Stage1LightStalker::FrameResult result = m_ArchiveStalker.Update(
        input,
        [this](Vector3& position, float radius)
        {
            for (const Wall* wall : m_StalkerWalls)
            {
                wall->ResolveCollision(position, radius);
            }
            for (const Door* door : m_StalkerDoors)
            {
                door->ResolveCollision(position, radius);
            }
        });

    // 影の位置と表示を、ルールの結果に合わせている
    if (result.Visible)
    {
        shadow->SetPosition(result.Position.x, result.Position.y, result.Position.z);
    }
    if (shadow->IsActive() != result.Visible)
    {
        shadow->SetActive(result.Visible);
    }

    // 現れたとき：棚の奥から低い音を鳴らし、気配のする方へ視線を誘う。
    if (result.Appeared)
    {
        game->PlayAudioCueAt(
            SOUND_CUE_PIPE_KNOCK, result.Position + Vector3(0.0f, ArchiveStalkerKnockHeight, 0.0f),
            ArchiveStalkerKnockSound.Pitch, ArchiveStalkerKnockSound.Volume);
        m_ArchiveStalkerNotice = result.FirstAppearance
            ? "棚の奥で、何かが動いた"
            : "また棚の間に何かいる";
        m_ArchiveStalkerNoticeTimer = result.FirstAppearance ? ArchiveStalkerFirstNoticeSeconds : ArchiveStalkerNoticeSeconds;
    }

    // 光が外れて近づいている間は、影の足元から重い足音を鳴らしている（立体音響で、見えなくても方向が分かる）
    if (result.Step)
    {
        game->PlayAudioCueAt(
            SOUND_CUE_FOOTSTEP, result.Position + Vector3(0.0f, ArchiveStalkerStepHeight, 0.0f),
            ArchiveStalkerStepSound.Pitch, ArchiveStalkerStepSound.Volume);
    }

    // 触れられたとき：懐中電灯の電池を奪っている（5%は残し、完全には空にしない）
    if (result.Caught)
    {
        constexpr float BatteryPenalty = 30.0f;
        const float taken = (std::min)(
            BatteryPenalty, (std::max)(0.0f, player.GetBattery() - ArchiveStalkerBatteryFloor));
        player.AddBattery(-taken);
        game->PlayAudioCue(SOUND_CUE_SCARE, ArchiveStalkerCaughtSound.Pitch, ArchiveStalkerCaughtSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), ArchiveStalkerCaughtPulse);
        EventFeedback::Rumble(ArchiveStalkerCaughtRumble);
        m_ArchiveStalkerNotice = "影に触れられた 電池を奪われた";
        m_ArchiveStalkerNoticeTimer = ArchiveStalkerCaughtNoticeSeconds;
    }
    return result.Visible && input.Lit;
}

// ----------------------------------------------------------------------------
// 誰かの濡れた足跡
// 西棟の水から初めて乾いた床へ出ると、少し遅れて同じ水から裸足の濡れた足跡が一歩ずつ現れ、
// プレイヤーの足跡のすぐ横に並んで近づいてくる（姿は見えず、足跡と水音だけ。1回のプレイで1回だけ）。
// 一番新しい足跡を懐中電灯で照らすとそこで止まり、照らさずにすぐ近くまで来させると、すぐ後ろで息づかいが聞こえる。
// ----------------------------------------------------------------------------
void StageScene::UpdateFootprintFollower(Player& player)
{
    Ground* ground = m_Objects.ground;
    if (ground == nullptr)
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    const float deltaTime = Application::GetDeltaTime();

    // 水から出た瞬間は毎回知らせが来るので、西棟の水から出たときだけ使っている（ほかの水たまりは足跡が残るだけ）
    Vector2 waterExit;
    if (ground->ConsumeWaterExit(waterExit) && !m_FootprintFollowerUsed &&
        Stage1WestWing::IsInside(Vector3(waterExit.x, player.GetPosition().y, waterExit.y)))
    {
        m_FootprintFollower.Start(waterExit);
        m_FootprintFollowerUsed = true;
    }

    const Camera* camera = game->GetCamera();
    FootprintFollower::Step step;
    const FootprintFollower::Event followerEvent = m_FootprintFollower.Update(
        deltaTime, player.GetPosition(), camera->GetPosition(), camera->GetForward(),
        player.IsFlashlightOn(), step);
    if (followerEvent == FootprintFollower::Event::Step)
    {
        ground->AddFootprint(step.Position, step.Yaw, step.Side, true, 1.0f);
        game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, Vector3(step.Position.x, FootY, step.Position.y),
            FollowerWaterStepSound.Pitch, FollowerWaterStepSound.Volume);
    }
    else if (followerEvent == FootprintFollower::Event::Lit)
    {
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FollowerLitPulse);
        EventFeedback::Rumble(FollowerLitRumble);
    }
    else if (followerEvent == FootprintFollower::Event::Reached)
    {
        const Vector3 playerPosition = player.GetPosition();
        Vector3 behind = playerPosition - camera->GetForward() * FollowerBreathBehindDistance;
        behind.y = playerPosition.y;
        game->PlayAudioCueAt(SOUND_CUE_BREATH, behind, FollowerBreathSound.Pitch, FollowerBreathSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FollowerReachedPulse);
        EventFeedback::Rumble(FollowerReachedRumble);
    }
}
