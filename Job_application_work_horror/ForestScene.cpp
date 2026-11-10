// ============================================================================
// ファイルの役割: 外の森の場面の、作る・更新する・描く・片付ける処理。
// ForestScene.hで宣言した処理の中身をここで定義している。
// ============================================================================

#include "ForestScene.h"
#include "ForestGrass.h"
#include "ForestRain.h"
#include "IntroScene.h"
#include "LabEntrance.h"
#include "Wall.h"
#include "SkyBackdrop.h"
#include "Application.h"
#include "Game.h"
#include "Ground.h"
#include "Input.h"
#include "Player.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "SceneLayoutBuilder.h"
#include "utility.h"

#include <Windows.h>
#include <shellapi.h>
#include <algorithm>

using namespace DirectX::SimpleMath;

bool ForestScene::IsRequestedByCommandLine()
{
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr)
    {
        return false;
    }
    bool requested = false;
    for (int index = 1; index < argumentCount; ++index)
    {
        if (std::wstring(arguments[index]) == L"--forest")
        {
            requested = true;
        }
    }
    LocalFree(arguments);
    return requested;
}

void ForestScene::ApplyAtmosphere()
{
    Core::Game* game = Core::Game::GetInstance();
    // 夜の森：電力の状態は使わない（灯りは配置ファイルの glow の付いた物だけ）。少し暗めにし、照明の下の霧の光の筋を出している
    game->SetPowerRestored(false);
    game->GetPostProcess()->SetCorridorTension(0.0f);
    game->GetPostProcess()->SetExposure(0.92f);
    game->GetPostProcess()->SetAtmosphere(0.20f, 0.62f);
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetVolumetricIntensity(0.42f);
    // 地面に建物の床の目地やひびが出ないよう、壁と床の古さは描かない。屋外なので、部屋の角の暗がりも使わない
    Renderer::SetWallWeathering(0.0f);
    Renderer::SetRoomOcclusion(nullptr, 0, 0.0f, 0.0f, 0.0f);
    Renderer::SetLoopDepth(0.0f);
}

void ForestScene::RestoreAtmosphere()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }
    game->GetPostProcess()->SetAtmosphere(0.18f, 0.55f);
    game->GetPostProcess()->SetExposure(1.0f);
    game->GetPostProcess()->SetVolumetricLight(false);
    game->GetPostProcess()->SetVolumetricIntensity(0.58f);
}

PropLayout ForestScene::LoadLayout()
{
    PropLayout layout = PropLayoutFile::Load(PropLayoutPath);
    if (!layout.IsValid())
    {
        utility::ReportFatalError("森の配置を読めませんでした（" + std::to_string(layout.ErrorLine) + "行目）: " + layout.Error);
    }
    return layout;
}

ForestScene::World ForestScene::BuildWorld(SceneLayoutBuilder& builder, const PropLayout& layout)
{
    World world;
    // 空の霧（地平線の線を消す）。手前に描く物を上書きしないよう、最初に作って最初に描いている
    builder.Create<SkyBackdrop>("ForestSky");
    world.PlayerObject = builder.Create<Player>("Player");
    PropLayoutFile::ApplyPlacement(*world.PlayerObject, PropLayoutFile::GetPlacement(layout, "Player"));

    // 地面（1面・2面と同じ濡れた床。水たまりは床全体のマス目に乱数で置かれ、雨のあとの夜の森になる）
    world.GroundObject = builder.Create<Ground>("ForestGround");
    PropLayoutFile::ApplyPlacement(*world.GroundObject, PropLayoutFile::GetPlacement(layout, "Ground"));

    // 木・岩・倒木・茂み・研究所・灯り（prop 行）
    world.Props = PropLayoutFile::Spawn(builder, layout);
    // 木や岩を置いた後で草を作り、その足元と小道を避けて植える。
    world.Grass = builder.Create<ForestGrass>("ForestGrass");
    PropLayoutFile::ApplyPlacement(*world.Grass, PropLayoutFile::GetPlacement(layout, "ForestGrass"));
    // 雨：加算合成で深度を書かないので、ほかの物を全部描いた後に描くよう、最後に作っている。
    // 水たまりには、同じ雨の輪を波のシミュレーションで広げている
    // （RainEnabled が false のときは作らない）
    if constexpr (RainEnabled)
    {
        ForestRain* rain = builder.Create<ForestRain>("ForestRain");
        rain->SetGroundHeight(world.GroundObject->GetPosition().y);
        rain->SetIntensity(RainStrength);
        world.GroundObject->SetRainIntensity(RainStrength);
    }
    return world;
}

ForestScene::ForestScene()
{
    Core::Game* game = Core::Game::GetInstance();
    ApplyAtmosphere();
    const PropLayout layout = LoadLayout();
    SceneLayoutBuilder builder(*game, m_ObjectNames);
    World world = BuildWorld(builder, layout);
    m_Player = world.PlayerObject;
    m_Grass = world.Grass;
    m_LayoutProps = std::move(world.Props);

    // 研究所の玄関：扉（配置ファイルの LabDoor）の手前に、「研究所に入る」と調べられる点を置いている
    m_Entrance = builder.Create<LabEntrance>("LabEntrance");
    Vector3 doorPosition(0.0f, -77.0f, 297.4f);
    for (const auto& [name, wall] : m_LayoutProps)
    {
        if (name == "LabDoor" && wall != nullptr)
        {
            doorPosition = wall->GetPosition();
        }
    }
    m_Entrance->SetPosition(doorPosition - Vector3(0.0f, 0.0f, 2.0f));

    // 配置を読み直したときに置き直す物（地面と草の範囲）
    Ground* ground = world.GroundObject;
    m_PlacementBindings.emplace_back("Ground", [ground](const PropPlacement& placement)
    {
        PropLayoutFile::ApplyPlacement(*ground, placement);
    });
    m_PlacementBindings.emplace_back("ForestGrass", [this](const PropPlacement& placement)
    {
        PropLayoutFile::ApplyPlacement(*m_Grass, placement);
    });

    // カメラの位置を最初から合わせるため、プレイヤーを一度更新している
    m_Player->Update();
    m_Hud.Init();
    // 森には記録（残された資料）を置いていないので、右上の「記録 0 / 4」は出さない
    m_Hud.SetEvidenceVisible(false);
}

ForestScene::~ForestScene()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }
    m_Radio.Stop();
    RestoreAtmosphere();
    game->GetPostProcess()->SetSignalInterference(0.0f);
    for (const std::string& name : m_ObjectNames)
    {
        game->DestroyObj(name);
    }
    m_Hud.Uninit();
}

void ForestScene::ReloadPropLayout()
{
    const PropLayout layout = PropLayoutFile::Load(PropLayoutPath);
    if (!layout.IsValid())
    {
        m_Notice = "配置のファイルの" + std::to_string(layout.ErrorLine) + "行目が読めない " + layout.Error;
        m_NoticeTimer = 6.0f;
        return;
    }
    SceneLayoutBuilder builder(*Core::Game::GetInstance(), m_ObjectNames);
    PropLayoutFile::Reload(builder, layout, m_LayoutProps);
    // プレイヤーの位置は、歩いている途中の状態なので変えない
    const int placed = PropLayoutFile::ReapplyPlacements(layout, m_PlacementBindings);
    if (m_Grass != nullptr) m_Grass->Rebuild();
    m_Notice = "配置のファイルを読み直した（小物 " + std::to_string(layout.Props.size()) + "個・置き場所 " +
        std::to_string(placed) + "個）";
    m_NoticeTimer = 3.0f;
}

void ForestScene::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    Core::Game* game = Core::Game::GetInstance();
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    if (m_Player == nullptr)
    {
        return;
    }
    // 街灯のちらつき・後ろの街灯が消える・研究所の窓の明かりが消える・草むらを何かが通る（ForestEvents）
    const float fear = m_Events.Update(deltaTime, m_Player->GetPosition(), m_LayoutProps);
    game->GetPostProcess()->SetFearVision(fear, fear > m_LastFear + 0.1f);
    m_LastFear = fear;
    // 草むらを通る「見えない何か」の足元の草を、草の Compute Shader で押し倒している
    if (m_Grass != nullptr)
    {
        m_Grass->SetUnseenWalker(m_Events.GetWalkerPosition(), 10.0f, m_Events.GetWalkerStrength());
    }

    // 研究棟からの無線：小道を進むと、受信記録が1行ずつ届き、研究所が見えてくると途絶える。行が届く瞬間に画面も乱れる
    const float radioNoise = m_EnterTimer < 0.0f ? m_Radio.Update(deltaTime, m_Player->GetPosition()) : 0.0f;
    game->GetPostProcess()->SetSignalInterference(radioNoise * 0.7f);
    game->GetPostProcess()->SetPresenceStatic(radioNoise * 0.45f);

    if (m_EnterTimer < 0.0f)
    {
        // 視線の先の調べられる物（研究所の玄関）を選び、E（A）で調べている
        m_InteractionSystem.Update(*m_Player);
        if (m_Entrance != nullptr && m_Entrance->WasEntered())
        {
            // 玄関を開けて入る：扉の音を鳴らし、暗転してから、目的の画面（IntroScene）を挟んで1面を始める
            m_EnterTimer = 0.0f;
            m_Radio.Stop();
            game->PlayAudioCue(SOUND_CUE_DOOR);
        }
    }
    else
    {
        m_EnterTimer += deltaTime;
        if (m_EnterTimer >= EnterFadeSeconds && !m_SceneChangeRequested)
        {
            m_SceneChangeRequested = true;
            IntroScene::SetStartAtMission(true);
            game->RequestSceneChange(SceneName::Intro);
        }
    }
#if defined(_DEBUG)
    // Debug版だけ：F5 を押したとき、または配置のファイルが書き換わったとき（Unity で書き出したときなど）に読み直している
    if (Input::GetKeyTrigger(VK_F5) || m_LayoutWatcher.HasChanged(PropLayoutPath, deltaTime))
    {
        ReloadPropLayout();
    }
#endif
}

void ForestScene::Draw(Camera* camera)
{
    (void)camera;
    if (m_Player == nullptr)
    {
        return;
    }
    // 目的：無線が途絶えるまでは研究所へ向かい、途絶えたら玄関を調べる
    std::string_view objective = m_Radio.IsSignalLost() ? "研究所の玄関を調べて、中に入る" : "小道を北へ進み、研究所へ向かう";
    if (m_NoticeTimer > 0.0f)
    {
        objective = m_Notice;
    }
    m_Hud.Draw(*m_Player, -1, m_EnterTimer < 0.0f ? m_InteractionSystem.GetPrompt() : std::string_view{}, objective);
    Hud::RadioView radio;
    m_Radio.FillView(radio);
    m_Hud.DrawRadio(radio);
    // 玄関から入るときの暗転
    if (m_EnterTimer >= 0.0f)
    {
        const float fade = (std::min)(1.0f, m_EnterTimer / (EnterFadeSeconds * 0.85f));
        m_Hud.DrawBlink(fade);
    }
    // ポーズ中はポーズメニューを重ねている（ESC・P・START で開くのは Game::Update。ここで描かないと、止まっただけに見える）
    Core::Game* game = Core::Game::GetInstance();
    if (game->IsPaused())
    {
        m_Hud.DrawPause(
            game->GetBrightnessLevel(),
            game->GetEffectLevel(),
            game->GetLookSensitivityLevel(),
            game->GetVolumeLevel(),
            game->IsGuideEnabled(),
            game->GetResolutionLevel(),
            game->GetPauseSettingIndex(),
            0,
            game->GetRunTimeSeconds(),
            game->GetCaughtCount());
    }
}
