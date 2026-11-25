// ============================================================================
// ファイルの役割: 2面の異変（偽の扉・壁の傷・血の手形・鏡・水たまり・肖像画・壁の向こうのノック・見えない影）を担当している。
// 主な技術: 視線と懐中電灯の判定、立体音響で聞き当てる判定、見つけた異変の進み具合
// 値（場所・秒数・強さ）は Stage2HorrorSettings.h にまとめている。
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
#include "Stage2HorrorSettings.h"
using namespace Stage2HorrorSettings;

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
    const bool lit = IsUnseenPresenceLit(player, enabled);
    const UnseenPresenceAnomaly::Result result = m_UnseenPresence.Update(
        deltaTime, player.GetPosition(), player.IsMovingHorizontally(), player.IsHiding(), enabled, lit);
    UpdateUnseenFootsteps(enabled, result);
    UpdateUnseenShadow(enabled, result, deltaTime);

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
    if (result == UnseenPresenceAnomaly::Result::Passed)
    {
        OnUnseenPresencePassed();
    }
}

// 懐中電灯の光が、見えない影の胸のあたりに当たっているか（光の円錐の中心付近で、75より近い）
bool Stage2Scene::IsUnseenPresenceLit(const Player& player, bool enabled) const
{
    if (!enabled || !m_UnseenPresence.IsActive() || !player.IsFlashlightOn())
    {
        return false;
    }
    const Camera* camera = Core::Game::GetInstance()->GetCamera();
    const Vector3 toChest = m_UnseenPresence.GetPosition() + Vector3(0.0f, UnseenChestHeight, 0.0f) - camera->GetPosition();
    const float distance = toChest.Length();
    return distance > MinimumDirectionLength && distance < UnseenLitDistance &&
        camera->GetForward().Dot(toChest / distance) > UnseenLitFacing;
}

// 見えない影の足もと：水たまりの中では波紋と水音、水から出た直後は濡れた足跡を残している
void Stage2Scene::UpdateUnseenFootsteps(bool enabled, UnseenPresenceAnomaly::Result result)
{
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
}

// 姿の見えない影の「影だけ」を、壁と床に落としている（天井の照明・揺れる裸電球・懐中電灯の光を、人の形で遮る。
// shader/figureShadow.hlsli）。捕まえた直後に奥へ戻る瞬間移動が目立たないよう、そのフレームは消し、0.4秒で上げ下げしている
void Stage2Scene::UpdateUnseenShadow(bool enabled, UnseenPresenceAnomaly::Result result, float deltaTime)
{
    const bool shadowShown = enabled && m_UnseenPresence.IsActive() && result == UnseenPresenceAnomaly::Result::None;
    m_UnseenShadowVisibility = result == UnseenPresenceAnomaly::Result::Caught ? 0.0f
        : shadowShown ? (std::min)(1.0f, m_UnseenShadowVisibility + deltaTime / UnseenShadowFadeSeconds)
        : (std::max)(0.0f, m_UnseenShadowVisibility - deltaTime / UnseenShadowFadeSeconds);
    Vector3 shadowFoot = m_UnseenPresence.GetPosition();
    shadowFoot.y = PuddleReflectionAnomaly::FloorY;
    Renderer::SetUnseenFigure(shadowFoot, m_UnseenShadowVisibility);
}

// すれ違えた：通り過ぎた先の照明が一度明滅し、吊るした裸電球が大きく揺れて、雑音が遠ざかっていく
void Stage2Scene::OnUnseenPresencePassed()
{
    Core::Game* game = Core::Game::GetInstance();
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
