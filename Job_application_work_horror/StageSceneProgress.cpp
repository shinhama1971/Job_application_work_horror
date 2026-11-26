// ============================================================================
// ファイルの役割: 1面の毎フレームの進行：時間とヒント、ヒューズを拾ったとき、非常用の充電器、面の出来事、出口の扉、廊下の緊張に合わせた画面効果。
// （StageScene.cpp から分けた。説明は元のファイルとヘッダーを参照）
// ============================================================================

#include "StageScene.h"
#include "Stage1FuseProgress.h"
#include "Stage1CeilingGeometry.h"
#include "MathUtility.h"
#include "Application.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"
#include "Renderer.h"
#include "Player.h"
#include "Wall.h"
#include "Ground.h"
#include "Item.h"
#include "Door.h"
#include "EventFeedback.h"
#include "FuseBox.h"
#include "CeilingLight.h"
#include "ExitTrigger.h"
#include "ShadowMan.h"
#include "SceneLayoutBuilder.h"
#include "PropLayoutFile.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

namespace
{
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;
    // H（LB）でヒントを頼んだときに、進めていない時間をこの秒数まで進め、すぐに強いヒントを出している
    constexpr float RequestedHintSeconds = 35.0f;
    // 知らせの文字を出す秒数（ヒューズを拾ったとき・非常用充電器・監視映像をすべて確かめたとき）
    constexpr float FuseNoticeSeconds = 2.35f;
    constexpr float ChargerNoticeSeconds = 2.8f;
    // 非常用充電器で回復する電池の量
    constexpr float ChargerBattery = 35.0f;
    // 電池がこれより少なくなると、少ないほど画面のノイズと周辺の暗さを強める
    constexpr float LowBatteryThreshold = 25.0f;
    // 電力が戻ってから、画面効果が落ち着いた状態へ移りきるまでの秒数
    constexpr float PowerRestoreBlendSeconds = 2.5f;

    using EventFeedback::FlickerSpec;

    // --- ヒントを頼んだときの短い振動
    constexpr RumbleSpec HintRequestRumble{ 2, 0.04f };
    // --- ヒューズを拾ったときの光と振動（基本＋拾った本数ごとに強める分）
    constexpr float FusePickupBloomBase = 0.48f;
    constexpr float FusePickupBloomPerFuse = 0.12f;
    constexpr float FusePickupBloomSeconds = 0.28f;
    constexpr int FusePickupRumbleFramesBase = 4;
    constexpr int FusePickupRumbleFramesPerFuse = 2;
    constexpr float FusePickupRumbleStrengthBase = 0.10f;
    constexpr float FusePickupRumbleStrengthPerFuse = 0.025f;
    // --- 非常用充電器：物音で呼ぶ影の強さ（ヒューズ2本目と同じ）、揺らす照明の番号、明滅・光・振動
    constexpr int ChargerWatcherFuseCount = 2;
    constexpr FlickerSpec ChargerFlicker{ 0.92f, 0.84f };
    constexpr PulseSpec ChargerBloom{ 0.66f, 0.24f };
    constexpr RumbleSpec ChargerRumble{ 6, 0.14f };

    // --- 画面効果（UpdateScreenAtmosphere）
    // 電力が戻ったときに和らげるノイズと周辺減光の量
    constexpr float PowerRestoredCalm = 0.03f;
    // 廊下の奥へ進むほど強め（この z から、この長さで最大）、廊下の中央から横へ離れるほど弱める（この幅から、この幅で0）
    constexpr float CorridorTensionStartZ = 90.0f;
    constexpr float CorridorTensionDepth = 190.0f;
    constexpr float CorridorTensionHalfWidth = 75.0f;
    constexpr float CorridorTensionWidthFade = 105.0f;
    constexpr float CorridorDepthTension = 0.72f;
    // 周回ごとに足す緊張と上限、電力が戻った後に出口の前だけに残す緊張（この z から、この長さで最大）
    constexpr float LoopTensionPerLoop = 0.14f;
    constexpr float LoopTensionMax = 0.42f;
    constexpr float PoweredExitTensionStartZ = 190.0f;
    constexpr float PoweredExitTensionDepth = 105.0f;
    constexpr float PoweredExitTension = 0.38f;
    // 露出：電力が戻る前（その後は普段の明るさへ移す）、ライトを点けているとき・消しているとき（目が慣れたように明るく）
    constexpr float PowerOffExposure = 1.12f;
    constexpr float PoweredExposure = 1.01f;
    constexpr float FlashlightOnExposure = 1.04f;
    constexpr float FlashlightOffExposure = 1.15f;
    // ノイズと周辺減光（基本＋電池の少なさ・走っている・廊下の緊張で足す分、光の安全地帯で引く分）
    constexpr float NoiseBase = 0.18f;
    constexpr float NoiseLowBattery = 0.18f;
    constexpr float NoiseSprint = 0.04f;
    constexpr float NoiseTension = 0.055f;
    constexpr float NoiseSafeCalm = 0.07f;
    constexpr float VignetteBase = 0.55f;
    constexpr float VignetteLowBattery = 0.20f;
    constexpr float VignetteSprint = 0.06f;
    constexpr float VignetteTension = 0.075f;
    constexpr float VignetteSafeCalm = 0.15f;
    // レンズの歪み・色調・レンズの汚れ：廊下の緊張で足す分（基本の強さは StageScene.h）
    constexpr float DistortionTension = 0.22f;
    constexpr float FilmGradeTension = 0.18f;
    constexpr float LensDirtTension = 0.12f;
}

// 経過時間と進行が止まっている時間を数え、ヒント・光の安全地帯・知らせの残り時間・配置の読み直しを更新している
void StageScene::UpdateTimersAndHints(Player& player, float deltaTime)
{
    // H（LB）が押されたら、すぐに強いヒントを出している
    m_StageVisualTimer += deltaTime;
    m_ProgressHintTimer += deltaTime;
    if (Input::GetKeyTrigger(VK_H) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_ProgressHintTimer = (std::max)(m_ProgressHintTimer, RequestedHintSeconds);
        EventFeedback::Rumble(HintRequestRumble);
    }

    // 光の安全地帯：暖かい灯りの下で、ライトを消して休んでいる間は電池が少しずつ戻る。
    // 落ち着きの度合いは、心拍・物音・画面のざらつきを弱めるのに使っている。
    const float safeLightCharge = m_SafeLights.Update(
        deltaTime, player.GetPosition(), player.IsFlashlightOn(), player.GetBattery());
    if (safeLightCharge > 0.0f)
    {
        player.AddBattery(safeLightCharge);
    }

    // 各知らせの残り時間を減らしている
    m_FuseNoticeTimer = (std::max)(
        0.0f, m_FuseNoticeTimer - deltaTime);
    m_FuseWatcherNoticeTimer = (std::max)(
        0.0f, m_FuseWatcherNoticeTimer - deltaTime);
    m_ChargerNoticeTimer = (std::max)(
        0.0f, m_ChargerNoticeTimer - deltaTime);
    m_LayoutNoticeTimer = (std::max)(0.0f, m_LayoutNoticeTimer - deltaTime);
#if defined(_DEBUG)
    // Debug版だけ：F5 を押したとき、または配置のファイル（assets/layout/stage1_props.txt）が書き換わったとき
    // （Unity のマップエディターで書き出したときなど）に読み直し、ゲームを動かしたまま置き直している。
    // 位置を少し直すたびにビルドし直さずに済むようにするため（Release版では効かない）。
    // 未保存の編集がある間は、自動読み直しを止める。F5 は明示的な破棄として扱う。
    if (Input::GetKeyTrigger(VK_F5) ||
        (!m_PropLayoutEditor.HasUnsavedChanges() && m_LayoutWatcher.HasChanged(Stage1Layout::PropLayoutPath, deltaTime)))
    {
        ReloadPropLayout();
    }
#endif
    m_EvidenceNoticeTimer = (std::max)(
        0.0f, m_EvidenceNoticeTimer - deltaTime);
    m_StorageScareNoticeTimer = (std::max)(
        0.0f, m_StorageScareNoticeTimer - deltaTime);
}

// ヒューズを拾った瞬間、知らせを出し、画面の光と振動で伝え（本数が多いほど強く）、廊下に影を出している。
// 電力が戻ったら、ヒューズの後に出る影を消している
void StageScene::UpdateFusePickup()
{
    Core::Game* game = Core::Game::GetInstance();
    const int currentFuseCount = game->GetItemCount();
    if (currentFuseCount > m_LastFuseCount)
    {
        m_LastFuseCount = currentFuseCount;
        m_ProgressHintTimer = 0.0f;
        m_FuseNoticeTimer = FuseNoticeSeconds;
        const float fuseCount = static_cast<float>(currentFuseCount);
        game->GetPostProcess()->TriggerBloomPulse(
            FusePickupBloomBase + fuseCount * FusePickupBloomPerFuse, FusePickupBloomSeconds);
        Input::SetVibration(
            FusePickupRumbleFramesBase + currentFuseCount * FusePickupRumbleFramesPerFuse,
            FusePickupRumbleStrengthBase + fuseCount * FusePickupRumbleStrengthPerFuse);
        StartFuseWatcher(currentFuseCount);
        StartFuseScare(currentFuseCount);
    }
    UpdateFuseScare(Application::GetDeltaTime());

    if (game->IsPowerRestored())
    {
        ShadowMan* watcher = m_Objects.fuseWatcher;
        if (watcher != nullptr) watcher->SetActive(false);
        m_FuseWatcherState = FuseWatcherState::None;
    }
}

// 非常用充電器を使ったら、電池を回復する代わりに、音で廊下に影を呼んでいる
void StageScene::UpdateEmergencyCharger(Player& player)
{
    FuseBox* emergencyCharger = m_Objects.emergencyCharger;
    if (m_ChargerHandled || emergencyCharger == nullptr || !emergencyCharger->IsActivated())
    {
        return;
    }
    Core::Game* game = Core::Game::GetInstance();
    m_ChargerHandled = true;
    m_ChargerNoticeTimer = ChargerNoticeSeconds;
    player.AddBattery(ChargerBattery);
    game->RegisterChargerUsed();
    // 充電器の物音には、ヒューズ2本目と同じ強さの影を使う。
    StartFuseWatcher(ChargerWatcherFuseCount);

    CeilingLight* chargerLight = m_Objects.CeilingLightAt(Stage1LightNumber::LeftStorage);
    if (chargerLight != nullptr)
    {
        EventFeedback::Flicker(*chargerLight, ChargerFlicker);
    }
    EventFeedback::BloomPulse(*game->GetPostProcess(), ChargerBloom);
    EventFeedback::Rumble(ChargerRumble);
}

// 各演出と仕組みを順に更新している（ループ廊下・入口・倉庫・照明の連鎖・電力の復旧・出口・壁の文字・足跡・
// 隠し部屋・書類保管室の影・無線・西棟・3本目のヒューズ・天井の映り込み・次に進む方向の灯り・心拍）
void StageScene::UpdateStageEvents(Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    UpdateCorridorLoop(player);
    UpdateEntranceThresholdEvent(player);
    UpdateStorageScare(player);
    UpdateScareLightSequence();
    UpdatePowerRestoreSequence();
    UpdateExitPowerSequence();
    UpdateExitOmen(player);
    UpdateWallWritings(player);
    UpdateFootprintFollower(player);
    UpdateCeilingStalker(player, deltaTime);
    UpdateCorridorStretch(player, deltaTime);
    UpdateBreathingWall(deltaTime);
    UpdateStrangeRooms(player, deltaTime);
    UpdateEchoRoom(player, deltaTime);
    // 隠し部屋の影も、書類保管室の影と同じく、棚・壁・閉じた扉を通り抜けずに回り込む
    m_HiddenRoom.Update(player, deltaTime, m_KeypadDoor.IsSolved(),
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
    const bool archiveStalkerLit = UpdateArchiveStalker(player, deltaTime);
    UpdateTransmissions(player, deltaTime, archiveStalkerLit);
    // 西棟の進行（排水バルブの長押しを含む）と、水の滴る音・背後で水の中を歩く音などの物音。
    m_WestWingCues.clear();
    m_WestWing.Update(player, deltaTime, game->GetCamera()->GetForward(),
        m_InteractionSystem.GetFocusedInteractable(), m_WestWingCues);
    for (const AmbientSoundCue& cue : m_WestWingCues)
    {
        game->PlayAudioCueAt(
            cue.Label, cue.Position, cue.Pitch, cue.Volume, cue.MinimumOcclusion);
    }
    // 西棟がヒューズを出した後に、3本目の手に入れ方を整えている（暗証番号の部屋のヒューズを取っていれば、西棟のヒューズを消す）
    UpdateThirdFuseRoutes();
    UpdateCorridorFlood(player, deltaTime);
    UpdateFearEvents(player, deltaTime);
    UpdateCeilingReflection(player, deltaTime);
    UpdateRouteLights();
    UpdateTensionPulse(player, deltaTime);
    UpdateHands(player, deltaTime);
}

// 最後のドア：電力が戻る前は開けて通ると次の周回へ進む（UpdateCorridorLoop）。
// 電力が戻ってから出口の送電が終わるまでは鍵をかけ、送電が終わったら、開けて通り抜けたときに2面への脱出を始めている。
void StageScene::UpdateExitDoor(Player& player)
{
    Door* stageExitDoor = m_Objects.exitDoor;
    if (stageExitDoor == nullptr)
    {
        return;
    }
    ExitTrigger* stageExit = m_Objects.exitTrigger;
    const bool exitPowerReady = m_PowerSequence.IsExitComplete();
    const bool waitingForExitPower = Core::Game::GetInstance()->IsPowerRestored() && !exitPowerReady;
    // 周回のために開けたまま電力を戻した場合は、閉じてから鍵をかけている（送電の前に通れないようにする）
    if (waitingForExitPower && !stageExitDoor->IsLocked())
    {
        stageExitDoor->ResetClosed(0);
    }
    stageExitDoor->SetLocked(waitingForExitPower);
    if (!exitPowerReady || stageExit == nullptr || stageExit->IsEscaping() ||
        !IsThroughEndDoor(player.GetPosition()))
    {
        return;
    }
    // ドアの先の階段を、自動で上らせている（上の踊り場の奥へ向けて歩き、段ごとに目の高さを上げる）。
    // 上り切る頃に、出口の演出が2面へ移す
    if (stageExit->IsStairClimb())
    {
        const Vector3 position = player.GetPosition();
        const Vector3 landing(
            Stage1StairStartX + Stage1StairStepRun * static_cast<float>(Stage1StairStepCount) +
                Player::ExitClimbLandingDepth,
            position.y, Stage1EndDoorZ);
        player.StartStairClimb(Player::MakeExitClimb(position, Vector3::UnitX, Stage1StairStartX,
            Stage1StairStepRun, Stage1StairStepRise, Stage1StairStepCount, landing));
    }
    stageExit->BeginEscape(player);
}

// 出口の表示：送電が終わるまで赤く、終わったら緑で脈打たせている。開始地点の奥のドアの裏側の表示も同じ赤で脈打たせ、
// 画面効果：電池の少なさ・走っているか・ループ廊下の奥への近さで、ノイズと周辺減光を強めている
void StageScene::UpdateScreenAtmosphere(const Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    const bool powerRestored = game->IsPowerRestored();
    const float lowBattery = (std::clamp)((LowBatteryThreshold - player.GetBattery()) / LowBatteryThreshold, 0.0f, 1.0f);
    const float powerBlend = powerRestored
        ? (std::clamp)(
            m_PowerSequence.GetRestoreTimer() / PowerRestoreBlendSeconds, 0.0f, 1.0f)
        : 0.0f;
    const float powerCalm = PowerRestoredCalm * powerBlend;
    const float sprintStress = player.IsSprinting() ? 1.0f : 0.0f;
    const Vector3 playerPosition = player.GetPosition();

    // ループ廊下の奥ほど圧迫感を強め、周回の数に応じて基準の値も上げている。
    // 電力が戻った後は効果を外し、出口の前だけに弱く残して、状況が変わったことを伝えている。
    const float corridorDepth = (std::clamp)(
        (playerPosition.z - CorridorTensionStartZ) / CorridorTensionDepth,
        0.0f,
        1.0f);
    const float corridorWidthMask = MathUtility::Proximity(
        std::abs(playerPosition.x), CorridorTensionHalfWidth, CorridorTensionWidthFade);
    const float loopTension = (std::min)(
        static_cast<float>(m_CorridorLoopCount) * LoopTensionPerLoop,
        LoopTensionMax);
    const float poweredExitDepth = (std::clamp)(
        (playerPosition.z - PoweredExitTensionStartZ) / PoweredExitTensionDepth,
        0.0f,
        1.0f);
    const float corridorTension = powerRestored
        ? poweredExitDepth * PoweredExitTension
        : (std::clamp)(
            corridorDepth * corridorWidthMask * CorridorDepthTension + loopTension,
            0.0f,
            1.0f);
    game->GetPostProcess()->SetCorridorTension(corridorTension);

    // ホラーらしい暗さを保ちつつ、懐中電灯なしでも時間とともに目が慣れ、
    // 最低限動ける明るさにしている。電力が戻ったら、普段の明るさへ移していく
    const float targetExposure = powerRestored
        ? PowerOffExposure + (PoweredExposure - PowerOffExposure) * powerBlend
        : (player.IsFlashlightOn() ? FlashlightOnExposure : FlashlightOffExposure);
    game->GetPostProcess()->SetExposure(targetExposure);
    // 暖かい灯りの下（光の安全地帯）では、ざらつきと周辺の暗さを和らげ、ほっとできる画面にしている
    const float safeCalm = m_SafeLights.GetCalm();
    const float noiseAmount =
        NoiseBase - powerCalm + lowBattery * NoiseLowBattery +
        sprintStress * NoiseSprint + corridorTension * NoiseTension - safeCalm * NoiseSafeCalm;
    const float vignetteStrength =
        VignetteBase - powerCalm + lowBattery * VignetteLowBattery +
        sprintStress * VignetteSprint + corridorTension * VignetteTension - safeCalm * VignetteSafeCalm;

    game->GetPostProcess()->SetAtmosphere(noiseAmount, vignetteStrength);
    game->GetPostProcess()->SetLensDistortionStrength(DistortionBase + corridorTension * DistortionTension);
    game->GetPostProcess()->SetFilmGradeStrength(FilmGradeBase + corridorTension * FilmGradeTension);
    game->GetPostProcess()->SetLensDirtStrength(LensDirtBase + corridorTension * LensDirtTension);
}
