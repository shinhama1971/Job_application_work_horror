// ============================================================================
// ファイルの役割: 1面の演出（StageSceneEvents・StageSceneLoop・StageSceneScares）で使う、場所・秒数・強さの値。
// 3つのファイルで同じ値を使うので、ここにまとめている（値を変えるときはここだけを直す）。
// ============================================================================

#pragma once

#include "EventFeedback.h"
#include "MathUtility.h"
#include "ShadowFigureGeometry.h"
#include "Stage1FuseProgress.h"
#include "Stage1BreathingGroup.h"
#include "Stage1LightNumber.h"

#include <SimpleMath.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace Stage1EventSettings
{
    using DirectX::SimpleMath::Vector2;
    using DirectX::SimpleMath::Vector3;
    using DirectX::SimpleMath::Color;

    // 懐中電灯で照らしているかを調べるとき、これより近い物は向きを計算しない（0で割らないため）
    constexpr float MinimumLitDistance = 0.001f;

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

    // --- 呼吸する壁（配置ファイルの壁の名前、膨らませる面の向き、組）
    // 廊下：南北と東西の区間で廊下を向く8枚（1周した後から）
    // 中央ホール：配電盤のある部屋を囲む4枚（3周目から）
    // 建物の外周：中央ホールと廊下を外から囲む長い壁（3本目のヒューズを拾った後から。戻ってくると建物全体が息をしている）
    struct BreathingWallFace
    {
        const char* Name;
        DirectX::SimpleMath::Vector3 Normal;
        Stage1BreathingGroup Group;
    };
    constexpr BreathingWallFace BreathingWallFaces[] =
    {
        { "Wall11", { 1.0f, 0.0f, 0.0f }, Stage1BreathingGroup::Corridor },
        { "Wall12", { 1.0f, 0.0f, 0.0f }, Stage1BreathingGroup::Corridor },
        { "Wall13", { -1.0f, 0.0f, 0.0f }, Stage1BreathingGroup::Corridor },
        { "Wall13North", { -1.0f, 0.0f, 0.0f }, Stage1BreathingGroup::Corridor },
        { "LoopWall1", { 1.0f, 0.0f, 0.0f }, Stage1BreathingGroup::Corridor },
        { "LoopWall2", { -1.0f, 0.0f, 0.0f }, Stage1BreathingGroup::Corridor },
        { "LoopWall3", { 0.0f, 0.0f, 1.0f }, Stage1BreathingGroup::Corridor },
        { "LoopWall4", { 0.0f, 0.0f, -1.0f }, Stage1BreathingGroup::Corridor },
        { "Wall5", { 0.0f, 0.0f, -1.0f }, Stage1BreathingGroup::Hall },
        { "Wall6", { 0.0f, 0.0f, -1.0f }, Stage1BreathingGroup::Hall },
        { "Wall7", { 0.0f, 0.0f, 1.0f }, Stage1BreathingGroup::Hall },
        { "Wall8", { 0.0f, 0.0f, 1.0f }, Stage1BreathingGroup::Hall },
        { "Wall1", { 0.0f, 0.0f, -1.0f }, Stage1BreathingGroup::Building },
        { "Wall2", { -1.0f, 0.0f, 0.0f }, Stage1BreathingGroup::Building },
        { "Wall4", { 1.0f, 0.0f, 0.0f }, Stage1BreathingGroup::Building },
        { "Wall4North", { 1.0f, 0.0f, 0.0f }, Stage1BreathingGroup::Building },
    };
    // 短い壁の格子数を減らしつつ、長い壁も細かく膨らむようにする。
    constexpr float BreathingWallColumnWidth = 3.0f;
    constexpr int BreathingWallRows = 24;
    constexpr float BreathingWallEdgeWidth = 14.0f;
    // 変位が届く壁の近くにある物だけを取り付ける。取り付け点は、動く面の中へ少し入れて採る。
    constexpr float BreathingFixtureAttachReach = 11.0f;
    constexpr float BreathingFixtureBackTolerance = 3.25f;
    constexpr float BreathingFixtureAnchorInset = 7.0f;
    constexpr float BreathingFixtureInsetLimit = 0.25f;
    // 廊下を2回抜けて開始地点へ戻ると、3周目に入る。
    constexpr int BreathingWallWholeFaceCompletedLoops = 2;
    // 廊下：1周目完了から膨らみ始め、周回が進むごとに大きくする。
    constexpr float BreathingWallFirstLoopAmplitude = 4.0f;
    constexpr float BreathingWallSecondLoopAmplitude = 5.5f;
    constexpr float BreathingWallLaterLoopAmplitude = 7.0f;
    // 中央ホール：3周目から膨らむ量
    constexpr float BreathingHallAmplitude = 5.5f;
    // 建物の外周：3本目のヒューズを拾った後から膨らむ量（廊下・中央ホールも、この量まで大きくする）
    constexpr float BreathingBuildingAmplitude = 7.0f;
    // 膨らむ量が目標へ近づく速さ（1秒あたり。急に動き出さないように）
    constexpr float BreathingWallAmplitudeRate = 1.0f;
    // 壁の少し手前から息を鳴らして、音源が壁の中にならないようにする。
    constexpr float BreathingWallSoundFaceOffset = 1.0f;
    constexpr float BreathingWallSoundDistance = 90.0f;
    constexpr EventFeedback::SoundSpec BreathingWallSound{ 0.55f, 0.75f };

    // --- 時間が止まった部屋（右の倉庫。Stage1TimeStopRoom）
    // 天井から滴る水の真下（x・z）、水たまりの一番大きな半径、広がりきるまでの秒数
    const Vector2 TimeStopLeakCenter(150.0f, -128.0f);
    constexpr float TimeStopLeakRadius = 14.0f;
    constexpr float TimeStopLeakGrowSeconds = 40.0f;
    // 時間が止まり始めたとき（電源が落ちるような低い音）と、戻ったとき（同じ音を高く）の音・画面の脈打ち
    constexpr EventFeedback::SoundSpec TimeStopFreezeSound{ 0.32f, 0.9f };
    constexpr EventFeedback::SoundSpec TimeStopResumeSound{ 1.35f, 0.8f };
    constexpr PulseSpec TimeStopFreezePulse{ 0.22f, 1.2f };
    constexpr PulseSpec TimeStopResumePulse{ 0.30f, 0.35f };
    // 止まった世界を近づいてくる人影：歩く速さ（歩くより遅い）、止まる距離、消えるまでの秒数、触れられたとみなす距離
    constexpr float TimeStopFigureSpeed = 13.0f;
    constexpr float TimeStopFigureStopDistance = 9.0f;
    constexpr float TimeStopFigureLifetime = 40.0f;
    constexpr float TimeStopFigureCatchDistance = 11.0f;
    // 触れられたとき：驚かせる音・強い画面の脈打ち・振動
    constexpr EventFeedback::SoundSpec TimeStopCaughtSound{ 0.75f, 1.1f };
    constexpr PulseSpec TimeStopCaughtPulse{ 0.8f, 0.7f };
    constexpr RumbleSpec TimeStopCaughtRumble{ 16, 0.45f };

    // --- 自分の手が変わっていく（FirstPersonHands・viewModelSkinPS）
    // 変わり具合の目標：1周目・2周目・3周目以降・3本目のヒューズを拾った後（0周目は0）
    constexpr float HandCorruptionFirstLoop = 0.3f;
    constexpr float HandCorruptionSecondLoop = 0.6f;
    constexpr float HandCorruptionLaterLoop = 0.85f;
    constexpr float HandCorruptionAllFuses = 1.0f;
    // 目標へ近づく速さ（1秒あたり。気づかないくらいゆっくり進む）
    constexpr float HandCorruptionRate = 0.05f;

    // --- 音で輪郭が浮かぶ真っ暗な部屋（開始地点の部屋。Stage1EchoRoom）
    // 暗くするときに消す照明：部屋の範囲からこれだけ外まで（壁に付いた器具も含めるため）
    constexpr float EchoRoomLightMargin = 6.0f;
    // 暗くなるとき：電源が落ちる低い音・懐中電灯がちらついて消えるまでの秒数・画面の脈打ち
    constexpr EventFeedback::SoundSpec EchoRoomDarkenSound{ 0.40f, 1.1f };
    constexpr float EchoRoomFlashlightSputterSeconds = 0.6f;
    constexpr PulseSpec EchoRoomDarkenPulse{ 0.30f, 0.8f };
    // 人影の足音（人より低く重く）
    constexpr EventFeedback::SoundSpec EchoFigureStepSound{ 0.50f, 1.0f };
    // 人影に触れられたとき：驚かせる音・強い画面の脈打ち・振動
    constexpr EventFeedback::SoundSpec EchoRoomCaughtSound{ 0.72f, 1.1f };
    constexpr PulseSpec EchoRoomCaughtPulse{ 0.85f, 0.7f };
    constexpr RumbleSpec EchoRoomCaughtRumble{ 18, 0.5f };

    // --- ループ廊下に押し寄せる赤い水（Stage1CorridorFlood）
    // 照明が消えるときの音（電源が落ちる音を低く）・画面の脈打ち・振動
    constexpr EventFeedback::SoundSpec CorridorFloodLightsOutSound{ 0.55f, 1.2f };
    constexpr PulseSpec CorridorFloodLightsOutPulse{ 0.35f, 0.6f };
    constexpr RumbleSpec CorridorFloodLightsOutRumble{ 12, 0.20f };
    // 水が押し寄せる音（排水口の音を低く大きく。水の先頭から続けて鳴らす）と、そのたびの小さな振動
    constexpr EventFeedback::SoundSpec CorridorFloodRushSound{ 0.62f, 1.6f };
    constexpr RumbleSpec CorridorFloodRushRumble{ 8, 0.16f };
    // 追いつかれたとき：大きな水しぶき（水の中を走る音を低く）と驚かせる音、強い画面の脈打ち、振動
    constexpr EventFeedback::SoundSpec CorridorFloodCaughtSplash{ 0.50f, 1.8f };
    constexpr float CorridorFloodImpactMoisture = 0.88f;
    constexpr float CorridorFloodImpactMoistureSeconds = 0.65f;
    constexpr EventFeedback::SoundSpec CorridorFloodCaughtScare{ 0.70f, 1.0f };
    constexpr PulseSpec CorridorFloodCaughtPulse{ 0.95f, 0.9f };
    constexpr RumbleSpec CorridorFloodCaughtRumble{ 24, 0.6f };
    // 追いつかれた後に戻す所（開始地点の部屋の中、廊下の方＝北を向く）
    constexpr float CorridorFloodRestartX = 0.0f;
    constexpr float CorridorFloodRestartZ = -130.0f;
    constexpr float CorridorFloodRestartYaw = 0.0f;
    // 水で消す照明：廊下の範囲（Stage1CorridorFlood の浸水範囲）からこれだけ外まで（壁に付いた器具も含めるため）
    constexpr float CorridorFloodLightMargin = 6.0f;

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
