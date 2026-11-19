#include "Stage3Scene.h"

#include "Application.h"
#include "Camera.h"
#include "CaptureMode.h"
#include "Door.h"
#include "ForestRain.h"
#include "FuseBox.h"
#include "Game.h"
#include "Ground.h"
#include "LayoutExtras.h"
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
    // 上から見た長方形の範囲（x と z の、両端を含まない範囲）
    struct AreaXZ
    {
        float MinX;
        float MaxX;
        float MinZ;
        float MaxZ;

        bool Contains(const Vector3& position) const
        {
            return position.x > MinX && position.x < MaxX && position.z > MinZ && position.z < MaxZ;
        }
    };

    // --- 場所（stage3_props.txt の壁の位置と合わせている）
    // 非常口の壁の x。これより東（+x）は屋外
    constexpr float OutsideX = 443.0f;
    // 通信室（監視映像を撮り、人影が通る部屋）
    constexpr AreaXZ CommunicationRoom{ 205.0f, 329.0f, 25.0f, 105.0f };
    // 通信室の中で、この z より奥へ入ると監視映像の時間を進め始める
    constexpr float MonitorStartZ = 40.0f;
    // 北の観測区画（迂回回線の通路）
    constexpr float ObservationSectorMinZ = 107.0f;
    constexpr float ObservationSectorMaxZ = 223.0f;
    // 観測区画の西側の起動盤の側（ここへ戻ると、見つかった状態から再挑戦できる）
    constexpr float ObservationRetryMaxX = 177.0f;
    // 観測区画のセンサーが光と走る音に反応する範囲（x）
    constexpr float ObservationSensorMinX = 179.0f;
    constexpr float ObservationSensorMaxX = 267.0f;
    // センサーに気づかれたときにノックの音を鳴らす位置
    constexpr Vector3 ObservationKnockPosition(211.0f, -72.0f, 160.0f);
    // 屋外の送信小屋（雨を止める屋根の下）と、原本の録音を聞ける範囲
    constexpr AreaXZ TransmitterShed{ 632.0f, 708.0f, -44.0f, 44.0f };
    constexpr AreaXZ SourceListeningArea{ 632.0f, 705.0f, -43.0f, 43.0f };
    // 道路の門を越えた所（ここへ着くと3面を終える）
    constexpr float RoadReachedX = 790.0f;
    constexpr float RoadHalfWidth = 34.0f;
    // 非常灯をたどって出口へ向かう間、後ろの足音を鳴らし始める x
    constexpr float LeavingFootstepStartX = 260.0f;
    // 出口へ向かう間の足音は、プレイヤーのこれだけ後ろ（-x）の廊下の中央で鳴らす。通信卓の前では、机の陰で鳴らす
    constexpr float LeavingFootstepBehind = 42.0f;
    constexpr float FootstepHeight = -95.0f;
    constexpr Vector3 DeskFootstepPosition(248.0f, FootstepHeight, 19.0f);

    // --- 時間（秒）
    // 送信を一時停止してから、主電源を操作できるまで
    constexpr float TestSeconds = 2.4f;
    // 監視映像を撮り直す間隔
    constexpr float FeedInterval = 0.30f;
    // 道路に出てから、リザルト画面へ移るまでの秒数
    constexpr float CompleteSeconds = 6.0f;
    // 監視映像の時間がこれを過ぎてから、人影が画面を横切り始める。横切るのにかかる時間
    constexpr float FigureCrossDelay = 0.35f;
    constexpr float FigureCrossSeconds = 1.50f;
    // センサーに見つかったときの警報と人影を出す時間
    constexpr float ObservationScareSeconds = 1.35f;
    // 回線の接続順を間違えたときの案内を出す時間
    constexpr float RoutingMistakeSeconds = 3.2f;
    // 原本の録音を聞き終えて、回収と消去を選べるようになるまで
    constexpr float SourceListenSeconds = 5.5f;
    // 足音を鳴らす間隔（出口へ向かう間と、通信卓の前）
    constexpr float LeavingFootstepInterval = 0.85f;
    constexpr float DeskFootstepInterval = 1.15f;
    // 無線の字幕を出す時間（試験停止・主電源の直後と、道路に出る前）
    constexpr float RadioWarningSeconds = 5.0f;
    constexpr float RadioEndingSeconds = 7.0f;
    // 「通信途絶」の文字が浮かび上がるまで
    constexpr float SignalLostFadeSeconds = 0.7f;

    // --- 監視映像（通信卓に貼る、廊下を向いた補助カメラ）
    constexpr Vector3 FeedEye(191.0f, -53.0f, 1.0f);
    constexpr Vector3 FeedTarget(55.0f, -79.0f, 0.0f);
    constexpr UINT FeedWidth = 320;
    constexpr UINT FeedHeight = 200;
    constexpr float FeedFieldOfView = 0.93f;
    constexpr float FeedAspect = 1.6f;
    constexpr float FeedNear = 1.0f;
    constexpr float FeedFar = 300.0f;
    // 通信卓の画面の半分の幅と高さ
    constexpr float MonitorHalfWidth = 12.0f;
    constexpr float MonitorHalfHeight = 7.5f;
    // 映像を横切る人影の通り道（x・y は一定、z を始点から幅の分だけ進める）
    constexpr Vector3 FigureCrossStart(137.0f, -99.0f, -18.0f);
    constexpr float FigureCrossLength = 36.0f;

    // --- 罰と音
    // センサーに見つかったとき、懐中電灯の電池を最大この量だけ奪う（ただし、この残量より下は奪わない）
    constexpr float ObservationBatteryPenalty = 12.0f;
    constexpr float ObservationBatteryFloor = 8.0f;
    // 屋外の雨の強さ（送信小屋の屋根の下では0）
    constexpr float OutdoorRainIntensity = 0.35f;
    // 無線の雑音の音量（試験中・停電後・屋外・中継箱を調べた後）
    constexpr float RadioStaticVolume = 0.24f;
    constexpr float RadioStaticAfterPowerOff = 0.08f;
    constexpr float RadioStaticOutside = 0.18f;
    constexpr float RadioStaticTraced = 0.34f;
    // センサーに見つかったときの、配管を叩く音（高さと音量）と画面の脈打ち（強さと秒数）
    constexpr float ObservationKnockPitch = 0.66f;
    constexpr float ObservationKnockVolume = 1.2f;
    constexpr float ObservationPulseStrength = 0.35f;
    constexpr float ObservationPulseSeconds = 0.30f;
    // 回線をつなぐ順番を間違えたときの音（高さと音量）
    constexpr float RoutingMistakePitch = 0.78f;
    constexpr float RoutingMistakeVolume = 0.8f;
    // 主電源を落としたときの音量、外へ出たときの雨の音量
    constexpr float PowerOffVolume = 0.72f;
    constexpr float RainVolume = 0.62f;
    // 誰かの足音（高さ・音量・壁がなくても少しこもらせる量）
    constexpr float PhantomFootstepPitch = 0.58f;
    constexpr float PhantomFootstepVolume = 1.1f;
    constexpr float PhantomFootstepOcclusion = 0.12f;

    // --- 3面の画面効果と描画の始めの値：露出（少し暗く）、光の筋、壁の傷み（血の跡などの恐怖の印は出さない）
    constexpr float StartExposure = 0.95f;
    constexpr float VolumetricIntensity = 0.36f;
    constexpr float WallWeathering = 0.85f;
    constexpr float WallHorrorMarks = 0.0f;
    // 部屋の角の暗がり：床と天井の高さ、強さ
    constexpr float RoomFloorY = -100.0f;
    constexpr float RoomCeilingY = -48.5f;
    constexpr float RoomOcclusionStrength = 0.52f;
    // 監視映像だけに使う環境光と、何も映らない所の色
    const Color FeedAmbient(0.22f, 0.26f, 0.24f, 1.0f);
    const Color FeedClearColor(0.012f, 0.021f, 0.019f, 1.0f);
    // 無線の文字に重ねる雑音（送信機を触るな・主電源を落とすな、の警告）
    constexpr float RadioTestWarningNoise = 0.18f;
    constexpr float RadioBreakerWarningNoise = 0.35f;

    bool IsInCommunicationRoom(const Vector3& position)
    {
        return CommunicationRoom.Contains(position);
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
        const VERTEX_3D topLeft{ Vector3(-MonitorHalfWidth, MonitorHalfHeight, 0.0f), normal, white, Vector2(0.0f, 0.0f) };
        const VERTEX_3D topRight{ Vector3(MonitorHalfWidth, MonitorHalfHeight, 0.0f), normal, white, Vector2(1.0f, 0.0f) };
        const VERTEX_3D bottomLeft{ Vector3(-MonitorHalfWidth, -MonitorHalfHeight, 0.0f), normal, white, Vector2(0.0f, 1.0f) };
        const VERTEX_3D bottomRight{ Vector3(MonitorHalfWidth, -MonitorHalfHeight, 0.0f), normal, white, Vector2(1.0f, 1.0f) };
        m_VertexBuffer.Create({ topLeft, topRight, bottomLeft, bottomLeft, topRight, bottomRight });
        m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/surveillanceFeedPS.hlsl");
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
        // 映像を貼る板（三角形2枚）
        constexpr UINT FeedPanelVertexCount = 6;
        context->Draw(FeedPanelVertexCount, 0);
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
    // 配置ファイルの ExtraLight_・ExtraBattery_ で始まる place（何個でも置ける照明と電池。Unity で足したり消したりできる）。
    // 3面は配置ファイルを読み直さないので、置き直し方は覚えずに捨てている
    PropLayoutFile::PlacementBindings extraBindings;
    LayoutExtras::Spawn(builder, layout, extraBindings);

    m_Figure = builder.Create<Stage3FeedFigure>("Stage3FeedFigure");
    m_Figure->SetExternallyControlled(true);
    m_Figure->SetActive(false);
    m_Monitor = builder.Create<Stage3Monitor>("Stage3Monitor");
    place(*m_Monitor, "Stage3Monitor");
    m_Rain = builder.Create<ForestRain>("Stage3Rain");
    m_Rain->SetIntensity(0.0f);
    for (auto& feed : m_Feeds)
    {
        feed.InitRequired(FeedWidth, FeedHeight, "通信室の監視映像");
    }

    game.SetPowerRestored(true);
    game.GetPostProcess()->SetAtmosphere(Effect::PostProcess::DefaultNoiseAmount, Effect::PostProcess::DefaultVignetteStrength);
    game.GetPostProcess()->SetExposure(StartExposure);
    game.GetPostProcess()->SetVolumetricLight(true);
    game.GetPostProcess()->SetVolumetricIntensity(VolumetricIntensity);
    Renderer::SetLoopDepth(0.0f);
    Renderer::SetWallWeathering(WallWeathering, WallHorrorMarks);
    const auto roomFootprints = PropLayoutFile::CollectRoomFootprints(layout);
    Renderer::SetRoomOcclusion(roomFootprints.data(), static_cast<unsigned int>(roomFootprints.size()),
        RoomFloorY, RoomCeilingY, RoomOcclusionStrength);
    m_Player->Update();
    game.GetCamera()->SetCameraDirection(0.0f);
    game.GetCamera()->SetCameraPitch(0.0f);
    m_Hud.Init();
    m_Hud.SetEvidenceVisible(false);

    // 2面の最後の階段を上ってきた続きとして、下の段から3段上がって踊り場に出る演出で始めている
    // （2面の始まりと同じ。少し見上げた視点で、目の高さを12低い所から段ごとに上げ、約1.6秒で普段の操作に戻る）。
    // 踊り場（x=2〜50、z=-82〜-24）の中で、北（+z）へ28だけ進む。自動撮影・計測では、この演出は出さない
    if (!Tools::CaptureMode::IsActive())
    {
        game.GetCamera()->SetCameraPitch(Player::ArrivalStartPitch);
        m_Player->StartStairClimb(Player::MakeArrivalClimb(Vector3(0.0f, 0.0f, 1.0f)));
    }
}

Stage3Scene::~Stage3Scene()
{
    auto& game = *Core::Game::GetInstance();
    game.StopAudioCue(SOUND_CUE_RADIO_STATIC);
    game.StopAudioCue(SOUND_CUE_RAIN);
    game.GetPostProcess()->SetExposure(Effect::PostProcess::DefaultExposure);
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
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_RADIO_STATIC, 1.0f, RadioStaticVolume);
    m_Step = Step::Test;
    m_StepTime = 0.0f;
}

void Stage3Scene::UpdateObservationRoute(float seconds)
{
    const Vector3 position = m_Player->GetPosition();
    const bool inSector = position.z > ObservationSectorMinZ && position.z < ObservationSectorMaxZ;
    const bool armed = m_Step == Step::Routing && m_ObservationStart->IsActivated();
    m_ObservationScare = (std::max)(0.0f, m_ObservationScare - seconds);

    if (armed && inSector && position.x < ObservationRetryMaxX)
    {
        m_ObservationBlocked = false;
    }
    const bool sensorArea = inSector && position.x > ObservationSensorMinX && position.x < ObservationSensorMaxX;
    if (armed && sensorArea && !m_ObservationBlocked &&
        (m_Player->IsFlashlightOn() || m_Player->IsSprinting()))
    {
        m_ObservationBlocked = true;
        m_ObservationScare = ObservationScareSeconds;
        auto& game = *Core::Game::GetInstance();
        game.PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, ObservationKnockPosition, ObservationKnockPitch, ObservationKnockVolume);
        game.GetPostProcess()->TriggerHorrorPulse(ObservationPulseStrength, ObservationPulseSeconds);
        m_Player->AddBattery(-(std::min)(ObservationBatteryPenalty,
            (std::max)(0.0f, m_Player->GetBattery() - ObservationBatteryFloor)));
    }
    m_ObservationEnd->SetManualInteractionAllowed(
        armed && !m_ObservationBlocked && inSector && position.x > ObservationSensorMaxX);
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
        const bool inShed = TransmitterShed.Contains(position);
        m_Rain->SetIntensity(position.x > OutsideX && !inShed ? OutdoorRainIntensity : 0.0f);
    }
    if (IsInCommunicationRoom(position) && m_Step < Step::Release)
    {
        m_FeedTimer -= seconds;
        if (m_MonitorTime > 0.0f || position.z > MonitorStartZ)
        {
            m_MonitorTime += seconds;
        }
    }
    const float crossing = (m_MonitorTime - FigureCrossDelay) / FigureCrossSeconds;
    m_Figure->SetActive(m_Step < Step::Release && crossing > 0.0f && crossing < 1.0f);
    m_Figure->SetPosition(FigureCrossStart.x, FigureCrossStart.y,
        FigureCrossStart.z + FigureCrossLength * (std::clamp)(crossing, 0.0f, 1.0f));

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
                m_RoutingFeedback = RoutingMistakeSeconds;
                game.PlayAudioCue(SOUND_CUE_PIPE_KNOCK, RoutingMistakePitch, RoutingMistakeVolume);
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
            game.PlayAudioCue(SOUND_CUE_RADIO_STATIC, 1.0f, RadioStaticVolume);
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
            game.SetAudioCueVolume(SOUND_CUE_RADIO_STATIC, RadioStaticAfterPowerOff);
            game.PlayAudioCue(SOUND_CUE_POWER, PowerOffVolume);
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
            game.SetAudioCueVolume(SOUND_CUE_RADIO_STATIC, RadioStaticOutside);
            game.PlayAudioCue(SOUND_CUE_RAIN, 1.0f, RainVolume);
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
            game.SetAudioCueVolume(SOUND_CUE_RADIO_STATIC, RadioStaticTraced);
            m_Step = Step::Source;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Source:
        if (SourceListeningArea.Contains(position))
        {
            m_SourceReadTime += seconds;
            if (m_SourceReadTime >= SourceListenSeconds)
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
        if (m_RoadGate->IsOpen() && position.x > RoadReachedX && std::abs(position.z) < RoadHalfWidth)
        {
            m_Step = Step::Complete;
            m_StepTime = 0.0f;
        }
        break;
    case Step::Complete:
        // 「通信途絶」の文字を見せてから、リザルト画面へ移っている（切り替えの要求は一度だけ）
        if (!m_ResultRequested && m_StepTime >= CompleteSeconds)
        {
            m_ResultRequested = true;
            game.RequestSceneChange(SceneName::Result);
        }
        break;
    }

    const bool nearDesk = (m_Step == Step::Test || m_Step == Step::Breaker) && IsInCommunicationRoom(position);
    const bool leaving = (m_Step == Step::Release || m_Step == Step::Exit) &&
        position.x > LeavingFootstepStartX && position.x < OutsideX;
    if (nearDesk || leaving)
    {
        m_FootstepTimer -= seconds;
        if (m_FootstepTimer <= 0.0f)
        {
            const Vector3 footstep = leaving
                ? Vector3(position.x - LeavingFootstepBehind, FootstepHeight, 0.0f)
                : DeskFootstepPosition;
            game.PlayAudioCueAt(SOUND_CUE_FOOTSTEP, footstep, PhantomFootstepPitch, PhantomFootstepVolume, PhantomFootstepOcclusion);
            m_FootstepTimer = leaving ? LeavingFootstepInterval : DeskFootstepInterval;
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
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(FeedFieldOfView, FeedAspect, FeedNear, FeedFar);
    const LIGHT originalLight = Renderer::GetLight();
    LIGHT feedLight = originalLight;
    feedLight.FlashlightEnabled = FALSE;
    feedLight.Ambient = FeedAmbient;

    auto& output = m_Feeds[m_NextFeed];
    output.SetRenderTarget();
    output.Clear(FeedClearColor.R(), FeedClearColor.G(), FeedClearColor.B(), FeedClearColor.A());
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
        if (position.z > ObservationSectorMinZ && m_ObservationStart->IsActivated())
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
    if (m_Step == Step::Routing && m_ObservationStart->IsActivated() && m_Player->GetPosition().z > ObservationSectorMinZ)
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
    else if (m_Step == Step::Test && m_StepTime < RadioWarningSeconds)
    {
        radio.Line = "「そこは触らなくていい。送信機はそのままにしろ。」";
        radio.LineAlpha = 1.0f;
        radio.Noise = RadioTestWarningNoise;
    }
    else if (m_Step == Step::Breaker && m_StepTime < RadioWarningSeconds)
    {
        radio.Line = "「……聞こえるか。主電源は落とすな。」";
        radio.LineAlpha = 1.0f;
        radio.Noise = RadioBreakerWarningNoise;
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
        radio.Line = m_SourceReadTime < SourceListenSeconds ? "『玄関で聞こえる声は、私が入る前に残した録音だ。』"
            : "『声は私のものだが、指示を変えて送り直している。送信機を止めてくれ。』";
        radio.LineAlpha = 1.0f;
        radio.Recording = true;
    }
    else if (m_Step == Step::Road && m_StepTime < RadioEndingSeconds)
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
        radio.SignalLostAlpha = (std::min)(m_StepTime / SignalLostFadeSeconds, 1.0f);
    }
    m_Hud.DrawRadio(radio);

    if (game.IsPaused())
    {
        m_Hud.DrawPause(game.GetBrightnessLevel(), game.GetEffectLevel(), game.GetLookSensitivityLevel(),
            game.GetVolumeLevel(), game.IsGuideEnabled(), game.GetResolutionLevel(),
            game.GetPauseSettingIndex(), 0, game.GetRunTimeSeconds(), game.GetCaughtCount());
    }
}
