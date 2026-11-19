// ============================================================================
// ファイルの役割: 2面の怖い演出（偽の扉・影を見たときの照明・最後の停電と追跡・捕まったとき・傷・肖像画・ノック・背後の気配・心拍）を担当している。
// 主な技術: 有限状態機械、照明の演出、距離と視線の判定、時間で進む演出
// ============================================================================

#include "Stage2Scene.h"
#include "MathUtility.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "MirrorSelf.h"
#include "Ground.h"
#include "HangingBulb.h"
#include "TensionPulseFeedback.h"
#include "EventFeedback.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace DirectX::SimpleMath;

#include "Stage2SceneConstants.h"

namespace
{
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

}


// 偽の扉の異変：ライトで照らして見た後、目を離した隙に反対側（右）の壁へ移している
void Stage2Scene::UpdateFalseDoorAnomaly(const Player& player)
{
    // 偽の扉を見つける周回（Stage2AnomalyPlanが決める）だけ判定している。
    if (!m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::FalseDoor) ||
        m_FalseDoorAnomaly.HasMoved())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Vector3 cameraToDoor = FalseDoorCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToDoor.Length();
    if (distance > MinimumDirectionLength)
    {
        cameraToDoor /= distance;
    }

    // 近く（105未満）で正面（内積0.88超）から見たとき、ライトが点いていれば「見た」、消えていれば間違いとして数えている
    const float facing = game->GetCamera()->GetForward().Dot(cameraToDoor);
    if (distance < FalseDoorObserveDistance && facing > FalseDoorObserveFacing)
    {
        if (!player.IsFlashlightOn())
        {
            RegisterPuzzleMistake(PuzzleFeedback::FalseDoorNeedsLight);
            return;
        }
        m_FalseDoorAnomaly.MarkObserved();
        m_PuzzleFeedback.Clear();
        return;
    }

    // 見た後で、目をそらす（内積0.30以下）か離れたら、右の壁へ移して照明と画面で知らせている
    if (!m_FalseDoorAnomaly.WasObserved() ||
        (facing > FalseDoorLookAwayFacing && distance < FalseDoorLeaveDistance))
    {
        return;
    }

    m_FalseDoorAnomaly.MarkMoved();
    SetFalseDoorState(true, true);

    m_Notices.loop = Stage2NoticeSeconds;

    CeilingLight* doorLight =
        m_Objects.Light(Stage2Light::DoorLight);
    if (doorLight != nullptr)
    {
        EventFeedback::Flicker(*doorLight, DoorLightAlertFlicker);
    }
    EventFeedback::BloomPulse(*game->GetPostProcess(), FalseDoorMovedBloom);

    CeilingLight* oldDoorLight =
        m_Objects.Light(Stage2Light::Light3);
    if (oldDoorLight != nullptr)
    {
        EventFeedback::Flicker(*oldDoorLight, FalseDoorOldLightFlicker);
    }
    EventFeedback::HorrorPulse(*game->GetPostProcess(), FalseDoorMovedPulse);
    EventFeedback::Rumble(FalseDoorMovedRumble);
}

// 影を見てしまったときの演出を始めている（驚かせる音・画面・振動）
void Stage2Scene::StartObservedScare()
{
    if (!m_ObservedScareSequence.Start())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), ObservedScarePulse);
    EventFeedback::Rumble(ObservedScareRumble);
}

// 影を見たときの演出を進めている：奥の扉の照明から入口の照明へ、順に明滅させている
void Stage2Scene::UpdateObservedScare(float deltaTime)
{
    if (!m_ObservedScareSequence.IsActive())
    {
        return;
    }

    m_ObservedScareSequence.Advance(deltaTime);
    Core::Game* game = Core::Game::GetInstance();

    struct LightBeat
    {
        Stage2Light Light;
        float Strength;
    };

    constexpr LightBeat beats[] =
    {
        { Stage2Light::DoorLight, ObservedScareDoorLightStrength },
        { Stage2Light::Light3, ObservedScareLight3Strength },
        { Stage2Light::Light2, ObservedScareLight2Strength },
        { Stage2Light::Light1, ObservedScareLight1Strength }
    };

    int pendingBeat = m_ObservedScareSequence.ConsumePendingBeat();
    while (pendingBeat >= 0)
    {
        const LightBeat& beat = beats[pendingBeat];
        CeilingLight* light = m_Objects.Light(beat.Light);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(ObservedScareFlickerSeconds, beat.Strength);
        }

        game->GetPostProcess()->TriggerBloomPulse(
            ObservedScareBloomBase + beat.Strength * ObservedScareBloomPerStrength,
            ObservedScareBloomSeconds);
        pendingBeat = m_ObservedScareSequence.ConsumePendingBeat();
    }

    if (m_ObservedScareSequence.GetTimer() < ObservedScareSeconds)
    {
        const float intensity =
            m_ObservedScareSequence.GetAtmosphereIntensity();
        game->GetPostProcess()->SetAtmosphere(
            ObservedScareNoiseBase + intensity * ObservedScareNoiseRange,
            ObservedScareVignetteBase + intensity * ObservedScareVignetteRange);
    }
    else
    {
        m_ObservedScareSequence.CompleteIfElapsed();
    }
}

// 最後の停電の演出を始めている：足音の影を消し、スタミナを満タンにして、背後72に追ってくる影を出している
void Stage2Scene::StartFinalSequence()
{
    m_FinalSequence.Start();
    KickHangingBulbs(FinalStartBulbKick);
    m_NoiseThreatSystem.SetThreat(0.0f);
    m_NoiseThreatSystem.SetWarningTimer(0.0f);
    m_Notices.loop = Stage2NoticeSeconds;

    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    ShadowMan* shadow = m_Objects.shadow;
    ShadowMan* noiseShadow = m_Objects.noiseShadow;
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
    if (player != nullptr)
    {
        player->RestoreStamina();
    }
    if (player != nullptr && shadow != nullptr)
    {
        const Vector3 playerPosition = player->GetPosition();
        shadow->SetPosition(
            playerPosition.x,
            FootY,
            playerPosition.z - PursuitStartBehind);
        shadow->SetActive(false);
        shadow->SetActive(true);
        // 捕まった回数が多いほど影を遅くしている（1回1.5ずつ、最大2回分）
        const float retryAssist = static_cast<float>((std::min)(
            game->GetCaughtCount(), PursuitRetrySlowdownMaxCatches)) * PursuitRetrySlowdown;
        shadow->EnableChase(PursuitSpeed - retryAssist, PursuitStopDistance);
        shadow->EnableGazeScare(PursuitGazeScareSeconds);
        // 追跡中に影を見てしまうと、照明が一斉に明滅する罰を出している（続けては出さない）
        shadow->SetOnObserved(
            [this]()
            {
                if (!m_FinalSequence.TryTriggerGazePenalty())
                {
                    return;
                }

                // 追跡の最中なので、知らせは短く（走りながらでも一目で読める長さ）
                constexpr float gazePenaltyNoticeSeconds = 1.65f;
                m_Notices.loop = gazePenaltyNoticeSeconds;
                Core::Game* game = Core::Game::GetInstance();
                for (CeilingLight* light : m_Objects.lights)
                {
                    if (light != nullptr)
                    {
                        EventFeedback::Flicker(*light, GazePenaltyFlicker);
                    }
                }
                EventFeedback::HorrorPulse(*game->GetPostProcess(), GazePenaltyPulse);
                EventFeedback::BloomPulse(*game->GetPostProcess(), GazePenaltyBloom);
                EventFeedback::Rumble(GazePenaltyRumble);
            });
    }
    // 影が通り過ぎて消した照明を、やり直したときのために点け直し、影の足跡を数え直している
    for (CeilingLight* light : m_Objects.lights)
    {
        if (light != nullptr)
        {
            light->SetForcedOff(false);
        }
    }
    if (shadow != nullptr)
    {
        m_PursuitLastShadowPosition = shadow->GetPosition();
    }
    m_PursuitStepDistance = 0.0f;
    m_PursuitStepSide = 1.0f;
    // 壁の傷を全部見せ、強く驚かせている
    RevealScratchPieces(0, Stage2ScratchCount, FinalStartScratchEmission);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), FinalStartPulse);
    EventFeedback::Rumble(FinalStartRumble);
}

// 最後の停電の演出を進め、終わったら出口の扉の鍵を外している
void Stage2Scene::UpdateFinalSequence(float deltaTime)
{
    if (!m_FinalSequence.IsSequenceActive() || m_FinalDoorReady)
    {
        return;
    }

    m_FinalSequence.AdvanceSequence(deltaTime);
    Core::Game* game = Core::Game::GetInstance();

    int pendingBeat = m_FinalSequence.ConsumePendingBeat();
    while (pendingBeat >= 0)
    {
        // 最後の演出の拍ごとに、入口側から出口の扉の上へ順に非常灯（赤）を点けている。
        CeilingLight* light = m_Objects.lights[static_cast<std::size_t>(pendingBeat)];
        if (light != nullptr)
        {
            light->SetEmergencyLight(
                true,
                FinalBeatEmergencyOffsetBase + static_cast<float>(pendingBeat) * FinalBeatEmergencyOffsetStep);
            EventFeedback::Flicker(*light, FinalBeatFlicker);
        }
        EventFeedback::BloomPulse(*game->GetPostProcess(), FinalBeatBloom);
        pendingBeat = m_FinalSequence.ConsumePendingBeat();
    }

    // 最初の1.65秒は、画面を強く明滅させている
    constexpr float FinalFlickerSeconds = 1.65f;
    constexpr float FinalFlickerRadiansPerSecond = 22.0f;
    constexpr float FinalFlickerMinimumExposure = Effect::PostProcess::MinimumSceneExposure;
    constexpr float FinalFlickerExposureAmplitude = 0.17f;
    if (m_FinalSequence.GetSequenceTimer() < FinalFlickerSeconds)
    {
        const float pulse =
            std::sin(m_FinalSequence.GetSequenceTimer() * FinalFlickerRadiansPerSecond) *
                0.5f + 0.5f;
        game->GetPostProcess()->SetExposure(FinalFlickerMinimumExposure + pulse * FinalFlickerExposureAmplitude);
        game->GetPostProcess()->SetAtmosphere(FinalFlickerNoise, FinalFlickerVignette);
        game->GetPostProcess()->SetLensDistortionStrength(
            FinalFlickerDistortionBase + pulse * FinalFlickerDistortionRange);
        game->GetPostProcess()->SetVolumetricIntensity(
            FinalFlickerVolumetricBase + pulse * FinalFlickerVolumetricRange);
        return;
    }

    // 演出が終わったら、出口の扉の鍵を外し、出口を調べられるようにし、扉のランプを緑にしている
    m_FinalDoorReady = true;
    m_FinalSequenceArmed = false;
    m_Notices.loop = Stage2LongNoticeSeconds;

    Door* finalDoor = m_Objects.door;
    if (finalDoor != nullptr)
    {
        finalDoor->SetLocked(false);
    }

    ExitTrigger* exit = m_Objects.exit;
    if (exit != nullptr)
    {
        exit->SetInteractionEnabled(true);
    }

    Wall* doorIndicator = m_Objects.doorIndicator;
    if (doorIndicator != nullptr)
    {
        doorIndicator->SetAppearance(
            DoorIndicatorOpenDiffuse, DoorIndicatorOpenEmission, DoorIndicatorOpenShininess);
    }

    EventFeedback::BloomPulse(*game->GetPostProcess(), FinalDoorReadyBloom);
    EventFeedback::Rumble(FinalDoorReadyRumble);
}

// 最後の追跡：影に追いつかれたら捕まり、近いほど画面を乱し、振動を強くしている
void Stage2Scene::UpdateFinalPursuit(float deltaTime)
{
    if (!m_FinalSequence.IsPursuitActive())
    {
        ShadowMan* shadow =
            m_Objects.shadow;
        if (shadow != nullptr)
        {
            shadow->SetActive(false);
        }

        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    ShadowMan* shadow = m_Objects.shadow;
    if (player == nullptr || shadow == nullptr)
    {
        return;
    }

    const Vector3 offset = player->GetPosition() - shadow->GetPosition();
    const float horizontalDistance =
        std::sqrt(offset.x * offset.x + offset.z * offset.z);
    // ここまで追いつかれたら捕まっている
    if (horizontalDistance <= PursuitCatchDistance)
    {
        StartCaughtSequence(*player, CaughtSequence::Reason::FinalPursuit);
        return;
    }

    // 追ってくる影は、裸足の濡れた足跡を残していく（左右の足を交互に、歩いた向きに合わせている）
    {
        Vector3 moved = shadow->GetPosition() - m_PursuitLastShadowPosition;
        moved.y = 0.0f;
        const float travel = moved.Length();
        m_PursuitLastShadowPosition = shadow->GetPosition();
        m_PursuitStepDistance += travel;
        Ground* ground = m_Objects.ground;
        if (ground != nullptr && travel > MinimumDirectionLength && m_PursuitStepDistance >= FootprintFollower::Stride)
        {
            m_PursuitStepDistance -= FootprintFollower::Stride;
            const float yaw = std::atan2(moved.x, moved.z);
            const Vector2 right(std::cos(yaw), -std::sin(yaw));
            const Vector2 position = Vector2(shadow->GetPosition().x, shadow->GetPosition().z) +
                right * (FootprintFollower::SideOffset * m_PursuitStepSide);
            ground->AddFootprint(position, yaw, m_PursuitStepSide, true, 1.0f);
            m_PursuitStepSide = -m_PursuitStepSide;
        }
    }
    // 影が通り過ぎた照明は、ぱちっと消える（背後から闇が迫ってくる）
    for (CeilingLight* light : m_Objects.lights)
    {
        if (light != nullptr && !light->IsForcedOff() &&
            shadow->GetPosition().z > light->GetPosition().z + PursuitLightPassMargin)
        {
            light->SetForcedOff(true);
            game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, light->GetPosition(),
                PursuitLightOffSound.Pitch, PursuitLightOffSound.Volume);
        }
    }

    // 近さ（0〜1）：近いほど1に近づく
    const float proximity = MathUtility::Proximity(horizontalDistance, PursuitNearDistance, PursuitProximityRange);
    const float dangerPulse =
        std::sin(m_VisualTimer * (PursuitPulseSpeedBase + proximity * PursuitPulseSpeedRange)) *
        0.5f + 0.5f;

    game->GetPostProcess()->SetCorridorTension(
        PursuitTensionBase + proximity * PursuitTensionRange);
    if (m_FinalDoorReady)
    {
        game->GetPostProcess()->SetAtmosphere(
            PursuitNoiseBase + proximity * PursuitNoiseRange + dangerPulse * PursuitNoisePulse,
            PursuitVignetteBase + proximity * PursuitVignetteRange);
        game->GetPostProcess()->SetLensDistortionStrength(
            PursuitDistortionBase + proximity * PursuitDistortionRange +
            dangerPulse * proximity * PursuitDistortionPulse);
        game->GetPostProcess()->SetVolumetricIntensity(
            PursuitVolumetricBase + proximity * PursuitVolumetricRange);
        game->GetPostProcess()->SetFilmGradeStrength(
            PursuitFilmGradeBase + proximity * PursuitFilmGradeRange);
        game->GetPostProcess()->SetLensDirtStrength(
            PursuitLensDirtBase + proximity * PursuitLensDirtRange);
        game->GetPostProcess()->SetExposure(
            1.0f - proximity * PursuitExposureDrop -
            dangerPulse * proximity * PursuitExposurePulse);
    }

    // 影を見てしまった罰の間は、画面を暗く乱している
    if (m_FinalSequence.HasGazePenalty())
    {
        const float penalty = m_FinalSequence.GetGazePenaltyRate();
        game->GetPostProcess()->SetAtmosphere(
            GazePenaltyNoiseBase + penalty * GazePenaltyNoiseRange,
            GazePenaltyVignetteBase + penalty * GazePenaltyVignetteRange);
        game->GetPostProcess()->SetLensDistortionStrength(
            GazePenaltyDistortionBase + penalty * GazePenaltyDistortionRange);
        game->GetPostProcess()->SetLensDirtStrength(GazePenaltyLensDirtBase + penalty * GazePenaltyLensDirtRange);
        constexpr float PenaltyMinimumExposure = 0.91f;
        constexpr float PenaltyExposureRange = 0.06f;
        game->GetPostProcess()->SetExposure(PenaltyMinimumExposure + (1.0f - penalty) * PenaltyExposureRange);
    }

    // 近いほど短い間隔で、振動と画面の脈動を出している
    if (!m_FinalSequence.AdvancePursuitPulse(deltaTime))
    {
        return;
    }

    const int vibrationFrames =
        PursuitRumbleBase.Frames + static_cast<int>(proximity * static_cast<float>(PursuitRumbleRange.Frames));
    Input::SetVibration(
        vibrationFrames,
        PursuitRumbleBase.Strength + proximity * PursuitRumbleRange.Strength);
    if (proximity > PursuitPulseMinProximity)
    {
        game->GetPostProcess()->TriggerHorrorPulse(
            PursuitPulseBase.Strength + proximity * PursuitPulseStrengthRange,
            PursuitPulseBase.Seconds);
    }
    m_FinalSequence.SchedulePursuitPulse(proximity);
}

// 捕まった演出を始めている（追跡・息を潜める操作・隠れている状態を止め、影を消している）
void Stage2Scene::StartCaughtSequence(
    Player& player,
    CaughtSequence::Reason reason)
{
    if (!m_CaughtSequence.Start(reason))
    {
        return;
    }

    m_QuietRecovery.Reset();
    m_FinalSequence.StopForCaught();
    // ロッカーの中で捕まった場合も、外へ出た扱いにしてから入口へ戻している。
    player.ForceExitHiding();
    m_HiddenStalkerTimer = -1.0f;
    player.SetCanControl(false);

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterCaught();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    ShadowMan* shadow = m_Objects.shadow;
    if (shadow != nullptr)
    {
        shadow->SetActive(false);
    }
    ShadowMan* noiseShadow = m_Objects.noiseShadow;
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
    // 捕まっている間はUpdateBehindPresenceが呼ばれないため、ここで背後の気配も消している。
    m_Objects.presence->SetActive(false);
    m_BehindPresence.Postpone(BehindPresence::MaxInterval);

    EventFeedback::HorrorPulse(*game->GetPostProcess(), CaughtPulse);
    EventFeedback::BloomPulse(*game->GetPostProcess(), CaughtBloom);
    EventFeedback::Rumble(CaughtRumble);
}

// 捕まった演出を進め、暗転した後に入口へ戻している
void Stage2Scene::UpdateCaughtSequence(Player& player, float deltaTime)
{
    m_CaughtSequence.Advance(deltaTime);
    Core::Game* game = Core::Game::GetInstance();

    if (!m_CaughtSequence.IsReadyToRecover())
    {
        const float darkness = m_CaughtSequence.GetFadeRate();
        game->GetPostProcess()->SetExposure(
            CaughtExposureBase - darkness * CaughtExposureDrop);
        game->GetPostProcess()->SetAtmosphere(
            CaughtNoiseBase + darkness * CaughtNoiseRange,
            CaughtVignetteBase + darkness * CaughtVignetteRange);
        game->GetPostProcess()->SetLensDistortionStrength(
            CaughtDistortionBase + darkness * CaughtDistortionRange);
        return;
    }

    // 足音の影に捕まった場合は今の周回を残し、入口へ戻して再挑戦させている。
    player.SetPosition(EntrancePosition);
    player.RestoreStamina();
    player.SetCanControl(true);
    const bool wasNoiseCatch = m_CaughtSequence.WasNoiseStalker();
    m_CaughtSequence.Complete();
    // 最後の追跡で捕まった場合は、停電の演出をもう一度最初からにして、扉を閉め直している
    if (!wasNoiseCatch)
    {
        m_FinalSequence.ResetSequenceForRetry();
        m_FinalSequenceArmed = true;
        m_FinalDoorReady = false;
    }
    else
    {
        m_NoiseThreatSystem.SetThreat(NoiseCatchResetThreat);
        m_NoiseThreatSystem.SetStalkerCooldown(NoiseCatchStalkerCooldown);
        m_NoiseThreatSystem.SetStalkerNoticeTimer(NoiseCatchStalkerNoticeSeconds);
    }
    m_Notices.loop = Stage2ImportantNoticeSeconds;

    Door* door = m_Objects.door;
    if (door != nullptr && !wasNoiseCatch)
    {
        door->ResetClosed(FinalDoorRetryOpeningPhase);
        door->SetLocked(true);
    }

    ExitTrigger* exit = m_Objects.exit;
    if (exit != nullptr && !wasNoiseCatch)
    {
        exit->SetInteractionEnabled(false);
    }

    Wall* indicator = m_Objects.doorIndicator;
    if (indicator != nullptr && !wasNoiseCatch)
    {
        indicator->SetAppearance(
            DoorIndicatorLockedDiffuse, DoorIndicatorLockedEmission, DoorIndicatorLockedShininess);
    }

    EventFeedback::HorrorPulse(*game->GetPostProcess(), CaughtRecoveredPulse);
    EventFeedback::Rumble(CaughtRecoveredRumble);
}

// 指定の範囲の傷を表示し、少しずつ色を変えて赤く光らせている
void Stage2Scene::RevealScratchPieces(int first, int last, float emission)
{
    first = (std::clamp)(first, 0, Stage2ScratchCount);
    last = (std::clamp)(last, first, Stage2ScratchCount);

    for (int index = first; index < last; ++index)
    {
        Wall* scratch = m_Objects.scratches[static_cast<std::size_t>(index)];
        if (scratch == nullptr)
        {
            continue;
        }

        const float variation = static_cast<float>(index % ScratchColorCycle) * ScratchColorVariation;
        // 新しく出る傷は、焦げた縁が広がるように壁へ浮かび上がらせている
        if (!scratch->IsVisible())
        {
            scratch->DissolveIn(ScratchDissolveSeconds);
        }
        scratch->SetAppearance(
            Color(ScratchDiffuseRed + variation, ScratchDiffuseGreen, ScratchDiffuseBlue, 1.0f),
            Color(emission + variation, ScratchEmissionGreen, 0.0f, 1.0f),
            ScratchShininess);
    }
}

// 照明の区画：廊下を進むと照明を明滅させ、2周目以降は通り過ぎた照明を消している
void Stage2Scene::UpdateLightZones(const Player& player)
{
    if (m_LoopCount >= Stage2FinalLoopCount || m_FinalSequence.IsSequenceActive())
    {
        return;
    }

    struct LightZone
    {
        float TriggerZ;
        Stage2Light Light;
    };
    constexpr LightZone zones[] =
    {
        { LightZone1Z, Stage2Light::Light1 },
        { LightZone2Z, Stage2Light::Light2 },
        { LightZone3Z, Stage2Light::Light3 },
        { LightZoneDoorZ, Stage2Light::DoorLight }
    };
    constexpr int zoneCount =
        static_cast<int>(sizeof(zones) / sizeof(zones[0]));

    Core::Game* game = Core::Game::GetInstance();
    for (int index = 0; index < zoneCount; ++index)
    {
        if (!m_LightZoneProgress.TryEnter(
            index, player.GetPosition().z, zones[index].TriggerZ))
        {
            continue;
        }
        CeilingLight* light = m_Objects.Light(zones[index].Light);
        const float loopStrength = static_cast<float>(m_LoopCount) * LightZoneLoopStrength;
        const float strength = (std::clamp)(
            LightZoneBaseStrength + loopStrength + static_cast<float>(index) * LightZoneIndexStrength,
            0.0f,
            LightZoneMaxStrength);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(LightZoneFlickerBaseSeconds + loopStrength, strength);
        }

        // 2周目以降はプレイヤーの背後から照明を消し、暗闇が迫るように見せている。
        // 次の周回の始まりに照明を戻し、同じ廊下を使い直せる状態にしている。
        if (m_LoopCount > 0 && index > 0)
        {
            CeilingLight* lightBehind = m_Objects.Light(zones[index - 1].Light);
            if (lightBehind != nullptr)
            {
                lightBehind->SetForcedOff(true);
            }
        }

        game->GetPostProcess()->TriggerBloomPulse(
            LightZoneBloomBase + strength * LightZoneBloomPerStrength,
            LightZoneBloomSeconds);
        if (m_LoopCount > 0 && index >= 1)
        {
            game->GetPostProcess()->TriggerHorrorPulse(
                LightZonePulseBase + loopStrength * LightZonePulsePerLoop,
                LightZonePulseSeconds);
        }
        Input::SetVibration(LightZoneRumbleBase.Frames + m_LoopCount,
            LightZoneRumbleBase.Strength + loopStrength * LightZoneRumblePerLoop);
    }
}

// 壁の傷：周回が進むほど増え、ライトで照らすと心臓の鼓動のように赤く脈打っている
void Stage2Scene::UpdateScratchMessage(
    const Player& player,
    float deltaTime)
{
    if (m_LoopCount <= 0)
    {
        return;
    }

    if (!m_ScratchAnomaly.ConsumeUpdateInterval(deltaTime))
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Vector3 cameraToMessage =
        ScratchMessageCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToMessage.Length();
    if (distance > MinimumDirectionLength)
    {
        cameraToMessage /= distance;
    }

    // 見ている度合い（正面ほど1）と近さ（24〜119）から、照らしている強さを求めている
    const float facing = (std::max)(
        game->GetCamera()->GetForward().Dot(cameraToMessage),
        0.0f);
    const float proximity = MathUtility::Proximity(distance, ScratchNearDistance, ScratchFadeRange);
    const float gaze = (std::clamp)(
        (facing - ScratchGazeStartFacing) / ScratchGazeRange,
        0.0f,
        1.0f);
    const float flashlightResponse =
        player.IsFlashlightOn() ? gaze * proximity : 0.0f;
    const float heartbeat =
        std::sin(m_VisualTimer * (ScratchHeartbeatSpeedBase + m_LoopCount * ScratchHeartbeatSpeedPerLoop)) *
        0.5f + 0.5f;
    const float finalBoost =
        (m_FinalSequence.IsSequenceActive() && !m_FinalDoorReady)
            ? ScratchFinalBoost : 0.0f;
    const float emission =
        ScratchEmissionBase + static_cast<float>(m_LoopCount) * ScratchEmissionPerLoop +
        flashlightResponse * (ScratchLitEmissionBase + heartbeat * ScratchLitEmissionHeartbeat) + finalBoost;

    // 周回に合わせた数の傷を見せ、2周目以降に近くで正面から照らしたら一度だけ驚かせている
    const int visibleCount = m_ScratchAnomaly.GetVisiblePieceCount(
        m_LoopCount, Stage2ScratchCount);
    RevealScratchPieces(0, visibleCount, emission);

    if (m_ScratchAnomaly.TryTriggerScare(
        m_LoopCount, player.IsFlashlightOn(), distance, facing))
    {
        game->GetPostProcess()->TriggerHorrorPulse(
            ScratchScarePulseBase + static_cast<float>(m_LoopCount) * ScratchScarePulsePerLoop,
            ScratchScarePulseSeconds);
        EventFeedback::BloomPulse(*game->GetPostProcess(), ScratchScareBloom);
        EventFeedback::Rumble(ScratchScareRumble);
    }
}

// 血の手形を見つける周回（Stage2AnomalyPlanが決める）だけ判定している。
// 壁に浮かんだ最後の手形を懐中電灯で照らし続けると、奥の反対側の壁に次の手形が「ぺたっ」と現れる。
// 4つ目まで照らすと見つけたことになる。手形の形と見え方は、毎フレーム壁のシェーダーへ渡している。
void Stage2Scene::UpdateHandprintAnomaly(const Player& player, float deltaTime)
{
    const bool active = m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Handprint);
    Core::Game* game = Core::Game::GetInstance();

    // 最後に現れた手形を照らしているか（ライトが点いていて、近く（110未満）で正面（内積0.93超）から見ている）
    bool litLatest = false;
    if (active && player.IsFlashlightOn())
    {
        Vector3 toPrint = m_HandprintAnomaly.GetLatestPosition() - game->GetCamera()->GetPosition();
        const float distance = toPrint.Length();
        if (distance > MinimumDirectionLength)
        {
            toPrint /= distance;
            litLatest = distance < HandprintLitDistance &&
                game->GetCamera()->GetForward().Dot(toPrint) > HandprintLitFacing;
        }
    }

    const HandprintAnomaly::Event handprintEvent = m_HandprintAnomaly.Update(deltaTime, active, litLatest);
    if (handprintEvent == HandprintAnomaly::Event::NextAppeared)
    {
        // 次の手形が現れた所で、濡れた手を壁に押し付けたような低い水音を鳴らし、近くの照明を一瞬揺らしている
        const Vector3 nextPrint = m_HandprintAnomaly.GetLatestPosition();
        game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, nextPrint, HandprintAppearSound.Pitch, HandprintAppearSound.Volume);
        CeilingLight* nearbyLight = m_Objects.Light(Stage2NearestLight(nextPrint.z));
        if (nearbyLight != nullptr)
        {
            EventFeedback::Flicker(*nearbyLight, HandprintAppearFlicker);
        }
        EventFeedback::HorrorPulse(*game->GetPostProcess(), HandprintAppearPulse);
        EventFeedback::Rumble(HandprintAppearRumble);
    }
    else if (handprintEvent == HandprintAnomaly::Event::Found)
    {
        // 最後の手形を照らした：奥の確認スイッチが押せるようになる
        m_Notices.loop = Stage2NoticeSeconds;
        game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, m_HandprintAnomaly.GetLatestPosition(),
            HandprintFoundSound.Pitch, HandprintFoundSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), HandprintFoundPulse);
        EventFeedback::BloomPulse(*game->GetPostProcess(), HandprintFoundBloom);
        EventFeedback::Rumble(HandprintFoundRumble);
    }

    // 壁のシェーダーへ、手形の場所と見え方を渡している（この周回でなければ描かない）
    std::array<Vector4, HandprintAnomaly::PrintCount> prints{};
    for (int index = 0; index < HandprintAnomaly::PrintCount; ++index)
    {
        const DirectX::XMFLOAT3& position = HandprintAnomaly::Positions[static_cast<std::size_t>(index)];
        prints[static_cast<std::size_t>(index)] = Vector4(
            position.x, position.y, position.z, m_HandprintAnomaly.GetVisibility(index));
    }
    Renderer::SetWallHandprints(prints.data(), active ? HandprintAnomaly::PrintCount : 0);
}

// 鏡を見つける周回（Stage2AnomalyPlanが決める）だけ判定している。
// 左の壁の鏡をのぞくと、すぐ後ろに黒い人影が映っている（人影は物として置かず、鏡のシェーダーが鏡の中にだけ描く）。
// 鏡で見た後に振り返り、人影が立っていた所を見ると、そこには誰もいない。それで見つけたことになる。
void Stage2Scene::UpdateMirrorAnomaly(const Player& player, float deltaTime)
{
    const bool active = m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Mirror);
    Core::Game* game = Core::Game::GetInstance();
    const Camera* camera = game->GetCamera();
    const MirrorAnomaly::Event mirrorEvent = m_MirrorAnomaly.Update(
        deltaTime, active, camera->GetPosition(), camera->GetForward(), player.GetPosition());

    // 鏡の異変の周回で、まだ見つけていない間は、鏡の中の自分だけが少し遅れて動く
    if (m_Objects.mirrorSelf != nullptr)
    {
        m_Objects.mirrorSelf->SetDelaySeconds(active && !m_MirrorAnomaly.WasFound() ? MirrorSelfDelaySeconds : 0.0f);
    }

    // 鏡の中の人影を、異変の周回で、まだ見つけていない間だけ映している（0.35秒で現れ、見つけたら0.5秒で消える）。
    // 人影は物として置かず、位置と見え方を鏡のシェーダーへ渡し、鏡で反射した光線の上にだけ黒い人の形を重ねている
    const bool showFigure = active && m_MirrorAnomaly.IsFigureVisible();
    m_MirrorFigureVisibility = showFigure
        ? (std::min)(1.0f, m_MirrorFigureVisibility + deltaTime / MirrorFigureFadeInSeconds)
        : (std::max)(0.0f, m_MirrorFigureVisibility - deltaTime / MirrorFigureFadeOutSeconds);
    Renderer::SetMirrorFigure(m_MirrorAnomaly.GetFigurePosition(), m_MirrorFigureVisibility);

    if (mirrorEvent == MirrorAnomaly::Event::Seen)
    {
        // 鏡の中の人影に気づいた：すぐ後ろから、かすかな息づかいが聞こえ、心拍がひとつ跳ねる
        game->PlayAudioCueAt(SOUND_CUE_BREATH, m_MirrorAnomaly.GetFigurePosition(),
            MirrorSeenBreathSound.Pitch, MirrorSeenBreathSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), MirrorSeenPulse);
        EventFeedback::Rumble(MirrorSeenRumble);
    }
    else if (mirrorEvent == MirrorAnomaly::Event::Found)
    {
        // 振り返ると誰もいない：鏡の上の照明が一瞬揺れ、奥の確認スイッチが押せるようになる
        m_Notices.loop = Stage2NoticeSeconds;
        KickHangingBulbs(MirrorFoundBulbKick);
        CeilingLight* mirrorLight = m_Objects.Light(Stage2NearestLight(MirrorAnomaly::CenterZ));
        if (mirrorLight != nullptr)
        {
            EventFeedback::Flicker(*mirrorLight, MirrorFoundFlicker);
        }
        game->PlayAudioCueAt(
            SOUND_CUE_PIPE_KNOCK,
            Vector3(MirrorAnomaly::SurfaceX, MirrorAnomaly::CenterY, MirrorAnomaly::CenterZ),
            MirrorFoundKnockSound.Pitch, MirrorFoundKnockSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), MirrorFoundPulse);
        EventFeedback::Rumble(MirrorFoundRumble);
    }
}

// 水たまりを見つける周回（Stage2AnomalyPlanが決める）だけ判定している。
// 中央の水たまりを足元からのぞくと、向こう側に黒い人影が立っているのが水面にだけ映る（床のシェーダーのレイ反射で描いている）。
// のぞき続けると水を踏みながら近づいて来る。顔を上げて人影が立っていた方を見ると、そこには誰もいない。それで見つけたことになる。
void Stage2Scene::UpdatePuddleAnomaly(const Player& player, float deltaTime)
{
    const bool active = m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Puddle);
    Core::Game* game = Core::Game::GetInstance();
    const Camera* camera = game->GetCamera();
    const Ground* ground = m_Objects.ground;
    const auto isInsidePuddle = [ground](const Vector3& position)
    {
        return ground != nullptr && ground->IsInsidePuddle(position);
    };
    const PuddleReflectionAnomaly::Event puddleEvent = m_PuddleAnomaly.Update(
        deltaTime, active, camera->GetPosition(), camera->GetForward(), player.GetPosition(), isInsidePuddle);

    // 人影の足もとの位置と見え方を、床のシェーダー（レイ反射）へ渡している。見え方が0なら描かない
    const Vector3 figurePosition = m_PuddleAnomaly.GetFigurePosition();
    // 最後の追跡では、迫ってくる影も水面に映している。影を直接見ると罰があるが、水面に映った影なら見ても罰はなく、
    // 振り返って足元の水たまりをのぞけば、追っ手がどこまで来ているかを確かめられる。
    // 影は物としても描いているが、画面の外にある上半身まで水面に映るよう、レイ反射の人影としても重ねている
    const ShadowMan* pursuer = m_Objects.shadow;
    if (m_FinalSequence.IsPursuitActive() && pursuer != nullptr && pursuer->IsActive())
    {
        const Vector3 pursuerPosition = pursuer->GetPosition();
        Renderer::SetPuddleFigure(
            Vector3(pursuerPosition.x, PuddleReflectionAnomaly::FloorY, pursuerPosition.z), 1.0f);
    }
    else
    {
        Renderer::SetPuddleFigure(figurePosition, m_PuddleAnomaly.GetVisibility());
    }

    if (puddleEvent == PuddleReflectionAnomaly::Event::Seen)
    {
        // 水面の人影に気づいた：水たまりの向こうから、かすかな息づかいが聞こえ、心拍がひとつ跳ねる
        game->PlayAudioCueAt(SOUND_CUE_BREATH, figurePosition + Vector3(0.0f, PuddleFigureHeadHeight, 0.0f),
            PuddleSeenBreathSound.Pitch, PuddleSeenBreathSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), PuddleSeenPulse);
        EventFeedback::Rumble(PuddleSeenRumble);
    }
    else if (puddleEvent == PuddleReflectionAnomaly::Event::Step)
    {
        // 水面をのぞき続けている：人影が水を踏んで一歩近づく（足もとから波紋が広がり、ぺたりと水音が鳴る）
        if (m_Objects.ground != nullptr)
        {
            m_Objects.ground->AddRippleImpulse(figurePosition, PuddleStepRippleStrength, PuddleStepRippleSpeed);
        }
        game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, figurePosition, PuddleStepSound.Pitch, PuddleStepSound.Volume);
        EventFeedback::Rumble(PuddleStepRumble);
    }
    else if (puddleEvent == PuddleReflectionAnomaly::Event::Caught)
    {
        // すぐそばまで来られた：照明が揺れて驚かされ、人影はまた水たまりの向こうに立ち直す
        game->PlayAudioCue(SOUND_CUE_SCARE, PuddleCaughtScareSound.Pitch, PuddleCaughtScareSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), PuddleCaughtPulse);
        KickHangingBulbs(PuddleCaughtBulbKick);
        CeilingLight* puddleLight = m_Objects.Light(Stage2NearestLight(PuddleReflectionAnomaly::PuddleCenterZ));
        if (puddleLight != nullptr)
        {
            EventFeedback::Flicker(*puddleLight, PuddleCaughtFlicker);
        }
        EventFeedback::Rumble(PuddleCaughtRumble);
    }
    else if (puddleEvent == PuddleReflectionAnomaly::Event::Found)
    {
        // 顔を上げると誰もいない：立っていた所の水面だけが揺れていて、奥の確認スイッチが押せるようになる
        m_Notices.loop = Stage2NoticeSeconds;
        if (m_Objects.ground != nullptr)
        {
            m_Objects.ground->AddRippleImpulse(figurePosition, PuddleFoundRippleStrength, PuddleFoundRippleSpeed);
        }
        KickHangingBulbs(PuddleFoundBulbKick);
        CeilingLight* puddleLight = m_Objects.Light(Stage2NearestLight(PuddleReflectionAnomaly::PuddleCenterZ));
        if (puddleLight != nullptr)
        {
            EventFeedback::Flicker(*puddleLight, PuddleFoundFlicker);
        }
        game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, figurePosition,
            PuddleFoundStepSound.Pitch, PuddleFoundStepSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), PuddleFoundPulse);
        EventFeedback::Rumble(PuddleFoundRumble);
    }
}

// 肖像画を見つける周回（Stage2AnomalyPlanが決める）だけ判定している。
// 懐中電灯で照らしたまま見つめ続けると、絵の中の閉じていた目がかすかに開きかけ、やがて見開く。
// 途中で目を離すと、目は閉じて最初からやり直しにしている。
void Stage2Scene::UpdatePortraitAnomaly(const Player& player, float deltaTime)
{
    if (!m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Portrait) ||
        m_PortraitAnomaly.HasChangedThisLoop())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Vector3 cameraToPortrait =
        PortraitCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToPortrait.Length();
    if (distance > MinimumDirectionLength)
    {
        cameraToPortrait /= distance;
    }

    const float facing =
        game->GetCamera()->GetForward().Dot(cameraToPortrait);
    // ライトが点いていて、近く（105未満）で正面（内積0.91超）から見ているか
    const bool lookingAtPortrait =
        player.IsFlashlightOn() && distance < PortraitLookDistance && facing > PortraitLookFacing;
    // 見つめている間は、絵の中の閉じた目がかすかに開きかけ、目を離すと閉じる（目の見え方は UpdatePortraitEyes）。
    if (!m_PortraitAnomaly.UpdateStare(lookingAtPortrait, deltaTime))
    {
        return;
    }

    // 見つめ続けた: 絵の中で目が見開かれ、赤く光る瞳がこちらを追い始める。奥の確認スイッチが押せるようになる。
    m_Notices.loop = Stage2NoticeSeconds;
    Wall* portrait = m_Objects.portrait;
    if (portrait != nullptr)
    {
        portrait->SetAppearance(
            PortraitOpenedDiffuse,
            Color(PortraitOpenedEmission * PortraitOpenedEmissionScale, 0.0f, 0.0f, 1.0f),
            PortraitOpenedShininess);
    }

    // 肖像画は時計の向かい（z=-25）にあるため、近くの照明と奥の扉の照明を揺らしている。
    CeilingLight* nearbyLight = m_Objects.Light(Stage2Light::Light2);
    if (nearbyLight != nullptr)
    {
        EventFeedback::Flicker(*nearbyLight, PortraitNearbyFlicker);
    }
    CeilingLight* doorLight = m_Objects.Light(Stage2Light::DoorLight);
    if (doorLight != nullptr)
    {
        EventFeedback::Flicker(*doorLight, DoorLightAlertFlicker);
    }
    EventFeedback::HorrorPulse(*game->GetPostProcess(), PortraitOpenedPulse);
    EventFeedback::Rumble(PortraitOpenedRumble);
}

// 肖像画の絵の中の目：普段は閉じている。肖像画の異変の周回で見つめている間はかすかに開きかけ（目を離すとすぐ閉じる）、
// 見つめ続けて異変を見つけると見開かれ、赤く光る瞳が、見ている人の方へ動いて目で追い続ける。
void Stage2Scene::UpdatePortraitEyes(float deltaTime)
{
    Wall* portrait = m_Objects.portrait;
    if (portrait == nullptr)
    {
        return;
    }
    const bool required = m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Portrait);
    // 最後の停電と追跡の間も、目を見開いて、逃げる人を目で追う
    const bool finalEvent = m_FinalSequence.IsSequenceActive() || m_FinalSequence.IsPursuitActive();
    const bool opened = (required && m_PortraitAnomaly.HasChangedThisLoop()) || finalEvent;
    const float targetOpen = opened
        ? 1.0f
        : (required && m_PortraitAnomaly.IsStaring() ? PortraitStareOpen * m_PortraitAnomaly.GetStareRate() : 0.0f);
    // 開くときはゆっくり、閉じるときはすばやく（目を離した瞬間に閉じる）
    const float openSpeed = targetOpen > m_PortraitEyeOpen ? PortraitEyeOpenSpeed : PortraitEyeCloseSpeed;
    m_PortraitEyeOpen = targetOpen > m_PortraitEyeOpen
        ? (std::min)(targetOpen, m_PortraitEyeOpen + openSpeed * deltaTime)
        : (std::max)(targetOpen, m_PortraitEyeOpen - openSpeed * deltaTime);
    m_PortraitEyeGlow = (std::clamp)(m_PortraitEyeGlow + (opened ? PortraitEyeGlowRiseSpeed : -PortraitEyeGlowFallSpeed) * deltaTime, 0.0f, 1.0f);

    // 瞳の向き：絵の目の高さ・位置から見たカメラの方向（絵の面からの距離で割った横と縦のずれ）
    const Vector3 toCamera = Core::Game::GetInstance()->GetCamera()->GetPosition() - PortraitEyeCenter;
    const float depth = (std::max)(-toCamera.x, 1.0f);
    const float gazeX = (std::clamp)(toCamera.z / depth, -1.0f, 1.0f);
    const float gazeY = (std::clamp)(toCamera.y / depth, -1.0f, 1.0f);
    portrait->SetPortraitEyes(m_PortraitEyeOpen, gazeX, gazeY, m_PortraitEyeGlow);
}

// ----------------------------------------------------------------------------
// 壁の向こうのノック
// ノックを見つける周回（Stage2AnomalyPlanが決める）だけ、壁の裏から叩く音を立体音響で鳴らしている。
// 出どころの壁の前で立ち止まり、壁の方を向いて耳を澄ますと見つけたことになる。
// ----------------------------------------------------------------------------
namespace
{
    constexpr float KnockListenDistance = 24.0f;    // 出どころの壁の前とみなす距離
    constexpr float KnockListenFacing = 0.60f;      // 壁の方を向いているとみなす内積

    // 音の出どころの番号から位置を作っている
    Vector3 GetKnockSpot(int index)
    {
        const float* spot = Stage2KnockSpots[index];
        return Vector3(spot[0], spot[1], spot[2]);
    }
}

// 音の出どころの壁の手前（廊下の内側）の位置を返している。耳を澄ます場所と、案内の矢印の先に使っている。
Vector3 Stage2Scene::GetKnockListenPoint() const
{
    const Vector3 spot = GetKnockSpot(m_KnockingAnomaly.GetSpot());
    return Vector3(spot.x > 0.0f ? KnockListenX : -KnockListenX, spot.y, spot.z);
}

// ノックの異変：決まったリズムで壁の裏から叩く音を鳴らし、聞き当てたかを判定している
void Stage2Scene::UpdateKnockingAnomaly(const Player& player, float deltaTime)
{
    if (!m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Knocking) ||
        m_KnockingAnomaly.WasFound())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 spot = GetKnockSpot(m_KnockingAnomaly.GetSpot());
    if (m_KnockingAnomaly.UpdateKnock(deltaTime))
    {
        // 配管の音より低くし、木の扉を拳で叩くような鈍い音にしている。
        std::uniform_real_distribution<float> pitch(KnockMinPitch, KnockMaxPitch);
        game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, spot, pitch(m_PresenceRandom), KnockVolume);
    }

    // 壁の手前（廊下の内側）の位置と、プレイヤーとの距離・向きで「耳を澄ませているか」を判定している（立ち止まっていることも条件）。
    const Vector3 listenPoint = GetKnockListenPoint();
    const Camera* camera = game->GetCamera();
    Vector3 toSpot = listenPoint - camera->GetPosition();
    toSpot.y = 0.0f;
    const float distance = toSpot.Length();
    Vector3 forward = camera->GetForward();
    forward.y = 0.0f;
    const bool facing = distance > MinimumDirectionLength && forward.LengthSquared() > MinimumHorizontalForwardSquared &&
        forward.Dot(toSpot / distance) / forward.Length() > KnockListenFacing;
    const bool listening = distance < KnockListenDistance && facing &&
        !player.IsMovingHorizontally();
    if (!m_KnockingAnomaly.UpdateListen(listening, deltaTime))
    {
        return;
    }

    // 聞き当てた: 壁のすぐ向こうで一度だけ強く叩き、音が止まる。
    game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, spot, KnockFoundSound.Pitch, KnockFoundSound.Volume);
    m_Notices.loop = Stage2NoticeSeconds;
    CeilingLight* nearbyLight = m_Objects.Light(Stage2NearestLight(spot.z));
    if (nearbyLight != nullptr)
    {
        EventFeedback::Flicker(*nearbyLight, KnockFoundFlicker);
    }
    CeilingLight* doorLight = m_Objects.Light(Stage2Light::DoorLight);
    if (doorLight != nullptr)
    {
        EventFeedback::Flicker(*doorLight, DoorLightAlertFlicker);
    }
    EventFeedback::HorrorPulse(*game->GetPostProcess(), KnockFoundPulse);
    EventFeedback::Rumble(KnockFoundRumble);
}

// 見えない影の異変：姿の見えない影を廊下の奥から歩かせ、近づかれたら捕まえ、すれ違えたら見つけたことにしている。
// 影の近さは、携帯ラジオの雑音（UpdateTensionPulse）と心拍（ComputeThreatRate）でだけ伝えている。
void Stage2Scene::UpdateUnseenPresence(Player& player, float deltaTime)
{
    const bool enabled = m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Unseen) &&
        !m_FinalSequence.IsSequenceActive();
    // 懐中電灯の光が、見えない影の胸のあたりに当たっているか（光の円錐の中心付近で、75より近い）
    bool lit = false;
    if (enabled && m_UnseenPresence.IsActive() && player.IsFlashlightOn())
    {
        const Camera* camera = Core::Game::GetInstance()->GetCamera();
        const Vector3 toChest = m_UnseenPresence.GetPosition() + Vector3(0.0f, UnseenChestHeight, 0.0f) - camera->GetPosition();
        const float distance = toChest.Length();
        lit = distance > MinimumDirectionLength && distance < UnseenLitDistance &&
            camera->GetForward().Dot(toChest / distance) > UnseenLitFacing;
    }
    const UnseenPresenceAnomaly::Result result = m_UnseenPresence.Update(
        deltaTime, player.GetPosition(), player.IsMovingHorizontally(), player.IsHiding(), enabled, lit);

    // 無効化・捕獲・周回切替時の瞬間移動は歩行距離に数えない。
    Ground* ground = m_Objects.ground;
    if (!enabled || result != UnseenPresenceAnomaly::Result::None || !m_UnseenPresence.IsActive())
    {
        m_UnseenHasStep = false;
        m_UnseenWetSteps = 0;
    }
    else if (ground != nullptr)
    {
        const Vector3 position = m_UnseenPresence.GetPosition();
        if (!m_UnseenHasStep)
        {
            m_UnseenLastStep = position;
            m_UnseenHasStep = true;
        }
        // 水に触れた直後だけ9歩残す。乾いた通路では新しく濡れない。
        if (ground->IsInsidePuddle(position)) m_UnseenWetSteps = UnseenWetStepCount;
        Vector3 direction = position - m_UnseenLastStep;
        direction.y = 0.0f;
        if (direction.LengthSquared() >= UnseenStride * UnseenStride)
        {
            const float yaw = std::atan2(direction.x, direction.z);
            // 水たまりの中を歩くと、姿は見えないまま、足もとから波紋が広がり、ぴちゃりと水音が鳴る
            // （遠くからでも「あの水たまりを今通った」と分かる手がかり）
            if (ground->IsInsidePuddle(position))
            {
                ground->AddRippleImpulse(Vector3(position.x, FloorY, position.z), UnseenRippleStrength, UnseenRippleSpeed);
                Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_WATER_STEP, position,
                    UnseenWaterStepSound.Pitch, UnseenWaterStepSound.Volume);
            }
            if (m_UnseenWetSteps > 0)
            {
                const Vector2 right(std::cos(yaw), -std::sin(yaw));
                // 濡れた足跡は、水から離れるほど薄くしている（最初の一歩で1、最後の一歩で約0.4）
                ground->AddFootprint(Vector2(position.x, position.z) + right * (UnseenFootOffset * m_UnseenFootSide),
                    yaw, m_UnseenFootSide, true,
                    UnseenFootprintMinOpacity + UnseenFootprintOpacityRange * m_UnseenWetSteps / static_cast<float>(UnseenWetStepCount));
                --m_UnseenWetSteps;
                m_UnseenFootSide = -m_UnseenFootSide;
            }
            m_UnseenLastStep = position;
        }
    }
    // 姿の見えない影の「影だけ」を、壁と床に落としている（天井の照明・揺れる裸電球・懐中電灯の光を、人の形で遮る。
    // shader/figureShadow.hlsli）。捕まえた直後に奥へ戻る瞬間移動が目立たないよう、そのフレームは消し、0.4秒で上げ下げしている
    const bool shadowShown = enabled && m_UnseenPresence.IsActive() && result == UnseenPresenceAnomaly::Result::None;
    m_UnseenShadowVisibility = result == UnseenPresenceAnomaly::Result::Caught ? 0.0f
        : shadowShown ? (std::min)(1.0f, m_UnseenShadowVisibility + deltaTime / UnseenShadowFadeSeconds)
        : (std::max)(0.0f, m_UnseenShadowVisibility - deltaTime / UnseenShadowFadeSeconds);
    Vector3 shadowFoot = m_UnseenPresence.GetPosition();
    shadowFoot.y = PuddleReflectionAnomaly::FloorY;
    Renderer::SetUnseenFigure(shadowFoot, m_UnseenShadowVisibility);

    Core::Game* game = Core::Game::GetInstance();
    if (m_UnseenPresence.WasNoticedThisFrame())
    {
        // 照らして気づかれた：影のいる所から短い息づかいが聞こえ、吊るした裸電球が揺れ、心拍が跳ねる
        game->PlayAudioCueAt(SOUND_CUE_BREATH, m_UnseenPresence.GetPosition() + Vector3(0.0f, UnseenHeadHeight, 0.0f),
            UnseenNoticedBreathSound.Pitch, UnseenNoticedBreathSound.Volume);
        KickHangingBulbs(UnseenNoticedBulbKick);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), UnseenNoticedPulse);
        EventFeedback::Rumble(UnseenNoticedRumble);
    }
    if (result == UnseenPresenceAnomaly::Result::Caught)
    {
        // 捕まった：足音の影に捕まったときと同じく、周回はそのままで入口へ戻している
        StartCaughtSequence(player, CaughtSequence::Reason::NoiseStalker);
        return;
    }
    if (result != UnseenPresenceAnomaly::Result::Passed)
    {
        return;
    }

    // すれ違えた：通り過ぎた先の照明が一度明滅し、吊るした裸電球が大きく揺れて、雑音が遠ざかっていく
    const Vector3 presencePosition = m_UnseenPresence.GetPosition();
    CeilingLight* passedLight = m_Objects.Light(Stage2NearestLight(presencePosition.z));
    if (passedLight != nullptr)
    {
        EventFeedback::Flicker(*passedLight, UnseenPassedFlicker);
    }
    CeilingLight* doorLight = m_Objects.Light(Stage2Light::DoorLight);
    if (doorLight != nullptr)
    {
        EventFeedback::Flicker(*doorLight, DoorLightAlertFlicker);
    }
    KickHangingBulbs(UnseenPassedBulbKick);
    game->PlayAudioCueAt(SOUND_CUE_FOOTSTEP, presencePosition, UnseenPassedStepSound.Pitch, UnseenPassedStepSound.Volume);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), UnseenPassedPulse);
    EventFeedback::Rumble(UnseenPassedRumble);
}

// ----------------------------------------------------------------------------
// 背後の気配
// 視界の外に人影を出し、見ていない間だけ近づけている。振り向けば消え、
// 気づかずに背後まで近づかれると、足音の危険度が一気に上がる。
// ----------------------------------------------------------------------------
namespace
{
    // 出す距離（背後58）、近づく速さ、見たと判定する距離と内積、追いつかれたときに上がる危険度
    constexpr float PresenceSpawnDistance = 58.0f;
    constexpr float PresenceCreepSpeed = 17.0f;     // 歩く速さ（30）より遅く、止まれば迫ってくる速さ
    constexpr float PresenceLookDistance = 170.0f;
    constexpr float PresenceLookAlignment = 0.88f;
    constexpr float PresenceReachThreat = 0.35f;
    // 廊下の内側に収め、壁の中に出ないようにしている。
    constexpr float CorridorHalfWidth = 30.0f;
    constexpr float CorridorMinZ = -150.0f;
    constexpr float CorridorMaxZ = 130.0f;
}

bool Stage2Scene::IsBehindPresenceAllowed() const
{
    // ほかの人影や大きな演出と重ねず、周回そのものの静けさの中でだけ出している。
    const bool otherFigureActive =
        (m_Objects.shadow != nullptr && m_Objects.shadow->IsActive()) ||
        (m_Objects.noiseShadow != nullptr && m_Objects.noiseShadow->IsActive());
    const bool hiding = m_Objects.player != nullptr && m_Objects.player->IsHiding();
    return m_LoopCount >= 1 &&
        !otherFigureActive &&
        !hiding &&
        m_LoopTransitionTimer < 0.0f &&
        !m_ObservedScareSequence.IsActive() &&
        !m_FinalSequence.IsSequenceActive() &&
        !m_FinalSequence.IsPursuitActive() &&
        !m_CaughtSequence.IsActive();
}

// 背後の気配：見ているかと距離を調べ、BehindPresenceが返す出来事ごとに、影を出す・消す・驚かせる
void Stage2Scene::UpdateBehindPresence(Player& player, float deltaTime)
{
    ShadowMan* presence = m_Objects.presence;
    Core::Game* game = Core::Game::GetInstance();
    const Camera* camera = game->GetCamera();

    bool looking = false;
    float distance = PresenceFarDistance;
    if (m_BehindPresence.IsFollowing())
    {
        Vector3 toPlayer = player.GetPosition() - presence->GetPosition();
        toPlayer.y = 0.0f;
        distance = toPlayer.Length();

        Vector3 toPresence = presence->GetPosition() + Vector3(0.0f, PresenceChestHeight, 0.0f) -
            camera->GetPosition();
        const float viewDistance = toPresence.Length();
        if (viewDistance > MinimumDirectionLength && viewDistance < PresenceLookDistance)
        {
            toPresence /= viewDistance;
            looking = camera->GetForward().Dot(toPresence) > PresenceLookAlignment;
        }
    }

    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    const BehindPresence::Event presenceEvent = m_BehindPresence.Update(
        deltaTime, IsBehindPresenceAllowed(), looking, distance,
        roll(m_PresenceRandom));

    switch (presenceEvent)
    {
    case BehindPresence::Event::None:
        break;
    case BehindPresence::Event::Spawn:
    {
        // カメラの真後ろへ置いている。廊下の外に出る場合は、次の機会（3秒後）に回している。
        Vector3 backward = -camera->GetForward();
        backward.y = 0.0f;
        if (backward.LengthSquared() < MinimumDirectionLength)
        {
            m_BehindPresence.Postpone(BehindPresenceRetrySeconds);
            break;
        }
        backward.Normalize();
        const Vector3 spawn = player.GetPosition() + backward * PresenceSpawnDistance;
        if (std::abs(spawn.x) > CorridorHalfWidth ||
            spawn.z < CorridorMinZ || spawn.z > CorridorMaxZ)
        {
            m_BehindPresence.Postpone(BehindPresenceRetrySeconds);
            break;
        }

        presence->SetPosition(spawn.x, FootY, spawn.z);
        presence->SetActive(true);
        presence->EnableGazeScare(BehindPresence::MaxFollowSeconds + PresenceGazeExtraSeconds);
        presence->EnableChase(PresenceCreepSpeed, PresenceChaseStopDistance);
        break;
    }
    case BehindPresence::Event::Seen:
        // 振り向いた瞬間に消し、見間違いだったのかと思わせる程度の反応にとどめている。
        presence->SetActive(false);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), PresenceSeenPulse);
        EventFeedback::Rumble(PresenceSeenRumble);
        break;
    // 気づかずに追いつかれた：照明が一斉に明滅し、危険度が上がる
    case BehindPresence::Event::Reached:
        presence->SetActive(false);
        for (CeilingLight* light : m_Objects.lights)
        {
            EventFeedback::Flicker(*light, PresenceReachedFlicker);
        }
        m_NoiseThreatSystem.SetThreat((std::min)(
            1.0f, m_NoiseThreatSystem.GetThreat() + PresenceReachThreat));
        game->PlayAudioCue(SOUND_CUE_SCARE, PresenceReachedScareSound.Pitch, PresenceReachedScareSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), PresenceReachedPulse);
        EventFeedback::Rumble(PresenceReachedRumble);
        break;
    case BehindPresence::Event::Vanished:
        presence->SetActive(false);
        break;
    }
}

// 今の危険度（0〜1）を求めている。足音の危険度、観察の間違い（必要なとき）、最後の追跡の影、足音の影のうち、一番高いものを使っている
float Stage2Scene::ComputeThreatRate(const Player& player, bool includeMistakes) const
{
    float threatRate = m_NoiseThreatSystem.GetThreat() * NoiseThreatToHeartbeat;
    // 見えない影が近いほど、心拍も速くなる
    threatRate = (std::max)(threatRate, m_UnseenPresence.GetProximity(player.GetPosition()) * UnseenProximityToHeartbeat);
    if (includeMistakes && m_LoopCount > 0 && m_LoopCount <= Stage2AnomalyPlan::PlannedLoopCount)
    {
        const float observationDanger =
            static_cast<float>(m_PuzzleFeedback.GetMistakeCount()) / MistakeCountForFullDanger;
        threatRate = (std::max)(threatRate, observationDanger * MistakeDangerToHeartbeat);
    }
    if (m_FinalSequence.IsPursuitActive())
    {
        const ShadowMan* shadow = m_Objects.shadow;
        if (shadow != nullptr)
        {
            Vector3 toShadow = shadow->GetPosition() - player.GetPosition();
            toShadow.y = 0.0f;
            const float distance = toShadow.Length();
            threatRate = MathUtility::Proximity(distance, PursuitDangerNearDistance, PursuitDangerRange);
        }
    }
    const ShadowMan* noiseShadow = m_Objects.noiseShadow;
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float noiseShadowDanger =
            MathUtility::Proximity(distance, NoiseShadowDangerNearDistance, NoiseShadowDangerRange);
        threatRate = (std::max)(threatRate, noiseShadowDanger);
    }
    return threatRate;
}

void Stage2Scene::UpdateTensionPulse(const Player& player, float deltaTime)
{
    // 観察の間違いの回数は含めていない（一度間違えると心拍が鳴りやまなくなるため）。今まさに迫っている危険だけを使っている。
    const TensionPulse::Output pulse = m_TensionPulse.Update(
        deltaTime, ComputeThreatRate(player, false), player.IsHiding());
    PlayTensionPulse(pulse);
    // 緊張が高いほど、画面の端から視界が狭まり、心拍に合わせて脈打つ（ロッカーの中では、息を殺す緊張でわずかに狭まる）
    ShowFearVision(m_TensionPulse, pulse);

    // 携帯ラジオの雑音：足音で上がる危険度ではなく、実際に近くにいる影（足音の影・最後の追跡の影・背後の気配・見えない影）の近さで鳴らしている
    float presence = 0.0f;
    for (const ShadowMan* figure : { m_Objects.noiseShadow, m_Objects.shadow, m_Objects.presence })
    {
        if (figure == nullptr || !figure->IsActive())
        {
            continue;
        }
        Vector3 toFigure = figure->GetPosition() - player.GetPosition();
        toFigure.y = 0.0f;
        presence = (std::max)(presence,
            MathUtility::Proximity(toFigure.Length(), RadioPresenceNearDistance, RadioPresenceRange));
    }
    // 見えない影の異変の周回では、姿の見えない影の近さも雑音に含めている（手がかりはこの雑音だけ）
    presence = (std::max)(presence, m_UnseenPresence.GetProximity(player.GetPosition()));
    m_RadioStatic.Update(deltaTime, presence, true);
    // 影が近いほど、吊るした裸電球がすきま風にあおられるように大きく揺れる
    for (HangingBulb* bulb : m_Objects.hangingBulbs)
    {
        if (bulb != nullptr)
        {
            bulb->SetDisturbance(m_RadioStatic.GetLevel());
        }
    }
}

// 誰かの濡れた足跡：2周目以降、その周回で初めて水たまりから出ると、少し遅れて同じ水たまりから
// 裸足の濡れた足跡が一歩ずつ現れ、プレイヤーの方へ近づいてくる（姿は見えず、足跡と水音だけ）。
// 一番新しい足跡を懐中電灯で照らすとそこで止まり、照らさずにすぐ近くまで来させると、照明が揺れて止まる。
void Stage2Scene::UpdateFootprintFollower(const Player& player, float deltaTime)
{
    Ground* ground = m_Objects.ground;
    if (ground == nullptr)
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();

    Vector2 waterExit;
    const bool leftWater = ground->ConsumeWaterExit(waterExit);
    if (leftWater && !m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Unseen) &&
        m_LoopCount >= FootprintFollowerStartLoop && !m_FootprintFollowerUsedThisLoop &&
        !m_FootprintFollower.IsActive() && !m_FinalSequence.IsSequenceActive())
    {
        m_FootprintFollower.Start(waterExit);
        m_FootprintFollowerUsedThisLoop = true;
    }

    const Camera* camera = game->GetCamera();
    FootprintFollower::Step step;
    const FootprintFollower::Event followerEvent = m_FootprintFollower.Update(
        deltaTime, player.GetPosition(), camera->GetPosition(), camera->GetForward(),
        player.IsFlashlightOn(), step);
    if (followerEvent == FootprintFollower::Event::Step)
    {
        // 裸足の濡れた足跡を床に残し、その場所から、ぺたりと濡れた足音を鳴らしている
        ground->AddFootprint(step.Position, step.Yaw, step.Side, true, 1.0f);
        game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, Vector3(step.Position.x, FootY, step.Position.y),
            FollowerWaterStepSound.Pitch, FollowerWaterStepSound.Volume);
    }
    else if (followerEvent == FootprintFollower::Event::Lit)
    {
        // 照らされて止まった：心拍がひとつ跳ねるだけにしている
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FollowerLitPulse);
        EventFeedback::Rumble(FollowerLitRumble);
    }
    else if (followerEvent == FootprintFollower::Event::Reached)
    {
        // すぐ近くまで来た：近くの照明が揺れ、すぐ後ろで息づかいが聞こえる
        KickHangingBulbs(FollowerReachedBulbKick);
        const Vector3 playerPosition = player.GetPosition();
        CeilingLight* nearbyLight = m_Objects.Light(Stage2NearestLight(playerPosition.z));
        if (nearbyLight != nullptr)
        {
            EventFeedback::Flicker(*nearbyLight, FollowerReachedFlicker);
        }
        Vector3 behind = playerPosition - camera->GetForward() * FollowerBreathBehindDistance;
        behind.y = playerPosition.y;
        game->PlayAudioCueAt(SOUND_CUE_BREATH, behind, FollowerBreathSound.Pitch, FollowerBreathSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FollowerReachedPulse);
        EventFeedback::Rumble(FollowerReachedRumble);
    }
}

// 吊るした裸電球を揺さぶっている（周回の切り替え・異変を見つけたとき・影がすぐ近くまで来たとき）
void Stage2Scene::KickHangingBulbs(float strength)
{
    for (HangingBulb* bulb : m_Objects.hangingBulbs)
    {
        if (bulb != nullptr)
        {
            bulb->Kick(strength);
        }
    }
}
