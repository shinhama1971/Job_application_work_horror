// ============================================================================
// ファイルの役割: 2面の周回進行、周回に伴う廊下の変化、時計と偽の扉。
// ============================================================================

#include "Stage2Scene.h"
#include "MathUtility.h"
#include "BatteryItem.h"
#include "Camera.h"
#include "CeilingLight.h"
#include "Door.h"
#include "EventFeedback.h"
#include "FuseBox.h"
#include "Game.h"
#include "Ground.h"
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
    constexpr int PuddleFigureBoldFromLoop = 2;

    // --- 時計の異変：近くで正面から見たか
    const Vector3 ClockCenter(-38.0f, -70.0f, -25.0f);
    constexpr float ClockLookDistance = 98.0f;
    constexpr float ClockLookFacing = 0.91f;

    // ------------------------------------------------------------------------
    // 演出の強さと長さ（照明の明滅・画面の脈打ち・振動・音）
    // ------------------------------------------------------------------------
    using EventFeedback::FlickerSpec;
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;

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

    // --- 偽の扉の部品の配置：扉の中心の高さ、枠の左右の間隔の半分と上の枠の高さ、取っ手の扉の中心からのずれ、現れる・消える秒数
    constexpr float FalseDoorCenterY = -76.0f;
    constexpr float FalseDoorFrameHalfWidth = 12.0f;
    constexpr float FalseDoorFrameTopY = -54.0f;
    constexpr float FalseDoorHandleOffsetZ = 8.0f;
    constexpr float FalseDoorDissolveInSeconds = 1.6f;
    constexpr float FalseDoorDissolveOutSeconds = 1.2f;
}

// 周回を進めている：状態を戻し、プレイヤーを入口へ戻し、扉を閉め、その周回の異変を出している
void Stage2Scene::AdvanceLoop(Player& player)
{
    m_QuietRecovery.Reset();
    ++m_LoopCount;
    ResetForNextLoop(player);
    ConfigureClockForLoop();
    PlayLoopStartFeedback();
    UpdateCycleMarks();

    // 周回ごとに廊下を変えている
    if (m_LoopCount == 1)
    {
        ApplyFirstLoopChanges();
    }
    else if (m_LoopCount == Stage2SecondLoop)
    {
        ApplySecondLoopChanges();
    }
    else
    {
        ApplyFinalLoopChanges();
    }
}

// 周回の進み具合・異変・脅威・扉・照明を、新しい周回の始めの状態に戻している
void Stage2Scene::ResetForNextLoop(Player& player)
{
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
    m_PuddleAnomaly.SetBoldness(m_LoopCount >= PuddleFigureBoldFromLoop ? 1.0f : 0.0f);
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
}

// 周回が切り替わった瞬間の画面と振動の演出（周回を重ねるほど強い）
void Stage2Scene::PlayLoopStartFeedback()
{
    Core::Game* game = Core::Game::GetInstance();
    const float loop = static_cast<float>(m_LoopCount);
    game->GetPostProcess()->TriggerHorrorPulse(
        LoopStartPulseBase.Strength + loop * LoopStartPulseStep.Strength,
        LoopStartPulseBase.Seconds + loop * LoopStartPulseStep.Seconds);
    game->GetPostProcess()->TriggerBloomPulse(
        LoopStartBloomBase + loop * LoopStartBloomStep,
        LoopStartBloomSeconds);
    Input::SetVibration(LoopStartRumbleBaseFrames + m_LoopCount * LoopStartRumbleFramesPerLoop, LoopStartRumbleStrength);
}

// 周回の数だけ、入口の右の壁に傷の印を出している
void Stage2Scene::UpdateCycleMarks()
{
    for (int markIndex = 0; markIndex < Stage2Objects::CycleMarkCount; ++markIndex)
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
}

// 1周目：中央の照明を故障させ、傷を3本出し、電池と周回の印を出している
void Stage2Scene::ApplyFirstLoopChanges()
{
    Wall* loopMark = m_Objects.loopMark;
    CeilingLight* light2 = m_Objects.Light(Stage2Light::Light2);
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
void Stage2Scene::ApplySecondLoopChanges()
{
    Wall* portrait = m_Objects.portrait;
    CeilingLight* light3 = m_Objects.Light(Stage2Light::Light3);
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

// 最後の周：偽の扉を消し、残りの傷を出し、信号盤を直すまで最後の停電と追跡を止めている
void Stage2Scene::ApplyFinalLoopChanges()
{
    Core::Game* game = Core::Game::GetInstance();
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
    if (distance > Stage2MinimumDirectionLength)
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
