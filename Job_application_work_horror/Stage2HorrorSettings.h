// ============================================================================
// ファイルの役割: 2面の怖い演出（Stage2SceneHorror・Stage2SceneAnomalies）で使う、場所・秒数・強さの値。
// 2つのファイルで同じ値を使うので、ここにまとめている（2面のほかのファイルと共有する値は Stage2SceneConstants.h）。
// ============================================================================

#pragma once

#include "Stage2SceneConstants.h"
#include "EventFeedback.h"
#include "MathUtility.h"

#include <SimpleMath.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace Stage2HorrorSettings
{
    using DirectX::SimpleMath::Vector2;
    using DirectX::SimpleMath::Vector3;
    using DirectX::SimpleMath::Color;

    constexpr int FootprintFollowerStartLoop = 2;
    // --- 床と入口（値は Stage2SceneConstants.h）
    constexpr float FloorY = Stage2FloorY;
    constexpr float FootY = Stage2FootY;
    // 足音の影に捕まった後に戻す入口の位置
    const Vector3 EntrancePosition(0.0f, FootY, Stage2EntranceZ);

    // --- 偽の扉：ライトで照らして正面から見たら「見た」、その後に目をそらすか離れたら右の壁へ移す。
    // 見ているかは、左の壁の扉の取っ手の高さあたり（y=-72）を狙って調べる
    const Vector3 FalseDoorCenter(-Stage2FalseDoorHandleX, -72.0f, Stage2FalseDoorLeftZ);
    constexpr float FalseDoorObserveDistance = 105.0f;
    constexpr float FalseDoorObserveFacing = 0.88f;   // 見ている向きとの内積（1が真正面）
    constexpr float FalseDoorLookAwayFacing = 0.30f;
    constexpr float FalseDoorLeaveDistance = 118.0f;

    // --- 壁の傷の文字：近さ（この距離から、この幅で遠ざかるほど弱く）と、正面から見ている度合いで照らす強さを決める
    const Vector3 ScratchMessageCenter(39.45f, -71.0f, 31.0f);
    constexpr float ScratchNearDistance = 24.0f;
    constexpr float ScratchFadeRange = 95.0f;
    constexpr float ScratchGazeStartFacing = 0.62f;
    constexpr float ScratchGazeRange = 0.30f;

    // --- 血の手形：最後に現れた手形を、近くでライトの中心に捉えたか
    constexpr float HandprintLitDistance = 110.0f;
    constexpr float HandprintLitFacing = 0.93f;

    // --- 鏡：鏡の中の自分が遅れる秒数と、鏡の中の人影が現れる・消える秒数
    constexpr float MirrorSelfDelaySeconds = 0.45f;
    constexpr float MirrorFigureFadeInSeconds = 0.35f;
    constexpr float MirrorFigureFadeOutSeconds = 0.5f;

    // --- 肖像画：ライトを点けて、近くで正面から見つめているか。瞳の向きを求める目の位置
    const Vector3 PortraitCenter(38.72f, -69.0f, -25.0f);
    constexpr float PortraitLookDistance = 105.0f;
    constexpr float PortraitLookFacing = 0.91f;
    const Vector3 PortraitEyeCenter(38.9f, -67.5f, -25.0f);

    // --- 最後の追跡：影をプレイヤーのこれだけ後ろに出し、この速さで追わせる。
    // 捕まった回数が多いほど、1回につきこの分だけ遅くする（最大の回数まで）
    constexpr float PursuitStartBehind = 72.0f;
    constexpr float PursuitSpeed = 18.0f;
    constexpr float PursuitRetrySlowdown = 1.5f;
    constexpr int PursuitRetrySlowdownMaxCatches = 2;
    constexpr float PursuitStopDistance = 30.0f;
    constexpr float PursuitGazeScareSeconds = 8.0f;

    // --- 足音の影に捕まって入口へ戻ったとき：危険度を下げ、しばらく影を出さない
    constexpr float NoiseCatchResetThreat = 0.18f;
    constexpr float NoiseCatchStalkerCooldown = 7.0f;
    constexpr float NoiseCatchStalkerNoticeSeconds = 3.0f;

    // --- 見えない影：ライトが胸に当たったか（胸の高さ・距離・光の中心からのずれ）。息づかいを鳴らす頭の高さ
    constexpr float UnseenChestHeight = 24.0f;
    constexpr float UnseenHeadHeight = 36.0f;
    constexpr float UnseenLitDistance = 75.0f;
    constexpr float UnseenLitFacing = 0.95f;
    // 歩幅、左右の足の間隔の半分、水に触れた後に濡れた足跡を残す歩数、影だけの見え方を上げ下げする秒数
    constexpr float UnseenStride = 7.0f;
    constexpr float UnseenFootOffset = 2.4f;
    constexpr int UnseenWetStepCount = 9;
    constexpr float UnseenShadowFadeSeconds = 0.4f;

    // --- 背後の気配：真後ろに置けないときに、次の機会へ回す秒数
    constexpr float BehindPresenceRetrySeconds = 3.0f;

    // ------------------------------------------------------------------------
    // 演出の強さと長さ（照明の明滅・画面の脈打ち・振動・音）と、怖さに合わせて画面効果を変える範囲
    // ------------------------------------------------------------------------
    using EventFeedback::FlickerSpec;
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;
    using EventFeedback::SoundSpec;

    // 方向を求めるとき、長さがこれ以下なら向きを決めない（ゼロ除算を避ける）
    constexpr float MinimumDirectionLength = 0.001f;
    // 奥の扉の上の照明を、異変を見つけたときに揺らす明滅（いくつもの異変で同じ揺らし方にそろえている）
    constexpr FlickerSpec DoorLightAlertFlicker{ 0.90f, 0.76f };

    // 偽の扉が右の壁へ移ったとき
    constexpr PulseSpec FalseDoorMovedBloom{ 0.58f, 0.24f };
    constexpr FlickerSpec FalseDoorOldLightFlicker{ 0.72f, 0.68f };
    constexpr PulseSpec FalseDoorMovedPulse{ 0.32f, 0.34f };
    constexpr RumbleSpec FalseDoorMovedRumble{ 8, 0.20f };

    // 影を見てしまったとき：驚かせる一撃と、奥から入口へ照明が順に明滅する拍（拍ごとの照明と強さ、光の脈打ちの強さの式）
    constexpr PulseSpec ObservedScarePulse{ 0.72f, 0.52f };
    constexpr RumbleSpec ObservedScareRumble{ 15, 0.34f };
    constexpr float ObservedScareFlickerSeconds = 0.82f;
    constexpr float ObservedScareDoorLightStrength = 0.94f;
    constexpr float ObservedScareLight3Strength = 0.90f;
    constexpr float ObservedScareLight2Strength = 0.86f;
    constexpr float ObservedScareLight1Strength = 0.80f;
    constexpr float ObservedScareBloomBase = 0.26f;
    constexpr float ObservedScareBloomPerStrength = 0.25f;
    constexpr float ObservedScareBloomSeconds = 0.16f;
    // 演出の間だけ、ノイズと周辺減光を強める（演出の強さ0〜1で、基本＋この分）
    constexpr float ObservedScareSeconds = 1.35f;
    constexpr float ObservedScareNoiseBase = 0.30f;
    constexpr float ObservedScareNoiseRange = 0.16f;
    constexpr float ObservedScareVignetteBase = 0.76f;
    constexpr float ObservedScareVignetteRange = 0.12f;

    // 最後の停電：始まりに裸電球を大きく揺らし、壁の傷を全部赤く光らせて、強く驚かせる
    constexpr float FinalStartBulbKick = 0.9f;
    constexpr float FinalStartScratchEmission = 0.34f;
    constexpr PulseSpec FinalStartPulse{ 0.82f, 0.72f };
    constexpr RumbleSpec FinalStartRumble{ 18, 0.42f };
    // 追跡中に影を見てしまった罰：照明が一斉に明滅し、画面が強く乱れる
    constexpr FlickerSpec GazePenaltyFlicker{ 0.72f, 0.92f };
    constexpr PulseSpec GazePenaltyPulse{ 0.92f, 0.64f };
    constexpr PulseSpec GazePenaltyBloom{ 0.62f, 0.22f };
    constexpr RumbleSpec GazePenaltyRumble{ 16, 0.45f };
    // 拍ごとに入口側から非常灯を点ける：点滅の周期のずらし（基本＋拍ごと）、点けた瞬間の明滅と光
    constexpr float FinalBeatEmergencyOffsetBase = 9.0f;
    constexpr float FinalBeatEmergencyOffsetStep = 0.7f;
    constexpr FlickerSpec FinalBeatFlicker{ 1.0f, 0.96f };
    constexpr PulseSpec FinalBeatBloom{ 0.48f, 0.18f };
    // 停電の明滅の間の画面（ノイズ・周辺減光・レンズの歪み・光の筋は、明滅に合わせて基本＋この分）
    constexpr float FinalFlickerNoise = 0.43f;
    constexpr float FinalFlickerVignette = 0.88f;
    constexpr float FinalFlickerDistortionBase = 0.68f;
    constexpr float FinalFlickerDistortionRange = 0.08f;
    constexpr float FinalFlickerVolumetricBase = 0.62f;
    constexpr float FinalFlickerVolumetricRange = 0.12f;
    // 出口の扉の鍵が外れたとき：扉の上のランプを緑にし、光で知らせる
    const Color DoorIndicatorOpenDiffuse(0.025f, 0.22f, 0.055f, 1.0f);
    const Color DoorIndicatorOpenEmission(0.005f, 0.30f, 0.025f, 1.0f);
    constexpr float DoorIndicatorOpenShininess = 30.0f;
    constexpr PulseSpec FinalDoorReadyBloom{ 0.88f, 0.62f };
    constexpr RumbleSpec FinalDoorReadyRumble{ 9, 0.24f };
    // 追いつかれ直して出口の扉を閉め直すとき：扉の開き方の段階と、赤いランプ
    constexpr int FinalDoorRetryOpeningPhase = 3;
    const Color DoorIndicatorLockedDiffuse(0.24f, 0.012f, 0.008f, 1.0f);
    const Color DoorIndicatorLockedEmission(0.18f, 0.001f, 0.0f, 1.0f);
    constexpr float DoorIndicatorLockedShininess = 24.0f;

    // 最後の追跡：この距離まで追いつかれたら捕まる。影が照明をこれだけ越えたら、その照明を消す（消える音の高さと音量）
    constexpr float PursuitCatchDistance = 31.5f;
    constexpr float PursuitLightPassMargin = 4.0f;
    constexpr SoundSpec PursuitLightOffSound{ 0.55f, 0.9f };
    // 影の近さ（0〜1）：この距離から、この幅で遠ざかるほど0に近づく
    constexpr float PursuitNearDistance = 30.0f;
    constexpr float PursuitProximityRange = 72.0f;
    // 近いほど速く脈打つ（基本の速さ＋近さ1のときに足す速さ、ラジアン毎秒）
    constexpr float PursuitPulseSpeedBase = 6.0f;
    constexpr float PursuitPulseSpeedRange = 5.0f;
    // 近さに合わせた画面：廊下の緊張と、扉が開いた後の各効果（基本＋近さ1のときに足す分、脈打ちで足す分）
    constexpr float PursuitTensionBase = 0.74f;
    constexpr float PursuitTensionRange = 0.16f;
    constexpr float PursuitNoiseBase = 0.30f;
    constexpr float PursuitNoiseRange = 0.085f;
    constexpr float PursuitNoisePulse = 0.018f;
    constexpr float PursuitVignetteBase = 0.76f;
    constexpr float PursuitVignetteRange = 0.135f;
    constexpr float PursuitDistortionBase = 0.46f;
    constexpr float PursuitDistortionRange = 0.18f;
    constexpr float PursuitDistortionPulse = 0.025f;
    constexpr float PursuitVolumetricBase = 0.50f;
    constexpr float PursuitVolumetricRange = 0.12f;
    constexpr float PursuitFilmGradeBase = 0.78f;
    constexpr float PursuitFilmGradeRange = 0.18f;
    constexpr float PursuitLensDirtBase = 0.20f;
    constexpr float PursuitLensDirtRange = 0.22f;
    constexpr float PursuitExposureDrop = 0.055f;
    constexpr float PursuitExposurePulse = 0.025f;
    // 影を見てしまった罰の間の画面（基本＋罰の強さ1のときに足す分）
    constexpr float GazePenaltyNoiseBase = 0.43f;
    constexpr float GazePenaltyNoiseRange = 0.12f;
    constexpr float GazePenaltyVignetteBase = 0.88f;
    constexpr float GazePenaltyVignetteRange = 0.08f;
    constexpr float GazePenaltyDistortionBase = 0.66f;
    constexpr float GazePenaltyDistortionRange = 0.14f;
    constexpr float GazePenaltyLensDirtBase = 0.32f;
    constexpr float GazePenaltyLensDirtRange = 0.36f;
    // 近いほど長く強い振動（基本＋近さ1のときに足す分）。この近さを超えたら画面も脈打たせる
    constexpr RumbleSpec PursuitRumbleBase{ 3, 0.08f };
    constexpr RumbleSpec PursuitRumbleRange{ 7, 0.17f };
    constexpr float PursuitPulseMinProximity = 0.34f;
    constexpr PulseSpec PursuitPulseBase{ 0.045f, 0.16f };
    constexpr float PursuitPulseStrengthRange = 0.075f;

    // 捕まったとき：一番強い一撃と、入口へ戻した後の余韻
    constexpr PulseSpec CaughtPulse{ 1.0f, 0.72f };
    constexpr PulseSpec CaughtBloom{ 0.18f, 0.16f };
    constexpr RumbleSpec CaughtRumble{ 24, 0.72f };
    constexpr PulseSpec CaughtRecoveredPulse{ 0.34f, 0.42f };
    constexpr RumbleSpec CaughtRecoveredRumble{ 8, 0.18f };
    // 暗転していく間の画面（基本＋暗さ1のときに変える分）
    constexpr float CaughtExposureBase = 0.92f;
    constexpr float CaughtExposureDrop = 0.38f;
    constexpr float CaughtNoiseBase = 0.46f;
    constexpr float CaughtNoiseRange = 0.18f;
    constexpr float CaughtVignetteBase = 0.90f;
    constexpr float CaughtVignetteRange = 0.08f;
    constexpr float CaughtDistortionBase = 0.72f;
    constexpr float CaughtDistortionRange = 0.18f;

    // 壁の傷：3本ごとに少しずつ色を変える量、新しく浮かび上がる秒数、地の赤と自己発光の色、光沢
    constexpr int ScratchColorCycle = 3;
    constexpr float ScratchColorVariation = 0.018f;
    constexpr float ScratchDissolveSeconds = 1.4f;
    constexpr float ScratchDiffuseRed = 0.20f;
    constexpr float ScratchDiffuseGreen = 0.008f;
    constexpr float ScratchDiffuseBlue = 0.004f;
    constexpr float ScratchEmissionGreen = 0.001f;
    constexpr float ScratchShininess = 18.0f;

    // 照明の区画：廊下を進むと照明を明滅させる位置（z）
    constexpr float LightZone1Z = -88.0f;
    constexpr float LightZone2Z = -14.0f;
    constexpr float LightZone3Z = 60.0f;
    constexpr float LightZoneDoorZ = 108.0f;
    // 明滅の強さ：周回ごとに強め、奥の区画ほど少し強め、上限を超えない
    constexpr float LightZoneLoopStrength = 0.17f;
    constexpr float LightZoneBaseStrength = 0.40f;
    constexpr float LightZoneIndexStrength = 0.035f;
    constexpr float LightZoneMaxStrength = 0.92f;
    constexpr float LightZoneFlickerBaseSeconds = 0.48f;
    // 区画に入ったときの光・画面の乱れ・振動（基本＋強さや周回で足す分）
    constexpr float LightZoneBloomBase = 0.20f;
    constexpr float LightZoneBloomPerStrength = 0.20f;
    constexpr float LightZoneBloomSeconds = 0.15f;
    constexpr float LightZonePulseBase = 0.08f;
    constexpr float LightZonePulsePerLoop = 0.24f;
    constexpr float LightZonePulseSeconds = 0.16f;
    constexpr RumbleSpec LightZoneRumbleBase{ 2, 0.08f };
    constexpr float LightZoneRumblePerLoop = 0.18f;

    // 壁の傷の脈打ち：心臓の鼓動のような速さ（基本＋周回ごと）、追跡中の上乗せ、光る強さ（基本＋周回ごと、照らしたときの分）
    constexpr float ScratchHeartbeatSpeedBase = 2.4f;
    constexpr float ScratchHeartbeatSpeedPerLoop = 0.45f;
    constexpr float ScratchFinalBoost = 0.16f;
    constexpr float ScratchEmissionBase = 0.07f;
    constexpr float ScratchEmissionPerLoop = 0.035f;
    constexpr float ScratchLitEmissionBase = 0.10f;
    constexpr float ScratchLitEmissionHeartbeat = 0.16f;
    // 近くで正面から照らしたときに一度だけ驚かせる（画面の乱れは周回ごとに強める）
    constexpr float ScratchScarePulseBase = 0.24f;
    constexpr float ScratchScarePulsePerLoop = 0.08f;
    constexpr float ScratchScarePulseSeconds = 0.32f;
    constexpr PulseSpec ScratchScareBloom{ 0.48f, 0.24f };
    constexpr RumbleSpec ScratchScareRumble{ 7, 0.20f };

    // 血の手形：次の手形が現れたときと、最後の手形を照らして見つけたとき
    constexpr SoundSpec HandprintAppearSound{ 0.55f, 0.9f };
    constexpr FlickerSpec HandprintAppearFlicker{ 0.45f, 0.30f };
    constexpr PulseSpec HandprintAppearPulse{ 0.14f, 0.22f };
    constexpr RumbleSpec HandprintAppearRumble{ 3, 0.08f };
    constexpr SoundSpec HandprintFoundSound{ 0.62f, 1.4f };
    constexpr PulseSpec HandprintFoundPulse{ 0.30f, 0.40f };
    constexpr PulseSpec HandprintFoundBloom{ 0.40f, 0.20f };
    constexpr RumbleSpec HandprintFoundRumble{ 7, 0.18f };

    // 鏡：鏡の中の人影に気づいたときと、振り返って見つけたとき
    constexpr SoundSpec MirrorSeenBreathSound{ 0.82f, 0.42f };
    constexpr PulseSpec MirrorSeenPulse{ 0.16f, 0.30f };
    constexpr RumbleSpec MirrorSeenRumble{ 3, 0.10f };
    constexpr float MirrorFoundBulbKick = 0.55f;
    constexpr FlickerSpec MirrorFoundFlicker{ 0.80f, 0.70f };
    constexpr SoundSpec MirrorFoundKnockSound{ 0.70f, 0.50f };
    constexpr PulseSpec MirrorFoundPulse{ 0.32f, 0.42f };
    constexpr RumbleSpec MirrorFoundRumble{ 6, 0.16f };

    // 水たまり：人影の息づかいを鳴らす頭の高さ、気づいたとき・一歩近づくとき・すぐそばまで来たとき・見つけたとき
    constexpr float PuddleFigureHeadHeight = 36.0f;
    constexpr SoundSpec PuddleSeenBreathSound{ 0.78f, 0.40f };
    constexpr PulseSpec PuddleSeenPulse{ 0.16f, 0.30f };
    constexpr RumbleSpec PuddleSeenRumble{ 3, 0.10f };
    // 水面の波紋の強さ（足もとを押し下げる量）と、広がりの速さ（負の値ほど速く沈む）
    constexpr float PuddleStepRippleStrength = 1.6f;
    constexpr float PuddleStepRippleSpeed = -1.3f;
    constexpr SoundSpec PuddleStepSound{ 0.70f, 0.75f };
    constexpr RumbleSpec PuddleStepRumble{ 1, 0.05f };
    constexpr SoundSpec PuddleCaughtScareSound{ 1.0f, 0.75f };
    constexpr PulseSpec PuddleCaughtPulse{ 0.55f, 0.45f };
    constexpr float PuddleCaughtBulbKick = 0.45f;
    constexpr FlickerSpec PuddleCaughtFlicker{ 0.90f, 0.60f };
    constexpr RumbleSpec PuddleCaughtRumble{ 8, 0.22f };
    constexpr float PuddleFoundRippleStrength = 2.2f;
    constexpr float PuddleFoundRippleSpeed = -1.6f;
    constexpr float PuddleFoundBulbKick = 0.55f;
    constexpr FlickerSpec PuddleFoundFlicker{ 0.80f, 0.70f };
    constexpr SoundSpec PuddleFoundStepSound{ 0.62f, 0.85f };
    constexpr PulseSpec PuddleFoundPulse{ 0.32f, 0.42f };
    constexpr RumbleSpec PuddleFoundRumble{ 6, 0.16f };

    // 肖像画：見開いた目が赤く光るときの絵の色と光沢、近くの照明の明滅、画面と振動
    constexpr float PortraitOpenedEmission = 0.28f;
    constexpr float PortraitOpenedEmissionScale = 0.12f;
    const Color PortraitOpenedDiffuse(0.10f, 0.014f, 0.009f, 1.0f);
    constexpr float PortraitOpenedShininess = 12.0f;
    constexpr FlickerSpec PortraitNearbyFlicker{ 0.82f, 0.72f };
    constexpr PulseSpec PortraitOpenedPulse{ 0.36f, 0.30f };
    constexpr RumbleSpec PortraitOpenedRumble{ 6, 0.17f };
    // 絵の中の目：見つめている間にかすかに開きかける大きさ、開く・閉じる速さ、瞳の光が点く・消える速さ（毎秒）
    constexpr float PortraitStareOpen = 0.30f;
    constexpr float PortraitEyeOpenSpeed = 1.4f;
    constexpr float PortraitEyeCloseSpeed = 5.0f;
    constexpr float PortraitEyeGlowRiseSpeed = 1.0f;
    constexpr float PortraitEyeGlowFallSpeed = 3.0f;

    // 壁の向こうのノック：耳を澄ます位置（廊下の内側の壁の手前のx）、叩く音の高さの幅と音量、聞き当てたときの一撃
    constexpr float KnockListenX = 38.0f;
    constexpr float KnockMinPitch = 0.58f;
    constexpr float KnockMaxPitch = 0.68f;
    constexpr float KnockVolume = 1.6f;
    constexpr SoundSpec KnockFoundSound{ 0.52f, 2.2f };
    constexpr FlickerSpec KnockFoundFlicker{ 0.82f, 0.72f };
    constexpr PulseSpec KnockFoundPulse{ 0.34f, 0.32f };
    constexpr RumbleSpec KnockFoundRumble{ 7, 0.18f };
    // 向きを比べるときに、水平の前向きがほぼ真上・真下で使えないとみなす長さの2乗
    constexpr float MinimumHorizontalForwardSquared = 0.0001f;

    // 見えない影：水たまりを踏んだときの波紋と水音、濡れた足跡の濃さ（水から離れるほど薄く：最後の一歩の濃さ＋水に触れた直後に足す分）
    constexpr float UnseenRippleStrength = 1.6f;
    constexpr float UnseenRippleSpeed = -1.3f;
    constexpr SoundSpec UnseenWaterStepSound{ 0.72f, 0.60f };
    constexpr float UnseenFootprintMinOpacity = 0.35f;
    constexpr float UnseenFootprintOpacityRange = 0.65f;
    // 照らして気づかれたときと、すれ違えたとき
    constexpr SoundSpec UnseenNoticedBreathSound{ 0.70f, 0.55f };
    constexpr float UnseenNoticedBulbKick = 0.35f;
    constexpr PulseSpec UnseenNoticedPulse{ 0.22f, 0.30f };
    constexpr RumbleSpec UnseenNoticedRumble{ 3, 0.10f };
    constexpr FlickerSpec UnseenPassedFlicker{ 0.86f, 0.70f };
    constexpr float UnseenPassedBulbKick = 0.7f;
    constexpr SoundSpec UnseenPassedStepSound{ 0.62f, 1.4f };
    constexpr PulseSpec UnseenPassedPulse{ 0.30f, 0.34f };
    constexpr RumbleSpec UnseenPassedRumble{ 6, 0.20f };

    // 背後の気配：まだ出ていないときの距離（どの判定にも入らない遠さ）、見ているかを調べる胸の高さ、
    // 見つめても消えない時間の余裕、追ってきて止まる距離、振り向いたときと追いつかれたとき
    constexpr float PresenceFarDistance = 1000.0f;
    constexpr float PresenceChestHeight = 17.0f;
    constexpr float PresenceGazeExtraSeconds = 10.0f;
    constexpr float PresenceChaseStopDistance = 8.0f;
    constexpr PulseSpec PresenceSeenPulse{ 0.14f, 0.18f };
    constexpr RumbleSpec PresenceSeenRumble{ 2, 0.05f };
    constexpr FlickerSpec PresenceReachedFlicker{ 0.60f, 0.86f };
    constexpr SoundSpec PresenceReachedScareSound{ 0.58f, 1.0f };
    constexpr PulseSpec PresenceReachedPulse{ 0.62f, 0.42f };
    constexpr RumbleSpec PresenceReachedRumble{ 10, 0.26f };

    // 心拍に使う危険度：足音の危険度・見えない影の近さ・観察の間違いを、それぞれこの割合で使う。間違いはこの回数で最大
    constexpr float NoiseThreatToHeartbeat = 0.78f;
    constexpr float UnseenProximityToHeartbeat = 0.85f;
    constexpr float MistakeDangerToHeartbeat = 0.72f;
    constexpr float MistakeCountForFullDanger = 3.0f;
    // 影の近さを危険度にする距離（この距離から、この幅で遠ざかるほど0に近づく）：最後の追跡の影・足音の影・ラジオの雑音
    constexpr float PursuitDangerNearDistance = 18.0f;
    constexpr float PursuitDangerRange = 92.0f;
    constexpr float NoiseShadowDangerNearDistance = 14.0f;
    constexpr float NoiseShadowDangerRange = 72.0f;
    constexpr float RadioPresenceNearDistance = 14.0f;
    constexpr float RadioPresenceRange = 96.0f;

    // 誰かの濡れた足跡：水音、照らして止めたとき、すぐ近くまで来たとき（息づかいは聞き手の後ろのこの距離から）
    constexpr SoundSpec FollowerWaterStepSound{ 0.78f, 0.55f };
    constexpr PulseSpec FollowerLitPulse{ 0.14f, 0.26f };
    constexpr RumbleSpec FollowerLitRumble{ 2, 0.08f };
    constexpr float FollowerReachedBulbKick = 0.45f;
    constexpr FlickerSpec FollowerReachedFlicker{ 0.70f, 0.60f };
    constexpr float FollowerBreathBehindDistance = 10.0f;
    constexpr SoundSpec FollowerBreathSound{ 0.85f, 0.45f };
    constexpr PulseSpec FollowerReachedPulse{ 0.30f, 0.40f };
    constexpr RumbleSpec FollowerReachedRumble{ 5, 0.14f };

    // --- 壁の向こうのノック
    constexpr float KnockListenDistance = 24.0f;    // 出どころの壁の前とみなす距離
    constexpr float KnockListenFacing = 0.60f;      // 壁の方を向いているとみなす内積

    // 音の出どころの番号から位置を作っている
    inline Vector3 GetKnockSpot(int index)
    {
        const float* spot = Stage2KnockSpots[index];
        return Vector3(spot[0], spot[1], spot[2]);
    }
}
