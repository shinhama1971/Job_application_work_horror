// ============================================================================
// ファイルの役割: 2面の信号盤の順番、やり直し、信号盤の影とパズル失敗時の反応。
// ============================================================================

#include "Stage2Scene.h"
#include "MathUtility.h"
#include "CeilingLight.h"
#include "EventFeedback.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "PostProcess.h"
#include "Player.h"
#include "ShadowMan.h"
#include "Wall.h"
#include "Stage2SceneConstants.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{

    // --- 信号盤の間に背後から来る影：段階が進むほど近くに出て速く追う。間違えた回数が多いほど遠く遅くして助ける
    constexpr float SignalStalkerBaseDistance = 78.0f;
    constexpr float SignalStalkerDistancePerStep = 7.0f;
    constexpr float SignalStalkerDistancePerAssist = 13.0f;
    constexpr float SignalStalkerBaseSpeed = 13.0f;
    constexpr float SignalStalkerSpeedPerStep = 2.0f;
    constexpr float SignalStalkerSlowdownPerAssist = 1.6f;
    constexpr float SignalStalkerStopDistance = 30.0f;
    constexpr float SignalStalkerGazeScareSeconds = 24.0f;
    // 近いほど危険度を上げる範囲と、この距離まで来たら信号盤をやり直させる距離
    constexpr float SignalStalkerNearDistance = 30.0f;
    constexpr float SignalStalkerThreatRange = 90.0f;
    constexpr float SignalStalkerCatchDistance = 35.0f;

    // ------------------------------------------------------------------------
    // 演出の強さと長さ（照明の明滅・画面の脈打ち・振動・音）
    // ------------------------------------------------------------------------
    using EventFeedback::FlickerSpec;
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;

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
    const bool puzzleActive =
        m_LoopCount >= Stage2FinalLoopCount && !m_SignalPuzzle.IsComplete();
    for (int signalIndex = 0; signalIndex < Stage2SignalTerminalCount; ++signalIndex)
    {
        FuseBox* terminal = m_Objects.signalTerminals[static_cast<std::size_t>(signalIndex)];
        if (terminal != nullptr)
        {
            terminal->SetManualInteractionAllowed(puzzleActive);
        }
    }

    if (puzzleActive)
    {
        AcceptActivatedSignal();
    }
    UpdateSignalMarkers(puzzleActive);
}

// 操作された信号盤を1つだけ受け付け、順番が合っているかで反応を変えている
void Stage2Scene::AcceptActivatedSignal()
{
    for (int signalIndex = 0; signalIndex < Stage2SignalTerminalCount; ++signalIndex)
    {
        FuseBox* terminal = m_Objects.signalTerminals[static_cast<std::size_t>(signalIndex)];
        if (terminal == nullptr || !terminal->IsActivated() ||
            m_SignalPuzzle.IsAccepted(signalIndex))
        {
            continue;
        }

        const SignalPuzzle::AcceptResult acceptResult =
            m_SignalPuzzle.Accept(signalIndex);
        if (acceptResult == SignalPuzzle::AcceptResult::WrongOrder)
        {
            OnSignalWrongOrder();
        }
        else
        {
            OnSignalAccepted(signalIndex, acceptResult == SignalPuzzle::AcceptResult::Completed);
        }
        break;
    }
}

// 順番が違えば、間違いを数えて危険度を大きく上げている
void Stage2Scene::OnSignalWrongOrder()
{
    Core::Game* game = Core::Game::GetInstance();
    if (m_LoopCount >= Stage2FinalLoopCount)
    {
        m_FinalSequenceArmed = false;
    }
    RegisterPuzzleMistake(PuzzleFeedback::SignalWrongOrder);
    m_Notices.signal = Stage2NoticeSeconds;
    m_NoiseThreatSystem.SetThreat(WrongOrderThreat);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), WrongOrderPulse);
    EventFeedback::Rumble(WrongOrderRumble);
}

// 順番どおりなら、危険度を少し上げて、対応する照明を明滅させている
void Stage2Scene::OnSignalAccepted(int signalIndex, bool completed)
{
    Core::Game* game = Core::Game::GetInstance();
    m_PuzzleFeedback.SetType(PuzzleFeedback::NoFeedback);
    const float retryAssist = static_cast<float>((std::min)(
        m_PuzzleFeedback.GetMistakeCount(), Stage2SignalStalkerMaxAssist));
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

    // 3つそろったら、最後の停電の準備をして、影を消している。途中なら、背後に影を出して追わせている
    if (completed)
    {
        CompleteSignalPuzzle();
    }
    else
    {
        SpawnSignalStalker();
    }
}

// 3つそろったときの処理：最後の停電の準備をして、影を消し、信号盤を操作できなくしている
void Stage2Scene::CompleteSignalPuzzle()
{
    Core::Game* game = Core::Game::GetInstance();
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

// 途中なら、背後に影を出して追わせている（間違えた回数が多いほど遠くに出し、遅くしている）
void Stage2Scene::SpawnSignalStalker()
{
    Player* player = m_Objects.player;
    ShadowMan* signalShadow =
        m_Objects.shadow;
    if (player == nullptr || signalShadow == nullptr)
    {
        return;
    }
    const Vector3 playerPosition = player->GetPosition();
    Vector3 backward = -player->GetForward();
    backward.y = 0.0f;
    if (backward.LengthSquared() < Stage2MinimumDirectionLength)
    {
        backward = Vector3(0.0f, 0.0f, -1.0f);
    }
    backward.Normalize();
    const float assistLevel = static_cast<float>((std::min)(
        m_PuzzleFeedback.GetMistakeCount(), Stage2SignalStalkerMaxAssist));
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

// 目印：受け付けた信号盤は緑に、次に押す信号盤は明るく脈打たせている
void Stage2Scene::UpdateSignalMarkers(bool puzzleActive)
{
    const auto& baseDiffuse = Stage2SignalDiffuse;
    const auto& baseEmission = Stage2SignalEmission;
    const float pulse = std::sin(m_VisualTimer * MarkerPulseSpeed) * 0.5f + 0.5f;
    for (int signalIndex = 0; signalIndex < Stage2SignalTerminalCount; ++signalIndex)
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

    for (int lightIndex = 0; lightIndex < Stage2SignalTerminalCount; ++lightIndex)
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
