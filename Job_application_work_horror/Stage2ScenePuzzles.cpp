// ============================================================================
// ファイルの役割: 2面の足音の危険度、周回の進め方、時計、信号盤パズル、偽の扉の表示を担当している。
// 主な技術: 入力の順番の照合、状態機械、環境を使ったパズル、条件を満たすまで先へ進めない仕組み
// ============================================================================

#include "Stage2Scene.h"
#include "MathUtility.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "EventFeedback.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "PostProcess.h"
#include "Ground.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace DirectX::SimpleMath;

#include "Stage2SceneConstants.h"


// ----------------------------------------------------------------------------
// ロッカーに隠れている間
// 足音の影が離れているうちに隠れれば、影は見失って立ち止まり、しばらくして去っていく。
// 影がすぐ近くまで来てから隠れても、入るところを見られていて捕まる。
// 隠れている間は足音の危険度が下がり、新しい影も出ない。
// 見失った影は、ロッカーのすぐ前まで探しに来る。近くにいる間は息を止めていないと呼吸を聞かれて見つかり、
// 息を止めすぎると大きく吐いてしまう（BreathHold）。
// ----------------------------------------------------------------------------
namespace
{
    constexpr float HidingSeenDistance = 30.0f;     // これより近くで隠れると見られている
    // 見失った影が、ロッカーの前まで探しに来て留まる秒数と、近づく速さ・止まる距離
    constexpr float HiddenStalkerLingerSeconds = 7.0f;
    constexpr float HiddenStalkerSearchSpeed = 5.5f;
    constexpr float HiddenStalkerStopDistance = 14.0f;
    // 探している影の息づかいが聞こえ始める距離と、その間隔。ロッカーに触れるのは、止まる距離よりこれだけ遠い所
    constexpr float SearcherBreathDistance = 45.0f;
    constexpr float SearcherBreathInterval = 1.8f;
    constexpr float SearcherTouchMargin = 3.0f;
    constexpr float SearcherBreathHeight = 30.0f;

    // --- 足音の影：危険度がこれ以上になったら、プレイヤーの後ろに出して追わせる（ライトで照らせば追い払える）
    constexpr float NoiseStalkerSpawnThreat = 0.74f;
    constexpr float NoiseStalkerSpawnBehind = 68.0f;
    // 出す位置は廊下の中に収める（壁から少し離した範囲）
    constexpr float NoiseStalkerSpawnHalfWidth = 30.0f;
    constexpr float NoiseStalkerSpawnMinZ = -145.0f;
    constexpr float NoiseStalkerSpawnMaxZ = 128.0f;
    // 追う速さ（危険度が高いほど速い）、止まる距離、見られたときに驚かせる秒数
    constexpr float NoiseStalkerBaseSpeed = 13.0f;
    constexpr float NoiseStalkerSpeedPerThreat = 6.0f;
    constexpr float NoiseStalkerStopDistance = 12.0f;
    constexpr float NoiseStalkerGazeScareSeconds = 7.5f;
    // 近いほど危険度を上げる範囲（この距離から、この幅で遠ざかるほど弱く）と、捕まる距離
    constexpr float NoiseStalkerNearDistance = 14.0f;
    constexpr float NoiseStalkerThreatRange = 72.0f;
    constexpr float NoiseStalkerCatchDistance = 15.5f;
    // これより近くに足音の影がいる間は、静かに歩いても危険度を下げない
    constexpr float NoiseStalkerNearbyDistance = 25.0f;

    // --- 時計の異変：近くで正面から見たか
    const Vector3 ClockCenter(-38.0f, -70.0f, -25.0f);
    constexpr float ClockLookDistance = 98.0f;
    constexpr float ClockLookFacing = 0.91f;

    // --- 信号盤の間に背後から来る影：段階が進むほど近くに出て速く追う。間違えた回数が多いほど遠く遅くして助ける
    constexpr float SignalStalkerBaseDistance = 78.0f;
    constexpr float SignalStalkerDistancePerStep = 7.0f;
    constexpr float SignalStalkerDistancePerAssist = 13.0f;
    constexpr float SignalStalkerBaseSpeed = 13.0f;
    constexpr float SignalStalkerSpeedPerStep = 2.0f;
    constexpr float SignalStalkerSlowdownPerAssist = 1.6f;
    constexpr int SignalStalkerMaxAssist = 2;
    constexpr float SignalStalkerStopDistance = 30.0f;
    constexpr float SignalStalkerGazeScareSeconds = 24.0f;
    // 近いほど危険度を上げる範囲と、この距離まで来たら信号盤をやり直させる距離
    constexpr float SignalStalkerNearDistance = 30.0f;
    constexpr float SignalStalkerThreatRange = 90.0f;
    constexpr float SignalStalkerCatchDistance = 35.0f;

    // ------------------------------------------------------------------------
    // 演出の強さと長さ（照明の明滅・画面の脈打ち・振動・音）と、足音の危険度の増え方・減り方
    // ------------------------------------------------------------------------
    using EventFeedback::FlickerSpec;
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;
    using EventFeedback::SoundSpec;

    // 方向を求めるとき、長さがこれ以下なら向きを決めない（ゼロ除算を避ける）
    constexpr float MinimumDirectionLength = 0.001f;

    // --- ロッカーに隠れている間：危険度の下がる速さ（毎秒）、最初の息づかいまでの秒数、影が来た足音
    constexpr float HiddenThreatDecayPerSecond = 0.30f;
    constexpr float SearcherFirstBreathDelay = 1.0f;
    constexpr SoundSpec HiddenStalkerArriveSound{ 0.70f, 1.4f };
    constexpr SoundSpec SearcherBreathSound{ 0.62f, 0.55f };
    // 影がロッカーに触れたときと、息が続かずに吐いてしまったとき
    constexpr SoundSpec SearcherTouchSound{ 0.55f, 0.45f };
    constexpr RumbleSpec SearcherTouchRumble{ 3, 0.10f };
    constexpr SoundSpec GaspSound{ 1.15f, 1.0f };
    constexpr RumbleSpec GaspRumble{ 4, 0.12f };
    // 影があきらめて去ったとき：危険度を下げる量、次の影を出さない秒数、静かな知らせ
    constexpr float HiddenStalkerLeftThreatDrop = 0.40f;
    constexpr float HiddenStalkerLeftCooldown = 9.0f;
    constexpr PulseSpec HiddenStalkerLeftBloom{ 0.16f, 0.16f };
    constexpr RumbleSpec HiddenStalkerLeftRumble{ 2, 0.06f };

    // --- 足音の危険度：最後の演出の間に下げる速さ（毎秒）
    constexpr float FinalThreatDecayPerSecond = 0.8f;
    // 足音の大きさで上がる量（信号盤パズルの間は上がりやすい）と、強い水音の知らせを出す大きさと秒数
    constexpr float SurfaceNoiseThreatSignal = 0.18f;
    constexpr float SurfaceNoiseThreatNormal = 0.11f;
    constexpr float WetStepNoticePulse = 0.60f;
    constexpr float WetStepNoticeSeconds = 1.8f;
    // 走ると上がり、歩くと下がる速さ（毎秒。信号盤パズルの間は上がりやすく下がりにくい）
    constexpr float SprintThreatRiseSignal = 0.48f;
    constexpr float SprintThreatRiseNormal = 0.36f;
    constexpr float WalkThreatDecaySignal = 0.12f;
    constexpr float WalkThreatDecayNormal = 0.22f;
    // 足音の影が近いほど上げる危険度の最大
    constexpr float NoiseStalkerProximityThreat = 0.92f;
    // 消灯して静止したら影を振り切れる：判定を始める危険度、成功したときに下げる量と次の影を出さない秒数、落ち着いた知らせ
    constexpr float QuietRecoveryMinThreat = 0.05f;
    constexpr float QuietRecoveryThreatDrop = 0.45f;
    constexpr float QuietRecoveryCooldown = 8.0f;
    constexpr PulseSpec QuietRecoveryPulse{ 0.08f, 0.16f };
    constexpr PulseSpec QuietRecoveryBloom{ 0.18f, 0.16f };
    constexpr RumbleSpec QuietRecoveryRumble{ 2, 0.06f };
    // 足音の影をライトで照らして追い払ったとき（危険度はこれより下げない）
    constexpr float NoiseStalkerSeenMinThreat = 0.10f;
    constexpr float NoiseStalkerSeenThreatDrop = 0.38f;
    constexpr float NoiseStalkerSeenCooldown = 6.0f;
    constexpr float NoiseStalkerSeenNoticeSeconds = 2.4f;
    // 足音の影を出したとき
    constexpr float NoiseStalkerSpawnCooldown = 8.5f;
    constexpr float NoiseStalkerSpawnNoticeSeconds = 2.8f;
    constexpr float NoiseStalkerSpawnWarningSeconds = 2.8f;
    constexpr SoundSpec NoiseStalkerSpawnScareSound{ 0.78f, 1.0f };
    constexpr PulseSpec NoiseStalkerSpawnPulse{ 0.42f, 0.38f };
    constexpr RumbleSpec NoiseStalkerSpawnRumble{ 8, 0.22f };
    // 信号盤パズルの間に危険度がこれ以上になったら、パズルをやり直させる
    constexpr float SignalResetThreat = 0.98f;
    constexpr float SignalResetFeedbackSeconds = 2.8f;
    constexpr float SignalResetWarningSeconds = 3.0f;
    constexpr float SignalResetThreatAfter = 0.30f;
    constexpr float SignalResetEventCooldown = 2.8f;
    constexpr PulseSpec SignalResetPulse{ 0.72f, 0.48f };
    constexpr PulseSpec SignalResetBloom{ 0.90f, 0.24f };
    constexpr RumbleSpec SignalResetRumble{ 13, 0.30f };
    // 危険度がこれ以上になると、近くの照明を明滅させて警告する。どの照明かは、プレイヤーの z で決める
    constexpr float ThreatWarningLevel = 0.70f;
    constexpr float WarningLight1MaxZ = -55.0f;
    constexpr float WarningLight2MaxZ = 18.0f;
    constexpr float WarningLight3MaxZ = 88.0f;
    // 警告の明滅・画面・振動（基本＋危険度1のときに足す分）と、次の警告までの秒数
    constexpr float WarningFlickerSecondsBase = 0.48f;
    constexpr float WarningFlickerSecondsPerThreat = 0.48f;
    constexpr float WarningFlickerStrengthBase = 0.46f;
    constexpr float WarningFlickerStrengthPerThreat = 0.42f;
    constexpr float WarningTimerSeconds = 2.1f;
    constexpr float WarningEventCooldown = 2.35f;
    constexpr float WarningPulseBase = 0.10f;
    constexpr float WarningPulsePerThreat = 0.16f;
    constexpr float WarningPulseSeconds = 0.22f;
    constexpr RumbleSpec WarningRumbleBase{ 3, 0.08f };
    constexpr RumbleSpec WarningRumblePerThreat{ 4, 0.08f };

    // --- 周回を進めたとき：次の判定までの秒数、案内の点滅までの秒数、入口の暗転の秒数、裸電球の揺れ
    constexpr float LoopCooldownSeconds = 1.0f;
    constexpr float GuidancePulseCooldownSeconds = 1.2f;
    constexpr float LoopStartBulbKick = 0.5f;
    // 周回の始まりの危険度、次の警告・次の影までの秒数
    constexpr float LoopStartThreat = 0.12f;
    constexpr float LoopStartEventCooldown = 1.0f;
    constexpr float LoopStartStalkerCooldown = 2.0f;
    // 周回の始まりの画面の脈打ちと光（基本＋周回ごとに強める分）、振動（基本のフレーム数＋周回ごと、強さ）
    constexpr PulseSpec LoopStartPulseBase{ 0.26f, 0.38f };
    constexpr PulseSpec LoopStartPulseStep{ 0.13f, 0.10f };
    constexpr float LoopStartBloomBase = 0.46f;
    constexpr float LoopStartBloomStep = 0.08f;
    constexpr float LoopStartBloomSeconds = 0.26f;
    constexpr int LoopStartRumbleBaseFrames = 6;
    constexpr int LoopStartRumbleFramesPerLoop = 3;
    constexpr float LoopStartRumbleStrength = 0.18f;
    // 入口の右の壁の、周回の数だけ出す傷の印：数、浮かび上がる秒数、地の色、自己発光（基本＋周回ごと）、光沢
    constexpr int CycleMarkCount = 3;
    constexpr float MarkDissolveSeconds = 1.6f;
    const Color CycleMarkDiffuse(0.24f, 0.006f, 0.003f, 1.0f);
    constexpr float CycleMarkEmissionBase = 0.10f;
    constexpr float CycleMarkEmissionPerLoop = 0.055f;
    constexpr float CycleMarkEmissionGreen = 0.001f;
    constexpr float CycleMarkShininess = 20.0f;
    // 1周目：傷を3本、周回の印の色、中央の照明の非常灯の周期のずらし
    constexpr int Loop1ScratchEnd = 3;
    constexpr float Loop1ScratchEmission = 0.10f;
    const Color LoopMarkDiffuse(0.28f, 0.008f, 0.004f, 1.0f);
    const Color LoopMarkEmission(0.16f, 0.001f, 0.0f, 1.0f);
    constexpr float LoopMarkShininess = 22.0f;
    constexpr float Light2Loop1EmergencyOffset = 5.2f;
    // 2周目：傷を9本まで、肖像画を赤く、奥の照明の非常灯の周期のずらし、見つめると演出が起きる影の見つめる秒数
    constexpr int Loop2ScratchEnd = 9;
    constexpr float Loop2ScratchEmission = 0.15f;
    const Color PortraitLoop2Diffuse(0.12f, 0.018f, 0.012f, 1.0f);
    const Color PortraitLoop2Emission(0.045f, 0.0f, 0.0f, 1.0f);
    constexpr float PortraitLoop2Shininess = 12.0f;
    constexpr float Light3Loop2EmergencyOffset = 7.1f;
    constexpr float Loop2ShadowGazeSeconds = 9.0f;
    // 最後の周回：傷を全部、奥の扉の照明の長い明滅、強い画面の乱れ
    constexpr float FinalLoopScratchEmission = 0.24f;
    constexpr FlickerSpec FinalLoopDoorFlicker{ 1.5f, 0.94f };
    constexpr PulseSpec FinalLoopPulse{ 0.64f, 0.58f };

    // --- 時計：最後の周回の赤黒い文字盤、最後の演出の間に逆回りする針の速さ（ラジアン毎秒）
    const Color ClockFinalDiffuse(0.075f, 0.018f, 0.012f, 1.0f);
    const Color ClockFinalEmission(0.018f, 0.001f, 0.0f, 1.0f);
    constexpr float ClockFinalShininess = 10.0f;
    constexpr float ClockFrenzyHourSpeed = 1.6f;
    constexpr float ClockFrenzyMinuteSpeed = 9.5f;
    // 時計の異変を見つけたとき
    constexpr FlickerSpec DoorLightAlertFlicker{ 0.90f, 0.76f };
    constexpr PulseSpec ClockFoundBloom{ 0.58f, 0.24f };
    constexpr FlickerSpec ClockNearbyFlicker{ 0.82f, 0.72f };
    constexpr PulseSpec ClockFoundPulse{ 0.28f, 0.32f };
    constexpr RumbleSpec ClockFoundRumble{ 6, 0.14f };

    // --- パズルの間違い：この回数で一番強く知らせ、近くの照明を消す。知らせの強さ（基本＋間違いの割合1のときに足す分）
    constexpr int MaxPuzzleMistakes = PuzzleFeedback::MaxMistakeCount;
    constexpr float MistakeFlickerSecondsBase = 0.48f;
    constexpr float MistakeFlickerSecondsPerRate = 0.52f;
    constexpr float MistakeFlickerStrengthBase = 0.42f;
    constexpr float MistakeFlickerStrengthPerRate = 0.48f;
    constexpr PulseSpec MistakePulseBase{ 0.08f, 0.18f };
    constexpr PulseSpec MistakePulsePerRate{ 0.24f, 0.18f };
    constexpr float MistakeBloomBase = 0.14f;
    constexpr float MistakeBloomPerRate = 0.28f;
    constexpr float MistakeBloomSeconds = 0.14f;
    constexpr RumbleSpec MistakeRumbleBase{ 2, 0.06f };
    constexpr int MistakeRumbleFramesPerMistake = 2;
    constexpr float MistakeRumbleStrengthPerRate = 0.12f;

    // --- 信号盤パズル：信号盤の数
    constexpr int SignalTerminalCount = 3;
    // 順番をまちがえたとき
    constexpr float WrongOrderThreat = 0.92f;
    constexpr PulseSpec WrongOrderPulse{ 0.62f, 0.42f };
    constexpr RumbleSpec WrongOrderRumble{ 11, 0.24f };
    // 順番どおりに押したとき：危険度を上げる量（間違えた回数が多いほど少し減らして助ける）、光・振動（段階ごとに長く）・照明
    constexpr float CorrectThreatRise = 0.12f;
    constexpr float CorrectThreatAssistRelief = 0.025f;
    constexpr PulseSpec CorrectBloom{ 0.52f, 0.20f };
    constexpr RumbleSpec CorrectRumbleBase{ 4, 0.10f };
    constexpr int CorrectRumbleFramesPerStep = 2;
    constexpr FlickerSpec CorrectFlicker{ 0.54f, 0.48f };
    // 3つそろったとき
    constexpr PulseSpec CompletedBloom{ 1.05f, 0.44f };
    constexpr PulseSpec CompletedPulse{ 0.34f, 0.34f };
    // 信号盤の間に来る影を照らして追い払ったとき（危険度はこれより下げない）
    constexpr float SignalStalkerSeenMinThreat = 0.12f;
    constexpr float SignalStalkerSeenThreatDrop = 0.24f;
    constexpr float SignalStalkerSeenFeedbackSeconds = 2.0f;
    constexpr PulseSpec SignalStalkerSeenBloom{ 0.72f, 0.22f };
    constexpr RumbleSpec SignalStalkerSeenRumble{ 4, 0.10f };
    // 目印の光り方：脈打つ速さ（ラジアン毎秒）、受け付けた信号盤の緑、次に押す信号盤の明るさと光沢
    constexpr float MarkerPulseSpeed = 5.4f;
    const Color AcceptedMarkerDiffuse(0.025f, 0.14f, 0.055f, 1.0f);
    constexpr float AcceptedMarkerEmissionRed = 0.02f;
    constexpr float AcceptedMarkerEmissionGreenBase = 0.42f;
    constexpr float AcceptedMarkerEmissionGreenPulse = 0.12f;
    constexpr float AcceptedMarkerEmissionBlue = 0.08f;
    constexpr float AcceptedMarkerShininess = 58.0f;
    constexpr float TargetMarkerEnergyBase = 0.72f;
    constexpr float TargetMarkerEnergyPulse = 0.42f;
    constexpr float IdleMarkerEnergy = 0.16f;
    constexpr float TargetMarkerShininess = 54.0f;
    constexpr float IdleMarkerShininess = 24.0f;
    // 出口の照明：直すまでの非常灯の周期のずらしと、直したときの明滅
    constexpr float ExitLightEmergencyOffset = 4.1f;
    constexpr FlickerSpec ExitLightRestoredFlicker{ 1.10f, 0.82f };
    // 信号盤の間に来る影：近いほど上げる危険度の最大と、追いつかれてやり直すとき
    constexpr float SignalStalkerProximityThreat = 0.78f;
    constexpr float SignalStalkerCaughtFeedbackSeconds = 2.8f;
    constexpr float SignalStalkerCaughtThreat = 0.38f;
    constexpr float SignalStalkerCaughtEventCooldown = 2.8f;
    constexpr PulseSpec SignalStalkerCaughtPulse{ 0.88f, 0.48f };
    constexpr PulseSpec SignalStalkerCaughtBloom{ 0.16f, 0.16f };
    constexpr RumbleSpec SignalStalkerCaughtRumble{ 15, 0.34f };

    // --- 偽の扉の部品の配置：扉の中心の高さ、枠の左右の間隔の半分と上の枠の高さ、取っ手の扉の中心からのずれ、現れる・消える秒数
    constexpr float FalseDoorCenterY = -76.0f;
    constexpr float FalseDoorFrameHalfWidth = 12.0f;
    constexpr float FalseDoorFrameTopY = -54.0f;
    constexpr float FalseDoorHandleOffsetZ = 8.0f;
    constexpr float FalseDoorDissolveInSeconds = 1.6f;
    constexpr float FalseDoorDissolveOutSeconds = 1.2f;
}

// 隠れている間の処理。隠れていなければfalseを返している
bool Stage2Scene::UpdateHiddenFromStalker(Player& player, float deltaTime)
{
    if (!player.IsHiding())
    {
        m_HiddenStalkerTimer = -1.0f;
        m_BreathHold.Reset();
        return false;
    }

    m_QuietRecovery.Reset();
    m_NoiseThreatSystem.SetThreat((std::max)(
        0.0f, m_NoiseThreatSystem.GetThreat() - deltaTime * HiddenThreatDecayPerSecond));

    ShadowMan* noiseShadow = m_Objects.noiseShadow;
    if (noiseShadow == nullptr || !noiseShadow->IsActive())
    {
        m_HiddenStalkerTimer = -1.0f;
        // 影がいなくても、息を止める操作はできる（息のゲージだけが動く）
        m_BreathHold.Update(deltaTime, IsBreathHoldPressed(), false);
        return true;
    }

    Core::Game* game = Core::Game::GetInstance();
    if (m_HiddenStalkerTimer < 0.0f)
    {
        // 隠れた瞬間に影がどれだけ近かったかで、見られていたかを決めている。
        Vector3 toShadow = noiseShadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        if (toShadow.Length() <= HidingSeenDistance)
        {
            StartCaughtSequence(player, CaughtSequence::Reason::NoiseStalker);
            return true;
        }
        // 見失った影は、辺りを探すように、ロッカーのすぐ前までゆっくり歩いて来る。
        noiseShadow->EnableChase(HiddenStalkerSearchSpeed, HiddenStalkerStopDistance);
        m_SearcherSoundTimer = SearcherFirstBreathDelay;
        m_SearcherTouchedLocker = false;
        m_HiddenStalkerTimer = HiddenStalkerLingerSeconds;
        game->PlayAudioCueAt(SOUND_CUE_FOOTSTEP, noiseShadow->GetPosition(),
            HiddenStalkerArriveSound.Pitch, HiddenStalkerArriveSound.Volume);
        return true;
    }

    // 影がロッカーの近くにいる間は、息を止めていないと呼吸を聞かれる。息が続かず吐いてしまっても、近ければ見つかる
    Vector3 toSearcher = noiseShadow->GetPosition() - player.GetPosition();
    toSearcher.y = 0.0f;
    const bool searcherNear = toSearcher.Length() <= BreathHold::DangerDistance;
    // 探している影の気配：近くにいる間は低い息づかいがときどき聞こえ、すぐ前まで来るとロッカーに一度触れる
    const float searcherDistance = toSearcher.Length();
    m_SearcherSoundTimer -= deltaTime;
    if (searcherDistance < SearcherBreathDistance && m_SearcherSoundTimer <= 0.0f)
    {
        game->PlayAudioCueAt(SOUND_CUE_BREATH,
            noiseShadow->GetPosition() + Vector3(0.0f, SearcherBreathHeight, 0.0f),
            SearcherBreathSound.Pitch, SearcherBreathSound.Volume);
        m_SearcherSoundTimer = SearcherBreathInterval;
    }
    if (!m_SearcherTouchedLocker && searcherDistance < HiddenStalkerStopDistance + SearcherTouchMargin)
    {
        game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, player.GetPosition(), SearcherTouchSound.Pitch, SearcherTouchSound.Volume);
        EventFeedback::Rumble(SearcherTouchRumble);
        m_SearcherTouchedLocker = true;
    }
    const BreathHold::Event breathEvent =
        m_BreathHold.Update(deltaTime, IsBreathHoldPressed(), searcherNear);
    if (breathEvent == BreathHold::Event::Gasped)
    {
        // 息が続かず、大きく吐いてしまった
        game->PlayAudioCueAt(SOUND_CUE_BREATH, player.GetPosition(), GaspSound.Pitch, GaspSound.Volume);
        EventFeedback::Rumble(GaspRumble);
        if (searcherNear)
        {
            StartCaughtSequence(player, CaughtSequence::Reason::NoiseStalker);
            return true;
        }
    }
    else if (breathEvent == BreathHold::Event::Heard)
    {
        StartCaughtSequence(player, CaughtSequence::Reason::NoiseStalker);
        return true;
    }

    m_HiddenStalkerTimer -= deltaTime;
    if (m_HiddenStalkerTimer > 0.0f)
    {
        return true;
    }

    // 影はあきらめて去っていく。危険度を下げ、しばらく次の影を出さない。
    noiseShadow->SetActive(false);
    m_HiddenStalkerTimer = -1.0f;
    m_NoiseThreatSystem.SetThreat((std::max)(
        0.0f, m_NoiseThreatSystem.GetThreat() - HiddenStalkerLeftThreatDrop));
    m_NoiseThreatSystem.SetStalkerCooldown((std::max)(
        m_NoiseThreatSystem.GetStalkerCooldown(), HiddenStalkerLeftCooldown));
    m_NoiseThreatSystem.SetWarningTimer(0.0f);
    m_Notices.hiding = Stage2NoticeSeconds;
    EventFeedback::BloomPulse(*game->GetPostProcess(), HiddenStalkerLeftBloom);
    EventFeedback::Rumble(HiddenStalkerLeftRumble);
    return true;
}

// 足音の危険度を更新し、高くなったら足音を追う影を出している
void Stage2Scene::UpdateNoiseThreat(Player& player, float deltaTime)
{
    // 最後の演出と追跡の間は、危険度を下げて足音の影を消している
    if (m_FinalSequence.IsSequenceActive() ||
        m_FinalSequence.IsPursuitActive())
    {
        m_QuietRecovery.Reset();
        m_NoiseThreatSystem.SetThreat((std::max)(0.0f,
            m_NoiseThreatSystem.GetThreat() - deltaTime * FinalThreatDecayPerSecond));
        ShadowMan* noiseShadow = m_Objects.noiseShadow;
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        return;
    }

    // 3周目で信号盤を直し終えた後も、同じように危険度を下げている
    const bool signalStealthActive =
        m_LoopCount >= Stage2FinalLoopCount && !m_SignalPuzzle.IsComplete();
    if (m_LoopCount >= Stage2FinalLoopCount && !signalStealthActive)
    {
        m_NoiseThreatSystem.SetThreat((std::max)(0.0f,
            m_NoiseThreatSystem.GetThreat() - deltaTime * FinalThreatDecayPerSecond));
        ShadowMan* noiseShadow = m_Objects.noiseShadow;
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        return;
    }

    if (UpdateHiddenFromStalker(player, deltaTime))
    {
        return;
    }

    // 足音の大きさ（水たまりや走った音）で危険度を上げ、強い水音なら知らせを出している
    const float surfacePulse = player.GetSurfaceNoisePulse();
    if (surfacePulse > 0.0f)
    {
        m_NoiseThreatSystem.SetThreat((std::min)(1.0f,
            m_NoiseThreatSystem.GetThreat() +
                surfacePulse * (signalStealthActive ? SurfaceNoiseThreatSignal : SurfaceNoiseThreatNormal)));
        if (surfacePulse >= WetStepNoticePulse)
        {
            m_NoiseThreatSystem.SetWetStepNoticeTimer(WetStepNoticeSeconds);
        }
    }

    // 走ると上がり、歩くと下がる（信号盤パズルの間は上がりやすく下がりにくい）
    const float change = player.IsSprinting()
        ? deltaTime * (signalStealthActive ? SprintThreatRiseSignal : SprintThreatRiseNormal)
        : -deltaTime * (signalStealthActive ? WalkThreatDecaySignal : WalkThreatDecayNormal);
    m_NoiseThreatSystem.SetThreat((std::clamp)(
        m_NoiseThreatSystem.GetThreat() + change, 0.0f, 1.0f));

    Core::Game* game = Core::Game::GetInstance();
    // 足音の影が出ていれば、近いほど危険度を上げ、15.5まで近づかれたら捕まっている
    ShadowMan* noiseShadow =
        m_Objects.noiseShadow;
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float proximity = MathUtility::Proximity(distance, NoiseStalkerNearDistance, NoiseStalkerThreatRange);
        m_NoiseThreatSystem.SetThreat((std::max)(
            m_NoiseThreatSystem.GetThreat(), proximity * NoiseStalkerProximityThreat));

        if (distance <= NoiseStalkerCatchDistance)
        {
            StartCaughtSequence(
                player, CaughtSequence::Reason::NoiseStalker);
            return;
        }
    }

    // 初めの3周では、距離を取って消灯・静止すると、足音の影を振り切れる。
    // 信号盤パズルと最後の追跡は、それぞれの対処のしかたのままにしている。
    bool nearbyThreat = false;
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 offset = noiseShadow->GetPosition() - player.GetPosition();
        offset.y = 0.0f;
        nearbyThreat = offset.LengthSquared() <= NoiseStalkerNearbyDistance * NoiseStalkerNearbyDistance;
    }
    const bool quietEligible = m_LoopCount < Stage2FinalLoopCount &&
        !m_ObservedScareSequence.IsActive() &&
        m_LoopTransitionTimer < 0.0f &&
        (m_NoiseThreatSystem.GetThreat() > QuietRecoveryMinThreat ||
            m_QuietRecovery.progress > 0.0f ||
            (noiseShadow != nullptr && noiseShadow->IsActive()));
    if (m_QuietRecovery.Update(deltaTime, quietEligible,
        !player.IsMovingHorizontally(), !player.IsFlashlightOn(), nearbyThreat))
    {
        m_NoiseThreatSystem.SetThreat((std::max)(
            0.0f, m_NoiseThreatSystem.GetThreat() - QuietRecoveryThreatDrop));
        if (noiseShadow != nullptr) noiseShadow->SetActive(false);
        m_NoiseThreatSystem.SetStalkerCooldown((std::max)(
            m_NoiseThreatSystem.GetStalkerCooldown(), QuietRecoveryCooldown));
        m_NoiseThreatSystem.SetWarningTimer(0.0f);
        // 成功したときは強い光を避け、視界が落ち着くことで成功を伝えている。
        EventFeedback::HorrorPulse(*game->GetPostProcess(), QuietRecoveryPulse);
        EventFeedback::BloomPulse(*game->GetPostProcess(), QuietRecoveryBloom);
        EventFeedback::Rumble(QuietRecoveryRumble);
    }

    // 危険度が0.74以上になったら、背後68に足音の影を出し、追わせている（ライトで照らせば追い払える）
    const bool canSpawnNoiseStalker =
        m_NoiseThreatSystem.GetThreat() >= NoiseStalkerSpawnThreat &&
        m_NoiseThreatSystem.GetStalkerCooldown() <= 0.0f &&
        !m_FinalSequence.IsSequenceActive() &&
        !m_FinalSequence.IsPursuitActive() &&
        noiseShadow != nullptr && !noiseShadow->IsActive();
    if (canSpawnNoiseStalker)
    {
        const Vector3 forward = player.GetForward();
        Vector3 spawnPosition = player.GetPosition() - forward * NoiseStalkerSpawnBehind;
        spawnPosition.x = (std::clamp)(spawnPosition.x, -NoiseStalkerSpawnHalfWidth, NoiseStalkerSpawnHalfWidth);
        spawnPosition.y = Stage2FootY;
        spawnPosition.z = (std::clamp)(spawnPosition.z, NoiseStalkerSpawnMinZ, NoiseStalkerSpawnMaxZ);

        noiseShadow->SetPosition(
            spawnPosition.x, spawnPosition.y, spawnPosition.z);
        noiseShadow->SetActive(true);
        noiseShadow->EnableChase(
            NoiseStalkerBaseSpeed + m_NoiseThreatSystem.GetThreat() * NoiseStalkerSpeedPerThreat,
            NoiseStalkerStopDistance);
        noiseShadow->EnableGazeScare(NoiseStalkerGazeScareSeconds);
        noiseShadow->SetOnObserved([this]()
        {
            ShadowMan* currentShadow =
                m_Objects.noiseShadow;
            if (currentShadow != nullptr)
            {
                currentShadow->SetActive(false);
            }
            m_NoiseThreatSystem.SetThreat((std::max)(
                NoiseStalkerSeenMinThreat, m_NoiseThreatSystem.GetThreat() - NoiseStalkerSeenThreatDrop));
            m_NoiseThreatSystem.SetStalkerCooldown(NoiseStalkerSeenCooldown);
            m_NoiseThreatSystem.SetStalkerNoticeTimer(NoiseStalkerSeenNoticeSeconds);
        });
        m_NoiseThreatSystem.SetStalkerCooldown(NoiseStalkerSpawnCooldown);
        m_NoiseThreatSystem.SetStalkerNoticeTimer(NoiseStalkerSpawnNoticeSeconds);
        m_NoiseThreatSystem.SetWarningTimer((std::max)(
            m_NoiseThreatSystem.GetWarningTimer(), NoiseStalkerSpawnWarningSeconds));
        game->PlayAudioCue(SOUND_CUE_SCARE, NoiseStalkerSpawnScareSound.Pitch, NoiseStalkerSpawnScareSound.Volume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), NoiseStalkerSpawnPulse);
        EventFeedback::Rumble(NoiseStalkerSpawnRumble);
    }

    // 信号盤パズルの間に危険度が最大になったら、パズルを最初からやり直させている
    if (signalStealthActive && m_NoiseThreatSystem.GetThreat() >= SignalResetThreat)
    {
        ResetSignalPuzzle();
        RegisterPuzzleMistake(PuzzleFeedback::SignalNoiseReset);
        m_PuzzleFeedback.Set(PuzzleFeedback::SignalNoiseReset, SignalResetFeedbackSeconds);
        m_Notices.signal = Stage2LongNoticeSeconds;
        m_NoiseThreatSystem.SetWarningTimer(SignalResetWarningSeconds);
        m_NoiseThreatSystem.SetThreat(SignalResetThreatAfter);
        m_NoiseThreatSystem.SetEventCooldown(SignalResetEventCooldown);

        EventFeedback::HorrorPulse(*game->GetPostProcess(), SignalResetPulse);
        EventFeedback::BloomPulse(*game->GetPostProcess(), SignalResetBloom);
        EventFeedback::Rumble(SignalResetRumble);
        return;
    }
    // 危険度が警告の高さ以上になると、近くの照明を明滅させて警告している
    if (m_NoiseThreatSystem.GetThreat() < ThreatWarningLevel ||
        m_NoiseThreatSystem.GetEventCooldown() > 0.0f)
    {
        return;
    }

    const float playerZ = player.GetPosition().z;
    const Stage2Light reactionLightId = playerZ < WarningLight1MaxZ
        ? Stage2Light::Light1
        : (playerZ < WarningLight2MaxZ
            ? Stage2Light::Light2
            : (playerZ < WarningLight3MaxZ ? Stage2Light::Light3 : Stage2Light::DoorLight));

    const float threat = m_NoiseThreatSystem.GetThreat();
    CeilingLight* reactionLight = m_Objects.Light(reactionLightId);
    if (reactionLight != nullptr)
    {
        reactionLight->TriggerEventFlicker(
            WarningFlickerSecondsBase + threat * WarningFlickerSecondsPerThreat,
            WarningFlickerStrengthBase + threat * WarningFlickerStrengthPerThreat);
    }

    m_NoiseThreatSystem.SetWarningTimer(WarningTimerSeconds);
    m_NoiseThreatSystem.SetEventCooldown(WarningEventCooldown);
    game->GetPostProcess()->TriggerHorrorPulse(
        WarningPulseBase + threat * WarningPulsePerThreat,
        WarningPulseSeconds);
    Input::SetVibration(
        WarningRumbleBase.Frames + static_cast<int>(threat * static_cast<float>(WarningRumblePerThreat.Frames)),
        WarningRumbleBase.Strength + threat * WarningRumblePerThreat.Strength);
}

// 周回を進めている：状態を戻し、プレイヤーを入口へ戻し、扉を閉め、その周回の異変を出している
void Stage2Scene::AdvanceLoop(Player& player)
{
    m_QuietRecovery.Reset();
    Core::Game* game = Core::Game::GetInstance();
    ++m_LoopCount;
    m_LoopCooldown = LoopCooldownSeconds;
    m_ProgressHintTimer = 0.0f;
    m_GuidancePulseCooldown = GuidancePulseCooldownSeconds;
    m_LoopBlinkTimer = Stage2LoopBlinkSeconds;
    m_LoopTransitionTimer = 0.0f;
    m_Notices.loop = Stage2LongNoticeSeconds;
    m_LightZoneProgress.Reset();
    m_ScratchAnomaly.ResetProgressForLoop();
    m_PortraitAnomaly.ResetProgressForLoop();
    m_HandprintAnomaly.ResetProgressForLoop();
    m_MirrorAnomaly.ResetProgressForLoop();
    m_PuddleAnomaly.ResetProgressForLoop();
    // 水たまりの異変は、2周目に出たときほど大胆に出る（すぐ近くに立ち、速く近づく）
    m_PuddleAnomaly.SetBoldness(m_LoopCount >= 2 ? 1.0f : 0.0f);
    m_FootprintFollowerUsedThisLoop = false;
    // 周回が切り替わった瞬間、吊るした裸電球が大きく揺れる（同じ廊下に戻ってきた合図）
    KickHangingBulbs(LoopStartBulbKick);
    // 周回ごとに、天井の雨漏りを1か所ずつ増やしている（3周目まで。周回を重ねるほど廊下が濡れていく）。
    // 位置と大きさは配置ファイルの place Stage2LoopLeak1〜3（Stage2Layout が読む）。水たまりは45秒かけて広がる
    constexpr float loopLeakGrowSeconds = 45.0f;
    if (m_Objects.ground != nullptr && m_LoopCount >= 1 && m_LoopCount <= Stage2FinalLoopCount)
    {
        const Vector3& leak = m_Objects.loopLeaks[static_cast<std::size_t>(m_LoopCount - 1)];
        m_Objects.ground->AddLeak(Vector2(leak.x, leak.y), leak.z, loopLeakGrowSeconds);
    }
    m_KnockingAnomaly.ResetProgressForLoop();
    m_UnseenPresence.ResetProgressForLoop();
    m_FalseDoorAnomaly.ResetProgressForLoop();
    m_ConfirmationHandledThisLoop = false;
    ResetSignalPuzzle();
    m_PuzzleFeedback.Reset();
    m_NoiseThreatSystem.SetThreat(LoopStartThreat);
    m_NoiseThreatSystem.SetEventCooldown(LoopStartEventCooldown);
    m_NoiseThreatSystem.SetWarningTimer(0.0f);
    m_NoiseThreatSystem.SetStalkerCooldown(LoopStartStalkerCooldown);
    m_NoiseThreatSystem.SetStalkerNoticeTimer(0.0f);
    ShadowMan* noiseShadow =
        m_Objects.noiseShadow;
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
    FuseBox* confirmationPanel =
        m_Objects.confirmationPanel;
    if (confirmationPanel != nullptr)
    {
        confirmationPanel->ResetActivation();
        confirmationPanel->SetManualInteractionAllowed(false);
    }
    player.SetPosition(Vector3(0.0f, Stage2FootY, Stage2EntranceZ));
    Door* loopDoor = m_Objects.door;
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(m_LoopCount);
        // 各周回には見つけないと先へ進めない変化を1つ設け、気づくまで同じ扉を開けないようにしている。
        // 同じ廊下をまっすぐ進むだけでなく、周りを観察する遊びにしている。
        loopDoor->SetLocked(m_LoopCount > 0);
    }

    // 消していた照明を戻している
    for (CeilingLight* light : m_Objects.lights)
    {
        if (light != nullptr)
        {
            light->SetForcedOff(false);
        }
    }

    ConfigureClockForLoop();

    const float loop = static_cast<float>(m_LoopCount);
    game->GetPostProcess()->TriggerHorrorPulse(
        LoopStartPulseBase.Strength + loop * LoopStartPulseStep.Strength,
        LoopStartPulseBase.Seconds + loop * LoopStartPulseStep.Seconds);
    game->GetPostProcess()->TriggerBloomPulse(
        LoopStartBloomBase + loop * LoopStartBloomStep,
        LoopStartBloomSeconds);
    Input::SetVibration(LoopStartRumbleBaseFrames + m_LoopCount * LoopStartRumbleFramesPerLoop, LoopStartRumbleStrength);

    // 周回の数だけ、入口の右の壁に傷の印を出している
    for (int markIndex = 0; markIndex < CycleMarkCount; ++markIndex)
    {
        Wall* cycleMark = m_Objects.cycleMarks[static_cast<std::size_t>(markIndex)];
        if (cycleMark == nullptr)
        {
            continue;
        }

        const bool revealed = markIndex < m_LoopCount;
        // 新しく増えた印は、焦げた縁が広がるように浮かび上がらせている
        if (revealed && !cycleMark->IsVisible())
        {
            cycleMark->DissolveIn(MarkDissolveSeconds);
        }
        else if (!revealed)
        {
            cycleMark->SetVisible(false);
        }
        if (revealed)
        {
            const float emission =
                CycleMarkEmissionBase + static_cast<float>(m_LoopCount) * CycleMarkEmissionPerLoop;
            cycleMark->SetAppearance(
                CycleMarkDiffuse,
                Color(emission, CycleMarkEmissionGreen, 0.0f, 1.0f),
                CycleMarkShininess);
        }
    }

    Wall* loopMark = m_Objects.loopMark;
    Wall* portrait = m_Objects.portrait;
    CeilingLight* light2 = m_Objects.Light(Stage2Light::Light2);
    CeilingLight* light3 = m_Objects.Light(Stage2Light::Light3);

    // 1周目：中央の照明を故障させ、傷を3本出し、電池と周回の印を出している
    if (m_LoopCount == 1)
    {
        // 偽の扉は、偽の扉を見つける周回（Stage2AnomalyPlanが決める）だけ出している。
        SetFalseDoorState(
            m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::FalseDoor), false);
        CeilingLight* failingLight =
            m_Objects.Light(Stage2Light::Light2);
        if (failingLight != nullptr)
        {
            failingLight->SetFaulted(true);
        }
        RevealScratchPieces(0, Loop1ScratchEnd, Loop1ScratchEmission);
        BatteryItem* battery = m_Objects.battery;
        if (battery != nullptr)
        {
            battery->SetActive(true);
        }
        if (loopMark != nullptr)
        {
            if (!loopMark->IsVisible())
            {
                loopMark->DissolveIn(MarkDissolveSeconds);
            }
            loopMark->SetAppearance(LoopMarkDiffuse, LoopMarkEmission, LoopMarkShininess);
        }
        if (light2 != nullptr)
        {
            light2->SetEmergencyLight(true, Light2Loop1EmergencyOffset);
        }
    }
    // 2周目：入口と奥の照明も故障させ、傷を9本にし、肖像画を赤く変え、見つめると照明の演出が起きる影を出している
    else if (m_LoopCount == 2)
    {
        SetFalseDoorState(
            m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::FalseDoor), false);
        CeilingLight* entranceLight =
            m_Objects.Light(Stage2Light::Light1);
        CeilingLight* farLight =
            m_Objects.Light(Stage2Light::Light3);
        if (entranceLight != nullptr)
        {
            entranceLight->SetFaulted(true);
        }
        if (farLight != nullptr)
        {
            farLight->SetFaulted(true);
        }
        RevealScratchPieces(Loop1ScratchEnd, Loop2ScratchEnd, Loop2ScratchEmission);
        if (portrait != nullptr)
        {
            portrait->SetAppearance(PortraitLoop2Diffuse, PortraitLoop2Emission, PortraitLoop2Shininess);
        }
        if (light3 != nullptr)
        {
            light3->SetEmergencyLight(true, Light3Loop2EmergencyOffset);
        }
        ShadowMan* shadow = m_Objects.shadow;
        if (shadow != nullptr)
        {
            shadow->SetActive(true);
            shadow->EnableGazeScare(Loop2ShadowGazeSeconds);
            shadow->SetOnObserved(
                [this]()
                {
                    StartObservedScare();
                });
        }
    }
    else
    {
        SetFalseDoorState(false, false);
        RevealScratchPieces(Loop2ScratchEnd, Stage2ScratchCount, FinalLoopScratchEmission);
        // 最後の周では、3つの信号盤を直す必要がある。
        // 廊下を探して、表示された色の順番どおりに操作した後だけ、最後の停電と追跡を始めている。
        m_FinalSequenceArmed = false;
        CeilingLight* doorLight = m_Objects.Light(Stage2Light::DoorLight);
        if (doorLight != nullptr)
        {
            EventFeedback::Flicker(*doorLight, FinalLoopDoorFlicker);
        }
        EventFeedback::HorrorPulse(*game->GetPostProcess(), FinalLoopPulse);
    }
}

// 周回に合わせて時計の針を決めている（3周目以降は文字盤を赤黒くしている）
void Stage2Scene::ConfigureClockForLoop()
{
    m_ClockAnomaly.ConfigureForLoop(
        m_LoopCount, m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Clock));
    if (m_LoopCount >= Stage2FinalLoopCount)
    {
        Wall* face = m_Objects.clockFace;
        if (face != nullptr)
        {
            face->SetAppearance(ClockFinalDiffuse, ClockFinalEmission, ClockFinalShininess);
        }
    }
}

void Stage2Scene::UpdateClock(float deltaTime)
{
    // 針の進み方と、逆回りするときのカクカクした表示は、時計の異変のクラス自身が管理している。
    const bool clockLoop = m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Clock);
    m_ClockAnomaly.Update(m_LoopCount, clockLoop, deltaTime);
    const float displayedHourAngle =
        m_ClockAnomaly.GetDisplayedHourAngle(clockLoop);
    const float displayedMinuteAngle =
        m_ClockAnomaly.GetDisplayedMinuteAngle(clockLoop);

    // 針は、文字盤のシェーダーが角度から描いている（12時が0、右回りが+）
    Wall* face = m_Objects.clockFace;
    if (face != nullptr)
    {
        // 最後の停電と追跡の間は、針が狂ったように逆回りし続ける
        if (m_FinalSequence.IsSequenceActive() || m_FinalSequence.IsPursuitActive())
        {
            face->SetClockHands(-m_VisualTimer * ClockFrenzyHourSpeed, -m_VisualTimer * ClockFrenzyMinuteSpeed);
        }
        else
        {
            face->SetClockHands(displayedHourAngle, displayedMinuteAngle);
        }
    }
}

void Stage2Scene::UpdateClockObservation()
{
    // 時計を見つける周回（Stage2AnomalyPlanが決める）だけ、観察を受け付けている。
    if (!m_AnomalyPlan.IsRequired(m_LoopCount, Stage2Anomaly::Clock) ||
        m_ClockAnomaly.WasObservedThisLoop())
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Vector3 cameraToClock =
        ClockCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToClock.Length();
    if (distance > MinimumDirectionLength)
    {
        cameraToClock /= distance;
    }

    // 近く（98以内）で正面（内積0.91以上）から見たとき、ライトが点いていれば間違い、消えていれば見つけたことにしている
    const float facing =
        game->GetCamera()->GetForward().Dot(cameraToClock);
    if (distance > ClockLookDistance || facing < ClockLookFacing)
    {
        return;
    }

    Player* player = m_Objects.player;
    if (player != nullptr && player->IsFlashlightOn())
    {
        RegisterPuzzleMistake(PuzzleFeedback::ClockNeedsDark);
        return;
    }

    m_ClockAnomaly.MarkObserved();
    m_Notices.loop = Stage2NoticeSeconds;

    CeilingLight* doorLight =
        m_Objects.Light(Stage2Light::DoorLight);
    if (doorLight != nullptr)
    {
        EventFeedback::Flicker(*doorLight, DoorLightAlertFlicker);
    }
    EventFeedback::BloomPulse(*game->GetPostProcess(), ClockFoundBloom);

    CeilingLight* clockLight =
        m_Objects.Light(Stage2Light::Light2);
    if (clockLight != nullptr)
    {
        EventFeedback::Flicker(*clockLight, ClockNearbyFlicker);
    }
    EventFeedback::HorrorPulse(*game->GetPostProcess(), ClockFoundPulse);
    EventFeedback::Rumble(ClockFoundRumble);
}

// パズルの間違いを記録し、間違えるほど強く照明と画面で知らせている（3回目で近くの照明を消している）
void Stage2Scene::RegisterPuzzleMistake(int type)
{
    if (!m_PuzzleFeedback.TryRegisterMistake(type))
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterPuzzleMistake();
    CeilingLight* warningLight = m_Objects.Light(
        type == 1 ? Stage2Light::Light3 : Stage2Light::Light2);
    const int mistakeCount = m_PuzzleFeedback.GetMistakeCount();
    const float mistakeRate = static_cast<float>(mistakeCount) / static_cast<float>(MaxPuzzleMistakes);
    if (warningLight != nullptr)
    {
        warningLight->TriggerEventFlicker(
            MistakeFlickerSecondsBase + mistakeRate * MistakeFlickerSecondsPerRate,
            MistakeFlickerStrengthBase + mistakeRate * MistakeFlickerStrengthPerRate);
        if (mistakeCount >= MaxPuzzleMistakes)
        {
            warningLight->SetForcedOff(true);
        }
    }

    game->GetPostProcess()->TriggerHorrorPulse(
        MistakePulseBase.Strength + mistakeRate * MistakePulsePerRate.Strength,
        MistakePulseBase.Seconds + mistakeRate * MistakePulsePerRate.Seconds);
    game->GetPostProcess()->TriggerBloomPulse(
        MistakeBloomBase + mistakeRate * MistakeBloomPerRate,
        MistakeBloomSeconds);
    Input::SetVibration(
        MistakeRumbleBase.Frames + mistakeCount * MistakeRumbleFramesPerMistake,
        MistakeRumbleBase.Strength + mistakeRate * MistakeRumbleStrengthPerRate);
}

// 信号盤パズルを最初からやり直している（3周目なら信号盤を押せる状態に戻している）
void Stage2Scene::ResetSignalPuzzle()
{
    m_SignalPuzzle.Reset();
    if (m_LoopCount >= Stage2FinalLoopCount)
    {
        m_FinalSequenceArmed = false;
    }

    ShadowMan* signalShadow = m_Objects.shadow;
    if (signalShadow != nullptr && !m_FinalSequence.IsPursuitActive())
    {
        signalShadow->SetActive(false);
    }
    for (FuseBox* terminal : m_Objects.signalTerminals)
    {
        if (terminal != nullptr)
        {
            terminal->ResetActivation();
            terminal->SetManualInteractionAllowed(m_LoopCount >= Stage2FinalLoopCount);
        }
    }
    ApplySignalLightingState();
}

// 信号盤パズル：押された信号盤を順番どおりか判定し、目印の光り方を更新している
void Stage2Scene::UpdateSignalPuzzle()
{
    Core::Game* game = Core::Game::GetInstance();
    const auto& baseDiffuse = Stage2SignalDiffuse;
    const auto& baseEmission = Stage2SignalEmission;

    const bool puzzleActive =
        m_LoopCount >= Stage2FinalLoopCount && !m_SignalPuzzle.IsComplete();
    for (int signalIndex = 0; signalIndex < SignalTerminalCount; ++signalIndex)
    {
        FuseBox* terminal = m_Objects.signalTerminals[static_cast<std::size_t>(signalIndex)];
        if (terminal != nullptr)
        {
            terminal->SetManualInteractionAllowed(puzzleActive);
        }
    }

    if (puzzleActive)
    {
        for (int signalIndex = 0; signalIndex < SignalTerminalCount; ++signalIndex)
        {
            FuseBox* terminal = m_Objects.signalTerminals[static_cast<std::size_t>(signalIndex)];
            if (terminal == nullptr || !terminal->IsActivated() ||
                m_SignalPuzzle.IsAccepted(signalIndex))
            {
                continue;
            }

            const SignalPuzzle::AcceptResult acceptResult =
                m_SignalPuzzle.Accept(signalIndex);
            // 順番が違えば、間違いを数えて危険度を大きく上げている
            if (acceptResult == SignalPuzzle::AcceptResult::WrongOrder)
            {
                if (m_LoopCount >= Stage2FinalLoopCount)
                {
                    m_FinalSequenceArmed = false;
                }
                RegisterPuzzleMistake(PuzzleFeedback::SignalWrongOrder);
                m_Notices.signal = Stage2NoticeSeconds;
                m_NoiseThreatSystem.SetThreat(WrongOrderThreat);
                EventFeedback::HorrorPulse(*game->GetPostProcess(), WrongOrderPulse);
                EventFeedback::Rumble(WrongOrderRumble);
                break;
            }

            // 順番どおりなら、危険度を少し上げて、対応する照明を明滅させている
            m_PuzzleFeedback.SetType(PuzzleFeedback::NoFeedback);
            const float retryAssist = static_cast<float>((std::min)(
                m_PuzzleFeedback.GetMistakeCount(), SignalStalkerMaxAssist));
            m_NoiseThreatSystem.SetThreat((std::min)(
                1.0f,
                m_NoiseThreatSystem.GetThreat() + CorrectThreatRise -
                    retryAssist * CorrectThreatAssistRelief));
            m_Notices.signal = Stage2ShortNoticeSeconds;
            m_ProgressHintTimer = 0.0f;
            EventFeedback::BloomPulse(*game->GetPostProcess(), CorrectBloom);
            Input::SetVibration(
                CorrectRumbleBase.Frames + m_SignalPuzzle.GetStep() * CorrectRumbleFramesPerStep,
                CorrectRumbleBase.Strength);

            CeilingLight* responseLight = m_Objects.Light(
                signalIndex == 0 ? Stage2Light::Light3 :
                signalIndex == 1 ? Stage2Light::Light2 : Stage2Light::Light1);
            if (responseLight != nullptr)
            {
                EventFeedback::Flicker(*responseLight, CorrectFlicker);
            }
            ApplySignalLightingState();

            // 3つそろったら、最後の停電の準備をして、影を消している
            if (acceptResult == SignalPuzzle::AcceptResult::Completed)
            {
                m_FinalSequenceArmed = true;
                m_Notices.loop = Stage2ImportantNoticeSeconds;
                ApplySignalLightingState();
                game->RegisterAnomalyHandled();
                ShadowMan* signalShadow =
                    m_Objects.shadow;
                if (signalShadow != nullptr)
                {
                    signalShadow->SetActive(false);
                }
                for (FuseBox* completedTerminal : m_Objects.signalTerminals)
                {
                    if (completedTerminal != nullptr)
                    {
                        completedTerminal->SetManualInteractionAllowed(false);
                    }
                }
                EventFeedback::BloomPulse(*game->GetPostProcess(), CompletedBloom);
                EventFeedback::HorrorPulse(*game->GetPostProcess(), CompletedPulse);
            }
            else
            {
                // 途中なら、背後に影を出して追わせている（間違えた回数が多いほど遠くに出し、遅くしている）
                Player* player = m_Objects.player;
                ShadowMan* signalShadow =
                    m_Objects.shadow;
                if (player != nullptr && signalShadow != nullptr)
                {
                    const Vector3 playerPosition = player->GetPosition();
                    Vector3 backward = -player->GetForward();
                    backward.y = 0.0f;
                    if (backward.LengthSquared() < MinimumDirectionLength)
                    {
                        backward = Vector3(0.0f, 0.0f, -1.0f);
                    }
                    backward.Normalize();
                    const float assistLevel = static_cast<float>((std::min)(
                        m_PuzzleFeedback.GetMistakeCount(), SignalStalkerMaxAssist));
                    const float spawnDistance =
                        SignalStalkerBaseDistance - m_SignalPuzzle.GetStep() * SignalStalkerDistancePerStep +
                            assistLevel * SignalStalkerDistancePerAssist;
                    signalShadow->SetPosition(
                        playerPosition.x + backward.x * spawnDistance,
                        Stage2FootY,
                        playerPosition.z + backward.z * spawnDistance);
                    signalShadow->SetActive(false);
                    signalShadow->SetActive(true);
                    signalShadow->EnableChase(
                        SignalStalkerBaseSpeed + static_cast<float>(
                            m_SignalPuzzle.GetStep()) * SignalStalkerSpeedPerStep -
                            assistLevel * SignalStalkerSlowdownPerAssist,
                        SignalStalkerStopDistance);
                    signalShadow->EnableGazeScare(SignalStalkerGazeScareSeconds);
                    signalShadow->SetOnObserved(
                        [this]()
                        {
                            Core::Game* currentGame =
                                Core::Game::GetInstance();
                            ShadowMan* currentShadow =
                                m_Objects.shadow;
                            if (currentShadow != nullptr)
                            {
                                currentShadow->SetActive(false);
                            }
                            m_NoiseThreatSystem.SetThreat((std::max)(SignalStalkerSeenMinThreat,
                                m_NoiseThreatSystem.GetThreat() - SignalStalkerSeenThreatDrop));
                            m_PuzzleFeedback.Set(PuzzleFeedback::SignalStalkerRepelled, SignalStalkerSeenFeedbackSeconds);
                            m_Notices.signal = Stage2ShortNoticeSeconds;
                            EventFeedback::BloomPulse(*currentGame->GetPostProcess(), SignalStalkerSeenBloom);
                            EventFeedback::Rumble(SignalStalkerSeenRumble);
                        });
                }
            }
            break;
        }
    }

    // 目印：受け付けた信号盤は緑に、次に押す信号盤は明るく脈打たせている
    const float pulse = std::sin(m_VisualTimer * MarkerPulseSpeed) * 0.5f + 0.5f;
    for (int signalIndex = 0; signalIndex < SignalTerminalCount; ++signalIndex)
    {
        Wall* marker = m_Objects.signalMarkers[static_cast<std::size_t>(signalIndex)];
        if (marker == nullptr)
        {
            continue;
        }

        if (m_SignalPuzzle.IsComplete() ||
            m_SignalPuzzle.IsAccepted(signalIndex))
        {
            marker->SetAppearance(
                AcceptedMarkerDiffuse,
                Color(AcceptedMarkerEmissionRed,
                    AcceptedMarkerEmissionGreenBase + pulse * AcceptedMarkerEmissionGreenPulse,
                    AcceptedMarkerEmissionBlue, 1.0f),
                AcceptedMarkerShininess);
        }
        else
        {
            const bool isTarget = puzzleActive &&
                signalIndex == m_SignalPuzzle.GetStep();
            const float energy = isTarget ? TargetMarkerEnergyBase + pulse * TargetMarkerEnergyPulse : IdleMarkerEnergy;
            marker->SetAppearance(
                baseDiffuse[signalIndex],
                baseEmission[signalIndex] * energy,
                isTarget ? TargetMarkerShininess : IdleMarkerShininess);
        }
    }
}

// 信号盤の段階に合わせて、照明を戻している（直していない区画は赤い非常灯のまま）
void Stage2Scene::ApplySignalLightingState()
{
    if (m_LoopCount < Stage2FinalLoopCount)
    {
        return;
    }

    constexpr Stage2Light restorationLights[] =
    {
        Stage2Light::Light3,
        Stage2Light::Light2,
        Stage2Light::Light1
    };
    constexpr float flickerOffsets[] = { 2.8f, 1.4f, 0.2f };

    for (int lightIndex = 0; lightIndex < SignalTerminalCount; ++lightIndex)
    {
        CeilingLight* light = m_Objects.Light(restorationLights[lightIndex]);
        if (light == nullptr)
        {
            continue;
        }

        const bool restored =
            m_SignalPuzzle.IsComplete() ||
            lightIndex < m_SignalPuzzle.GetStep();
        light->SetForcedOff(false);
        light->SetFaulted(!restored);
        light->SetEmergencyLight(!restored, flickerOffsets[lightIndex]);
    }

    CeilingLight* exitLight = m_Objects.Light(Stage2Light::DoorLight);
    if (exitLight != nullptr)
    {
        exitLight->SetForcedOff(false);
        exitLight->SetFaulted(!m_SignalPuzzle.IsComplete());
        exitLight->SetEmergencyLight(
            !m_SignalPuzzle.IsComplete(),
            ExitLightEmergencyOffset);
        if (m_SignalPuzzle.IsComplete())
        {
            EventFeedback::Flicker(*exitLight, ExitLightRestoredFlicker);
        }
    }
}

// 信号盤パズルの間に背後から来る影：近いほど危険度を上げ、35まで近づかれたらパズルをやり直させている
void Stage2Scene::UpdateSignalStalker()
{
    if (m_LoopCount < Stage2FinalLoopCount || m_SignalPuzzle.IsComplete() ||
        m_SignalPuzzle.GetStep() <= 0)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Player* player = m_Objects.player;
    ShadowMan* shadow = m_Objects.shadow;
    if (player == nullptr || shadow == nullptr || !shadow->IsActive())
    {
        return;
    }

    Vector3 toShadow = shadow->GetPosition() - player->GetPosition();
    toShadow.y = 0.0f;
    const float distance = toShadow.Length();
    const float proximity = MathUtility::Proximity(distance, SignalStalkerNearDistance, SignalStalkerThreatRange);
    m_NoiseThreatSystem.SetThreat((std::max)(
        m_NoiseThreatSystem.GetThreat(), proximity * SignalStalkerProximityThreat));

    if (distance > SignalStalkerCatchDistance)
    {
        return;
    }

    shadow->SetActive(false);
    ResetSignalPuzzle();
    RegisterPuzzleMistake(PuzzleFeedback::SignalStalkerCaught);
    m_PuzzleFeedback.Set(PuzzleFeedback::SignalStalkerCaught, SignalStalkerCaughtFeedbackSeconds);
    m_Notices.signal = Stage2LongNoticeSeconds;
    m_NoiseThreatSystem.SetThreat(SignalStalkerCaughtThreat);
    m_NoiseThreatSystem.SetEventCooldown(SignalStalkerCaughtEventCooldown);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), SignalStalkerCaughtPulse);
    EventFeedback::BloomPulse(*game->GetPostProcess(), SignalStalkerCaughtBloom);
    EventFeedback::Rumble(SignalStalkerCaughtRumble);
}

// 偽の扉の表示と位置を切り替えている。右側に移るときは、入口寄り（z=-88）の右の壁に置いている
void Stage2Scene::SetFalseDoorState(bool visible, bool rightSide)
{
    m_FalseDoorAnomaly.SetVisible(visible);
    // パネルと枠を少しずらして、表面が重ならないように配置する。
    constexpr float FalseDoorSurfaceOffsetX = 39.3f;
    constexpr float FalseDoorFrameOffsetX = 39.0f;
    const float surfaceX = rightSide ? FalseDoorSurfaceOffsetX : -FalseDoorSurfaceOffsetX;
    const float frameX = rightSide ? FalseDoorFrameOffsetX : -FalseDoorFrameOffsetX;
    const float handleX = rightSide ? Stage2FalseDoorHandleX : -Stage2FalseDoorHandleX;
    const float centerZ = rightSide ? Stage2FalseDoorRightZ : Stage2FalseDoorLeftZ;

    Wall* panel = m_Objects.falseDoorPanel;
    Wall* frameNear = m_Objects.falseDoorFrameNear;
    Wall* frameFar = m_Objects.falseDoorFrameFar;
    Wall* frameTop = m_Objects.falseDoorFrameTop;
    Wall* handle = m_Objects.falseDoorHandle;
    if (panel == nullptr || frameNear == nullptr || frameFar == nullptr ||
        frameTop == nullptr || handle == nullptr)
    {
        return;
    }

    panel->SetPosition(surfaceX, FalseDoorCenterY, centerZ);
    // 扉の絵の鍵穴を、取っ手の側（左の壁なら見て左、右の壁なら見て右）に描いている
    panel->SetSurfaceParams(Color(rightSide ? 1.0f : -1.0f, 0.0f, 0.0f, 0.0f));
    frameNear->SetPosition(frameX, FalseDoorCenterY, centerZ - FalseDoorFrameHalfWidth);
    frameFar->SetPosition(frameX, FalseDoorCenterY, centerZ + FalseDoorFrameHalfWidth);
    frameTop->SetPosition(frameX, FalseDoorFrameTopY, centerZ);
    handle->SetPosition(handleX, FalseDoorCenterY, centerZ - FalseDoorHandleOffsetZ);

    // 現れるときと消えるときは、焦げた縁が広がるように崩していく。表示したまま反対側の壁へ移るときは、
    // 見ていない間に一瞬で移している（移る瞬間を見せない）
    for (Wall* piece : { panel, frameNear, frameFar, frameTop, handle })
    {
        if (visible && (!piece->IsVisible() || piece->IsDissolvingOut()))
        {
            piece->DissolveIn(FalseDoorDissolveInSeconds);
        }
        else if (!visible && piece->IsVisible())
        {
            piece->DissolveOut(FalseDoorDissolveOutSeconds);
        }
    }
}

// 息を止めるボタン（キーボードは Space、コントローラーは RB）を押しているか
bool Stage2Scene::IsBreathHoldPressed()
{
    return Input::GetKeyPress(VK_SPACE) || Input::GetButtonPress(XINPUT_RIGHT_SHOULDER);
}
