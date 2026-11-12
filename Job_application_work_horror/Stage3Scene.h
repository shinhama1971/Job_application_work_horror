#pragma once

#include "Hud.h"
#include "InteractionSystem.h"
#include "RenderTexture.h"
#include "Scene.h"

#include <array>
#include <string>
#include <vector>

class Door;
class ForestRain;
class FuseBox;
class Player;
class ShadowMan;
class Stage3FeedFigure;
class Stage3Monitor;
class Wall;

// 通常の進行から独立した、研究棟の通信室の試作。
class Stage3Scene final : public Scene
{
public:
    static bool IsRequestedByCommandLine();
    Stage3Scene();
    ~Stage3Scene() override;
    void Update() override;
    void Draw(Camera* camera) override;
    void RenderOffscreen() override;

private:
    enum class Step { Relay, Routing, Test, Testing, Breaker, Release, Exit, Trace, Source, Road, Complete };
    static constexpr std::size_t FeedFrames = 7;
    void EnableTransmissionTest();
    void UpdateObservationRoute(float seconds);
    std::vector<std::string> m_ObjectNames;
    std::vector<Wall*> m_MainLights;
    Player* m_Player = nullptr;
    FuseBox* m_Relay = nullptr;
    FuseBox* m_Log = nullptr;
    std::array<FuseBox*, 3> m_RouteControls{};
    std::size_t m_RouteProgress = 0;
    FuseBox* m_ObservationStart = nullptr;
    FuseBox* m_ObservationEnd = nullptr;
    Door* m_ObservationDoor = nullptr;
    ShadowMan* m_ObservationShadow = nullptr;
    Wall* m_ObservationAlarm = nullptr;
    bool m_ObservationBlocked = false;
    float m_ObservationScare = 0.0f;
    std::array<FuseBox*, 2> m_OutdoorPosts{};
    FuseBox* m_KeepTape = nullptr;
    FuseBox* m_EraseTape = nullptr;
    Door* m_RoadGate = nullptr;
    FuseBox* m_Release = nullptr;
    FuseBox* m_Test = nullptr;
    FuseBox* m_Breaker = nullptr;
    Door* m_Exit = nullptr;
    ForestRain* m_Rain = nullptr;
    Stage3FeedFigure* m_Figure = nullptr;
    Stage3Monitor* m_Monitor = nullptr;
    std::array<Graphics::RenderTexture, FeedFrames> m_Feeds;
    std::size_t m_NextFeed = 0;
    std::size_t m_FeedCount = 0;
    float m_FeedTimer = 0.0f;
    float m_MonitorTime = 0.0f;
    float m_StepTime = 0.0f;
    float m_FootstepTimer = 0.0f;
    float m_RoutingFeedback = 0.0f;
    float m_SourceReadTime = 0.0f;
    Step m_Step = Step::Relay;
    Hud m_Hud;
    InteractionSystem m_Interaction;
};
