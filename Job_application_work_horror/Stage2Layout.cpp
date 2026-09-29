// ============================================================================
// ファイルの役割: 2面のループ廊下に置くObject（壁・照明・端末・異変用の小物）を生成して配置します。
// 主な技術: 配置データの分離、生成時のポインタ受け渡し、名前一覧の一元管理
// ============================================================================

#include "Stage2Layout.h"

#include "SceneLayoutBuilder.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
#include "FuseBox.h"
#include "Game.h"
#include "Player.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <SimpleMath.h>
#include <iterator>

using namespace DirectX::SimpleMath;

namespace Stage2Layout
{
    Stage2Objects Build(Core::Game& game, float clockHourAngle, float clockMinuteAngle)
    {
        Stage2Objects objects;
        SceneLayoutBuilder builder(game, objects.objectNames);

        Player* player = builder.Create<Player>("Player");
        player->SetPosition(Vector3(0.0f, -99.0f, -125.0f));
        player->SetSprintAllowed(false);

        ShadowMan* stageShadow = builder.Create<ShadowMan>("Stage2Shadow");
        stageShadow->SetPosition(0.0f, -99.0f, 62.0f);
        stageShadow->SetDeactivateOnExpire(true);
        stageShadow->SetActive(false);

        // 謎解き用の影と分け、足音だけに反応する追跡者を独立して管理します。
        ShadowMan* noiseShadow = builder.Create<ShadowMan>("Stage2NoiseShadow");
        noiseShadow->SetPosition(0.0f, -99.0f, -145.0f);
        noiseShadow->SetDeactivateOnExpire(true);
        noiseShadow->SetActive(false);

        // 2周目以降、視界の外から静かに近づく「背後の気配」です。
        ShadowMan* presence = builder.Create<ShadowMan>("Stage2Presence");
        presence->SetDeactivateOnExpire(true);
        presence->SetActive(false);

        FuseBox* confirmationPanel =
            builder.Create<FuseBox>("Stage2ConfirmationPanel");
        confirmationPanel->SetManualControl("異常確認スイッチを押す");
        confirmationPanel->SetManualInteractionAllowed(false);
        confirmationPanel->SetPosition(35.5f, -90.0f, 112.0f);
        confirmationPanel->SetRotation(Vector3(0.0f, -DirectX::XM_PIDIV2, 0.0f));

        FuseBox* emergencyCharger =
            builder.Create<FuseBox>("Stage2EmergencyCharger");
        emergencyCharger->SetManualControl("非常用充電器を使う");
        emergencyCharger->SetManualInteractionAllowed(true);
        emergencyCharger->SetPosition(-35.5f, -90.0f, -106.0f);
        emergencyCharger->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));

        FuseBox* evidenceTerminal1 =
            builder.Create<FuseBox>("Stage2EvidenceTerminal1");
        evidenceTerminal1->SetManualControl("残された記録を回収する");
        evidenceTerminal1->SetManualInteractionAllowed(true);
        evidenceTerminal1->SetPosition(35.5f, -90.0f, -76.0f);
        evidenceTerminal1->SetRotation(Vector3(0.0f, -DirectX::XM_PIDIV2, 0.0f));

        FuseBox* evidenceTerminal2 =
            builder.Create<FuseBox>("Stage2EvidenceTerminal2");
        evidenceTerminal2->SetManualControl("残された記録を回収する");
        evidenceTerminal2->SetManualInteractionAllowed(true);
        evidenceTerminal2->SetPosition(-35.5f, -90.0f, 108.0f);
        evidenceTerminal2->SetRotation(Vector3(0.0f, DirectX::XM_PIDIV2, 0.0f));

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
                builder.Create<FuseBox>(signalTerminalNames[signalIndex]);
            signalTerminal->SetManualControl(signalPrompts[signalIndex]);
            signalTerminal->SetManualInteractionAllowed(false);
            signalTerminal->SetPosition(
                signalPositions[signalIndex].x,
                signalPositions[signalIndex].y,
                signalPositions[signalIndex].z);
            signalTerminal->SetRotation(Vector3(
                0.0f,
                signalIndex == 1 ? DirectX::XM_PIDIV2 : -DirectX::XM_PIDIV2,
                0.0f));
            objects.signalTerminals[signalIndex] = signalTerminal;
        }

        const auto createWall = [&builder](
            const char* name,
            const Vector3& position,
            const Vector3& scale,
            const Color& diffuse,
            bool collision)
        {
            Wall* wall = builder.Create<Wall>(name);
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
        for (std::size_t puddleIndex = 0; puddleIndex < std::size(corridorPuddles); ++puddleIndex)
        {
            const CorridorPuddle& puddle = corridorPuddles[puddleIndex];
            Wall* surface = createWall(
                puddle.Name,
                Vector3(puddle.X, -99.32f, puddle.Z),
                Vector3(puddle.Width, 0.12f, puddle.Depth),
                Color(0.025f, 0.052f, 0.060f, 0.72f), false);
            surface->SetAppearance(
                Color(0.025f, 0.052f, 0.060f, 0.72f),
                Color(0.045f, 0.095f, 0.11f, 1.0f), 92.0f);
            surface->SetCastsShadow(false);
            objects.puddles[puddleIndex] = surface;
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
            objects.signalMarkers[signalIndex] = marker;
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
        clockHourHand->SetRotation(Vector3(
            clockHourAngle, 0.0f, 0.0f));
        clockMinuteHand->SetRotation(Vector3(
            clockMinuteAngle, 0.0f, 0.0f));

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
            objects.cycleMarks[markIndex] = cycleMark;
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
            objects.scratches[index] = scratch;
        }

        Wall* batteryShelf = createWall(
            "Stage2BatteryShelf",
            Vector3(35.8f, -87.0f, -45.0f),
            Vector3(8.0f, 2.0f, 10.0f),
            Color(0.07f, 0.055f, 0.045f, 1.0f),
            false);
        batteryShelf->SetCastsShadow(false);
        BatteryItem* battery = builder.Create<BatteryItem>("Stage2Battery");
        battery->SetPosition(35.0f, -82.5f, -45.0f);
        battery->SetActive(false);

        CeilingLight* light1 = builder.Create<CeilingLight>("Stage2Light1");
        light1->SetPosition(0.0f, -50.5f, -112.0f);
        light1->SetScale(18.0f, 2.0f, 8.0f);
        light1->SetEmergencyLight(false, 0.2f);
        CeilingLight* light2 = builder.Create<CeilingLight>("Stage2Light2");
        light2->SetPosition(0.0f, -50.5f, -38.0f);
        light2->SetScale(18.0f, 2.0f, 8.0f);
        light2->SetEmergencyLight(false, 1.4f);
        CeilingLight* light3 = builder.Create<CeilingLight>("Stage2Light3");
        light3->SetPosition(0.0f, -50.5f, 38.0f);
        light3->SetScale(18.0f, 2.0f, 8.0f);
        light3->SetEmergencyLight(false, 2.8f);
        CeilingLight* light4 = builder.Create<CeilingLight>("CeilingLight4");
        light4->SetPosition(0.0f, -50.5f, 112.0f);
        light4->SetScale(18.0f, 2.0f, 8.0f);
        light4->SetEmergencyLight(false, 4.1f);

        Door* door = builder.Create<Door>("Stage2Door");
        door->SetPosition(0.0f, -74.0f, 140.0f);
        door->ResetClosed(0);

        ExitTrigger* exit = builder.Create<ExitTrigger>("Stage2Exit");
        exit->SetPosition(0.0f, -80.0f, 153.0f);
        exit->SetNextScene(SceneName::Result);
        exit->SetInteractionEnabled(false);

        objects.player = player;
        objects.exit = exit;
        objects.door = door;
        objects.shadow = stageShadow;
        objects.noiseShadow = noiseShadow;
        objects.presence = presence;
        objects.confirmationPanel = confirmationPanel;
        objects.emergencyCharger = emergencyCharger;
        objects.battery = battery;
        objects.doorIndicator = doorIndicator;
        objects.portrait = portrait;
        objects.loopMark = loopMark;
        objects.clockFace = clockFace;
        objects.clockHourHand = clockHourHand;
        objects.clockMinuteHand = clockMinuteHand;
        objects.falseDoorPanel = falseDoorPanel;
        objects.falseDoorFrameNear = falseDoorFrameNear;
        objects.falseDoorFrameFar = falseDoorFrameFar;
        objects.falseDoorFrameTop = falseDoorFrameTop;
        objects.falseDoorHandle = falseDoorHandle;
        objects.lights = { light1, light2, light3, light4 };
        objects.evidenceTerminals = { evidenceTerminal1, evidenceTerminal2 };
        objects.evidenceMarkers = { evidenceMarker1, evidenceMarker2 };
        objects.portraitEyes = { portraitEyeLeft, portraitEyeRight };
        return objects;
    }
}
