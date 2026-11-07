// ============================================================================
// ファイルの役割: 外の森（試作）の場面の、作る・更新する・描く・片付ける処理。
// ForestScene.hで宣言した処理の中身をここで定義している。
// ============================================================================

#include "ForestScene.h"
#include "ForestGrass.h"
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

ForestScene::ForestScene()
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

    const PropLayout layout = PropLayoutFile::Load(PropLayoutPath);
    if (!layout.IsValid())
    {
        utility::ReportFatalError("森の配置を読めませんでした（" + std::to_string(layout.ErrorLine) + "行目）: " + layout.Error);
    }
    SceneLayoutBuilder builder(*game, m_ObjectNames);

    // 空の霧（地平線の線を消す）。手前に描く物を上書きしないよう、最初に作って最初に描いている
    builder.Create<SkyBackdrop>("ForestSky");
    m_Player = builder.Create<Player>("Player");
    PropLayoutFile::ApplyPlacement(*m_Player, PropLayoutFile::GetPlacement(layout, "Player"));

    // 地面（1面・2面と同じ濡れた床。水たまりは床全体のマス目に乱数で置かれ、雨上がりの森になる）
    Ground* ground = builder.Create<Ground>("ForestGround");
    PropLayoutFile::ApplyPlacement(*ground, PropLayoutFile::GetPlacement(layout, "Ground"));
    m_PlacementBindings.emplace_back("Ground", [ground](const PropPlacement& placement)
    {
        PropLayoutFile::ApplyPlacement(*ground, placement);
    });

    // 木・岩・倒木・茂み・研究所・灯り（prop 行）
    m_LayoutProps = PropLayoutFile::Spawn(builder, layout);
    // 木や岩を置いた後で草を作り、その足元と小道を避けて植える。
    m_Grass = builder.Create<ForestGrass>("ForestGrass");
    PropLayoutFile::ApplyPlacement(*m_Grass, PropLayoutFile::GetPlacement(layout, "ForestGrass"));
    m_PlacementBindings.emplace_back("ForestGrass", [this](const PropPlacement& placement)
    {
        PropLayoutFile::ApplyPlacement(*m_Grass, placement);
    });

    // カメラの位置を最初から合わせるため、プレイヤーを一度更新している
    m_Player->Update();
    m_Hud.Init();
    // 森には記録（残された資料）を置いていないので、右上の「記録 0 / 4」は出さない
    m_Hud.SetEvidenceVisible(false);
    m_Notice = "外の森（試作）  小道を北へ進むと、森の奥に研究所がある";
    m_NoticeTimer = 8.0f;
}

ForestScene::~ForestScene()
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
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
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
    m_Hud.Draw(*m_Player, -1, std::string_view{},
        m_NoticeTimer > 0.0f ? std::string_view(m_Notice) : std::string_view("外の森（試作）"));
}
