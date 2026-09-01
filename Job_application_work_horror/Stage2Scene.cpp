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

namespace
{
    constexpr const char* Stage2ScratchNames[] =
    {
        "Stage2Scratch01", "Stage2Scratch02", "Stage2Scratch03",
        "Stage2Scratch04", "Stage2Scratch05", "Stage2Scratch06",
        "Stage2Scratch07", "Stage2Scratch08", "Stage2Scratch09",
        "Stage2Scratch10", "Stage2Scratch11", "Stage2Scratch12",
        "Stage2Scratch13"
    };
    constexpr int Stage2ScratchCount =
        static_cast<int>(sizeof(Stage2ScratchNames) /
            sizeof(Stage2ScratchNames[0]));

    constexpr const char* Stage2FalseDoorNames[] =
    {
        "Stage2FalseDoorPanel",
        "Stage2FalseDoorFrameNear",
        "Stage2FalseDoorFrameFar",
        "Stage2FalseDoorFrameTop",
        "Stage2FalseDoorHandle"
    };

    constexpr const char* Stage2ClockNames[] =
    {
        "Stage2ClockFace",
        "Stage2ClockFrameTop",
        "Stage2ClockFrameBottom",
        "Stage2ClockFrameNear",
        "Stage2ClockFrameFar",
        "Stage2ClockHourHand",
        "Stage2ClockMinuteHand"
    };
}

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

void Stage2Scene::UpdateNoiseThreat(Player& player, float deltaTime)
{
    if (m_FinalSequenceTimer >= 0.0f || m_FinalPursuitTimer > 0.0f)
    {
        m_NoiseThreat = (std::max)(0.0f, m_NoiseThreat - deltaTime * 0.8f);
        ShadowMan* noiseShadow = Core::Game::GetInstance()->GetObj<ShadowMan>(
            "Stage2NoiseShadow");
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        return;
    }

    const bool signalStealthActive =
        m_LoopCount >= 3 && !m_SignalPuzzleComplete;
    if (m_LoopCount >= 3 && !signalStealthActive)
    {
        m_NoiseThreat = (std::max)(0.0f, m_NoiseThreat - deltaTime * 0.8f);
        ShadowMan* noiseShadow = Core::Game::GetInstance()->GetObj<ShadowMan>(
            "Stage2NoiseShadow");
        if (noiseShadow != nullptr)
        {
            noiseShadow->SetActive(false);
        }
        return;
    }

    const float surfacePulse = player.GetSurfaceNoisePulse();
    if (surfacePulse > 0.0f)
    {
        m_NoiseThreat = (std::min)(1.0f,
            m_NoiseThreat + surfacePulse * (signalStealthActive ? 0.18f : 0.11f));
        if (surfacePulse >= 0.60f)
        {
            m_WetStepNoticeTimer = 1.8f;
        }
    }

    const float change = player.IsSprinting()
        ? deltaTime * (signalStealthActive ? 0.48f : 0.36f)
        : -deltaTime * (signalStealthActive ? 0.12f : 0.22f);
    m_NoiseThreat = (std::clamp)(m_NoiseThreat + change, 0.0f, 1.0f);

    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* noiseShadow =
        game->GetObj<ShadowMan>("Stage2NoiseShadow");
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player.GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float proximity = 1.0f - (std::clamp)(
            (distance - 14.0f) / 72.0f, 0.0f, 1.0f);
        m_NoiseThreat = (std::max)(m_NoiseThreat, proximity * 0.92f);

        if (distance <= 15.5f)
        {
            m_NoiseCatch = true;
            StartCaughtSequence(player);
            return;
        }
    }

    const bool canSpawnNoiseStalker =
        m_NoiseThreat >= 0.74f &&
        m_NoiseStalkerCooldown <= 0.0f &&
        m_FinalSequenceTimer < 0.0f &&
        m_FinalPursuitTimer <= 0.0f &&
        noiseShadow != nullptr && !noiseShadow->IsActive();
    if (canSpawnNoiseStalker)
    {
        const Vector3 forward = player.GetForward();
        Vector3 spawnPosition = player.GetPosition() - forward * 68.0f;
        spawnPosition.x = (std::clamp)(spawnPosition.x, -30.0f, 30.0f);
        spawnPosition.y = -99.0f;
        spawnPosition.z = (std::clamp)(spawnPosition.z, -145.0f, 128.0f);

        noiseShadow->SetPosition(
            spawnPosition.x, spawnPosition.y, spawnPosition.z);
        noiseShadow->SetActive(true);
        noiseShadow->EnableChase(13.0f + m_NoiseThreat * 6.0f, 12.0f);
        noiseShadow->EnableGazeScare(7.5f);
        noiseShadow->SetOnObserved([this]()
        {
            Core::Game* currentGame = Core::Game::GetInstance();
            ShadowMan* currentShadow =
                currentGame->GetObj<ShadowMan>("Stage2NoiseShadow");
            if (currentShadow != nullptr)
            {
                currentShadow->SetActive(false);
            }
            m_NoiseThreat = (std::max)(0.10f, m_NoiseThreat - 0.38f);
            m_NoiseStalkerCooldown = 6.0f;
            m_NoiseStalkerNoticeTimer = 2.4f;
        });
        m_NoiseStalkerCooldown = 8.5f;
        m_NoiseStalkerNoticeTimer = 2.8f;
        m_NoiseWarningTimer = (std::max)(m_NoiseWarningTimer, 2.8f);
        game->PlayAudioCue(SOUND_CUE_SCARE, 0.78f);
        game->GetPostProcess()->TriggerHorrorPulse(0.42f, 0.38f);
        Input::SetVibration(8, 0.22f);
    }

    if (signalStealthActive && m_NoiseThreat >= 0.98f)
    {
        ResetSignalPuzzle();
        RegisterPuzzleMistake(4);
        m_PuzzleFeedbackType = 4;
        m_PuzzleFeedbackTimer = 2.8f;
        m_SignalNoticeTimer = 3.0f;
        m_NoiseWarningTimer = 3.0f;
        m_NoiseThreat = 0.30f;
        m_NoiseEventCooldown = 2.8f;

        game->GetPostProcess()->TriggerHorrorPulse(0.72f, 0.48f);
        game->GetPostProcess()->TriggerBloomPulse(0.90f, 0.24f);
        Input::SetVibration(13, 0.30f);
        return;
    }
    if (m_NoiseThreat < 0.70f || m_NoiseEventCooldown > 0.0f)
    {
        return;
    }

    const float playerZ = player.GetPosition().z;
    const char* reactionLightName = playerZ < -55.0f
        ? "Stage2Light1"
        : (playerZ < 18.0f
            ? "Stage2Light2"
            : (playerZ < 88.0f ? "Stage2Light3" : "CeilingLight4"));

    CeilingLight* reactionLight =
        game->GetObj<CeilingLight>(reactionLightName);
    if (reactionLight != nullptr)
    {
        reactionLight->TriggerEventFlicker(
            0.48f + m_NoiseThreat * 0.48f,
            0.46f + m_NoiseThreat * 0.42f);
    }

    m_NoiseWarningTimer = 2.1f;
    m_NoiseEventCooldown = 2.35f;
    game->GetPostProcess()->TriggerHorrorPulse(
        0.10f + m_NoiseThreat * 0.16f,
        0.22f);
    Input::SetVibration(
        3 + static_cast<int>(m_NoiseThreat * 4.0f),
        0.08f + m_NoiseThreat * 0.08f);
}

void Stage2Scene::AdvanceLoop(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    ++m_LoopCount;
    m_LoopCooldown = 1.0f;
    m_ProgressHintTimer = 0.0f;
    m_GuidancePulseCooldown = 1.2f;
    m_LoopBlinkTimer = 0.28f;
    m_LoopTransitionTimer = 0.0f;
    m_NoticeTimer = 3.0f;
    m_LightZoneMask = 0;
    m_ScratchScareTriggered = false;
    m_PortraitObserved = false;
    m_PortraitChangedThisLoop = false;
    m_FalseDoorObserved = false;
    m_FalseDoorMoved = false;
    m_ClockObservedThisLoop = false;
    m_ConfirmationHandledThisLoop = false;
    ResetSignalPuzzle();
    m_PuzzleFeedbackTimer = 0.0f;
    m_PuzzleFeedbackType = 0;
    m_PuzzleMistakeCount = 0;
    m_NoiseThreat = 0.12f;
    m_NoiseEventCooldown = 1.0f;
    m_NoiseWarningTimer = 0.0f;
    m_NoiseStalkerCooldown = 2.0f;
    m_NoiseStalkerNoticeTimer = 0.0f;
    ShadowMan* noiseShadow =
        game->GetObj<ShadowMan>("Stage2NoiseShadow");
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
    FuseBox* confirmationPanel =
        game->GetObj<FuseBox>("Stage2ConfirmationPanel");
    if (confirmationPanel != nullptr)
    {
        confirmationPanel->ResetActivation();
        confirmationPanel->SetManualInteractionAllowed(false);
    }
    player.SetPosition(Vector3(0.0f, -99.0f, -125.0f));
    Door* loopDoor = game->GetObj<Door>("Stage2Door");
    if (loopDoor != nullptr)
    {
        loopDoor->ResetClosed(m_LoopCount);
        // Each repeated hallway has one change that must be noticed before
        // the familiar door will open. This turns the loop into observation
        // gameplay instead of a straight walk through the same corridor.
        loopDoor->SetLocked(m_LoopCount > 0);
    }

    constexpr const char* corridorLightNames[] =
    {
        "Stage2Light1", "Stage2Light2",
        "Stage2Light3", "CeilingLight4"
    };
    for (const char* lightName : corridorLightNames)
    {
        CeilingLight* light = game->GetObj<CeilingLight>(lightName);
        if (light != nullptr)
        {
            light->SetForcedOff(false);
        }
    }

    ConfigureClockForLoop();

    game->GetPostProcess()->TriggerHorrorPulse(
        0.26f + static_cast<float>(m_LoopCount) * 0.13f,
        0.38f + static_cast<float>(m_LoopCount) * 0.10f);
    game->GetPostProcess()->TriggerBloomPulse(
        0.46f + static_cast<float>(m_LoopCount) * 0.08f,
        0.26f);
    Input::SetVibration(6 + m_LoopCount * 3, 0.18f);

    const char* cycleMarkNames[] =
    {
        "Stage2CycleMark1",
        "Stage2CycleMark2",
        "Stage2CycleMark3"
    };
    for (int markIndex = 0; markIndex < 3; ++markIndex)
    {
        Wall* cycleMark =
            game->GetObj<Wall>(cycleMarkNames[markIndex]);
        if (cycleMark == nullptr)
        {
            continue;
        }

        const bool revealed = markIndex < m_LoopCount;
        cycleMark->SetVisible(revealed);
        if (revealed)
        {
            const float emission =
                0.10f + static_cast<float>(m_LoopCount) * 0.055f;
            cycleMark->SetAppearance(
                Color(0.24f, 0.006f, 0.003f, 1.0f),
                Color(emission, 0.001f, 0.0f, 1.0f),
                20.0f);
        }
    }

    Wall* loopMark = game->GetObj<Wall>("Stage2LoopMark");
    Wall* portrait = game->GetObj<Wall>("Stage2Portrait");
    CeilingLight* light2 = game->GetObj<CeilingLight>("Stage2Light2");
    CeilingLight* light3 = game->GetObj<CeilingLight>("Stage2Light3");

    if (m_LoopCount == 1)
    {
        SetFalseDoorState(true, false);
        CeilingLight* failingLight =
            game->GetObj<CeilingLight>("Stage2Light2");
        if (failingLight != nullptr)
        {
            failingLight->SetFaulted(true);
        }
        RevealScratchPieces(0, 3, 0.10f);
        BatteryItem* battery = game->GetObj<BatteryItem>("Stage2Battery");
        if (battery != nullptr)
        {
            battery->SetActive(true);
        }
        if (loopMark != nullptr)
        {
            loopMark->SetVisible(true);
            loopMark->SetAppearance(
                Color(0.28f, 0.008f, 0.004f, 1.0f),
                Color(0.16f, 0.001f, 0.0f, 1.0f), 22.0f);
        }
        if (light2 != nullptr)
        {
            light2->SetEmergencyLight(true, 5.2f);
        }
    }
    else if (m_LoopCount == 2)
    {
        SetFalseDoorState(false, false);
        CeilingLight* entranceLight =
            game->GetObj<CeilingLight>("Stage2Light1");
        CeilingLight* farLight =
            game->GetObj<CeilingLight>("Stage2Light3");
        if (entranceLight != nullptr)
        {
            entranceLight->SetFaulted(true);
        }
        if (farLight != nullptr)
        {
            farLight->SetFaulted(true);
        }
        RevealScratchPieces(3, 9, 0.15f);
        if (portrait != nullptr)
        {
            portrait->SetAppearance(
                Color(0.12f, 0.018f, 0.012f, 1.0f),
                Color(0.045f, 0.0f, 0.0f, 1.0f), 12.0f);
        }
        if (light3 != nullptr)
        {
            light3->SetEmergencyLight(true, 7.1f);
        }
        ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
        if (shadow != nullptr)
        {
            shadow->SetActive(true);
            shadow->EnableGazeScare(9.0f);
            shadow->SetOnObserved(
                [this]()
                {
                    StartObservedScare();
                });
        }
    }
    else
    {
        SetFalseDoorState(false, false);
        RevealScratchPieces(9, Stage2ScratchCount, 0.24f);
        // The last loop now requires restoring the three signal panels.
        // The chase is armed only after the player has explored the corridor
        // and entered the visible colour sequence correctly.
        m_FinalSequenceArmed = false;
        CeilingLight* doorLight = game->GetObj<CeilingLight>("CeilingLight4");
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(1.5f, 0.94f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.64f, 0.58f);
    }
}

void Stage2Scene::ConfigureClockForLoop()
{
    if (m_LoopCount == 1)
    {
        m_ClockHourAngle = 1.18f;
        m_ClockMinuteAngle = -2.34f;
    }
    else if (m_LoopCount == 2)
    {
        m_ClockHourAngle = -0.62f;
        m_ClockMinuteAngle = 2.72f;
    }
    else if (m_LoopCount >= 3)
    {
        m_ClockHourAngle = 3.14159265f;
        m_ClockMinuteAngle = 3.14159265f;

        Core::Game* game = Core::Game::GetInstance();
        Wall* face = game->GetObj<Wall>("Stage2ClockFace");
        if (face != nullptr)
        {
            face->SetAppearance(
                Color(0.075f, 0.018f, 0.012f, 1.0f),
                Color(0.018f, 0.001f, 0.0f, 1.0f),
                10.0f);
        }
    }
}

void Stage2Scene::UpdateClock(float deltaTime)
{
    if (m_LoopCount == 0)
    {
        m_ClockMinuteAngle += deltaTime * 0.035f;
        m_ClockHourAngle += deltaTime * 0.0029f;
    }
    else if (m_LoopCount == 2)
    {
        // The reverse motion is deliberately slow enough to be noticed only
        // when the player compares the hands against the previous loop.
        m_ClockMinuteAngle -= deltaTime * 0.82f;
        m_ClockHourAngle -= deltaTime * 0.068f;
    }

    float displayedHourAngle = m_ClockHourAngle;
    float displayedMinuteAngle = m_ClockMinuteAngle;
    if (m_LoopCount == 2)
    {
        constexpr float clockStep = 0.105f;
        displayedHourAngle =
            std::floor(m_ClockHourAngle / clockStep) * clockStep;
        displayedMinuteAngle =
            std::floor(m_ClockMinuteAngle / clockStep) * clockStep;
    }

    Core::Game* game = Core::Game::GetInstance();
    Wall* hourHand = game->GetObj<Wall>("Stage2ClockHourHand");
    Wall* minuteHand = game->GetObj<Wall>("Stage2ClockMinuteHand");
    if (hourHand != nullptr)
    {
        hourHand->SetRotation(Vector3(displayedHourAngle, 0.0f, 0.0f));
    }
    if (minuteHand != nullptr)
    {
        minuteHand->SetRotation(Vector3(displayedMinuteAngle, 0.0f, 0.0f));
    }
}

void Stage2Scene::UpdateClockObservation()
{
    if (m_LoopCount <= 0 || m_LoopCount >= 3 ||
        m_ClockObservedThisLoop)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 clockCenter(-38.0f, -70.0f, -25.0f);
    Vector3 cameraToClock =
        clockCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToClock.Length();
    if (distance > 0.001f)
    {
        cameraToClock /= distance;
    }

    const float facing =
        game->GetCamera()->GetForward().Dot(cameraToClock);
    if (distance > 98.0f || facing < 0.91f)
    {
        return;
    }

    Player* player = game->GetObj<Player>("Player");
    if (m_LoopCount == 2 && player != nullptr && player->IsFlashlightOn())
    {
        RegisterPuzzleMistake(2);
        return;
    }

    m_ClockObservedThisLoop = true;
    m_ClockNoticeTimer = 2.6f;

    if (m_LoopCount == 2)
    {
        m_NoticeTimer = 2.8f;

        CeilingLight* doorLight =
            game->GetObj<CeilingLight>("CeilingLight4");
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(0.90f, 0.76f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.58f, 0.24f);
    }

    CeilingLight* clockLight =
        game->GetObj<CeilingLight>("Stage2Light2");
    if (clockLight != nullptr)
    {
        clockLight->TriggerEventFlicker(
            m_LoopCount == 1 ? 0.46f : 0.82f,
            m_LoopCount == 1 ? 0.42f : 0.72f);
    }
    game->GetPostProcess()->TriggerHorrorPulse(
        m_LoopCount == 1 ? 0.14f : 0.28f,
        0.32f);
    Input::SetVibration(m_LoopCount == 1 ? 3 : 6, 0.14f);
}

void Stage2Scene::RegisterPuzzleMistake(int type)
{
    if (m_PuzzleFeedbackTimer > 0.0f)
    {
        return;
    }

    m_PuzzleFeedbackType = type;
    m_PuzzleFeedbackTimer = 2.2f;
    m_PuzzleMistakeCount = (std::min)(m_PuzzleMistakeCount + 1, 3);

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterPuzzleMistake();
    CeilingLight* warningLight = game->GetObj<CeilingLight>(
        type == 1 ? "Stage2Light3" : "Stage2Light2");
    const float mistakeRate =
        static_cast<float>(m_PuzzleMistakeCount) / 3.0f;
    if (warningLight != nullptr)
    {
        warningLight->TriggerEventFlicker(
            0.48f + mistakeRate * 0.52f,
            0.42f + mistakeRate * 0.48f);
        if (m_PuzzleMistakeCount >= 3)
        {
            warningLight->SetForcedOff(true);
        }
    }

    game->GetPostProcess()->TriggerHorrorPulse(
        0.08f + mistakeRate * 0.24f,
        0.18f + mistakeRate * 0.18f);
    game->GetPostProcess()->TriggerBloomPulse(
        0.14f + mistakeRate * 0.28f,
        0.14f);
    Input::SetVibration(
        2 + m_PuzzleMistakeCount * 2,
        0.06f + mistakeRate * 0.12f);
}

void Stage2Scene::ResetSignalPuzzle()
{
    m_SignalStep = 0;
    m_SignalPuzzleComplete = false;
    m_SignalAccepted[0] = false;
    m_SignalAccepted[1] = false;
    m_SignalAccepted[2] = false;
    if (m_LoopCount >= 3)
    {
        m_FinalSequenceArmed = false;
    }

    Core::Game* game = Core::Game::GetInstance();
    ShadowMan* signalShadow = game->GetObj<ShadowMan>("Stage2Shadow");
    if (signalShadow != nullptr && m_FinalPursuitTimer <= 0.0f)
    {
        signalShadow->SetActive(false);
    }
    constexpr const char* terminalNames[] =
    {
        "Stage2SignalTerminalBlue",
        "Stage2SignalTerminalAmber",
        "Stage2SignalTerminalRed"
    };
    for (const char* terminalName : terminalNames)
    {
        FuseBox* terminal = game->GetObj<FuseBox>(terminalName);
        if (terminal != nullptr)
        {
            terminal->ResetActivation();
            terminal->SetManualInteractionAllowed(m_LoopCount >= 3);
        }
    }
    ApplySignalLightingState();
}

void Stage2Scene::UpdateSignalPuzzle()
{
    Core::Game* game = Core::Game::GetInstance();
    constexpr const char* terminalNames[] =
    {
        "Stage2SignalTerminalBlue",
        "Stage2SignalTerminalAmber",
        "Stage2SignalTerminalRed"
    };
    constexpr const char* markerNames[] =
    {
        "Stage2SignalMarkerBlue",
        "Stage2SignalMarkerAmber",
        "Stage2SignalMarkerRed"
    };
    const Color baseDiffuse[] =
    {
        Color(0.015f, 0.055f, 0.13f, 1.0f),
        Color(0.13f, 0.075f, 0.012f, 1.0f),
        Color(0.13f, 0.018f, 0.012f, 1.0f)
    };
    const Color baseEmission[] =
    {
        Color(0.015f, 0.18f, 0.48f, 1.0f),
        Color(0.40f, 0.17f, 0.008f, 1.0f),
        Color(0.42f, 0.018f, 0.008f, 1.0f)
    };

    const bool puzzleActive = m_LoopCount >= 3 && !m_SignalPuzzleComplete;
    for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
    {
        FuseBox* terminal = game->GetObj<FuseBox>(terminalNames[signalIndex]);
        if (terminal != nullptr)
        {
            terminal->SetManualInteractionAllowed(puzzleActive);
        }
    }

    if (puzzleActive)
    {
        for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
        {
            FuseBox* terminal = game->GetObj<FuseBox>(terminalNames[signalIndex]);
            if (terminal == nullptr || !terminal->IsActivated() ||
                m_SignalAccepted[signalIndex])
            {
                continue;
            }

            if (signalIndex != m_SignalStep)
            {
                ResetSignalPuzzle();
                RegisterPuzzleMistake(3);
                m_SignalNoticeTimer = 2.8f;
                m_NoiseThreat = 0.92f;
                game->GetPostProcess()->TriggerHorrorPulse(0.62f, 0.42f);
                Input::SetVibration(11, 0.24f);
                break;
            }

            m_SignalAccepted[signalIndex] = true;
            ++m_SignalStep;
            m_PuzzleFeedbackType = 0;
            const float retryAssist = static_cast<float>((std::min)(
                m_PuzzleMistakeCount, 2));
            m_NoiseThreat = (std::min)(
                1.0f,
                m_NoiseThreat + 0.12f - retryAssist * 0.025f);
            m_SignalNoticeTimer = 2.4f;
            m_ProgressHintTimer = 0.0f;
            game->GetPostProcess()->TriggerBloomPulse(0.52f, 0.20f);
            Input::SetVibration(4 + m_SignalStep * 2, 0.10f);

            CeilingLight* responseLight = game->GetObj<CeilingLight>(
                signalIndex == 0 ? "Stage2Light3" :
                signalIndex == 1 ? "Stage2Light2" : "Stage2Light1");
            if (responseLight != nullptr)
            {
                responseLight->TriggerEventFlicker(0.54f, 0.48f);
            }
            ApplySignalLightingState();

            if (m_SignalStep >= 3)
            {
                m_SignalPuzzleComplete = true;
                m_FinalSequenceArmed = true;
                m_NoticeTimer = 3.2f;
                ApplySignalLightingState();
                game->RegisterAnomalyHandled();
                ShadowMan* signalShadow =
                    game->GetObj<ShadowMan>("Stage2Shadow");
                if (signalShadow != nullptr)
                {
                    signalShadow->SetActive(false);
                }
                for (const char* terminalName : terminalNames)
                {
                    FuseBox* completedTerminal =
                        game->GetObj<FuseBox>(terminalName);
                    if (completedTerminal != nullptr)
                    {
                        completedTerminal->SetManualInteractionAllowed(false);
                    }
                }
                game->GetPostProcess()->TriggerBloomPulse(1.05f, 0.44f);
                game->GetPostProcess()->TriggerHorrorPulse(0.34f, 0.34f);
            }
            else
            {
                Player* player = game->GetObj<Player>("Player");
                ShadowMan* signalShadow =
                    game->GetObj<ShadowMan>("Stage2Shadow");
                if (player != nullptr && signalShadow != nullptr)
                {
                    const Vector3 playerPosition = player->GetPosition();
                    Vector3 backward = -player->GetForward();
                    backward.y = 0.0f;
                    if (backward.LengthSquared() < 0.001f)
                    {
                        backward = Vector3(0.0f, 0.0f, -1.0f);
                    }
                    backward.Normalize();
                    const float assistLevel = static_cast<float>((std::min)(
                        m_PuzzleMistakeCount, 2));
                    const float spawnDistance =
                        78.0f - m_SignalStep * 7.0f + assistLevel * 13.0f;
                    signalShadow->SetPosition(
                        playerPosition.x + backward.x * spawnDistance,
                        -99.0f,
                        playerPosition.z + backward.z * spawnDistance);
                    signalShadow->SetActive(false);
                    signalShadow->SetActive(true);
                    signalShadow->EnableChase(
                        13.0f + static_cast<float>(m_SignalStep) * 2.0f -
                            assistLevel * 1.6f,
                        30.0f);
                    signalShadow->EnableGazeScare(24.0f);
                    signalShadow->SetOnObserved(
                        [this]()
                        {
                            Core::Game* currentGame =
                                Core::Game::GetInstance();
                            ShadowMan* currentShadow =
                                currentGame->GetObj<ShadowMan>("Stage2Shadow");
                            if (currentShadow != nullptr)
                            {
                                currentShadow->SetActive(false);
                            }
                            m_NoiseThreat = (std::max)(0.12f, m_NoiseThreat - 0.24f);
                            m_PuzzleFeedbackType = 6;
                            m_PuzzleFeedbackTimer = 2.0f;
                            m_SignalNoticeTimer = 2.4f;
                            currentGame->GetPostProcess()->TriggerBloomPulse(0.72f, 0.22f);
                            Input::SetVibration(4, 0.10f);
                        });
                }
            }
            break;
        }
    }

    const float pulse = std::sin(m_VisualTimer * 5.4f) * 0.5f + 0.5f;
    for (int signalIndex = 0; signalIndex < 3; ++signalIndex)
    {
        Wall* marker = game->GetObj<Wall>(markerNames[signalIndex]);
        if (marker == nullptr)
        {
            continue;
        }

        if (m_SignalPuzzleComplete || m_SignalAccepted[signalIndex])
        {
            marker->SetAppearance(
                Color(0.025f, 0.14f, 0.055f, 1.0f),
                Color(0.02f, 0.42f + pulse * 0.12f, 0.08f, 1.0f),
                58.0f);
        }
        else
        {
            const bool isTarget = puzzleActive && signalIndex == m_SignalStep;
            const float energy = isTarget ? 0.72f + pulse * 0.42f : 0.16f;
            marker->SetAppearance(
                baseDiffuse[signalIndex],
                baseEmission[signalIndex] * energy,
                isTarget ? 54.0f : 24.0f);
        }
    }
}

void Stage2Scene::ApplySignalLightingState()
{
    if (m_LoopCount < 3)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    constexpr const char* restorationLights[] =
    {
        "Stage2Light3",
        "Stage2Light2",
        "Stage2Light1"
    };
    constexpr float flickerOffsets[] = { 2.8f, 1.4f, 0.2f };

    for (int lightIndex = 0; lightIndex < 3; ++lightIndex)
    {
        CeilingLight* light =
            game->GetObj<CeilingLight>(restorationLights[lightIndex]);
        if (light == nullptr)
        {
            continue;
        }

        const bool restored =
            m_SignalPuzzleComplete || lightIndex < m_SignalStep;
        light->SetForcedOff(false);
        light->SetFaulted(!restored);
        light->SetEmergencyLight(!restored, flickerOffsets[lightIndex]);
    }

    CeilingLight* exitLight = game->GetObj<CeilingLight>("CeilingLight4");
    if (exitLight != nullptr)
    {
        exitLight->SetForcedOff(false);
        exitLight->SetFaulted(!m_SignalPuzzleComplete);
        exitLight->SetEmergencyLight(
            !m_SignalPuzzleComplete,
            4.1f);
        if (m_SignalPuzzleComplete)
        {
            exitLight->TriggerEventFlicker(1.10f, 0.82f);
        }
    }
}

void Stage2Scene::UpdateSignalStalker()
{
    if (m_LoopCount < 3 || m_SignalPuzzleComplete || m_SignalStep <= 0)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
    if (player == nullptr || shadow == nullptr || !shadow->IsActive())
    {
        return;
    }

    Vector3 toShadow = shadow->GetPosition() - player->GetPosition();
    toShadow.y = 0.0f;
    const float distance = toShadow.Length();
    const float proximity = 1.0f - (std::clamp)((distance - 30.0f) / 90.0f, 0.0f, 1.0f);
    m_NoiseThreat = (std::max)(m_NoiseThreat, proximity * 0.78f);

    if (distance > 35.0f)
    {
        return;
    }

    shadow->SetActive(false);
    ResetSignalPuzzle();
    RegisterPuzzleMistake(5);
    m_PuzzleFeedbackType = 5;
    m_PuzzleFeedbackTimer = 2.8f;
    m_SignalNoticeTimer = 3.0f;
    m_NoiseThreat = 0.38f;
    m_NoiseEventCooldown = 2.8f;
    game->GetPostProcess()->TriggerHorrorPulse(0.88f, 0.48f);
    game->GetPostProcess()->TriggerBloomPulse(0.16f, 0.16f);
    Input::SetVibration(15, 0.34f);
}

void Stage2Scene::SetFalseDoorState(bool visible, bool rightSide)
{
    Core::Game* game = Core::Game::GetInstance();
    const float surfaceX = rightSide ? 39.3f : -39.3f;
    const float frameX = rightSide ? 39.0f : -39.0f;
    const float handleX = rightSide ? 38.3f : -38.3f;
    const float centerZ = rightSide ? -88.0f : 70.0f;

    Wall* panel = game->GetObj<Wall>("Stage2FalseDoorPanel");
    Wall* frameNear = game->GetObj<Wall>("Stage2FalseDoorFrameNear");
    Wall* frameFar = game->GetObj<Wall>("Stage2FalseDoorFrameFar");
    Wall* frameTop = game->GetObj<Wall>("Stage2FalseDoorFrameTop");
    Wall* handle = game->GetObj<Wall>("Stage2FalseDoorHandle");
    if (panel == nullptr || frameNear == nullptr || frameFar == nullptr ||
        frameTop == nullptr || handle == nullptr)
    {
        return;
    }

    panel->SetPosition(surfaceX, -76.0f, centerZ);
    frameNear->SetPosition(frameX, -76.0f, centerZ - 12.0f);
    frameFar->SetPosition(frameX, -76.0f, centerZ + 12.0f);
    frameTop->SetPosition(frameX, -54.0f, centerZ);
    handle->SetPosition(handleX, -76.0f, centerZ - 8.0f);

    for (const char* name : Stage2FalseDoorNames)
    {
        Wall* piece = game->GetObj<Wall>(name);
        if (piece != nullptr)
        {
            piece->SetVisible(visible);
        }
    }
}

void Stage2Scene::UpdateFalseDoorAnomaly(const Player& player)
{
    if (m_LoopCount != 1 || m_FalseDoorMoved)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 doorCenter(-38.3f, -72.0f, 70.0f);
    Vector3 cameraToDoor = doorCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToDoor.Length();
    if (distance > 0.001f)
    {
        cameraToDoor /= distance;
    }

    const float facing = game->GetCamera()->GetForward().Dot(cameraToDoor);
    if (distance < 105.0f && facing > 0.88f)
    {
        if (!player.IsFlashlightOn())
        {
            RegisterPuzzleMistake(1);
            return;
        }
        m_FalseDoorObserved = true;
        m_PuzzleFeedbackTimer = 0.0f;
        m_PuzzleFeedbackType = 0;
        return;
    }

    if (!m_FalseDoorObserved || (facing > 0.30f && distance < 118.0f))
    {
        return;
    }

    m_FalseDoorMoved = true;
    m_FalseDoorNoticeTimer = 2.8f;
    SetFalseDoorState(true, true);

    m_NoticeTimer = 2.8f;

    CeilingLight* doorLight =
        game->GetObj<CeilingLight>("CeilingLight4");
    if (doorLight != nullptr)
    {
        doorLight->TriggerEventFlicker(0.90f, 0.76f);
    }
    game->GetPostProcess()->TriggerBloomPulse(0.58f, 0.24f);

    CeilingLight* oldDoorLight =
        game->GetObj<CeilingLight>("Stage2Light3");
    if (oldDoorLight != nullptr)
    {
        oldDoorLight->TriggerEventFlicker(0.72f, 0.68f);
    }
    game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.34f);
    Input::SetVibration(8, 0.20f);
}

void Stage2Scene::StartObservedScare()
{
    if (m_ObservedScareTimer >= 0.0f)
    {
        return;
    }

    m_ObservedScareTimer = 0.0f;
    m_GazeNoticeTimer = 2.8f;
    m_ObservedScarePhase = 0;

    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCue(SOUND_CUE_SCARE);
    game->GetPostProcess()->TriggerHorrorPulse(0.72f, 0.52f);
    Input::SetVibration(15, 0.34f);
}

void Stage2Scene::UpdateObservedScare(float deltaTime)
{
    if (m_ObservedScareTimer < 0.0f)
    {
        return;
    }

    m_ObservedScareTimer += deltaTime;
    Core::Game* game = Core::Game::GetInstance();

    struct LightBeat
    {
        float Time;
        const char* Name;
        float Strength;
    };

    constexpr LightBeat beats[] =
    {
        { 0.05f, "CeilingLight4", 0.94f },
        { 0.30f, "Stage2Light3", 0.90f },
        { 0.58f, "Stage2Light2", 0.86f },
        { 0.90f, "Stage2Light1", 0.80f }
    };

    constexpr int beatCount =
        static_cast<int>(sizeof(beats) / sizeof(beats[0]));
    while (m_ObservedScarePhase < beatCount &&
        m_ObservedScareTimer >= beats[m_ObservedScarePhase].Time)
    {
        const LightBeat& beat = beats[m_ObservedScarePhase];
        CeilingLight* light = game->GetObj<CeilingLight>(beat.Name);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.82f, beat.Strength);
        }

        game->GetPostProcess()->TriggerBloomPulse(
            0.26f + beat.Strength * 0.25f,
            0.16f);
        ++m_ObservedScarePhase;
    }

    if (m_ObservedScareTimer < 1.35f)
    {
        const float intensity =
            1.0f - (std::min)(m_ObservedScareTimer / 1.35f, 1.0f);
        game->GetPostProcess()->SetAtmosphere(
            0.30f + intensity * 0.16f,
            0.76f + intensity * 0.12f);
    }
    else
    {
        m_ObservedScareTimer = -1.0f;
    }
}

void Stage2Scene::StartFinalSequence()
{
    m_FinalSequenceTimer = 0.0f;
    m_FinalPursuitTimer = 7.0f;
    m_PursuitPulseTimer = 0.12f;
    m_PursuitGazePenaltyTimer = 0.0f;
    m_NoiseThreat = 0.0f;
    m_NoiseWarningTimer = 0.0f;
    m_FinalSequencePhase = 0;
    m_NoticeTimer = 2.8f;

    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
    ShadowMan* noiseShadow = game->GetObj<ShadowMan>("Stage2NoiseShadow");
    if (noiseShadow != nullptr)
    {
        noiseShadow->SetActive(false);
    }
    if (player != nullptr)
    {
        player->RestoreStamina();
    }
    if (player != nullptr && shadow != nullptr)
    {
        const Vector3 playerPosition = player->GetPosition();
        shadow->SetPosition(
            playerPosition.x,
            -99.0f,
            playerPosition.z - 72.0f);
        shadow->SetActive(false);
        shadow->SetActive(true);
        const float retryAssist = static_cast<float>((std::min)(
            game->GetCaughtCount(), 2)) * 1.5f;
        shadow->EnableChase(18.0f - retryAssist, 30.0f);
        shadow->EnableGazeScare(8.0f);
        shadow->SetOnObserved(
            [this]()
            {
                if (m_FinalPursuitTimer <= 0.0f ||
                    m_PursuitGazePenaltyTimer > 0.0f)
                {
                    return;
                }

                m_PursuitGazePenaltyTimer = 1.45f;
                m_NoticeTimer = 1.65f;
                Core::Game* game = Core::Game::GetInstance();
                const char* lightNames[] =
                {
                    "Stage2Light1", "Stage2Light2",
                    "Stage2Light3", "CeilingLight4"
                };
                for (const char* lightName : lightNames)
                {
                    CeilingLight* light =
                        game->GetObj<CeilingLight>(lightName);
                    if (light != nullptr)
                    {
                        light->TriggerEventFlicker(0.72f, 0.92f);
                    }
                }
                game->GetPostProcess()->TriggerHorrorPulse(0.92f, 0.64f);
                game->GetPostProcess()->TriggerBloomPulse(0.62f, 0.22f);
                Input::SetVibration(16, 0.45f);
            });
    }
    RevealScratchPieces(0, Stage2ScratchCount, 0.34f);
    game->GetPostProcess()->TriggerHorrorPulse(0.82f, 0.72f);
    Input::SetVibration(18, 0.42f);
}

void Stage2Scene::UpdateFinalSequence(float deltaTime)
{
    if (m_FinalSequenceTimer < 0.0f || m_FinalDoorReady)
    {
        return;
    }

    m_FinalSequenceTimer += deltaTime;
    Core::Game* game = Core::Game::GetInstance();

    struct FinalBeat
    {
        float Time;
        const char* LightName;
    };
    constexpr FinalBeat beats[] =
    {
        { 0.05f, "Stage2Light1" },
        { 0.30f, "Stage2Light2" },
        { 0.58f, "Stage2Light3" },
        { 0.90f, "CeilingLight4" }
    };
    constexpr int beatCount =
        static_cast<int>(sizeof(beats) / sizeof(beats[0]));

    while (m_FinalSequencePhase < beatCount &&
        m_FinalSequenceTimer >= beats[m_FinalSequencePhase].Time)
    {
        CeilingLight* light = game->GetObj<CeilingLight>(
            beats[m_FinalSequencePhase].LightName);
        if (light != nullptr)
        {
            light->SetEmergencyLight(
                true,
                9.0f + static_cast<float>(m_FinalSequencePhase) * 0.7f);
            light->TriggerEventFlicker(1.0f, 0.96f);
        }
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.18f);
        ++m_FinalSequencePhase;
    }

    if (m_FinalSequenceTimer < 1.65f)
    {
        const float pulse =
            std::sin(m_FinalSequenceTimer * 22.0f) * 0.5f + 0.5f;
        game->GetPostProcess()->SetExposure(0.76f + pulse * 0.17f);
        game->GetPostProcess()->SetAtmosphere(0.43f, 0.88f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.68f + pulse * 0.08f);
        game->GetPostProcess()->SetVolumetricIntensity(
            0.62f + pulse * 0.12f);
        return;
    }

    m_FinalDoorReady = true;
    m_FinalSequenceArmed = false;
    m_NoticeTimer = 3.0f;

    Door* finalDoor = game->GetObj<Door>("Stage2Door");
    if (finalDoor != nullptr)
    {
        finalDoor->SetLocked(false);
    }

    ExitTrigger* exit = game->GetObj<ExitTrigger>("Stage2Exit");
    if (exit != nullptr)
    {
        exit->SetInteractionEnabled(true);
    }

    Wall* doorIndicator = game->GetObj<Wall>("Stage2DoorIndicator");
    if (doorIndicator != nullptr)
    {
        doorIndicator->SetAppearance(
            Color(0.025f, 0.22f, 0.055f, 1.0f),
            Color(0.005f, 0.30f, 0.025f, 1.0f), 30.0f);
    }

    game->GetPostProcess()->TriggerBloomPulse(0.88f, 0.62f);
    Input::SetVibration(9, 0.24f);
}

void Stage2Scene::UpdateFinalPursuit(float deltaTime)
{
    if (m_FinalPursuitTimer <= 0.0f)
    {
        ShadowMan* shadow =
            Core::Game::GetInstance()->GetObj<ShadowMan>("Stage2Shadow");
        if (shadow != nullptr)
        {
            shadow->SetActive(false);
        }

        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
    if (player == nullptr || shadow == nullptr)
    {
        return;
    }

    const Vector3 offset = player->GetPosition() - shadow->GetPosition();
    const float horizontalDistance =
        std::sqrt(offset.x * offset.x + offset.z * offset.z);
    if (horizontalDistance <= 31.5f)
    {
        StartCaughtSequence(*player);
        return;
    }

    const float proximity = 1.0f - (std::clamp)(
        (horizontalDistance - 30.0f) / 72.0f,
        0.0f,
        1.0f);
    const float dangerPulse =
        std::sin(m_VisualTimer * (6.0f + proximity * 5.0f)) *
        0.5f + 0.5f;

    game->GetPostProcess()->SetCorridorTension(
        0.74f + proximity * 0.16f);
    if (m_FinalDoorReady)
    {
        game->GetPostProcess()->SetAtmosphere(
            0.30f + proximity * 0.085f + dangerPulse * 0.018f,
            0.76f + proximity * 0.135f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.46f + proximity * 0.18f +
            dangerPulse * proximity * 0.025f);
        game->GetPostProcess()->SetVolumetricIntensity(
            0.50f + proximity * 0.12f);
        game->GetPostProcess()->SetFilmGradeStrength(
            0.78f + proximity * 0.18f);
        game->GetPostProcess()->SetLensDirtStrength(
            0.20f + proximity * 0.22f);
        game->GetPostProcess()->SetExposure(
            1.0f - proximity * 0.055f -
            dangerPulse * proximity * 0.025f);
    }

    if (m_PursuitGazePenaltyTimer > 0.0f)
    {
        const float penalty = (std::clamp)(
            m_PursuitGazePenaltyTimer / 1.45f, 0.0f, 1.0f);
        game->GetPostProcess()->SetAtmosphere(
            0.43f + penalty * 0.12f,
            0.88f + penalty * 0.08f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.66f + penalty * 0.14f);
        game->GetPostProcess()->SetLensDirtStrength(0.32f + penalty * 0.36f);
        game->GetPostProcess()->SetExposure(0.91f + (1.0f - penalty) * 0.06f);
    }

    m_PursuitPulseTimer -= deltaTime;
    if (m_PursuitPulseTimer > 0.0f)
    {
        return;
    }

    const int vibrationFrames =
        3 + static_cast<int>(proximity * 7.0f);
    Input::SetVibration(
        vibrationFrames,
        0.08f + proximity * 0.17f);
    if (proximity > 0.34f)
    {
        game->GetPostProcess()->TriggerHorrorPulse(
            0.045f + proximity * 0.075f,
            0.16f);
    }
    m_PursuitPulseTimer = 0.72f - proximity * 0.40f;
}

void Stage2Scene::StartCaughtSequence(Player& player)
{
    if (m_CaughtTimer >= 0.0f)
    {
        return;
    }

    m_CaughtTimer = 0.0f;
    m_FinalPursuitTimer = 0.0f;
    m_PursuitPulseTimer = 0.0f;
    m_PursuitGazePenaltyTimer = 0.0f;
    player.SetCanControl(false);

    Core::Game* game = Core::Game::GetInstance();
    game->RegisterCaught();
    game->PlayAudioCue(SOUND_CUE_SCARE);
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

    game->GetPostProcess()->TriggerHorrorPulse(1.0f, 0.72f);
    game->GetPostProcess()->TriggerBloomPulse(0.18f, 0.16f);
    Input::SetVibration(24, 0.72f);
}

void Stage2Scene::UpdateCaughtSequence(Player& player, float deltaTime)
{
    m_CaughtTimer += deltaTime;
    Core::Game* game = Core::Game::GetInstance();

    if (m_CaughtTimer < 1.15f)
    {
        const float darkness = (std::clamp)(
            m_CaughtTimer / 0.34f, 0.0f, 1.0f);
        game->GetPostProcess()->SetExposure(
            0.92f - darkness * 0.38f);
        game->GetPostProcess()->SetAtmosphere(
            0.46f + darkness * 0.18f,
            0.90f + darkness * 0.08f);
        game->GetPostProcess()->SetLensDistortionStrength(
            0.72f + darkness * 0.18f);
        return;
    }

    // 騒音追跡で捕まった場合は現在の周回を保持し、入口へ戻して再挑戦させます。
    player.SetPosition(Vector3(0.0f, -99.0f, -125.0f));
    player.RestoreStamina();
    player.SetCanControl(true);
    m_CaughtTimer = -1.0f;
    const bool wasNoiseCatch = m_NoiseCatch;
    if (!wasNoiseCatch)
    {
        m_FinalSequenceTimer = -1.0f;
        m_FinalSequencePhase = 0;
        m_FinalSequenceArmed = true;
        m_FinalDoorReady = false;
    }
    else
    {
        m_NoiseThreat = 0.18f;
        m_NoiseStalkerCooldown = 7.0f;
        m_NoiseStalkerNoticeTimer = 3.0f;
        m_NoiseCatch = false;
    }
    m_NoticeTimer = 3.2f;

    Door* door = game->GetObj<Door>("Stage2Door");
    if (door != nullptr && !wasNoiseCatch)
    {
        door->ResetClosed(3);
        door->SetLocked(true);
    }

    ExitTrigger* exit = game->GetObj<ExitTrigger>("Stage2Exit");
    if (exit != nullptr && !wasNoiseCatch)
    {
        exit->SetInteractionEnabled(false);
    }

    Wall* indicator = game->GetObj<Wall>("Stage2DoorIndicator");
    if (indicator != nullptr && !wasNoiseCatch)
    {
        indicator->SetAppearance(
            Color(0.24f, 0.012f, 0.008f, 1.0f),
            Color(0.18f, 0.001f, 0.0f, 1.0f),
            24.0f);
    }

    game->GetPostProcess()->TriggerHorrorPulse(0.34f, 0.42f);
    Input::SetVibration(8, 0.18f);
}

void Stage2Scene::RevealScratchPieces(int first, int last, float emission)
{
    Core::Game* game = Core::Game::GetInstance();
    first = (std::clamp)(first, 0, Stage2ScratchCount);
    last = (std::clamp)(last, first, Stage2ScratchCount);

    for (int index = first; index < last; ++index)
    {
        Wall* scratch = game->GetObj<Wall>(Stage2ScratchNames[index]);
        if (scratch == nullptr)
        {
            continue;
        }

        const float variation = static_cast<float>(index % 3) * 0.018f;
        scratch->SetVisible(true);
        scratch->SetAppearance(
            Color(0.20f + variation, 0.008f, 0.004f, 1.0f),
            Color(emission + variation, 0.001f, 0.0f, 1.0f),
            18.0f);
    }
}

void Stage2Scene::UpdateLightZones(const Player& player)
{
    if (m_LoopCount >= 3 || m_FinalSequenceTimer >= 0.0f)
    {
        return;
    }

    struct LightZone
    {
        float TriggerZ;
        const char* LightName;
    };
    constexpr LightZone zones[] =
    {
        { -88.0f, "Stage2Light1" },
        { -14.0f, "Stage2Light2" },
        {  60.0f, "Stage2Light3" },
        { 108.0f, "CeilingLight4" }
    };
    constexpr int zoneCount =
        static_cast<int>(sizeof(zones) / sizeof(zones[0]));

    Core::Game* game = Core::Game::GetInstance();
    for (int index = 0; index < zoneCount; ++index)
    {
        const unsigned int zoneBit = 1u << index;
        if ((m_LightZoneMask & zoneBit) != 0u ||
            player.GetPosition().z <= zones[index].TriggerZ)
        {
            continue;
        }

        m_LightZoneMask |= zoneBit;
        CeilingLight* light = game->GetObj<CeilingLight>(zones[index].LightName);
        const float loopStrength = static_cast<float>(m_LoopCount) * 0.17f;
        const float strength = (std::clamp)(
            0.40f + loopStrength + static_cast<float>(index) * 0.035f,
            0.0f,
            0.92f);
        if (light != nullptr)
        {
            light->TriggerEventFlicker(0.48f + loopStrength, strength);
        }

        // From the second pass onward, darkness closes behind the player.
        // The next loop restores the fixtures so the corridor can repeat.
        if (m_LoopCount > 0 && index > 0)
        {
            CeilingLight* lightBehind = game->GetObj<CeilingLight>(
                zones[index - 1].LightName);
            if (lightBehind != nullptr)
            {
                lightBehind->SetForcedOff(true);
            }
        }

        game->GetPostProcess()->TriggerBloomPulse(
            0.20f + strength * 0.20f,
            0.15f);
        if (m_LoopCount > 0 && index >= 1)
        {
            game->GetPostProcess()->TriggerHorrorPulse(
                0.08f + loopStrength * 0.24f,
                0.16f);
        }
        Input::SetVibration(2 + m_LoopCount, 0.08f + loopStrength * 0.18f);
    }
}

void Stage2Scene::UpdateScratchMessage(
    const Player& player,
    float deltaTime)
{
    if (m_LoopCount <= 0)
    {
        return;
    }

    m_ScratchUpdateAccumulator += deltaTime;
    if (m_ScratchUpdateAccumulator < 0.05f)
    {
        return;
    }
    m_ScratchUpdateAccumulator = 0.0f;

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 messageCenter(39.45f, -71.0f, 31.0f);
    Vector3 cameraToMessage =
        messageCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToMessage.Length();
    if (distance > 0.001f)
    {
        cameraToMessage /= distance;
    }

    const float facing = (std::max)(
        game->GetCamera()->GetForward().Dot(cameraToMessage),
        0.0f);
    const float proximity = 1.0f - (std::clamp)(
        (distance - 24.0f) / 95.0f,
        0.0f,
        1.0f);
    const float gaze = (std::clamp)(
        (facing - 0.62f) / 0.30f,
        0.0f,
        1.0f);
    const float flashlightResponse =
        player.IsFlashlightOn() ? gaze * proximity : 0.0f;
    const float heartbeat =
        std::sin(m_VisualTimer * (2.4f + m_LoopCount * 0.45f)) *
        0.5f + 0.5f;
    const float finalBoost =
        (m_FinalSequenceTimer >= 0.0f && !m_FinalDoorReady) ? 0.16f : 0.0f;
    const float emission =
        0.07f + static_cast<float>(m_LoopCount) * 0.035f +
        flashlightResponse * (0.10f + heartbeat * 0.16f) + finalBoost;

    int visibleCount = Stage2ScratchCount;
    if (m_LoopCount == 1)
    {
        visibleCount = 3;
    }
    else if (m_LoopCount == 2)
    {
        visibleCount = 9;
    }
    RevealScratchPieces(0, visibleCount, emission);

    if (!m_ScratchScareTriggered && m_LoopCount >= 2 &&
        player.IsFlashlightOn() && distance < 92.0f && facing > 0.90f)
    {
        m_ScratchScareTriggered = true;
        m_ScratchNoticeTimer = 2.2f;
        game->GetPostProcess()->TriggerHorrorPulse(
            0.24f + static_cast<float>(m_LoopCount) * 0.08f,
            0.32f);
        game->GetPostProcess()->TriggerBloomPulse(0.48f, 0.24f);
        Input::SetVibration(7, 0.20f);
    }
}

void Stage2Scene::UpdatePortraitAnomaly(const Player& player)
{
    if (m_LoopCount <= 0 || m_LoopCount >= 3 ||
        m_PortraitChangedThisLoop)
    {
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    const Vector3 portraitCenter(38.72f, -69.0f, -25.0f);
    Vector3 cameraToPortrait =
        portraitCenter - game->GetCamera()->GetPosition();
    const float distance = cameraToPortrait.Length();
    if (distance > 0.001f)
    {
        cameraToPortrait /= distance;
    }

    const float facing =
        game->GetCamera()->GetForward().Dot(cameraToPortrait);
    const bool lookingAtPortrait =
        player.IsFlashlightOn() && distance < 105.0f && facing > 0.91f;
    if (lookingAtPortrait)
    {
        m_PortraitObserved = true;
        return;
    }

    if (!m_PortraitObserved || (facing > 0.55f && distance < 112.0f))
    {
        return;
    }

    m_PortraitChangedThisLoop = true;
    m_PortraitNoticeTimer = 2.4f;

    const float emission = m_LoopCount == 1 ? 0.10f : 0.28f;
    const char* eyeNames[] =
    {
        "Stage2PortraitEyeLeft",
        "Stage2PortraitEyeRight"
    };
    for (const char* name : eyeNames)
    {
        Wall* eye = game->GetObj<Wall>(name);
        if (eye != nullptr)
        {
            eye->SetVisible(true);
            eye->SetAppearance(
                Color(0.18f, 0.008f, 0.003f, 1.0f),
                Color(emission, 0.002f, 0.0f, 1.0f),
                28.0f);
        }
    }

    Wall* portrait = game->GetObj<Wall>("Stage2Portrait");
    if (portrait != nullptr)
    {
        portrait->SetAppearance(
            Color(0.10f, 0.014f, 0.009f, 1.0f),
            Color(emission * 0.12f, 0.0f, 0.0f, 1.0f),
            12.0f);
    }

    CeilingLight* nearbyLight = game->GetObj<CeilingLight>(
        m_LoopCount == 1 ? "Stage2Light2" : "Stage2Light3");
    if (nearbyLight != nullptr)
    {
        nearbyLight->TriggerEventFlicker(
            0.62f + static_cast<float>(m_LoopCount) * 0.18f,
            0.58f + static_cast<float>(m_LoopCount) * 0.12f);
    }
    game->GetPostProcess()->TriggerHorrorPulse(
        0.18f + static_cast<float>(m_LoopCount) * 0.10f,
        0.30f);
    Input::SetVibration(6, 0.17f);
}

void Stage2Scene::Draw(Camera* camera)
{
    (void)camera;
    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    if (player == nullptr)
    {
        return;
    }

    ExitTrigger* exit = game->GetObj<ExitTrigger>("Stage2Exit");
    Door* finalDoor = game->GetObj<Door>("Stage2Door");
    FuseBox* confirmationPanel =
        game->GetObj<FuseBox>("Stage2ConfirmationPanel");
    const bool confirmationPending =
        (m_LoopCount == 1 || m_LoopCount == 2) &&
        confirmationPanel != nullptr &&
        !confirmationPanel->IsActivated() &&
        ((m_LoopCount == 1 && m_FalseDoorMoved) ||
         (m_LoopCount == 2 && m_ClockObservedThisLoop));
    constexpr std::string_view closedLoopObjectives[] =
    {
        "奥のドアを開ける 1回目",
        "奥のドアを開ける 2回目",
        "奥のドアを開ける 3回目"
    };
    constexpr std::string_view openLoopObjectives[] =
    {
        "開いたドアを通り抜ける 1回目",
        "開いたドアを通り抜ける 2回目",
        "開いたドアを通り抜ける 3回目"
    };

    std::string_view objective = "廊下の奥にある出口へ向かう";
    if (m_LoopCount < 3)
    {
        const size_t loopIndex = static_cast<size_t>(m_LoopCount);
        objective = finalDoor != nullptr && finalDoor->IsOpen()
            ? openLoopObjectives[loopIndex]
            : closedLoopObjectives[loopIndex];
    }
    if (m_LoopCount == 1 && !m_FalseDoorMoved)
    {
        objective = m_FalseDoorObserved
            ? "偽物のドアから視線を外す"
            : "懐中電灯で左の偽物のドアを照らす";
    }
    else if (m_LoopCount == 2 && !m_ClockObservedThisLoop)
    {
        objective = "ライトを消して左の時計を見る";
    }
    else if (confirmationPending)
    {
        objective = "奥の異常確認スイッチを押す";
    }
    else if (m_LoopCount >= 3 && !m_SignalPuzzleComplete)
    {
        if (m_SignalStep == 0)
        {
            objective = m_PuzzleMistakeCount >= 2
                ? "再試行補助中 奥の青い信号盤からやり直す"
                : "信号復旧 まず奥の青い信号盤を操作する";
        }
        else if (m_SignalStep == 1)
        {
            objective = "信号復旧 黄色へ戻る 背後の影はライトで追い払う";
        }
        else
        {
            objective = "信号復旧 赤へ戻る 背後の影はライトで追い払う";
        }
    }
    if (exit != nullptr && exit->IsEscaping())
    {
        objective = "脱出中";
    }
    else if (m_GazeNoticeTimer > 0.0f)
    {
        objective = "止まらず奥のドアへ進む";
    }
    else if (m_CaughtTimer >= 0.0f)
    {
        objective = "捕まった チェックポイントへ戻る";
    }
    else if (m_ChargerNoticeTimer > 0.0f)
    {
        objective = "充電器の音で廊下が反応した";
    }
    else if (m_EvidenceNoticeTimer > 0.0f)
    {
        objective = "残された記録を回収した";
    }
    else if (m_SignalNoticeTimer > 0.0f)
    {
        if (m_SignalPuzzleComplete)
        {
            objective = "信号復旧完了 廊下の中央へ進む";
        }
        else if (m_PuzzleFeedbackType == 3)
        {
            objective = "順番が違う 青からやり直す";
        }
        else if (m_PuzzleFeedbackType == 4)
        {
            objective = "走る音で同期が切れた 歩いて青からやり直す";
        }
        else if (m_PuzzleFeedbackType == 5)
        {
            objective = "影に追いつかれた 青からやり直す";
        }
        else if (m_PuzzleFeedbackType == 6)
        {
            objective = "影を追い払った 次の信号盤へ進む";
        }
        else if (m_SignalStep == 1)
        {
            objective = "青を確認 黄色へ戻る 背後に注意";
        }
        else
        {
            objective = "黄色を確認 赤へ戻る 背後に注意";
        }
    }
    else if (m_NoiseStalkerNoticeTimer > 0.0f)
    {
        ShadowMan* noiseShadow =
            game->GetObj<ShadowMan>("Stage2NoiseShadow");
        objective = noiseShadow != nullptr && noiseShadow->IsActive()
            ? "水音を聞いた影が来る 振り返ってライトを当てる"
            : "影を追い払った 静かに進む";
    }
    else if (m_WetStepNoticeTimer > 0.0f)
    {
        objective = "水音が廊下に響いた 水たまりは歩いて渡る";
    }
    else if (m_NoiseWarningTimer > 0.0f)
    {
        objective = "足音が響いている 歩いて静める";
    }
    else if (m_PuzzleFeedbackTimer > 0.0f)
    {
        if (m_PuzzleFeedbackType == 5)
        {
            objective = "影に追いつかれた 青からやり直す";
        }
        else if (m_PuzzleFeedbackType == 6)
        {
            objective = "影を追い払った 次の信号盤へ進む";
        }
        else if (m_PuzzleMistakeCount >= 3)
        {
            objective = "照明が消えた 正しい方法を試す";
        }
        else
        {
            if (m_PuzzleFeedbackType == 1)
            {
                objective = "光が必要だ 懐中電灯でドアを照らす";
            }
            else if (m_PuzzleFeedbackType == 2)
            {
                objective = "光が邪魔だ 懐中電灯を消して時計を見る";
            }
            else if (m_PuzzleFeedbackType == 3)
            {
                objective = "信号の順番が違う 青からやり直す";
            }
            else
            {
                objective = "走る音で同期が切れた 歩いて青からやり直す";
            }
        }
    }
    else if (m_PursuitGazePenaltyTimer > 0.0f)
    {
        objective = "それを見てはいけない";
    }
    else if (m_FinalSequenceTimer >= 0.0f && !m_FinalDoorReady)
    {
        objective = Input::IsControllerConnected()
            ? "左スティック押し込みで出口まで走る"
            : "SHIFTを押して出口まで走る";
    }
    else if (m_FinalPursuitTimer > 0.0f)
    {
        objective = Input::IsControllerConnected()
            ? "左スティック押し込みで出口まで走る"
            : "SHIFTを押して出口まで走る";
    }
    else if (m_FinalDoorReady)
    {
        objective = finalDoor != nullptr && !finalDoor->IsOpen()
            ? "奥のドアを開ける"
            : "開いた出口を通り抜ける";
    }
    else if (m_ScratchNoticeTimer > 0.0f)
    {
        objective = "止まらず奥のドアへ進む";
    }
    else if (m_FalseDoorNoticeTimer > 0.0f)
    {
        objective = "異常を確認した 奥のスイッチへ進む";
    }
    else if (m_ClockNoticeTimer > 0.0f)
    {
        objective = m_LoopCount == 2
            ? "逆回転を確認した 奥のスイッチへ進む"
            : "時計の時刻が変わった";
    }
    else if (m_PortraitNoticeTimer > 0.0f)
    {
        objective = "止まらず奥のドアへ進む";
    }
    else if (m_NoticeTimer > 0.0f)
    {
        if (m_LoopCount == 0) objective = "1回目 奥のドアを開ける";
        else if (m_LoopCount == 1) objective = m_FalseDoorMoved
            ? (m_ConfirmationHandledThisLoop
                ? "鍵が開いた 奥のドアへ進む"
                : "奥の異常確認スイッチを押す")
            : "2回目 廊下の変化を探す";
        else if (m_LoopCount == 2) objective = m_ClockObservedThisLoop
            ? (m_ConfirmationHandledThisLoop
                ? "鍵が開いた 後ろを見ずに進む"
                : "奥の異常確認スイッチを押す")
            : "3回目 左の時計を調べる";
        else objective = m_SignalPuzzleComplete
            ? "信号が復旧した 廊下の中央へ進む"
            : "奥の青い信号盤から復旧する";
    }
    else if (m_ProgressHintTimer >= 30.0f)
    {
        if (m_LoopCount == 1 && !m_FalseDoorMoved)
        {
            objective = "ヒント ライトで偽物のドアを照らして視線を外す";
        }
        else if (m_LoopCount == 2 && !m_ClockObservedThisLoop)
        {
            objective = "ヒント ライトを消して左の時計を正面から見る";
        }
        else if (confirmationPending)
        {
            objective = "ヒント 奥の壁にある赤い確認スイッチを押す";
        }
        else if (m_LoopCount >= 3 && !m_SignalPuzzleComplete)
        {
            objective = m_SignalStep == 0
                ? "ヒント 青は廊下の奥の右壁"
                : m_SignalStep == 1
                    ? "ヒント 黄色は中央左 影は振り返ってライトを当てる"
                    : "ヒント 赤は入口右 影は振り返ってライトを当てる";
        }
        else if (m_FinalDoorReady)
        {
            objective = finalDoor != nullptr && finalDoor->IsOpen()
                ? "ヒント 開いた出口を通り抜ける"
                : "ヒント 今すぐ奥のドアを開ける";
        }
        else if (m_FinalSequenceArmed)
        {
            objective = "ヒント 廊下の中央より先へ進む";
        }
        else if (finalDoor != nullptr && finalDoor->IsOpen())
        {
            objective = "ヒント 開いた奥のドアを通り抜ける";
        }
        else
        {
            objective = "ヒント まっすぐ進み奥のドアを開ける";
        }
    }
    else if (m_ProgressHintTimer >= 15.0f)
    {
        if (m_LoopCount == 1 && !m_FalseDoorMoved)
        {
            objective = m_FalseDoorObserved
                ? "ヒント 偽物のドアから視線を外す"
                : "ヒント ライトを点け前方左側の壁を探す";
        }
        else if (m_LoopCount == 2 && !m_ClockObservedThisLoop)
        {
            objective = "ヒント ライトを消して左の時計を見る";
        }
        else if (confirmationPending)
        {
            objective = "ヒント ドア手前の確認スイッチへ進む";
        }
        else if (m_LoopCount >= 3 && !m_SignalPuzzleComplete)
        {
            objective = "ヒント 発光している信号盤を 青 黄 赤 の順で操作する";
        }
        else if (m_FinalDoorReady)
        {
            objective = "ヒント 廊下の奥にある出口が開いている";
        }
        else if (m_FinalSequenceArmed)
        {
            objective = "ヒント 廊下をそのまま歩き続ける";
        }
        else if (finalDoor != nullptr && finalDoor->IsOpen())
        {
            objective = "ヒント 奥のドアを開けると次へ進む";
        }
        else
        {
            objective = "ヒント 奥のドアを通り抜ける";
        }
    }
    else if (m_FinalSequenceArmed)
    {
        objective = "廊下の奥にある出口へ向かう";
    }

    m_Hud.Draw(*player, -1, m_InteractionSystem.GetPrompt(), objective);
    float threatRate = 0.0f;
    threatRate = (std::max)(threatRate, m_NoiseThreat * 0.78f);
    if (m_LoopCount == 1 || m_LoopCount == 2)
    {
        const float observationDanger =
            static_cast<float>(m_PuzzleMistakeCount) / 3.0f;
        threatRate = (std::max)(threatRate, observationDanger * 0.72f);
    }
    if (m_FinalPursuitTimer > 0.0f)
    {
        ShadowMan* shadow = game->GetObj<ShadowMan>("Stage2Shadow");
        if (shadow != nullptr)
        {
            Vector3 toShadow = shadow->GetPosition() - player->GetPosition();
            toShadow.y = 0.0f;
            const float distance = toShadow.Length();
            threatRate = 1.0f - (std::clamp)(
                (distance - 18.0f) / 92.0f, 0.0f, 1.0f);
        }
    }
    ShadowMan* noiseShadow = game->GetObj<ShadowMan>("Stage2NoiseShadow");
    if (noiseShadow != nullptr && noiseShadow->IsActive())
    {
        Vector3 toShadow = noiseShadow->GetPosition() - player->GetPosition();
        toShadow.y = 0.0f;
        const float distance = toShadow.Length();
        const float noiseShadowDanger = 1.0f - (std::clamp)(
            (distance - 14.0f) / 72.0f, 0.0f, 1.0f);
        threatRate = (std::max)(threatRate, noiseShadowDanger);
    }
    if (m_VisualTimer >= 4.20f &&
        m_CaughtTimer < 0.0f &&
        (exit == nullptr || !exit->IsEscaping()))
    {
        m_Hud.DrawStage2Status(
            m_LoopCount, threatRate, m_FinalDoorReady,
            m_SignalStep,
            m_LoopCount >= 3 && !m_SignalPuzzleComplete);
    }
    if (m_VisualTimer >= 4.20f &&
        m_CaughtTimer < 0.0f &&
        (exit == nullptr || !exit->IsEscaping()))
    {
        Vector3 guideTarget = finalDoor != nullptr
            ? finalDoor->GetPosition()
            : Vector3(0.0f, -74.0f, 140.0f);
        if (m_LoopCount == 1 && !m_FalseDoorMoved)
        {
            guideTarget = Vector3(-38.3f, -72.0f, 70.0f);
        }
        else if (m_LoopCount == 2 && !m_ClockObservedThisLoop)
        {
            guideTarget = Vector3(-38.0f, -70.0f, -25.0f);
        }
        else if (confirmationPending)
        {
            guideTarget = Vector3(35.5f, -90.0f, 112.0f);
        }
        else if (m_LoopCount >= 3 && !m_SignalPuzzleComplete)
        {
            if (m_SignalStep == 0)
            {
                guideTarget = Vector3(35.5f, -90.0f, 82.0f);
            }
            else if (m_SignalStep == 1)
            {
                guideTarget = Vector3(-35.5f, -90.0f, 18.0f);
            }
            else
            {
                guideTarget = Vector3(35.5f, -90.0f, -108.0f);
            }
        }
        m_Hud.DrawObjectiveGuide(
            *camera, player->GetPosition(), guideTarget);
    }
    if (m_VisualTimer < 0.65f)
    {
        const float fade = 1.0f - m_VisualTimer / 0.65f;
        m_Hud.DrawBlink(fade * fade);
    }
    if (m_VisualTimer < 4.20f)
    {
        m_Hud.DrawChapterCard(
            "2階",
            "廊下の変化を見逃さない",
            m_VisualTimer);
    }
    else if (m_LoopTransitionTimer >= 0.0f)
    {
        constexpr std::string_view cycleTitles[] =
        {
            "1回目を通過",
            "2回目を通過",
            "3回目を通過"
        };
        constexpr std::string_view cycleSubtitles[] =
        {
            "同じ廊下へ戻ってきた",
            "何かが移動している",
            "信号復旧を開始する"
        };
        const size_t cycleIndex = static_cast<size_t>((std::clamp)(
            m_LoopCount - 1, 0, 2));
        m_Hud.DrawChapterCard(
            cycleTitles[cycleIndex],
            cycleSubtitles[cycleIndex],
            m_LoopTransitionTimer);
    }
    if (m_LoopBlinkTimer > 0.0f)
    {
        const float blinkRate =
            (std::clamp)(m_LoopBlinkTimer / 0.28f, 0.0f, 1.0f);
        m_Hud.DrawBlink(blinkRate * blinkRate * 0.90f);
    }

    if (m_CaughtTimer >= 0.0f)
    {
        const float caughtFade = (std::clamp)(
            m_CaughtTimer / 0.34f, 0.0f, 1.0f);
        m_Hud.DrawBlink(caughtFade * 0.96f);
    }

    if (exit != nullptr && exit->IsEscaping())
    {
        const float fadeRate = (std::clamp)(
            (exit->GetEscapeProgress() - 0.36f) / 0.64f,
            0.0f, 1.0f);
        const float smoothFade =
            fadeRate * fadeRate * (3.0f - 2.0f * fadeRate);
        m_Hud.DrawBlink(smoothFade * 0.90f);
    }

    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->GetPauseSettingIndex(),
            2,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
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
