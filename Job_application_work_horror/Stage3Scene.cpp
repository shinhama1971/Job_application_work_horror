#include "Stage3Scene.h"

#include "Application.h"
#include "Door.h"
#include "ForestRain.h"
#include "FuseBox.h"
#include "Game.h"
#include "Ground.h"
#include "Player.h"
#include "PostProcess.h"
#include "PropLayoutFile.h"
#include "SceneLayoutBuilder.h"
#include "ShadowMan.h"
#include "SkyBackdrop.h"
#include "Wall.h"

#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float OutsideX = 443.0f;
    constexpr float TestSeconds = 2.4f;
    constexpr float FeedInterval = 0.30f;
    constexpr Vector3 FeedEye(191.0f, -53.0f, 1.0f);
    constexpr Vector3 FeedTarget(55.0f, -79.0f, 0.0f);

    bool IsInCommunicationRoom(const Vector3& position)
    {
        return position.x > 205.0f && position.x < 329.0f && position.z > 25.0f && position.z < 105.0f;
    }
}

// 通信卓の前面にだけ、監視映像を貼る。
class Stage3Monitor final : public Object
{
public:
    void Init() override
    {
        const Vector3 normal(0.0f, 0.0f, -1.0f);
        const Color white(1.0f, 1.0f, 1.0f, 1.0f);
        const VERTEX_3D topLeft{ Vector3(-12.0f, 7.5f, 0.0f), normal, white, Vector2(0.0f, 0.0f) };
        const VERTEX_3D topRight{ Vector3(12.0f, 7.5f, 0.0f), normal, white, Vector2(1.0f, 0.0f) };
        const VERTEX_3D bottomLeft{ Vector3(-12.0f, -7.5f, 0.0f), normal, white, Vector2(0.0f, 1.0f) };
        const VERTEX_3D bottomRight{ Vector3(12.0f, -7.5f, 0.0f), normal, white, Vector2(1.0f, 1.0f) };
        m_VertexBuffer.Create({ topLeft, topRight, bottomLeft, bottomLeft, topRight, bottomRight });
        m_Shader.Create("shader/litTextureVS.hlsl", "shader/surveillanceFeedPS.hlsl");
    }
    void Update() override {}
    void Uninit() override {}
    bool DrawsInAuxiliaryView() const override { return false; }
    void SetFeed(ID3D11ShaderResourceView* feed) { m_Feed = feed; }
    void Draw(Camera* camera) override
    {
        if (m_Feed == nullptr)
        {
            return;
        }
        camera->SetCamera();
        Matrix world = Matrix::CreateTranslation(m_Position);
        Renderer::SetWorldMatrix(&world);
        auto* context = Renderer::GetDeviceContext();
        m_Shader.SetGPU();
        m_VertexBuffer.SetGPU();
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->PSSetShaderResources(0, 1, &m_Feed);
        context->Draw(6, 0);
        ID3D11ShaderResourceView* none = nullptr;
        context->PSSetShaderResources(0, 1, &none);
    }

private:
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    ID3D11ShaderResourceView* m_Feed = nullptr;
};

// 影の出現時間は通常の更新で進め、映像を撮る間だけ描く。
class Stage3FeedFigure final : public ShadowMan
{
public:
    void SetFeedPass(bool enabled) { m_FeedPass = enabled; }
    bool ContributesToReflection() const override { return false; }

    void Draw(Camera* camera) override
    {
        if (m_FeedPass)
        {
            ShadowMan::Draw(camera);
        }
    }

private:
    bool m_FeedPass = false;
};

bool Stage3Scene::IsRequestedByCommandLine()
{
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr)
    {
        return false;
    }
    bool requested = false;
    for (int index = 1; index < count; ++index)
    {
        requested |= std::wstring_view(arguments[index]) == L"--stage3";
    }
    LocalFree(arguments);
    return requested;
}

Stage3Scene::Stage3Scene()
{
    auto& game = *Core::Game::GetInstance();
    SceneLayoutBuilder builder(game, m_ObjectNames);
    const PropLayout layout = PropLayoutFile::Load("assets/layout/stage3_props.txt");
    if (!layout.IsValid())
    {
        utility::ReportFatalError("3面の配置を読めませんでした（" + std::to_string(layout.ErrorLine) +
            "行目）: " + layout.Error);
    }

    const auto place = [&layout](auto& object, const char* name)
    {
        PropLayoutFile::ApplyPlacement(object, PropLayoutFile::GetPlacement(layout, name));
    };
    builder.Create<SkyBackdrop>("Stage3Sky");
    m_Player = builder.Create<Player>("Player");
    place(*m_Player, "Player");
    Ground* ground = builder.Create<Ground>("Stage3Ground");
    place(*ground, "Stage3Ground");
    for (const auto& [name, object] : PropLayoutFile::Spawn(builder, layout))
    {
        if (name == "ObservationAlarm")
        {
            m_ObservationAlarm = object;
            m_ObservationAlarm->SetEmissionScale(0.0f);
        }
        if (name.starts_with("MainLamp"))
        {
            m_MainLights.push_back(object);
        }
    }

    const auto makeSwitch = [&builder, &place](const char* name, const char* prompt, bool enabled)
    {
        FuseBox* control = builder.Create<FuseBox>(name);
        place(*control, name);
        control->SetManualControl(prompt);
        control->SetManualInteractionAllowed(enabled);
        return control;
    };
    m_Relay = makeSwitch("Stage3Relay", "中継ラックの表示を調べる", true);
    m_ObservationStart = makeSwitch("Stage3ObservationStart", "観測区画の迂回回線を起動する", false);
    m_ObservationEnd = makeSwitch("Stage3ObservationEnd", "迂回回線を通信室へつなぐ", false);
    m_ObservationDoor = builder.Create<Door>("Stage3ObservationDoor");
    place(*m_ObservationDoor, "Stage3ObservationDoor");
    m_ObservationDoor->SetLocked(true);
    m_ObservationShadow = builder.Create<ShadowMan>("Stage3ObservationShadow");
    place(*m_ObservationShadow, "Stage3ObservationShadow");
    m_ObservationShadow->SetExternallyControlled(true);
    m_ObservationShadow->SetActive(false);
    m_Log = makeSwitch("Stage3Log", "保守記録を読む", true);
    // 保守記録の手順と同じ順に並べる。
    m_RouteControls = {
        makeSwitch("Stage3MonitorChannel", "監視回線を接続する", false),
        makeSwitch("Stage3Receive", "受信回線を接続する", false),
        makeSwitch("Stage3Transmit", "送信回線を接続する", false)
    };
    m_OutdoorPosts = {
        makeSwitch("Stage3PostNorth", "林の北側の中継箱を調べる", false),
        makeSwitch("Stage3PostSouth", "林の南側の中継箱を調べる", false)
    };
    m_KeepTape = makeSwitch("Stage3KeepTape", "原本を持ち出して送信を止める", false);
    m_EraseTape = makeSwitch("Stage3EraseTape", "原本を消去して送信を止める", false);
    m_RoadGate = builder.Create<Door>("Stage3RoadGate");
    place(*m_RoadGate, "Stage3RoadGate");
    m_RoadGate->SetLocked(true);
    m_Release = makeSwitch("Stage3Release", "非常口の手動解錠を引く", false);
    m_Test = makeSwitch("Stage3Test", "送信を一時停止する", false);
    m_Breaker = makeSwitch("Stage3Breaker", "送信機の主電源を落とす", false);
    m_Exit = builder.Create<Door>("Stage3Exit");
    place(*m_Exit, "Stage3Exit");
    m_Exit->SetLocked(true);

    m_Figure = builder.Create<Stage3FeedFigure>("Stage3FeedFigure");
    m_Figure->SetExternallyControlled(true);
    m_Figure->SetActive(false);
    m_Monitor = builder.Create<Stage3Monitor>("Stage3Monitor");
    place(*m_Monitor, "Stage3Monitor");
    m_Rain = builder.Create<ForestRain>("Stage3Rain");
    m_Rain->SetIntensity(0.0f);
    for (auto& feed : m_Feeds)
    {
        feed.Init(320, 200);
    }

    game.SetPowerRestored(true);
    game.GetPostProcess()->SetAtmosphere(0.18f, 0.55f);
    game.GetPostProcess()->SetExposure(0.95f);
    game.GetPostProcess()->SetVolumetricLight(true);
    game.GetPostProcess()->SetVolumetricIntensity(0.36f);
    Renderer::SetLoopDepth(0.0f);
    Renderer::SetWallWeathering(0.85f, 0.0f);
    const auto roomFootprints = PropLayoutFile::CollectRoomFootprints(layout);
    Renderer::SetRoomOcclusion(roomFootprints.data(), static_cast<unsigned int>(roomFootprints.size()),
        -100.0f, -48.5f, 0.52f);
    m_Player->Update();
    game.GetCamera()->SetCameraDirection(0.0f);
    game.GetCamera()->SetCameraPitch(0.0f);
    m_Hud.Init();
    m_Hud.SetEvidenceVisible(false);
}

Stage3Scene::~Stage3Scene()
{
    auto& game = *Core::Game::GetInstance();
    game.StopAudioCue(SOUND_CUE_RADIO_STATIC);
    game.StopAudioCue(SOUND_CUE_RAIN);
    game.GetPostProcess()->SetExposure(1.0f);
    game.GetPostProcess()->SetVolumetricLight(false);
    game.GetPostProcess()->SetSignalInterference(0.0f);
    Renderer::SetWallWeathering(0.0f);
    Renderer::SetRoomOcclusion(nullptr, 0, 0.0f, 0.0f, 0.0f);
    for (const auto& name : m_ObjectNames)
    {
        game.DestroyObj(name);
    }
    m_Hud.Uninit();
}

void Stage3Scene::EnableTransmissionTest()
{
    for (FuseBox* control : m_RouteControls)
    {
        control->SetManualInteractionAllowed(false);
    }
    m_ObservationStart->SetManualInteractionAllowed(false);
    m_ObservationEnd->SetManualInteractionAllowed(false);
    m_ObservationDoor->SetLocked(false);
    m_Test->SetManualInteractionAllowed(true);
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_RADIO_STATIC, 1.0f, 0.24f);
    m_Step = Step::Test;
    m_StepTime = 0.0f;
}

void Stage3Scene::UpdateObservationRoute(float seconds)
{
    const Vector3 position = m_Player->GetPosition();
    const bool inSector = position.z > 107.0f && position.z < 223.0f;
    const bool armed = m_Step == Step::Routing && m_ObservationStart->IsActivated();
    m_ObservationScare = (std::max)(0.0f, m_ObservationScare - seconds);

    if (armed && inSector && position.x < 177.0f)
    {
        m_ObservationBlocked = false;
    }
    const bool sensorArea = inSector && position.x > 179.0f && position.x < 267.0f;
    if (armed && sensorArea && !m_ObservationBlocked &&
        (m_Player->IsFlashlightOn() || m_Player->IsSprinting()))
    {
        m_ObservationBlocked = true;
        m_ObservationScare = 1.35f;
        auto& game = *Core::Game::GetInstance();
        game.PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, Vector3(211.0f, -72.0f, 160.0f), 0.66f, 1.2f);
        game.GetPostProcess()->TriggerHorrorPulse(0.35f, 0.30f);
        m_Player->AddBattery(-(std::min)(12.0f, (std::max)(0.0f, m_Player->GetBattery() - 8.0f)));
    }
    m_ObservationEnd->SetManualInteractionAllowed(armed && !m_ObservationBlocked && inSector && position.x > 267.0f);
    if (m_ObservationEnd->IsActivated() && m_Step == Step::Routing)
    {
        EnableTransmissionTest();
    }
    m_ObservationShadow->SetActive(m_ObservationScare > 0.0f);
    if (m_ObservationAlarm != nullptr)
    {
        m_ObservationAlarm->SetEmissionScale(m_ObservationScare > 0.0f ? 1.0f : 0.0f);
    }
}

void Stage3Scene::Update()
{
    auto& game = *Core::Game::GetInstance();
    const float seconds = Application::GetDeltaTime();
    const Vector3 position = m_Player->GetPosition();
    m_StepTime += seconds;
    if (m_Step >= Step::Trace)
    {
        const bool inShed = position.x > 632.0f && position.x < 708.0f && std::abs(position.z) < 44.0f;
        m_Rain->SetIntensity(position.x > OutsideX && !inShed ? 0.35f : 0.0f);
    }
    if (IsInCommunicationRoom(position) && m_Step < Step::Release)
    {
        m_FeedTimer -= seconds;
        if (m_MonitorTime > 0.0f || position.z > 40.0f)
        {
            m_MonitorTime += seconds;
        }
    }
    const float crossing = (m_MonitorTime - 0.35f) / 1.50f;
    m_Figure->SetActive(m_Step < Step::Release && crossing > 0.0f && crossing < 1.0f);
    m_Figure->SetPosition(137.0f, -99.0f, -18.0f + 36.0f * (std::clamp)(crossing, 0.0f, 1.0f));

    m_RoutingFeedback = (std::max)(0.0f, m_RoutingFeedback - seconds);
    m_Interaction.Update(*m_Player);
    UpdateObservationRoute(seconds);
    switch (m_Step)
    {
    case Step::Relay:
        if (m_Relay->IsActivated())
        {
            for (FuseBox* control : m_RouteControls)
            {
                control->SetManualInteractionAllowed(true);
            }
            m_ObservationStart->SetManualInteractionAllowed(true);
            m_Step = Step::Routing;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Routing:
        // 既に接続した回線を飛ばし、今回押したスイッチだけを判定する。
        for (std::size_t index = m_RouteProgress; index < m_RouteControls.size(); ++index)
        {
            if (!m_RouteControls[index]->IsActivated())
            {
                continue;
            }
            if (index != m_RouteProgress)
            {
                for (FuseBox* control : m_RouteControls)
                {
                    control->ResetActivation();
                }
                m_RouteProgress = 0;
                m_RoutingFeedback = 3.2f;
                game.PlayAudioCue(SOUND_CUE_PIPE_KNOCK, 0.78f, 0.8f);
                break;
            }
            ++m_RouteProgress;
            m_RoutingFeedback = 0.0f;
            if (m_RouteProgress == m_RouteControls.size())
            {
                EnableTransmissionTest();
            }
            break;
        }
        break;
    case Step::Test:
        if (m_Test->IsActivated())
        {
            game.StopAudioCue(SOUND_CUE_RADIO_STATIC);
            m_Step = Step::Testing;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Testing:
        if (m_StepTime >= TestSeconds)
        {
            m_Breaker->SetManualInteractionAllowed(true);
            game.PlayAudioCue(SOUND_CUE_RADIO_STATIC, 1.0f, 0.24f);
            m_Step = Step::Breaker;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Breaker:
        if (m_Breaker->IsActivated())
        {
            for (Wall* light : m_MainLights)
            {
                light->SetEmissionScale(0.0f);
            }
            m_Monitor->SetFeed(nullptr);
            m_Release->SetManualInteractionAllowed(true);
            game.SetAudioCueVolume(SOUND_CUE_RADIO_STATIC, 0.08f);
            game.PlayAudioCue(SOUND_CUE_POWER, 0.72f);
            m_Step = Step::Release;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Release:
        if (m_Release->IsActivated())
        {
            m_Exit->SetLocked(false);
            m_Step = Step::Exit;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Exit:
        if (m_Exit->IsOpen() && position.x > OutsideX)
        {
            game.SetAudioCueVolume(SOUND_CUE_RADIO_STATIC, 0.18f);
            game.PlayAudioCue(SOUND_CUE_RAIN, 1.0f, 0.62f);
            for (FuseBox* post : m_OutdoorPosts)
            {
                post->SetManualInteractionAllowed(true);
            }
            m_Step = Step::Trace;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Trace:
        if (m_OutdoorPosts[0]->IsActivated() && m_OutdoorPosts[1]->IsActivated())
        {
            game.SetAudioCueVolume(SOUND_CUE_RADIO_STATIC, 0.34f);
            m_Step = Step::Source;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Source:
        if (position.x > 632.0f && position.x < 705.0f && std::abs(position.z) < 43.0f)
        {
            m_SourceReadTime += seconds;
            if (m_SourceReadTime >= 5.5f)
            {
                m_KeepTape->SetManualInteractionAllowed(true);
                m_EraseTape->SetManualInteractionAllowed(true);
            }
        }
        if (m_KeepTape->IsActivated() || m_EraseTape->IsActivated())
        {
            m_KeepTape->SetManualInteractionAllowed(false);
            m_EraseTape->SetManualInteractionAllowed(false);
            m_RoadGate->SetLocked(false);
            game.StopAudioCue(SOUND_CUE_RADIO_STATIC);
            m_Step = Step::Road;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Road:
        if (m_RoadGate->IsOpen() && position.x > 790.0f && std::abs(position.z) < 34.0f)
        {
            m_Step = Step::Complete;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Complete:
        break;
    }

    const bool nearDesk = (m_Step == Step::Test || m_Step == Step::Breaker) && IsInCommunicationRoom(position);
    const bool leaving = (m_Step == Step::Release || m_Step == Step::Exit) && position.x > 260.0f && position.x < OutsideX;
    if (nearDesk || leaving)
    {
        m_FootstepTimer -= seconds;
        if (m_FootstepTimer <= 0.0f)
        {
            const Vector3 footstep = leaving ? Vector3(position.x - 42.0f, -95.0f, 0.0f)
                : Vector3(248.0f, -95.0f, 19.0f);
            game.PlayAudioCueAt(SOUND_CUE_FOOTSTEP, footstep, 0.58f, 1.1f, 0.12f);
            m_FootstepTimer = leaving ? 0.85f : 1.15f;
        }
    }
}

void Stage3Scene::RenderOffscreen()
{
    if (!IsInCommunicationRoom(m_Player->GetPosition()) || m_Step >= Step::Release ||
        m_FeedTimer > 0.0f || Core::Game::GetInstance()->IsPaused())
    {
        return;
    }

    m_FeedTimer = FeedInterval;
    auto& game = *Core::Game::GetInstance();
    Camera& camera = *game.GetCamera();
    const Matrix view = Matrix::CreateLookAt(FeedEye, FeedTarget, Vector3::Up);
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(0.93f, 1.6f, 1.0f, 300.0f);
    const LIGHT originalLight = Renderer::GetLight();
    LIGHT feedLight = originalLight;
    feedLight.FlashlightEnabled = FALSE;
    feedLight.Ambient = Color(0.22f, 0.26f, 0.24f, 1.0f);

    auto& output = m_Feeds[m_NextFeed];
    output.SetRenderTarget();
    output.Clear(0.012f, 0.021f, 0.019f, 1.0f);
    camera.SetOverrideMatrices(view, projection);
    Renderer::SetLight(feedLight);
    m_Figure->SetFeedPass(true);
    game.DrawWorldForAuxiliaryCamera(camera);
    m_Figure->SetFeedPass(false);
    camera.ClearOverrideMatrices();
    Renderer::SetLight(originalLight);
    Renderer::SetBackBufferRenderTarget();

    m_NextFeed = (m_NextFeed + 1) % FeedFrames;
    m_FeedCount = (std::min)(m_FeedCount + 1, FeedFrames);
    const std::size_t oldest = m_FeedCount == FeedFrames ? m_NextFeed : 0;
    m_Monitor->SetFeed(m_Feeds[oldest].GetSRV());
}

void Stage3Scene::Draw(Camera*)
{
    constexpr const char* objectives[] = {
        "床のケーブルをたどり、中継ラックを調べる",
        "南の回線室か、北の観測区画から通信室の回線をつなぐ",
        "通信室へ進み、机の試験スイッチを押す",
        "送信停止中",
        "壁の主電源を落とす",
        "非常灯をたどり、出口脇の手動解錠を引く",
        "非常口を開けて、外へ出る",
        "林に出ても無線が聞こえる。分岐の両側で中継箱を調べる",
        "白い灯りの送信小屋で、調査員の原本をどうするか決める",
        "送信小屋から配線をたどり、道路の門を開ける",
        "道路に出た"
    };
    auto& game = *Core::Game::GetInstance();
    std::string_view objective = objectives[static_cast<std::size_t>(m_Step)];
    if (m_Step == Step::Routing)
    {
        const Vector3 position = m_Player->GetPosition();
        if (position.z > 107.0f && m_ObservationStart->IsActivated())
        {
            objective = m_ObservationBlocked ? "見つかった。西側の起動盤まで戻ってやり直す"
                : "ライトを消し、走らずに緑の灯りの端末まで進む";
        }
        else if (m_RoutingFeedback > 0.0f)
        {
            objective = "接続順が違う。回線を戻した。記録室で手順を確認できる";
        }
        else if (m_RouteProgress > 0)
        {
            objective = m_RouteProgress == 1 ? "回線 接続済み 1 / 3" : "回線 接続済み 2 / 3";
        }
        else if (m_Log->IsActivated())
        {
            objective = "保守記録　映像を確認し、外の声を受けてから、こちらの声を送る";
        }
        else
        {
            objective = "南は回線室、北は観測区画。どちらからでも進める";
        }
    }
    else if (m_Step == Step::Trace)
    {
        const bool northChecked = m_OutdoorPosts[0]->IsActivated();
        const bool southChecked = m_OutdoorPosts[1]->IsActivated();
        if (northChecked || southChecked)
        {
            objective = northChecked ? "南の橙の灯りへ。もう一つの中継箱を調べる"
                : "北の緑の灯りへ。もう一つの中継箱を調べる";
        }
    }
    m_Hud.Draw(*m_Player, -1, m_Interaction.GetPrompt(), objective);
    Hud::RadioView radio;
    radio.Time = m_StepTime;
    radio.Header = "受信　——　第3研究棟";
    if (m_Step == Step::Routing && m_ObservationStart->IsActivated() && m_Player->GetPosition().z > 107.0f)
    {
        radio.Header = "観測区画　——　保守用迂回回線";
        radio.Line = "『センサーは強い光と走る音に反応する。消灯し、歩いて通過。』";
        radio.LineAlpha = 1.0f;
        radio.Recording = true;
    }
    else if (m_Step == Step::Routing && m_Log->IsActivated() && m_RouteProgress == 0 && m_RoutingFeedback <= 0.0f)
    {
        radio.Header = "保守記録　——　第3研究棟";
        radio.Line = "『まず映像を確認。外の声を受け取ってから、こちらの声を送る。』";
        radio.LineAlpha = 1.0f;
        radio.Recording = true;
    }
    else if (m_Step == Step::Test && m_StepTime < 5.0f)
    {
        radio.Line = "「そこは触らなくていい。送信機はそのままにしろ。」";
        radio.LineAlpha = 1.0f;
        radio.Noise = 0.18f;
    }
    else if (m_Step == Step::Breaker && m_StepTime < 5.0f)
    {
        radio.Line = "「……聞こえるか。主電源は落とすな。」";
        radio.LineAlpha = 1.0f;
        radio.Noise = 0.35f;
    }
    else if (m_Step == Step::Trace)
    {
        const bool northChecked = m_OutdoorPosts[0]->IsActivated();
        const bool southChecked = m_OutdoorPosts[1]->IsActivated();
        radio.Line = northChecked ? "『北側は録音を受信している。ここでは誰も話していない。』"
            : (southChecked ? "『南側は屋内停電後も送信中。配線は屋外の小屋へ。』"
                            : "「……外は危険だ。研究棟へ戻れ。私が案内する。」");
        radio.LineAlpha = 1.0f;
        radio.Recording = northChecked || southChecked;
    }
    else if (m_Step == Step::Source && m_SourceReadTime <= 0.0f)
    {
        radio.Line = "『北は受信のみ。南から送信機へ電力が続いている。』";
        radio.LineAlpha = 1.0f;
        radio.Recording = true;
    }
    else if (m_Step == Step::Source)
    {
        radio.Header = "原本　——　調査員の最後の記録";
        radio.Line = m_SourceReadTime < 5.5f ? "『玄関で聞こえる声は、私が入る前に残した録音だ。』"
            : "『声は私のものだが、指示を変えて送り直している。送信機を止めてくれ。』";
        radio.LineAlpha = 1.0f;
        radio.Recording = true;
    }
    else if (m_Step == Step::Road && m_StepTime < 7.0f)
    {
        radio.Header = "送信停止";
        radio.Line = m_KeepTape->IsActivated() ? "『原本を回収した。今度は、こちらから伝えられる。』"
            : "『原本を消去した。もう、この声に呼び戻される人はいない。』";
        radio.LineAlpha = 1.0f;
        radio.Recording = true;
    }
    else if (m_Step == Step::Complete)
    {
        radio.SignalLost = m_KeepTape->IsActivated() ? "通信途絶　原本回収" : "通信途絶　原本消去";
        radio.SignalLostAlpha = (std::min)(m_StepTime / 0.7f, 1.0f);
    }
    m_Hud.DrawRadio(radio);

    if (game.IsPaused())
    {
        m_Hud.DrawPause(game.GetBrightnessLevel(), game.GetEffectLevel(), game.GetLookSensitivityLevel(),
            game.GetVolumeLevel(), game.IsGuideEnabled(), game.GetResolutionLevel(),
            game.GetPauseSettingIndex(), 0, game.GetRunTimeSeconds(), game.GetCaughtCount());
    }
}
