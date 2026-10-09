// ============================================================================
// ファイルの役割: 1面の演出（入口・左の倉庫・ループ廊下・照明の連鎖・ヒューズの後の影・電力の復旧・出口・物音・壁の文字・心拍）を担当している。
// 主な技術: 出来事をきっかけにした処理、有限状態機械、照明・音・画面効果のタイミング合わせ
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
            position.z > 62.0f &&
            std::abs(position.x) < 58.0f;
        if (!crossedDoorThreshold)
        {
            return;
        }

        // カメラを無理に動かす演出の代わりに、空間の中の照明の変化で出来事を見せている。
        // 操作を奪わず、プレイヤー自身が異変に気づける演出にしている。
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

    // 0.38秒で前の照明、1.12秒で後ろの照明を明滅させて終えている
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

// 左の倉庫の演出：2本目のヒューズを探しに入ると、物音の後に背後へ影を出している
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
        // 2回目に左の倉庫へ踏み込んだとき、先に物音と照明で背後を意識させている。
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

    // 倉庫から離れた場合は出さず、視界の外に人影が突然残るのを防いでいる。
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
    // 影を見たら、異変に対処した数を増やして、照明と音で反応している
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

// ループ廊下：電力が戻る前に、廊下の出口（右の奥）へ入ったら開始地点へ戻している
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

// ループ廊下を1周進めている：開始地点へ戻し、周回の数に応じて印・扉・照明・ヒューズ・影を変えている
void StageScene::AdvanceCorridorLoop(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    ++m_CorridorLoopCount;

    // 同じフレームの後半でPlayerがカメラを更新するため、場所を移した瞬間を隠しつつ、
    // プレイヤーが見ていた方向をそのまま保てる。
    player.SetPosition(Vector3(0.0f, -99.0f, -150.0f));
    m_LoopCooldown = 1.0f;
    m_ProgressHintTimer = 0.0f;
    m_LoopNoticeTimer = 2.4f;

    const int loopPhase = m_CorridorLoopCount < 3
        ? m_CorridorLoopCount
        : 3;

    // 周回の数だけ、壁の赤い印を出している
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

    // 開始地点へ戻すときに、廊下の扉も閉じた状態に戻している。同じ境目をもう一度開けさせることで、
    // 各周回が意図された繰り返しとして感じられるようにしている。
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
            middleLight->SetEmergencyLight(false, 2.4f);
        }
    }
    else if (loopPhase == 2)
    {
        // 2周目：3本目のヒューズは西棟の奥にある。右の倉庫に西棟の鍵を出し、鍵で西棟の扉を開けさせている。
        Item* thirdFuse = m_Objects.thirdFuse;
        if (thirdFuse != nullptr && !thirdFuse->IsCollected())
        {
            thirdFuse->SetActive(true);
        }
        m_WestWing.Activate();

        if (middleLight != nullptr)
        {
            middleLight->SetEmergencyLight(true, 5.8f);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(true, 1.1f);
        }

        // 中央の廊下に影を出している
        game->RequestAddObject<ShadowMan>(
            [](ShadowMan& shadow)
            {
                shadow.SetPosition(0.0f, -99.0f, -25.0f);
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
            middleLight->SetEmergencyLight(true, 8.2f);
        }
        if (cornerLight != nullptr)
        {
            cornerLight->SetEmergencyLight(false, 4.3f);
        }

        // 3周目だけ、壁の文字の警告どおり、プレイヤーの背後に一度だけ人影を出している（見ると照明の連鎖が始まる）。
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
    // 最後の段階で、廊下の照明を赤い非常灯に戻している
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

// ヒューズを拾った後の影：2本目なら中央のホール、3本目なら細い廊下に出している（見つめると消える）
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
// 台本の演出（照明の連鎖・倉庫の気配・出口の前兆など）の最中や、監視映像を見ている間は鳴らしていない。
// 演出の驚きを物音で打ち消さず、何も起きていない静かな時間にだけ不安を足すためである。
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
// ループ廊下の「ふりかえるな」は、目を離した隙に「ふりかえったな」へ書き換わる。
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
    if (!changedWritingRead)
    {
        return;
    }

    m_AmbientSounds.StartCeilingStepsNow(
        game->IsPowerRestored(), camera->GetPosition(), camera->GetForward());
    game->GetPostProcess()->TriggerHorrorPulse(0.22f, 0.36f);
    Input::SetVibration(5, 0.16f);
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
    // 隠し部屋に閉じ込められている間は、影が出ていなくても小さく心拍が聞こえるようにしている（息は荒くしない）。
    if (m_HiddenRoom.IsTrapped())
    {
        threatRate = (std::max)(threatRate, 0.30f);
    }
    return threatRate;
}

void StageScene::UpdateTensionPulse(const Player& player, float deltaTime)
{
    // 1面には隠れる場所がないため、隠れている扱いにはしていない。
    PlayTensionPulse(m_TensionPulse.Update(deltaTime, ComputeThreatRate(player), false));
}
