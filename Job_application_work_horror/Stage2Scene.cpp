#include "Stage2Scene.h"

#include "BatteryItem.h"
#include "CeilingLight.h"
#include "Door.h"
#include "ExitTrigger.h"
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
}

Stage2Scene::Stage2Scene()
{
    Init();
}

Stage2Scene::~Stage2Scene()
{
    Uninit();
}

void Stage2Scene::Init()
{
    Core::Game* game = Core::Game::GetInstance();
    game->SetPowerRestored(true);
    game->GetPostProcess()->SetCorridorTension(0.38f);
    game->GetPostProcess()->SetExposure(1.02f);
    game->GetPostProcess()->SetAtmosphere(0.20f, 0.62f);
    game->GetPostProcess()->SetLensDistortionStrength(0.20f);
    game->GetPostProcess()->SetFilmGradeStrength(0.58f);
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
    m_ScratchUpdateAccumulator = 0.0f;
    m_ObservedScarePhase = 0;
    m_FinalSequencePhase = 0;
    m_LightZoneMask = 0;
    m_ScratchScareTriggered = false;
    m_PortraitObserved = false;
    m_PortraitChangedThisLoop = false;
    m_FalseDoorObserved = false;
    m_FalseDoorMoved = false;
    m_FinalSequenceArmed = false;
    m_FinalDoorReady = false;
    m_DebugCommand = 0;

    Player* player = game->CreateObj<Player>("Player");
    player->SetPosition(Vector3(0.0f, -99.0f, -125.0f));

    ShadowMan* stageShadow = game->CreateObj<ShadowMan>("Stage2Shadow");
    stageShadow->SetPosition(0.0f, -99.0f, 62.0f);
    stageShadow->SetDeactivateOnExpire(true);
    stageShadow->SetActive(false);

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
    Wall* loopMark = createWall("Stage2LoopMark", Vector3(-39.4f, -68.0f, 52.0f),
        Vector3(1.0f, 18.0f, 12.0f), Color(0.22f, 0.01f, 0.006f, 1.0f), false);
    loopMark->SetCastsShadow(false);
    loopMark->SetVisible(false);
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
    door->ResetClosed(3);

    ExitTrigger* exit = game->CreateObj<ExitTrigger>("Stage2Exit");
    exit->SetPosition(0.0f, -80.0f, 153.0f);
    exit->SetNextScene(SceneName::Result);
    exit->SetInteractionEnabled(false);

    player->Update();
    m_Hud.Init();
}

void Stage2Scene::Update()
{
    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    if (player == nullptr || !player->CanControl())
    {
        return;
    }

    constexpr float deltaTime = 1.0f / 60.0f;

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
    m_LoopCooldown = (std::max)(0.0f, m_LoopCooldown - deltaTime);
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    m_GazeNoticeTimer = (std::max)(0.0f, m_GazeNoticeTimer - deltaTime);
    m_ScratchNoticeTimer =
        (std::max)(0.0f, m_ScratchNoticeTimer - deltaTime);
    m_PortraitNoticeTimer =
        (std::max)(0.0f, m_PortraitNoticeTimer - deltaTime);
    m_FalseDoorNoticeTimer =
        (std::max)(0.0f, m_FalseDoorNoticeTimer - deltaTime);

    if (m_LoopCount < 3 && m_LoopCooldown <= 0.0f &&
        player->GetPosition().z > 116.0f)
    {
        AdvanceLoop(*player);
    }

    if (m_FinalSequenceArmed && m_FinalSequenceTimer < 0.0f &&
        player->GetPosition().z > -8.0f)
    {
        StartFinalSequence();
    }
    UpdateLightZones(*player);
    UpdateScratchMessage(*player, deltaTime);
    UpdatePortraitAnomaly(*player);
    UpdateFalseDoorAnomaly(*player);

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
        (std::clamp)(0.38f + loopRate * 0.42f + pulse, 0.0f, 0.86f));
    game->GetPostProcess()->SetAtmosphere(
        0.20f + loopRate * 0.10f + localDarkness * 0.018f,
        0.62f + loopRate * 0.12f + localDarkness * 0.035f);
    const float adaptedExposure = player->IsFlashlightOn()
        ? 1.00f + localDarkness * 0.035f
        : 1.055f + localDarkness * 0.090f;
    game->GetPostProcess()->SetExposure(adaptedExposure);
    game->GetPostProcess()->SetLensDistortionStrength(
        0.20f + loopRate * 0.34f);
    game->GetPostProcess()->SetFilmGradeStrength(
        0.58f + loopRate * 0.20f);
    game->GetPostProcess()->SetVolumetricLight(player->IsFlashlightOn());
    game->GetPostProcess()->SetVolumetricIntensity(
        0.38f + loopRate * 0.20f + localDarkness * 0.055f);
    UpdateObservedScare(deltaTime);
    UpdateFinalSequence(deltaTime);

    Wall* doorIndicator = game->GetObj<Wall>("Stage2DoorIndicator");
    if (doorIndicator != nullptr && !m_FinalDoorReady)
    {
        const float indicatorPulse =
            0.13f + (std::sin(m_VisualTimer * 3.4f) * 0.5f + 0.5f) * 0.08f;
        doorIndicator->SetAppearance(
            Color(0.24f, 0.012f, 0.008f, 1.0f),
            Color(indicatorPulse, 0.001f, 0.0f, 1.0f), 24.0f);
    }

    m_InteractionSystem.Update(*player);
}

void Stage2Scene::AdvanceLoop(Player& player)
{
    Core::Game* game = Core::Game::GetInstance();
    ++m_LoopCount;
    m_LoopCooldown = 1.0f;
    m_NoticeTimer = 3.0f;
    m_LightZoneMask = 0;
    m_ScratchScareTriggered = false;
    m_PortraitObserved = false;
    m_PortraitChangedThisLoop = false;
    m_FalseDoorObserved = false;
    m_FalseDoorMoved = false;
    player.SetPosition(Vector3(0.0f, -99.0f, -125.0f));

    game->GetPostProcess()->TriggerHorrorPulse(
        0.26f + static_cast<float>(m_LoopCount) * 0.13f,
        0.38f + static_cast<float>(m_LoopCount) * 0.10f);
    Input::SetVibration(6 + m_LoopCount * 3, 0.18f);

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
        m_FinalSequenceArmed = true;
        Door* door = game->GetObj<Door>("Stage2Door");
        if (door != nullptr)
        {
            door->ResetClosed(3);
        }
        CeilingLight* doorLight = game->GetObj<CeilingLight>("CeilingLight4");
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(1.5f, 0.94f);
        }
        game->GetPostProcess()->TriggerHorrorPulse(0.64f, 0.58f);
    }
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
        m_FalseDoorObserved = true;
        return;
    }

    if (!m_FalseDoorObserved || (facing > 0.30f && distance < 118.0f))
    {
        return;
    }

    m_FalseDoorMoved = true;
    m_FalseDoorNoticeTimer = 2.8f;
    SetFalseDoorState(true, true);

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
    m_FinalSequencePhase = 0;
    m_NoticeTimer = 2.8f;

    Core::Game* game = Core::Game::GetInstance();
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

    std::string_view objective = "WALK THE HALL";
    ExitTrigger* exit = game->GetObj<ExitTrigger>("Stage2Exit");
    if (exit != nullptr && exit->IsEscaping())
    {
        objective = "ESCAPED";
    }
    else if (m_GazeNoticeTimer > 0.0f)
    {
        objective = "KEEP WALKING";
    }
    else if (m_FinalSequenceTimer >= 0.0f && !m_FinalDoorReady)
    {
        objective = "DO NOT STOP";
    }
    else if (m_ScratchNoticeTimer > 0.0f)
    {
        objective = "HELP ME";
    }
    else if (m_FalseDoorNoticeTimer > 0.0f)
    {
        objective = "THAT DOOR MOVED";
    }
    else if (m_PortraitNoticeTimer > 0.0f)
    {
        objective = "IT MOVED";
    }
    else if (m_NoticeTimer > 0.0f)
    {
        if (m_LoopCount == 0) objective = "YOU HAVE BEEN HERE";
        else if (m_LoopCount == 1) objective = "IT CHANGED";
        else if (m_LoopCount == 2) objective = "DO NOT LOOK";
        else if (m_FinalDoorReady) objective = "OPEN THE DOOR";
        else objective = "WALK TO THE LIGHT";
    }
    else if (m_FinalSequenceArmed)
    {
        objective = "WALK TO THE LIGHT";
    }
    else if (m_FinalDoorReady)
    {
        objective = "LEAVE";
    }

    m_Hud.Draw(*player, -1, m_InteractionSystem.GetPrompt(), objective);
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
    game->GetPostProcess()->SetVolumetricLight(false);
    game->GetPostProcess()->SetVolumetricIntensity(0.58f);

    const char* objectNames[] =
    {
        "Player", "Stage2Shadow", "Stage2WallLeft", "Stage2WallRight", "Stage2WallBack",
        "Stage2WallFrontLeft", "Stage2WallFrontRight", "Stage2Floor",
        "Stage2Ceiling", "Stage2TrimLeft", "Stage2TrimRight",
        "Stage2PipeLeft", "Stage2PipeRight", "Stage2Portrait",
        "Stage2PortraitEyeLeft", "Stage2PortraitEyeRight",
        "Stage2LoopMark", "Stage2DoorIndicator", "Stage2BatteryShelf",
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
    m_Hud.Uninit();
}
