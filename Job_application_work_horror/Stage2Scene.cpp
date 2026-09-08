// ============================================================================
// ファイルの役割: 2面のループ廊下、謎解き、段階的な異変とクリア条件を管理します。
// ============================================================================

#include "Stage2Scene.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
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


Stage2Scene::Stage2Scene()
{
    Init();
}

Stage2Scene::~Stage2Scene()
{
    Uninit();
}

// ループ廊下の基本形と、周回によって表示を切り替える異変Objectを準備します。
void Stage2Scene::Init()
{
    Core::Game* game = Core::Game::GetInstance();
    game->SetPowerRestored(true);
    game->GetPostProcess()->SetCorridorTension(0.38f);
    game->GetPostProcess()->SetExposure(1.02f);
    game->GetPostProcess()->SetAtmosphere(0.20f, 0.62f);
    game->GetPostProcess()->SetLensDistortionStrength(0.20f);
    game->GetPostProcess()->SetFilmGradeStrength(0.58f);
    game->GetPostProcess()->SetSignalInterference(0.0f);
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetVolumetricIntensity(0.38f);

    m_LoopCount = 0;
    m_LoopCooldown = 0.0f;
    m_NoticeTimer = 2.8f;
    m_VisualTimer = 0.0f;
    m_ObservedScareTimer = -1.0f;
    m_GazeNoticeTimer = 0.0f;
    m_FinalSequenceTimer = -1.0f;
    m_ScratchNoticeTimer = 0.0f;
    m_PortraitNoticeTimer = 0.0f;
    m_FalseDoorNoticeTimer = 0.0f;
    m_ClockHourAngle = 0.42f;
    m_ClockMinuteAngle = -0.78f;
    m_ClockNoticeTimer = 0.0f;
    m_PuzzleFeedbackTimer = 0.0f;
    m_NoiseThreat = 0.0f;
    m_NoiseEventCooldown = 0.0f;
    m_NoiseWarningTimer = 0.0f;
    m_WetStepNoticeTimer = 0.0f;
    m_NoiseStalkerCooldown = 0.0f;
    m_NoiseStalkerNoticeTimer = 0.0f;
    m_ChargerNoticeTimer = 0.0f;
    m_EvidenceNoticeTimer = 0.0f;
    m_SignalNoticeTimer = 0.0f;
    m_LoopBlinkTimer = 0.0f;
    m_LoopTransitionTimer = -1.0f;
    m_FinalPursuitTimer = 0.0f;
    m_PursuitPulseTimer = 0.0f;
    m_PursuitGazePenaltyTimer = 0.0f;
    m_CaughtTimer = -1.0f;
    m_ProgressHintTimer = 0.0f;
    m_GuidancePulseCooldown = 0.0f;
    m_ScratchUpdateAccumulator = 0.0f;
    m_ObservedScarePhase = 0;
    m_FinalSequencePhase = 0;
    m_LightZoneMask = 0;
    m_ScratchScareTriggered = false;
    m_PortraitObserved = false;
    m_PortraitChangedThisLoop = false;
    m_FalseDoorObserved = false;
    m_FalseDoorMoved = false;
    m_ClockObservedThisLoop = false;
    m_ConfirmationHandledThisLoop = false;
    m_ChargerHandled = false;
    m_EvidenceHandled[0] = false;
    m_EvidenceHandled[1] = false;
    m_SignalAccepted[0] = false;
    m_SignalAccepted[1] = false;
    m_SignalAccepted[2] = false;
    m_SignalStep = 0;
    m_SignalPuzzleComplete = false;
    m_PuzzleFeedbackType = 0;
    m_PuzzleMistakeCount = 0;
    m_FinalSequenceArmed = false;
    m_FinalDoorReady = false;
    m_NoiseCatch = false;
    m_DebugCommand = 0;

    Player* player = game->CreateObj<Player>("Player");
    player->SetPosition(Vector3(0.0f, -99.0f, -125.0f));

    ShadowMan* stageShadow = game->CreateObj<ShadowMan>("Stage2Shadow");
    stageShadow->SetPosition(0.0f, -99.0f, 62.0f);
    stageShadow->SetDeactivateOnExpire(true);
    stageShadow->SetActive(false);

    // 謎解き用の影と分け、足音だけに反応する追跡者を独立して管理します。
    ShadowMan* noiseShadow = game->CreateObj<ShadowMan>("Stage2NoiseShadow");
    noiseShadow->SetPosition(0.0f, -99.0f, -145.0f);
    noiseShadow->SetDeactivateOnExpire(true);
    noiseShadow->SetActive(false);

    FuseBox* confirmationPanel =
        game->CreateObj<FuseBox>("Stage2ConfirmationPanel");
    confirmationPanel->SetManualControl("異常確認スイッチを押す");
    confirmationPanel->SetManualInteractionAllowed(false);
    confirmationPanel->SetPosition(35.5f, -90.0f, 112.0f);
    confirmationPanel->SetRotation(Vector3(0.0f, -1.5707963f, 0.0f));

    FuseBox* emergencyCharger =
        game->CreateObj<FuseBox>("Stage2EmergencyCharger");
    emergencyCharger->SetManualControl("非常用充電器を使う");
    emergencyCharger->SetManualInteractionAllowed(true);
    emergencyCharger->SetPosition(-35.5f, -90.0f, -106.0f);
    emergencyCharger->SetRotation(Vector3(0.0f, 1.5707963f, 0.0f));

    FuseBox* evidenceTerminal1 =
        game->CreateObj<FuseBox>("Stage2EvidenceTerminal1");
    evidenceTerminal1->SetManualControl("残された記録を回収する");
    evidenceTerminal1->SetManualInteractionAllowed(true);
    evidenceTerminal1->SetPosition(35.5f, -90.0f, -76.0f);
    evidenceTerminal1->SetRotation(Vector3(0.0f, -1.5707963f, 0.0f));

    FuseBox* evidenceTerminal2 =
        game->CreateObj<FuseBox>("Stage2EvidenceTerminal2");
    evidenceTerminal2->SetManualControl("残された記録を回収する");
    evidenceTerminal2->SetManualInteractionAllowed(true);
    evidenceTerminal2->SetPosition(-35.5f, -90.0f, 108.0f);
    evidenceTerminal2->SetRotation(Vector3(0.0f, 1.5707963f, 0.0f));

    constexpr const char* signalTerminalNames[] =
    {
        "Stage2SignalTerminalBlue",
        "Stage2SignalTerminalAmber",
        "Stage2SignalTerminalRed"
    };
    constexpr const char* signalPrompts[] =
    {
        "青い信号盤を操作する",
        "黄色い信号盤を操作する",
        "赤い信号盤を操作する"
    };
    const Vector3 signalPositions[] =
    {
        Vector3(35.5f, -90.0f, 82.0f),
        Vector3(-35.5f, -90.0f, 18.0f),
        Vector3(35.5f, -90.0f, -108.0f)
    };
    for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
    {
        FuseBox* signalTerminal =
            game->CreateObj<FuseBox>(signalTerminalNames[signalIndex]);
        signalTerminal->SetManualControl(signalPrompts[signalIndex]);
        signalTerminal->SetManualInteractionAllowed(false);
        signalTerminal->SetPosition(
            signalPositions[signalIndex].x,
            signalPositions[signalIndex].y,
            signalPositions[signalIndex].z);
        signalTerminal->SetRotation(Vector3(
            0.0f,
            signalIndex == 1 ? 1.5707963f : -1.5707963f,
            0.0f));
    }

    const auto createWall = [game](
        const char* name,
        const Vector3& position,
        const Vector3& scale,
        const Color& diffuse,
        bool collision)
    {
        Wall* wall = game->CreateObj<Wall>(name);
        wall->SetPosition(position.x, position.y, position.z);
        wall->SetScale(scale.x, scale.y, scale.z);
        wall->SetAppearance(diffuse, Color(0.0f, 0.0f, 0.0f, 1.0f), 7.0f);
        wall->SetCollisionEnabled(collision);
        return wall;
    };

    const Color wallColor(0.17f, 0.16f, 0.145f, 1.0f);
    const Color trimColor(0.075f, 0.068f, 0.06f, 1.0f);
    const Color floorColor(0.105f, 0.085f, 0.065f, 1.0f);

    createWall("Stage2WallLeft", Vector3(-42.0f, -74.0f, -10.0f),
        Vector3(4.0f, 50.0f, 300.0f), wallColor, true);
    createWall("Stage2WallRight", Vector3(42.0f, -74.0f, -10.0f),
        Vector3(4.0f, 50.0f, 300.0f), wallColor, true);
    createWall("Stage2WallBack", Vector3(0.0f, -74.0f, -160.0f),
        Vector3(88.0f, 50.0f, 4.0f), wallColor, true);
    createWall("Stage2WallFrontLeft", Vector3(-28.5f, -74.0f, 140.0f),
        Vector3(27.0f, 50.0f, 4.0f), wallColor, true);
    createWall("Stage2WallFrontRight", Vector3(28.5f, -74.0f, 140.0f),
        Vector3(27.0f, 50.0f, 4.0f), wallColor, true);

    Wall* floor = createWall("Stage2Floor", Vector3(0.0f, -100.5f, -10.0f),
        Vector3(84.0f, 2.0f, 300.0f), floorColor, false);
    floor->SetCastsShadow(false);

    // 水たまりは安全な近道と静かな迂回路の間に置き、走り抜けるほど敵へ音が伝わります。
    struct CorridorPuddle { const char* Name; float X; float Z; float Width; float Depth; };
    const CorridorPuddle corridorPuddles[] =
    {
        { "Stage2Puddle1", -9.0f, -82.0f, 40.0f, 22.0f },
        { "Stage2Puddle2", 10.0f, 12.0f, 38.0f, 25.0f },
        { "Stage2Puddle3", -7.0f, 92.0f, 44.0f, 20.0f }
    };
    for (const CorridorPuddle& puddle : corridorPuddles)
    {
        Wall* surface = createWall(
            puddle.Name,
            Vector3(puddle.X, -99.32f, puddle.Z),
            Vector3(puddle.Width, 0.12f, puddle.Depth),
            Color(0.025f, 0.052f, 0.060f, 0.72f), false);
        surface->SetAppearance(
            Color(0.025f, 0.052f, 0.060f, 0.72f),
            Color(0.045f, 0.095f, 0.11f, 1.0f), 92.0f);
        surface->SetCastsShadow(false);
    }
    Wall* ceiling = createWall("Stage2Ceiling", Vector3(0.0f, -47.0f, -10.0f),
        Vector3(84.0f, 3.0f, 300.0f), trimColor, false);
    ceiling->SetCastsShadow(false);

    createWall("Stage2TrimLeft", Vector3(-39.4f, -96.0f, -10.0f),
        Vector3(1.0f, 6.0f, 292.0f), trimColor, false);
    createWall("Stage2TrimRight", Vector3(39.4f, -96.0f, -10.0f),
        Vector3(1.0f, 6.0f, 292.0f), trimColor, false);

    Wall* pipeLeft = createWall("Stage2PipeLeft", Vector3(-34.0f, -53.0f, -10.0f),
        Vector3(3.0f, 3.0f, 282.0f), trimColor, false);
    pipeLeft->SetCastsShadow(false);
    Wall* pipeRight = createWall("Stage2PipeRight", Vector3(34.0f, -53.0f, -10.0f),
        Vector3(3.0f, 3.0f, 282.0f), trimColor, false);
    pipeRight->SetCastsShadow(false);

    Wall* evidenceMarker1 = createWall(
        "Stage2EvidenceMarker1", Vector3(39.4f, -65.0f, -76.0f),
        Vector3(1.0f, 5.0f, 18.0f),
        Color(0.025f, 0.11f, 0.09f, 1.0f), false);
    evidenceMarker1->SetAppearance(
        Color(0.025f, 0.11f, 0.09f, 1.0f),
        Color(0.02f, 0.24f, 0.16f, 1.0f), 36.0f);
    evidenceMarker1->SetCastsShadow(false);
    Wall* evidenceMarker2 = createWall(
        "Stage2EvidenceMarker2", Vector3(-39.4f, -65.0f, 108.0f),
        Vector3(1.0f, 5.0f, 18.0f),
        Color(0.025f, 0.11f, 0.09f, 1.0f), false);
    evidenceMarker2->SetAppearance(
        Color(0.025f, 0.11f, 0.09f, 1.0f),
        Color(0.02f, 0.24f, 0.16f, 1.0f), 36.0f);
    evidenceMarker2->SetCastsShadow(false);

    const char* signalMarkerNames[] =
    {
        "Stage2SignalMarkerBlue",
        "Stage2SignalMarkerAmber",
        "Stage2SignalMarkerRed"
    };
    const Color signalDiffuse[] =
    {
        Color(0.015f, 0.055f, 0.13f, 1.0f),
        Color(0.13f, 0.075f, 0.012f, 1.0f),
        Color(0.13f, 0.018f, 0.012f, 1.0f)
    };
    const Color signalEmission[] =
    {
        Color(0.015f, 0.18f, 0.48f, 1.0f),
        Color(0.40f, 0.17f, 0.008f, 1.0f),
        Color(0.42f, 0.018f, 0.008f, 1.0f)
    };
    for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
    {
        const bool leftWall = signalIndex == 1;
        Wall* marker = createWall(
            signalMarkerNames[signalIndex],
            Vector3(leftWall ? -39.4f : 39.4f, -72.0f,
                signalPositions[signalIndex].z),
            Vector3(1.0f, 17.0f, 18.0f),
            signalDiffuse[signalIndex], false);
        marker->SetAppearance(
            signalDiffuse[signalIndex], signalEmission[signalIndex], 48.0f);
        marker->SetSignalSurface(true);
        marker->SetCastsShadow(false);
    }

    Wall* portrait = createWall("Stage2Portrait", Vector3(39.4f, -70.0f, -25.0f),
        Vector3(1.0f, 22.0f, 16.0f), Color(0.055f, 0.042f, 0.034f, 1.0f), false);
    portrait->SetCastsShadow(false);
    Wall* portraitEyeLeft = createWall(
        "Stage2PortraitEyeLeft",
        Vector3(38.72f, -67.5f, -28.5f),
        Vector3(0.3f, 2.2f, 1.8f),
        Color(0.14f, 0.012f, 0.004f, 1.0f),
        false);
    portraitEyeLeft->SetCastsShadow(false);
    portraitEyeLeft->SetVisible(false);
    Wall* portraitEyeRight = createWall(
        "Stage2PortraitEyeRight",
        Vector3(38.72f, -67.5f, -21.5f),
        Vector3(0.3f, 2.2f, 1.8f),
        Color(0.14f, 0.012f, 0.004f, 1.0f),
        false);
    portraitEyeRight->SetCastsShadow(false);
    portraitEyeRight->SetVisible(false);

    Wall* clockFace = createWall(
        "Stage2ClockFace", Vector3(-39.3f, -70.0f, -25.0f),
        Vector3(1.0f, 20.0f, 20.0f),
        Color(0.095f, 0.086f, 0.070f, 1.0f), false);
    Wall* clockFrameTop = createWall(
        "Stage2ClockFrameTop", Vector3(-38.9f, -59.0f, -25.0f),
        Vector3(1.8f, 2.0f, 24.0f), trimColor, false);
    Wall* clockFrameBottom = createWall(
        "Stage2ClockFrameBottom", Vector3(-38.9f, -81.0f, -25.0f),
        Vector3(1.8f, 2.0f, 24.0f), trimColor, false);
    Wall* clockFrameNear = createWall(
        "Stage2ClockFrameNear", Vector3(-38.9f, -70.0f, -36.0f),
        Vector3(1.8f, 24.0f, 2.0f), trimColor, false);
    Wall* clockFrameFar = createWall(
        "Stage2ClockFrameFar", Vector3(-38.9f, -70.0f, -14.0f),
        Vector3(1.8f, 24.0f, 2.0f), trimColor, false);
    Wall* clockHourHand = createWall(
        "Stage2ClockHourHand", Vector3(-38.0f, -70.0f, -25.0f),
        Vector3(1.6f, 9.0f, 1.4f),
        Color(0.025f, 0.021f, 0.017f, 1.0f), false);
    Wall* clockMinuteHand = createWall(
        "Stage2ClockMinuteHand", Vector3(-37.7f, -70.0f, -25.0f),
        Vector3(1.4f, 14.0f, 1.0f),
        Color(0.035f, 0.028f, 0.020f, 1.0f), false);
    Wall* clockPieces[] =
    {
        clockFace, clockFrameTop, clockFrameBottom,
        clockFrameNear, clockFrameFar, clockHourHand, clockMinuteHand
    };
    for (Wall* piece : clockPieces)
    {
        piece->SetCastsShadow(false);
    }
    clockHourHand->SetRotation(Vector3(m_ClockHourAngle, 0.0f, 0.0f));
    clockMinuteHand->SetRotation(Vector3(m_ClockMinuteAngle, 0.0f, 0.0f));

    Wall* loopMark = createWall("Stage2LoopMark", Vector3(-39.4f, -68.0f, 52.0f),
        Vector3(1.0f, 18.0f, 12.0f), Color(0.22f, 0.01f, 0.006f, 1.0f), false);
    loopMark->SetCastsShadow(false);
    loopMark->SetVisible(false);

    const char* cycleMarkNames[] =
    {
        "Stage2CycleMark1",
        "Stage2CycleMark2",
        "Stage2CycleMark3"
    };
    constexpr float cycleMarkRotations[] =
    {
        0.22f, -0.18f, 0.12f
    };
    for (int markIndex = 0; markIndex < 3; ++markIndex)
    {
        Wall* cycleMark = createWall(
            cycleMarkNames[markIndex],
            Vector3(
                39.45f,
                -69.0f,
                -112.0f + static_cast<float>(markIndex) * 8.0f),
            Vector3(1.0f, 20.0f, 2.0f),
            Color(0.18f, 0.004f, 0.002f, 1.0f),
            false);
        cycleMark->SetRotation(
            Vector3(cycleMarkRotations[markIndex], 0.0f, 0.0f));
        cycleMark->SetCastsShadow(false);
        cycleMark->SetVisible(false);
    }

    Wall* doorIndicator = createWall("Stage2DoorIndicator",
        Vector3(22.0f, -67.0f, 137.4f), Vector3(10.0f, 5.0f, 1.0f),
        Color(0.24f, 0.012f, 0.008f, 1.0f), false);
    doorIndicator->SetCastsShadow(false);
    doorIndicator->SetAppearance(
        Color(0.24f, 0.012f, 0.008f, 1.0f),
        Color(0.18f, 0.001f, 0.0f, 1.0f), 24.0f);

    Wall* falseDoorPanel = createWall(
        "Stage2FalseDoorPanel", Vector3(-39.3f, -76.0f, 70.0f),
        Vector3(1.2f, 40.0f, 22.0f),
        Color(0.07f, 0.025f, 0.018f, 1.0f), false);
    Wall* falseDoorFrameNear = createWall(
        "Stage2FalseDoorFrameNear", Vector3(-39.0f, -76.0f, 58.0f),
        Vector3(1.8f, 44.0f, 2.0f), trimColor, false);
    Wall* falseDoorFrameFar = createWall(
        "Stage2FalseDoorFrameFar", Vector3(-39.0f, -76.0f, 82.0f),
        Vector3(1.8f, 44.0f, 2.0f), trimColor, false);
    Wall* falseDoorFrameTop = createWall(
        "Stage2FalseDoorFrameTop", Vector3(-39.0f, -54.0f, 70.0f),
        Vector3(1.8f, 2.0f, 26.0f), trimColor, false);
    Wall* falseDoorHandle = createWall(
        "Stage2FalseDoorHandle", Vector3(-38.3f, -76.0f, 62.0f),
        Vector3(2.0f, 5.0f, 2.0f),
        Color(0.16f, 0.09f, 0.025f, 1.0f), false);
    Wall* falseDoorPieces[] =
    {
        falseDoorPanel, falseDoorFrameNear, falseDoorFrameFar,
        falseDoorFrameTop, falseDoorHandle
    };
    for (Wall* piece : falseDoorPieces)
    {
        piece->SetCastsShadow(false);
        piece->SetVisible(false);
    }

    struct ScratchPiece
    {
        Vector3 Position;
        Vector3 Scale;
    };
    const ScratchPiece scratchPieces[] =
    {
        { Vector3(39.45f, -71.0f,   1.0f), Vector3(1.0f, 18.0f, 2.0f) },
        { Vector3(39.45f, -71.0f,   9.0f), Vector3(1.0f, 18.0f, 2.0f) },
        { Vector3(39.45f, -71.0f,   5.0f), Vector3(1.0f,  2.0f, 10.0f) },
        { Vector3(39.45f, -71.0f,  20.0f), Vector3(1.0f, 18.0f, 2.0f) },
        { Vector3(39.45f, -63.0f,  25.0f), Vector3(1.0f,  2.0f, 10.0f) },
        { Vector3(39.45f, -71.0f,  25.0f), Vector3(1.0f,  2.0f, 10.0f) },
        { Vector3(39.45f, -79.0f,  25.0f), Vector3(1.0f,  2.0f, 10.0f) },
        { Vector3(39.45f, -71.0f,  36.0f), Vector3(1.0f, 18.0f, 2.0f) },
        { Vector3(39.45f, -79.0f,  41.0f), Vector3(1.0f,  2.0f, 12.0f) },
        { Vector3(39.45f, -71.0f,  52.0f), Vector3(1.0f, 18.0f, 2.0f) },
        { Vector3(39.45f, -63.0f,  57.0f), Vector3(1.0f,  2.0f, 10.0f) },
        { Vector3(39.45f, -71.0f,  57.0f), Vector3(1.0f,  2.0f, 10.0f) },
        { Vector3(39.45f, -67.0f,  62.0f), Vector3(1.0f, 10.0f, 2.0f) }
    };
    for (int index = 0; index < Stage2ScratchCount; ++index)
    {
        Wall* scratch = createWall(
            Stage2ScratchNames[index],
            scratchPieces[index].Position,
            scratchPieces[index].Scale,
            Color(0.16f, 0.006f, 0.003f, 1.0f),
            false);
        scratch->SetCastsShadow(false);
        scratch->SetVisible(false);
    }

    Wall* batteryShelf = createWall(
        "Stage2BatteryShelf",
        Vector3(35.8f, -87.0f, -45.0f),
        Vector3(8.0f, 2.0f, 10.0f),
        Color(0.07f, 0.055f, 0.045f, 1.0f),
        false);
    batteryShelf->SetCastsShadow(false);
    BatteryItem* battery = game->CreateObj<BatteryItem>("Stage2Battery");
    battery->SetPosition(35.0f, -82.5f, -45.0f);
    battery->SetActive(false);

    CeilingLight* light1 = game->CreateObj<CeilingLight>("Stage2Light1");
    light1->SetPosition(0.0f, -50.5f, -112.0f);
    light1->SetScale(18.0f, 2.0f, 8.0f);
    light1->SetEmergencyLight(false, 0.2f);
    CeilingLight* light2 = game->CreateObj<CeilingLight>("Stage2Light2");
    light2->SetPosition(0.0f, -50.5f, -38.0f);
    light2->SetScale(18.0f, 2.0f, 8.0f);
    light2->SetEmergencyLight(false, 1.4f);
    CeilingLight* light3 = game->CreateObj<CeilingLight>("Stage2Light3");
    light3->SetPosition(0.0f, -50.5f, 38.0f);
    light3->SetScale(18.0f, 2.0f, 8.0f);
    light3->SetEmergencyLight(false, 2.8f);
    CeilingLight* light4 = game->CreateObj<CeilingLight>("CeilingLight4");
    light4->SetPosition(0.0f, -50.5f, 112.0f);
    light4->SetScale(18.0f, 2.0f, 8.0f);
    light4->SetEmergencyLight(false, 4.1f);

    Door* door = game->CreateObj<Door>("Stage2Door");
    door->SetPosition(0.0f, -74.0f, 140.0f);
    door->ResetClosed(0);

    ExitTrigger* exit = game->CreateObj<ExitTrigger>("Stage2Exit");
    exit->SetPosition(0.0f, -80.0f, 153.0f);
    exit->SetNextScene(SceneName::Result);
    exit->SetInteractionEnabled(false);

    player->Update();
    m_Hud.Init();
}

// 周回数、視線、騒音、信号パズル、追跡演出を同時に監視して進行を更新します。
void Stage2Scene::Update()
{
    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    if (player == nullptr)
    {
        return;
    }

    ExitTrigger* exit = game->GetObj<ExitTrigger>("Stage2Exit");
    if (exit != nullptr && exit->IsEscaping())
    {
        m_FinalPursuitTimer = 0.0f;
        ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
        if (shadow != nullptr)
        {
            shadow->SetActive(false);
        }
        ShadowMan* noiseShadow = game->GetObj<ShadowMan>("Stage2NoiseShadow");
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        return;
    }

    constexpr float deltaTime = 1.0f / 60.0f;
    if (m_CaughtTimer >= 0.0f)
    {
        UpdateCaughtSequence(*player, deltaTime);
        return;
    }

    if (!player->CanControl())
    {
        return;
    }

    const int debugCommand = m_DebugCommand;
    m_DebugCommand = 0;
    if (debugCommand == 1 && m_LoopCount < 3)
    {
        AdvanceLoop(*player);
    }
    else if (debugCommand == 2)
    {
        while (m_LoopCount < 3)
        {
            AdvanceLoop(*player);
        }
        m_SignalAccepted[0] = true;
        m_SignalAccepted[1] = true;
        m_SignalAccepted[2] = true;
        m_SignalStep = 3;
        m_SignalPuzzleComplete = true;
        m_FinalSequenceArmed = true;
        if (m_FinalSequenceArmed && m_FinalSequenceTimer < 0.0f)
        {
            StartFinalSequence();
        }
    }
    else if (debugCommand == 3)
    {
        StartObservedScare();
    }

    m_VisualTimer += deltaTime;
    if (m_LoopTransitionTimer >= 0.0f)
    {
        m_LoopTransitionTimer += deltaTime;
        if (m_LoopTransitionTimer >= 4.20f)
        {
            m_LoopTransitionTimer = -1.0f;
        }
    }
    m_ProgressHintTimer += deltaTime;
    if (Input::GetKeyTrigger(VK_H) ||
        Input::GetButtonTrigger(XINPUT_LEFT_SHOULDER))
    {
        m_ProgressHintTimer = (std::max)(m_ProgressHintTimer, 30.0f);
        Input::SetVibration(2, 0.04f);
    }

    m_LoopCooldown = (std::max)(0.0f, m_LoopCooldown - deltaTime);
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    m_GazeNoticeTimer = (std::max)(0.0f, m_GazeNoticeTimer - deltaTime);
    m_ScratchNoticeTimer =
        (std::max)(0.0f, m_ScratchNoticeTimer - deltaTime);
    m_PortraitNoticeTimer =
        (std::max)(0.0f, m_PortraitNoticeTimer - deltaTime);
    m_FalseDoorNoticeTimer =
        (std::max)(0.0f, m_FalseDoorNoticeTimer - deltaTime);
    m_ClockNoticeTimer =
        (std::max)(0.0f, m_ClockNoticeTimer - deltaTime);
    m_PuzzleFeedbackTimer =
        (std::max)(0.0f, m_PuzzleFeedbackTimer - deltaTime);
    m_NoiseEventCooldown =
        (std::max)(0.0f, m_NoiseEventCooldown - deltaTime);
    m_NoiseWarningTimer =
        (std::max)(0.0f, m_NoiseWarningTimer - deltaTime);
    m_WetStepNoticeTimer =
        (std::max)(0.0f, m_WetStepNoticeTimer - deltaTime);
    m_NoiseStalkerCooldown =
        (std::max)(0.0f, m_NoiseStalkerCooldown - deltaTime);
    m_NoiseStalkerNoticeTimer =
        (std::max)(0.0f, m_NoiseStalkerNoticeTimer - deltaTime);

    const Vector3 playerPosition = player->GetPosition();
    const bool onWetSurface =
        (std::abs(playerPosition.x + 9.0f) <= 20.0f &&
            std::abs(playerPosition.z + 82.0f) <= 11.0f) ||
        (std::abs(playerPosition.x - 10.0f) <= 19.0f &&
            std::abs(playerPosition.z - 12.0f) <= 12.5f) ||
        (std::abs(playerPosition.x + 7.0f) <= 22.0f &&
            std::abs(playerPosition.z - 92.0f) <= 10.0f);
    player->SetWetSurface(onWetSurface);

    // 濡れ面は静止画にせず、微細な反射の揺れと危険時の照明反射を与えます。
    const char* puddleNames[] =
    {
        "Stage2Puddle1", "Stage2Puddle2", "Stage2Puddle3"
    };
    for (int puddleIndex = 0; puddleIndex < 3; ++puddleIndex)
    {
        Wall* puddle = game->GetObj<Wall>(puddleNames[puddleIndex]);
        if (puddle == nullptr)
        {
            continue;
        }
        const float shimmer = std::sin(
            m_VisualTimer * (1.25f + puddleIndex * 0.17f) + puddleIndex * 2.1f)
            * 0.5f + 0.5f;
        const float dangerReflection = m_NoiseThreat * 0.055f;
        puddle->SetAppearance(
            Color(0.020f + shimmer * 0.010f,
                0.045f + shimmer * 0.014f,
                0.052f + shimmer * 0.018f, 0.76f),
            Color(0.035f + dangerReflection,
                0.078f + dangerReflection * 0.55f,
                0.095f + shimmer * 0.025f, 1.0f),
            96.0f + shimmer * 28.0f);
    }
    m_ChargerNoticeTimer =
        (std::max)(0.0f, m_ChargerNoticeTimer - deltaTime);
    m_EvidenceNoticeTimer =
        (std::max)(0.0f, m_EvidenceNoticeTimer - deltaTime);
    m_SignalNoticeTimer =
        (std::max)(0.0f, m_SignalNoticeTimer - deltaTime);
    m_LoopBlinkTimer =
        (std::max)(0.0f, m_LoopBlinkTimer - deltaTime);
    m_FinalPursuitTimer =
        (std::max)(0.0f, m_FinalPursuitTimer - deltaTime);
    m_PursuitGazePenaltyTimer = (std::max)(
        0.0f, m_PursuitGazePenaltyTimer - deltaTime);
    m_GuidancePulseCooldown = (std::max)(
        0.0f, m_GuidancePulseCooldown - deltaTime);

    Door* corridorDoor = game->GetObj<Door>("Stage2Door");
    if (corridorDoor != nullptr && corridorDoor->IsLocked() &&
        m_ProgressHintTimer >= 15.0f &&
        m_GuidancePulseCooldown <= 0.0f)
    {
        const char* guideLightName = m_LoopCount == 1
            ? "Stage2Light3"
            : "Stage2Light2";
        CeilingLight* guideLight =
            game->GetObj<CeilingLight>(guideLightName);
        if (guideLight != nullptr)
        {
            guideLight->TriggerEventFlicker(0.72f, 0.58f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.30f, 0.16f);
        m_GuidancePulseCooldown = 2.8f;
    }

    if (m_LoopCount < 3 && m_LoopCooldown <= 0.0f &&
        // The loop changes only after the player has opened and crossed the
        // corridor door.  The door itself is centred at z = 140.
        player->GetPosition().z > 146.0f)
    {
        AdvanceLoop(*player);
    }

    if (m_SignalPuzzleComplete && m_FinalSequenceArmed &&
        m_FinalSequenceTimer < 0.0f &&
        player->GetPosition().z > -8.0f)
    {
        StartFinalSequence();
    }
    UpdateLightZones(*player);
    UpdateScratchMessage(*player, deltaTime);
    UpdatePortraitAnomaly(*player);
    UpdateFalseDoorAnomaly(*player);
    UpdateClock(deltaTime);
    UpdateClockObservation();
    UpdateNoiseThreat(*player, deltaTime);

    FuseBox* emergencyCharger =
        game->GetObj<FuseBox>("Stage2EmergencyCharger");
    if (!m_ChargerHandled && emergencyCharger != nullptr &&
        emergencyCharger->IsActivated())
    {
        m_ChargerHandled = true;
        m_ChargerNoticeTimer = 2.8f;
        m_NoiseWarningTimer = 3.2f;
        m_NoiseThreat = (std::max)(m_NoiseThreat, 0.76f);
        m_NoiseEventCooldown = 0.12f;
        player->AddBattery(30.0f);
        game->RegisterChargerUsed();

        CeilingLight* startLight =
            game->GetObj<CeilingLight>("Stage2Light1");
        if (startLight != nullptr)
        {
            startLight->TriggerEventFlicker(1.10f, 0.88f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.28f, 0.30f);
        Input::SetVibration(7, 0.16f);
    }

    constexpr const char* evidenceNames[] =
    {
        "Stage2EvidenceTerminal1",
        "Stage2EvidenceTerminal2"
    };
    for (int evidenceIndex = 0; evidenceIndex < 2; ++evidenceIndex)
    {
        FuseBox* evidence = game->GetObj<FuseBox>(evidenceNames[evidenceIndex]);
        if (!m_EvidenceHandled[evidenceIndex] && evidence != nullptr &&
            evidence->IsActivated())
        {
            m_EvidenceHandled[evidenceIndex] = true;
            m_EvidenceNoticeTimer = 3.2f;
            game->RegisterEvidenceCollected();
            player->AddBattery(6.0f);
            m_NoiseThreat = (std::max)(0.0f, m_NoiseThreat - 0.18f);
            const char* markerName = evidenceIndex == 0
                ? "Stage2EvidenceMarker1"
                : "Stage2EvidenceMarker2";
            Wall* marker = game->GetObj<Wall>(markerName);
            if (marker != nullptr)
            {
                marker->SetAppearance(
                    Color(0.08f, 0.18f, 0.10f, 1.0f),
                    Color(0.16f, 0.52f, 0.22f, 1.0f),
                    44.0f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.38f, 0.18f);
            Input::SetVibration(4, 0.08f);
        }
    }

    UpdateSignalPuzzle();
    UpdateSignalStalker();

    FuseBox* confirmationPanel =
        game->GetObj<FuseBox>("Stage2ConfirmationPanel");
    const bool evidenceConfirmed =
        (m_LoopCount == 1 && m_FalseDoorMoved) ||
        (m_LoopCount == 2 && m_ClockObservedThisLoop);
    if (confirmationPanel != nullptr)
    {
        confirmationPanel->SetManualInteractionAllowed(evidenceConfirmed);
        if (evidenceConfirmed && confirmationPanel->IsActivated() &&
            !m_ConfirmationHandledThisLoop)
        {
            m_ConfirmationHandledThisLoop = true;
            game->RegisterAnomalyHandled();
            m_NoticeTimer = 2.8f;
            m_FalseDoorNoticeTimer = 0.0f;
            m_ClockNoticeTimer = 0.0f;
            m_ProgressHintTimer = 0.0f;
            if (corridorDoor != nullptr)
            {
                corridorDoor->SetLocked(false);
            }

            CeilingLight* doorLight =
                game->GetObj<CeilingLight>("CeilingLight4");
            if (doorLight != nullptr)
            {
                doorLight->TriggerEventFlicker(0.90f, 0.78f);
            }
            game->GetPostProcess()->TriggerBloomPulse(0.76f, 0.28f);
            Input::SetVibration(7, 0.16f);
        }
    }

    const float loopRate = static_cast<float>(m_LoopCount) / 3.0f;
    const char* stageLightNames[] =
    {
        "Stage2Light1", "Stage2Light2",
        "Stage2Light3", "CeilingLight4"
    };
    float localFixtureLight = 0.0f;
    for (const char* lightName : stageLightNames)
    {
        CeilingLight* fixture = game->GetObj<CeilingLight>(lightName);
        if (fixture == nullptr)
        {
            continue;
        }

        const float longitudinalDistance = std::fabs(
            fixture->GetPosition().z - player->GetPosition().z);
        const float proximity = 1.0f - (std::min)(
            longitudinalDistance / 105.0f, 1.0f);
        localFixtureLight = (std::max)(
            localFixtureLight,
            fixture->GetBrightness() * proximity);
    }
    const float localDarkness =
        1.0f - (std::clamp)(localFixtureLight, 0.0f, 1.0f);
    const float pulse = std::sin(m_VisualTimer * 1.7f) * 0.025f;
    game->GetPostProcess()->SetCorridorTension(
        (std::clamp)(0.38f + loopRate * 0.42f +
            m_NoiseThreat * 0.14f + pulse, 0.0f, 0.92f));
    game->GetPostProcess()->SetAtmosphere(
        0.20f + loopRate * 0.10f + localDarkness * 0.018f +
            m_NoiseThreat * 0.025f,
        0.62f + loopRate * 0.12f + localDarkness * 0.035f +
            m_NoiseThreat * 0.045f);
    const float adaptedExposure = player->IsFlashlightOn()
        ? 1.00f + localDarkness * 0.035f
        : 1.055f + localDarkness * 0.090f;
    game->GetPostProcess()->SetExposure(adaptedExposure);
    game->GetPostProcess()->SetLensDistortionStrength(
        0.20f + loopRate * 0.34f);
    game->GetPostProcess()->SetFilmGradeStrength(
        0.58f + loopRate * 0.20f);
    game->GetPostProcess()->SetLensDirtStrength(
        0.14f + loopRate * 0.08f);
    const bool signalRestorationActive =
        m_LoopCount >= 3 && !m_SignalPuzzleComplete;
    const float unresolvedSignalRate =
        1.0f - static_cast<float>(m_SignalStep) / 3.0f;
    ShadowMan* activeNoiseShadow =
        game->GetObj<ShadowMan>("Stage2NoiseShadow");
    const float stalkerInterference =
        activeNoiseShadow != nullptr && activeNoiseShadow->IsActive()
            ? (std::clamp)((m_NoiseThreat - 0.52f) * 0.72f, 0.0f, 0.30f)
            : 0.0f;
    game->GetPostProcess()->SetSignalInterference(
        signalRestorationActive
            ? (std::clamp)(0.10f + m_NoiseThreat * 0.62f +
                unresolvedSignalRate * 0.16f, 0.0f, 0.88f)
            : stalkerInterference);
    game->GetPostProcess()->SetVolumetricLight(player->IsFlashlightOn());
    game->GetPostProcess()->SetVolumetricIntensity(
        0.38f + loopRate * 0.20f + localDarkness * 0.055f);
    UpdateObservedScare(deltaTime);
    UpdateFinalPursuit(deltaTime);
    UpdateFinalSequence(deltaTime);

    Wall* doorIndicator = game->GetObj<Wall>("Stage2DoorIndicator");
    if (doorIndicator != nullptr && !m_FinalDoorReady)
    {
        const bool locked = corridorDoor != nullptr && corridorDoor->IsLocked();
        const float indicatorPulse = std::sin(
            m_VisualTimer * (locked ? 3.4f : 6.2f)) * 0.5f + 0.5f;
        if (locked)
        {
            doorIndicator->SetAppearance(
                Color(0.24f, 0.012f, 0.008f, 1.0f),
                Color(0.13f + indicatorPulse * 0.08f,
                    0.001f, 0.0f, 1.0f), 24.0f);
        }
        else
        {
            doorIndicator->SetAppearance(
                Color(0.04f, 0.18f, 0.035f, 1.0f),
                Color(0.015f, 0.16f + indicatorPulse * 0.10f,
                    0.008f, 1.0f), 28.0f);
        }
    }

    m_InteractionSystem.Update(*player);
}




void Stage2Scene::Uninit()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }

    game->GetPostProcess()->SetCorridorTension(0.0f);
    game->GetPostProcess()->SetAtmosphere(0.18f, 0.55f);
    game->GetPostProcess()->SetExposure(1.0f);
    game->GetPostProcess()->SetLensDistortionStrength(0.32f);
    game->GetPostProcess()->SetFilmGradeStrength(0.55f);
    game->GetPostProcess()->SetSignalInterference(0.0f);
    game->GetPostProcess()->SetVolumetricLight(false);
    game->GetPostProcess()->SetVolumetricIntensity(0.58f);

    const char* objectNames[] =
    {
        "Player", "Stage2Shadow", "Stage2NoiseShadow", "Stage2ConfirmationPanel",
        "Stage2EmergencyCharger",
        "Stage2EvidenceTerminal1", "Stage2EvidenceTerminal2",
        "Stage2EvidenceMarker1", "Stage2EvidenceMarker2",
        "Stage2SignalTerminalBlue", "Stage2SignalTerminalAmber",
        "Stage2SignalTerminalRed", "Stage2SignalMarkerBlue",
        "Stage2SignalMarkerAmber", "Stage2SignalMarkerRed",
        "Stage2WallLeft", "Stage2WallRight", "Stage2WallBack",
        "Stage2WallFrontLeft", "Stage2WallFrontRight", "Stage2Floor",
        "Stage2Puddle1", "Stage2Puddle2", "Stage2Puddle3",
        "Stage2Ceiling", "Stage2TrimLeft", "Stage2TrimRight",
        "Stage2PipeLeft", "Stage2PipeRight", "Stage2Portrait",
        "Stage2PortraitEyeLeft", "Stage2PortraitEyeRight",
        "Stage2LoopMark", "Stage2CycleMark1", "Stage2CycleMark2",
        "Stage2CycleMark3", "Stage2DoorIndicator", "Stage2BatteryShelf",
        "Stage2Battery", "Stage2Light1", "Stage2Light2",
        "Stage2Light3", "CeilingLight4", "Stage2Door", "Stage2Exit"
    };
    for (const char* name : objectNames)
    {
        game->DestroyObj(name);
    }
    for (const char* name : Stage2ScratchNames)
    {
        game->DestroyObj(name);
    }
    for (const char* name : Stage2FalseDoorNames)
    {
        game->DestroyObj(name);
    }
    for (const char* name : Stage2ClockNames)
    {
        game->DestroyObj(name);
    }
    m_Hud.Uninit();
}
