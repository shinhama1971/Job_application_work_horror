// ============================================================================
// ファイルの役割: 2面の足音による危険度、影の追跡、ロッカーに隠れている間の処理。
// ============================================================================

#include "Stage2Scene.h"
#include "MathUtility.h"
#include "CeilingLight.h"
#include "EventFeedback.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

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
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;
    using EventFeedback::SoundSpec;
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
        FadeOutNoiseThreat(deltaTime);
        return;
    }

    // 3周目で信号盤を直し終えた後も、同じように危険度を下げている
    const bool signalStealthActive =
        m_LoopCount >= Stage2FinalLoopCount && !m_SignalPuzzle.IsComplete();
    if (m_LoopCount >= Stage2FinalLoopCount && !signalStealthActive)
    {
        FadeOutNoiseThreat(deltaTime);
        return;
    }

    if (UpdateHiddenFromStalker(player, deltaTime))
    {
        return;
    }

    AddFootstepThreat(player, deltaTime, signalStealthActive);
    if (UpdateNoiseStalkerProximity(player))
    {
        return;
    }
    UpdateQuietRecovery(player, deltaTime);
    TrySpawnNoiseStalker(player);
    if (ResetSignalPuzzleOnNoise(signalStealthActive))
    {
        return;
    }
    TriggerThreatWarning(player);
}

// 危険度を少しずつ下げ、足音の影を消している（最後の演出の間と、信号盤を直し終えた後）
void Stage2Scene::FadeOutNoiseThreat(float deltaTime)
{
    m_NoiseThreatSystem.SetThreat((std::max)(0.0f,
        m_NoiseThreatSystem.GetThreat() - deltaTime * FinalThreatDecayPerSecond));
    ShadowMan* noiseShadow = m_Objects.noiseShadow;
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
}

// 足音の大きさ（水たまり・走る・歩く）で危険度を上げ下げしている
void Stage2Scene::AddFootstepThreat(const Player& player, float deltaTime, bool signalStealthActive)
{
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
}

// 足音の影が近いほど危険度を上げている。捕まったら true を返す
bool Stage2Scene::UpdateNoiseStalkerProximity(Player& player)
{
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
            return true;
        }
    }
    return false;
}

// 初めの3周では、距離を取って消灯・静止すると、足音の影を振り切れる。
// 信号盤パズルと最後の追跡は、それぞれの対処のしかたのままにしている。
void Stage2Scene::UpdateQuietRecovery(const Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* noiseShadow = m_Objects.noiseShadow;
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
}

// 危険度が上がったら、背後に足音の影を出して追わせている
void Stage2Scene::TrySpawnNoiseStalker(const Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* noiseShadow = m_Objects.noiseShadow;
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
}

// 信号盤パズルの間に危険度が最大になったら、パズルを最初からやり直させている。やり直したら true を返す
bool Stage2Scene::ResetSignalPuzzleOnNoise(bool signalStealthActive)
{
    if (!signalStealthActive || m_NoiseThreatSystem.GetThreat() < SignalResetThreat)
    {
        return false;
    }
    Core::Game* game = Core::Game::GetInstance();
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
    return true;
}

// 危険度が警告の高さ以上になると、近くの照明を明滅させて警告している
void Stage2Scene::TriggerThreatWarning(const Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
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

// 息を止めるボタン（キーボードは Space、コントローラーは RB）を押しているか
bool Stage2Scene::IsBreathHoldPressed()
{
    return Input::GetKeyPress(VK_SPACE) || Input::GetButtonPress(XINPUT_RIGHT_SHOULDER);
}
