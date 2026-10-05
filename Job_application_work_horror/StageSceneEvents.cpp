// ============================================================================
// ファイルの役割: 1面の入口演出、廊下ループ、電力復旧、出口イベントを管理します。
// 主な技術: イベント駆動、有限状態機械、カメラ・照明・音の同期
// ============================================================================

#include "StageScene.h"
#include "Application.h"
#include "Game.h"
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
#include "TensionPulseFeedback.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

void StageScene::UpdateEntranceThresholdEvent(Player& player)
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();

    if (!m_EntranceEventTriggered)
    {
        const Vector3 position = player.GetPosition();
        const bool crossedDoorThreshold =
            !game->IsPowerRestored() &&
            position.z > 62.0f &&
            std::abs(position.x) < 58.0f;
        if (!crossedDoorThreshold)
        {
            return;
        }

        // 強制カメラ演出の代わりに、空間内の照明変化で出来事を見せます。
        // 操作を奪わず、プレイヤー自身が異変へ気付ける演出にします。
        m_EntranceEventTriggered = true;
        m_EntranceEventTimer = 0.0f;
        m_EntranceEventPhase = 0;

        CeilingLight* lightBehind =
            m_Objects.CeilingLightAt(4);
        if (lightBehind != nullptr)
        {
            lightBehind->TriggerEventFlicker(0.38f, 0.52f);
        }

        game->GetPostProcess()->TriggerHorrorPulse(0.075f, 0.14f);
        Input::SetVibration(3, 0.07f);
        return;
    }

    if (m_EntranceEventTimer < 0.0f)
    {
        return;
    }
    if (game->IsPowerRestored())
    {
        m_EntranceEventTimer = -1.0f;
        m_EntranceEventPhase = -1;
        return;
    }

    m_EntranceEventTimer += deltaTime;
    if (m_EntranceEventPhase == 0 && m_EntranceEventTimer >= 0.38f)
    {
        CeilingLight* lightAhead =
            m_Objects.CeilingLightAt(5);
        if (lightAhead != nullptr)
        {
            lightAhead->TriggerEventFlicker(0.62f, 0.72f);
        }

        game->GetPostProcess()->TriggerHorrorPulse(0.10f, 0.18f);
        Input::SetVibration(4, 0.09f);
        m_EntranceEventPhase = 1;
    }
    else if (m_EntranceEventPhase == 1 &&
        m_EntranceEventTimer >= 1.12f)
    {
        CeilingLight* lightBehind =
            m_Objects.CeilingLightAt(4);
        if (lightBehind != nullptr)
        {
            lightBehind->TriggerEventFlicker(0.20f, 0.28f);
        }

        m_EntranceEventTimer = -1.0f;
        m_EntranceEventPhase = 2;
    }
}

void StageScene::UpdateStorageScare(Player& player)
{
    if (m_StorageScarePhase >= 2)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 position = player.GetPosition();
    if (game->IsPowerRestored() || game->GetItemCount() >= 2)
    {
        m_StorageScarePhase = 3;
        return;
    }

    if (m_StorageScarePhase == 0)
    {
        // 二周目の左倉庫へ踏み込んだとき、先に物音と照明で背後を意識させます。
        if (m_CorridorLoopCount < 1 || game->GetItemCount() != 1 ||
            position.x > -110.0f || position.z > -108.0f)
        {
            return;
        }

        m_StorageScarePhase = 1;
        m_StorageScareTimer = 0.0f;
        m_StorageScareNoticeTimer = 2.1f;
        game->PlayAudioCue(SOUND_CUE_DOOR, 0.68f);
        CeilingLight* light = m_Objects.CeilingLightAt(2);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.42f, 0.62f);
        }
        return;
    }

    m_StorageScareTimer += Application::GetDeltaTime();
    if (m_StorageScareTimer < 0.8f)
    {
        return;
    }
    m_StorageScarePhase = 2;

    // 倉庫から離れた場合は出現させず、視界の外に突然残る人影を防ぎます。
    if (position.x > -100.0f || position.z > -90.0f)
    {
        m_StorageScarePhase = 3;
        return;
    }

    ShadowMan* shadow = m_Objects.storageShadow;
    if (shadow == nullptr)
    {
        m_StorageScarePhase = 3;
        return;
    }
    shadow->SetPosition(-155.0f, -99.0f, -88.0f);
    shadow->SetActive(true);
    shadow->EnableGazeScare(5.0f);
    m_StorageScareNoticeTimer = 2.0f;
    shadow->SetOnObserved([this]()
    {
        Core::Game* currentGame = Core::Game::GetInstance();
        currentGame->RegisterAnomalyHandled();
        m_StorageScarePhase = 3;
        m_StorageScareNoticeTimer = 1.8f;
        CeilingLight* light =
            m_Objects.CeilingLightAt(2);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.75f, 0.88f);
        }
        currentGame->PlayAudioCue(SOUND_CUE_SCARE, 0.82f);
        currentGame->GetPostProcess()->TriggerHorrorPulse(0.24f, 0.26f);
        Input::SetVibration(7, 0.15f);
    });
}

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
    if (game->IsPowerRestored() || m_LoopCooldown > 0.0f)
    {
        return;
    }

    const Vector3 position = player.GetPosition();
    const bool insideLoopExit =
        position.x > 178.0f &&
        position.z > 237.0f && position.z < 273.0f;
    if (insideLoopExit)
    {
        AdvanceCorridorLoop(player);
    }
}

void StageScene::AdvanceCorridorLoop(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    ++m_CorridorLoopCount;

    // 同じフレーム後半でPlayerがカメラを更新するため、再配置の瞬間を隠しつつ
    // プレイヤーが見ていた方向を維持できます。
    player.SetPosition(Vector3(0.0f, -99.0f, -150.0f));
    m_LoopCooldown = 1.0f;
    m_ProgressHintTimer = 0.0f;
    m_LoopNoticeTimer = 2.4f;

    const int loopPhase = m_CorridorLoopCount < 3
        ? m_CorridorLoopCount
        : 3;

    for (int markerIndex = 1; markerIndex <= 3; ++markerIndex)
    {
        Wall* marker = m_Objects.loopMarkers[static_cast<std::size_t>(markerIndex - 1)];
        if (marker != nullptr)
        {
            marker->SetVisible(markerIndex <= loopPhase);
            const float intensity =
                0.14f + static_cast<float>(markerIndex) * 0.055f;
            marker->SetAppearance(
                Color(0.26f, 0.012f, 0.008f, 1.0f),
                Color(intensity, 0.002f, 0.001f, 1.0f),
                24.0f);
        }
    }
    game->GetPostProcess()->TriggerHorrorPulse(
        0.24f + static_cast<float>(loopPhase) * 0.13f,
        0.42f + static_cast<float>(loopPhase) * 0.10f);
    Input::SetVibration(7 + loopPhase * 3, 0.18f + loopPhase * 0.04f);

    // 入口へ戻すときに廊下の扉も復元します。同じ境界を再び開けさせることで、
    // 各周回が意図的な反復として感じられるようにします。
    Door* loopDoor = m_Objects.loopDoor;
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(loopPhase);
    }

    CeilingLight* entranceLight =
        m_Objects.CeilingLightAt(1);
    CeilingLight* middleLight =
        m_Objects.CeilingLightAt(4);
    CeilingLight* cornerLight =
        m_Objects.CeilingLightAt(8);

    if (loopPhase == 1)
    {
        Item* secondFuse = m_Objects.secondFuse;
        if (secondFuse != nullptr && !secondFuse->IsCollected())
        {
            secondFuse->SetActive(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(false, 2.4f);
        }
    }
    else if (loopPhase == 2)
    {
        Item* thirdFuse = m_Objects.thirdFuse;
        if (thirdFuse != nullptr && !thirdFuse->IsCollected())
        {
            thirdFuse->SetActive(true);
        }

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, 5.8f);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(true, 1.1f);
        }

        game->RequestAddObject<ShadowMan>(
            [](ShadowMan& shadow)
            {
                shadow.SetPosition(0.0f, -99.0f, -25.0f);
            });
    }
    else
    {
        if (entranceLight != nullptr)
        {
            entranceLight->SetEmergencyLight(false, 0.0f);
        }
        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, 8.2f);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(false, 4.3f);
        }

        // 警告文どおり、プレイヤーの背後に一度だけ人影を出現させます。
        if (m_CorridorLoopCount == 3)
        {
            game->RequestAddObject<ShadowMan>(
                [this](ShadowMan& shadow)
                {
                    shadow.SetPosition(0.0f, -99.0f, -170.0f);
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

void StageScene::StartScareLightSequence()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPowerRestored())
    {
        return;
    }

    m_ScareLightSequence.Start();

    CeilingLight* entrance =
        m_Objects.CeilingLightAt(1);
    CeilingLight* middle =
        m_Objects.CeilingLightAt(4);
    CeilingLight* hall =
        m_Objects.CeilingLightAt(5);
    CeilingLight* corner =
        m_Objects.CeilingLightAt(8);
    CeilingLight* loopExit =
        m_Objects.CeilingLightAt(7);

    if (entrance != nullptr)
    {
        entrance->SetEmergencyLight(true, 0.35f);
    }
    if (middle != nullptr)
    {
        middle->SetEmergencyLight(false, 8.2f);
    }
    if (hall != nullptr)
    {
        hall->SetEmergencyLight(false, 3.1f);
    }
    if (corner != nullptr)
    {
        corner->SetEmergencyLight(false, 4.3f);
    }
    if (loopExit != nullptr)
    {
        loopExit->SetEmergencyLight(false, 4.5f);
    }
}

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
        m_Objects.CeilingLightAt(1);
    CeilingLight* middle =
        m_Objects.CeilingLightAt(4);
    CeilingLight* hall =
        m_Objects.CeilingLightAt(5);
    CeilingLight* corner =
        m_Objects.CeilingLightAt(8);
    CeilingLight* loopExit =
        m_Objects.CeilingLightAt(7);

    switch (m_ScareLightSequence.ConsumePendingBeat())
    {
    case 0:
        if (entrance != nullptr)
        {
            entrance->SetEmergencyLight(false, 0.35f);
        }
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, 0.75f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.13f, 0.18f);
        Input::SetVibration(5, 0.10f);
        break;
    case 1:
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(false, 0.75f);
        }
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(true, 0.42f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.14f, 0.18f);
        Input::SetVibration(6, 0.13f);
        break;
    case 2:
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(false, 0.42f);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, 0.18f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.20f);
        Input::SetVibration(7, 0.16f);
        break;
    case 3:
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(false, 0.18f);
        }
        if (loopExit != nullptr)
        {
            loopExit->SetEmergencyLight(true, 0.08f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.19f, 0.22f);
        Input::SetVibration(8, 0.19f);
        break;
    case 4:
        if (middle != nullptr)
        {
            middle->SetEmergencyLight(true, 2.6f);
        }
        if (hall != nullptr)
        {
            hall->SetEmergencyLight(true, 3.1f);
        }
        if (corner != nullptr)
        {
            corner->SetEmergencyLight(true, 4.3f);
        }
        Input::SetVibration(4, 0.09f);
        break;
    default:
        break;
    }
}

void StageScene::StartFuseWatcher(int fuseCount)
{
    if (fuseCount < 2 || Core::Game::GetInstance()->IsPowerRestored())
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
        -99.0f,
        fuseCount == 2 ? -22.0f : 118.0f);
    watcher->SetActive(true);
    watcher->EnableGazeScare(fuseCount == 2 ? 5.8f : 7.2f);
    watcher->SetOnObserved(
        [this]()
        {
            m_FuseWatcherState = 2;
            m_FuseWatcherNoticeTimer = 1.8f;

            Core::Game* game = Core::Game::GetInstance();
            game->RegisterAnomalyHandled();
            ShadowMan* activeWatcher =
                m_Objects.fuseWatcher;
            if (activeWatcher != nullptr)
            {
                activeWatcher->SetActive(false);
            }

            CeilingLight* reactionLight =
                m_Objects.CeilingLightAt(4);
            if (reactionLight != nullptr)
            {
                reactionLight->TriggerEventFlicker(0.72f, 0.74f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.42f, 0.18f);
            Input::SetVibration(7, 0.16f);
        });

    m_FuseWatcherState = 1;
    m_FuseWatcherNoticeTimer = fuseCount == 2 ? 5.8f : 7.2f;
    game->GetPostProcess()->TriggerHorrorPulse(
        fuseCount == 2 ? 0.20f : 0.32f,
        0.30f);
}

void StageScene::UpdatePowerRestoreSequence()
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();
    const bool powerRestored = game->IsPowerRestored();

    if (m_PowerSequence.ObservePowerState(powerRestored))
    {
        m_ProgressHintTimer = 0.0f;

        // ここから先の画面・照明・音の演出は通電シーケンス側で一括管理します。
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
    case 0:
        game->GetPostProcess()->TriggerBloomPulse(1.18f, 0.55f);
        Input::SetVibration(9, 0.16f);
        break;
    case 1:
        game->GetPostProcess()->TriggerHorrorPulse(0.12f, 0.22f);
        Input::SetVibration(6, 0.11f);
        break;
    case 2:
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.40f);
        Input::SetVibration(4, 0.07f);
        break;
    default:
        break;
    }
}

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

        CeilingLight* corner = m_Objects.CeilingLightAt(7);
        CeilingLight* exitLight = m_Objects.CeilingLightAt(8);
        if (corner != nullptr) corner->SetForcedOff(true);
        if (exitLight != nullptr) exitLight->SetForcedOff(true);
        game->GetPostProcess()->TriggerHorrorPulse(0.30f, 0.28f);
        return;
    }

    if (m_PowerSequence.IsExitComplete())
    {
        return;
    }

    m_PowerSequence.AdvanceExit(deltaTime);
    switch (m_PowerSequence.ConsumeExitBeat())
    {
    case 0:
    {
        CeilingLight* corner = m_Objects.CeilingLightAt(7);
        if (corner != nullptr)
        {
            corner->SetForcedOff(false);
            corner->SetEmergencyLight(false, 0.0f);
            corner->TriggerEventFlicker(0.72f, 0.82f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.68f, 0.24f);
        Input::SetVibration(5, 0.11f);
        break;
    }
    case 1:
    {
        CeilingLight* exitLight = m_Objects.CeilingLightAt(8);
        if (exitLight != nullptr)
        {
            exitLight->SetForcedOff(false);
            exitLight->SetEmergencyLight(false, 0.0f);
            exitLight->TriggerEventFlicker(0.82f, 0.92f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.92f, 0.30f);
        Input::SetVibration(7, 0.15f);
        break;
    }
    case 2:
        game->GetPostProcess()->TriggerBloomPulse(1.18f, 0.42f);
        Input::SetVibration(10, 0.20f);
        break;
    default:
        break;
    }
}

void StageScene::UpdateExitOmen(Player& player)
{
    const float deltaTime = Application::GetDeltaTime();
    m_ExitOmenSequence.Update(deltaTime);
    if (m_ExitOmenSequence.IsTriggered())
    {
        Core::Game* game = Core::Game::GetInstance();
        switch (m_ExitOmenSequence.ConsumePendingBeat())
        {
        case 0:
        {
            CeilingLight* lightBehind =
                m_Objects.CeilingLightAt(8);
            if (lightBehind != nullptr)
            {
                lightBehind->SetForcedOff(true);
            }
            game->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.30f);
            Input::SetVibration(7, 0.16f);
            break;
        }
        case 1:
        {
            CeilingLight* exitLight =
                m_Objects.CeilingLightAt(7);
            if (exitLight != nullptr)
            {
                exitLight->SetFaulted(true);
                exitLight->TriggerEventFlicker(1.10f, 0.88f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.44f, 0.20f);
            break;
        }
        default:
            break;
        }
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 playerPosition = player.GetPosition();
    if (!m_PowerSequence.IsExitComplete() ||
        playerPosition.z < 215.0f ||
        playerPosition.x < 28.0f)
    {
        return;
    }

    m_ExitOmenSequence.Start();

    ShadowMan* shadow =
        m_Objects.exitOmen;
    if (shadow != nullptr)
    {
        shadow->SetActive(true);
        shadow->EnableGazeScare(4.2f);
    }

    for (int lightNumber : { 7, 8 })
    {
        CeilingLight* light = m_Objects.CeilingLightAt(lightNumber);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.82f, 0.78f);
        }
    }

    game->GetPostProcess()->TriggerHorrorPulse(0.28f, 0.42f);
    game->GetPostProcess()->TriggerBloomPulse(0.54f, 0.24f);
    Input::SetVibration(10, 0.22f);
}

// ----------------------------------------------------------------------------
// 姿の見えない物音
// 台本の演出（照明連鎖・倉庫の気配・出口の前兆など）の最中や、監視映像を見ている間は鳴らしません。
// 演出の驚きを物音で打ち消さず、何も起きていない静かな時間にだけ不安を足すためです。
// ----------------------------------------------------------------------------
bool StageScene::IsAmbientSoundAllowed() const
{
    const Player* player = m_Objects.player;
    const ExitTrigger* exit = m_Objects.exitTrigger;
    const bool powerRestoring =
        m_PowerSequence.IsRestoreActive() && m_PowerSequence.GetRestoreTimer() < 6.0f;
    return player != nullptr &&
        player->CanControl() &&
        !m_Surveillance.IsViewing() &&
        !m_Surveillance.IsCaughtActive() &&
        !m_ScareLightSequence.IsActive() &&
        !m_HiddenRoom.IsTrapped() &&
        m_EntranceEventTimer < 0.0f &&
        m_StorageScarePhase != 1 &&
        m_FuseWatcherState != 1 &&
        m_ExitOmenSequence.GetTimer() <= 0.0f &&
        !powerRestoring &&
        (exit == nullptr || !exit->IsEscaping());
}

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
// ループ廊下の「ふりかえるな」は、目を離した隙に「ふりかえったな」へ書き換わります。
// それを読んだ瞬間、背後の天井裏を何かが歩いていき、振り返らせる流れを作ります。
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
    // 読んだ数はリザルト画面の「壁の文字」に出します。
    game->SetWallWritingsRead(m_WallWritings.GetReadCount());
    if (!changedWritingRead)
    {
        return;
    }

    m_AmbientSounds.StartCeilingStepsNow(
        game->IsPowerRestored(), camera->GetPosition(), camera->GetForward());
    game->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.36f);
    Input::SetVibration(5, 0.16f);
}

float StageScene::ComputeThreatRate(const Player& player) const
{
    // 出ている影のうち、いちばん近いものほど危険とします（2面の足音の影と同じ距離の感じ方）。
    const ShadowMan* shadows[] =
    {
        m_Objects.fuseWatcher,
        m_Objects.storageShadow,
        m_Objects.evidenceShadow,
        m_Objects.exitOmen,
        m_Objects.hiddenRoom.shadow,
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
        const float danger = 1.0f - (std::clamp)(
            (toShadow.Length() - 14.0f) / 72.0f, 0.0f, 1.0f);
        threatRate = (std::max)(threatRate, danger);
    }
    // 隠し部屋に閉じ込められている間は、影が出ていなくても小さく心拍が聞こえるようにします（息は荒くしません）。
    if (m_HiddenRoom.IsTrapped())
    {
        threatRate = (std::max)(threatRate, 0.30f);
    }
    return threatRate;
}

void StageScene::UpdateTensionPulse(const Player& player, float deltaTime)
{
    // 1面には隠れる場所がないため、隠れている扱いにはしません。
    PlayTensionPulse(m_TensionPulse.Update(deltaTime, ComputeThreatRate(player), false));
}
