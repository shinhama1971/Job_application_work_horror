// ============================================================================
// ファイルの役割: 1面の進行の演出（電力の復旧・出口への送電・出口の前兆・姿の見えない物音・壁の文字・危険度と心拍・録音と無線）を担当している。
// 主な技術: 出来事をきっかけにした処理、有限状態機械、照明・音・画面効果のタイミング合わせ
// 値（場所・秒数・強さ）は Stage1EventSettings.h にまとめている。
// ============================================================================

#include "StageScene.h"
#include "Stage1EventSettings.h"
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
using namespace Stage1EventSettings;

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
        !m_CorridorFlood.IsActive() &&
        !m_TimeStopRoom.IsActive() &&
        !m_EchoRoom.IsDark() &&
        !powerRestoring &&
        m_SafeLights.GetCalm() < SafeLightQuietCalm &&
        (exit == nullptr || !exit->IsEscaping());
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

void StageScene::UpdateHands(Player& player, float deltaTime)
{
    const bool allFuses = Core::Game::GetInstance()->GetItemCount() >= Core::GameState::RequiredFuseCount;
    const float target =
        allFuses && m_CorridorLoopCount > 0 ? HandCorruptionAllFuses
        : m_CorridorLoopCount <= 0 ? 0.0f
        : m_CorridorLoopCount == 1 ? HandCorruptionFirstLoop
        : m_CorridorLoopCount == 2 ? HandCorruptionSecondLoop
        : HandCorruptionLaterLoop;
    const float step = HandCorruptionRate * deltaTime;
    m_HandCorruption = std::clamp(target, m_HandCorruption - step, m_HandCorruption + step);
    player.SetHandCorruption(m_HandCorruption);
    // 影が近いほど、手が細かく震える（心拍と同じ影の近さ）
    player.SetHandFear(ComputeThreatRate(player));
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
    // 閉じ込められている間・照明が次々に明滅する演出の間・監視映像を見ている間・赤い濁流が押し寄せている間は、新しい無線を予約しない
    input.RadioAllowed = !m_HiddenRoom.IsTrapped() && !m_ScareLightSequence.IsActive() && !m_Surveillance.IsViewing() &&
        !m_CorridorFlood.IsActive();
    input.ArchiveStalkerLit = archiveStalkerLit;
    m_Transmissions.Update(input);
}
