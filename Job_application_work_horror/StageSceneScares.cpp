// ============================================================================
// ファイルの役割: 1面の驚かせる演出（入口・左の倉庫・照明の連鎖・ヒューズを拾った瞬間・ヒューズの後の影・天井裏の足音・影のまわりのゆがみ・書類保管室の影・足跡）を担当している。
// 主な技術: 有限状態機械、懐中電灯の円錐での照らし判定、画面上の位置へのゆがみ、照明・音・画面効果のタイミング合わせ
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
void StageScene::UpdateStrangeRooms(const Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    const Vector3 position = player.GetPosition();
    ShadowMan* figure = m_Objects.timeStopFigure;

    m_TimeStopCues.clear();
    // 人影を出してよいか：時間が止まっている間は、ほかの演出を止めている（IsAmbientSoundAllowed が false になる）ので、そのときは出してよいことにしている
    m_TimeStopRoom.Update(deltaTime, position, m_CorridorLoopCount, IsAmbientSoundAllowed() || m_TimeStopRoom.IsActive(),
        m_TimeStopCues);
    for (const Stage1TimeStopRoom::Cue& cue : m_TimeStopCues)
    {
        switch (cue.Type)
        {
        case Stage1TimeStopRoom::CueType::Freeze:
            game->PlayAudioCue(SOUND_CUE_POWER, TimeStopFreezeSound.Pitch, TimeStopFreezeSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), TimeStopFreezePulse);
            break;
        case Stage1TimeStopRoom::CueType::Resume:
            game->PlayAudioCue(SOUND_CUE_POWER, TimeStopResumeSound.Pitch, TimeStopResumeSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), TimeStopResumePulse);
            // 部屋を出たら、人影も止まった世界と一緒に消える
            if (figure != nullptr)
            {
                figure->SetActive(false);
            }
            break;
        case Stage1TimeStopRoom::CueType::SpawnFigure:
            if (figure != nullptr)
            {
                figure->SetPosition(cue.Position.x, cue.Position.y, cue.Position.z);
                figure->SetActive(true);
                figure->EnableGazeScare(TimeStopFigureLifetime);
                figure->EnableChase(TimeStopFigureSpeed, TimeStopFigureStopDistance);
                // 照らして崩したら、止まっていた時間が一気に戻る
                figure->SetOnObserved([this]()
                {
                    BreakTimeStop();
                });
            }
            break;
        }
    }

    // 人影に触れられたら、驚かせて、時間を一気に戻している
    if (figure != nullptr && figure->IsActive())
    {
        const Vector3 toFigure = figure->GetPosition() - position;
        if (Vector2(toFigure.x, toFigure.z).Length() <= TimeStopFigureCatchDistance)
        {
            figure->SetActive(false);
            game->PlayAudioCue(SOUND_CUE_SCARE, TimeStopCaughtSound.Pitch, TimeStopCaughtSound.Volume);
            EventFeedback::HorrorPulse(*game->GetPostProcess(), TimeStopCaughtPulse);
            EventFeedback::Rumble(TimeStopCaughtRumble);
            BreakTimeStop();
        }
    }
    Application::SetWorldTimeScale(m_TimeStopRoom.GetWorldTimeScale());

    // 傾く部屋：奥へ進むほど視界が傾き、暖かい灯りの下ではまっすぐに戻る
    m_TiltRoom.Update(deltaTime, position, m_CorridorLoopCount, m_SafeLights.GetCalm());
    game->GetCamera()->SetViewRoll(m_TiltRoom.GetRoll());
}

void StageScene::BreakTimeStop()
{
    if (!m_TimeStopRoom.IsActive())
    {
        return;
    }
    m_TimeStopRoom.BreakSpell();
    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCue(SOUND_CUE_POWER, TimeStopResumeSound.Pitch, TimeStopResumeSound.Volume);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), TimeStopResumePulse);
}

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
