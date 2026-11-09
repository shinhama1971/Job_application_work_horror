// ============================================================================
// ファイルの役割: タイトル画面や導入の後ろに映す森（説明は ForestBackdrop.h を参照）。
// ============================================================================

#include "ForestBackdrop.h"

#include "Camera.h"
#include "ForestGrass.h"
#include "ForestScene.h"
#include "Game.h"
#include "Player.h"
#include "PostProcess.h"
#include "SceneLayoutBuilder.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    float SmoothStep(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

void ForestBackdrop::Init(const std::vector<Shot>& shots, bool loop)
{
    m_Shots = shots;
    m_Loop = loop;
    // 森は、森の場面（ForestScene）と同じ作り方で作っている
    Core::Game* game = Core::Game::GetInstance();
    ForestScene::ApplyAtmosphere();
    const PropLayout layout = ForestScene::LoadLayout();
    SceneLayoutBuilder builder(*game, m_ObjectNames);
    ForestScene::World world = ForestScene::BuildWorld(builder, layout);
    m_Player = world.PlayerObject;
    m_Grass = world.Grass;
    m_Props = std::move(world.Props);

    // プレイヤーは入力を読まず、位置と視点の向きはここから動かす（マウスと矢印キーでも視点を回さない）
    m_Player->SetAttractMode(true);
    game->GetCamera()->SetMouseLookEnabled(false);
    game->GetCamera()->SetArrowKeyLookEnabled(false);
    m_ShotIndex = 0;
    m_ShotTime = 0.0f;
    m_Time = 0.0f;
    m_CutNoise = 1.0f;
    ApplyCamera();
    m_Player->Update();
}

void ForestBackdrop::ApplyCamera()
{
    if (m_Shots.empty() || m_Player == nullptr)
    {
        return;
    }
    const Shot& shot = m_Shots[static_cast<std::size_t>(m_ShotIndex)];
    const float progress = SmoothStep(m_ShotTime / shot.Seconds);
    m_Player->SetPosition(Vector3::Lerp(shot.From, shot.To, progress));
    // 手持ちのカメラのように、ほんの少し揺らしている
    const float sway = std::sin(m_Time * 0.7f) * 0.012f + std::sin(m_Time * 1.9f) * 0.004f;
    Camera* camera = Core::Game::GetInstance()->GetCamera();
    camera->SetCameraDirection(shot.YawFrom + (shot.YawTo - shot.YawFrom) * progress + sway);
    camera->SetCameraPitch(shot.PitchFrom + (shot.PitchTo - shot.PitchFrom) * progress + sway * 0.5f);
    // 懐中電灯の電池が切れないようにしている
    m_Player->AddBattery(100.0f);
}

void ForestBackdrop::Update(float deltaTime)
{
    if (m_Shots.empty() || m_Player == nullptr)
    {
        return;
    }
    m_Time += deltaTime;
    m_ShotTime += deltaTime;
    const float shotSeconds = m_Shots[static_cast<std::size_t>(m_ShotIndex)].Seconds;
    if (m_ShotTime >= shotSeconds)
    {
        const bool last = m_ShotIndex + 1 >= static_cast<int>(m_Shots.size());
        if (last && !m_Loop)
        {
            // 最後の場面の終わりで止めている
            m_ShotTime = shotSeconds;
        }
        else
        {
            m_ShotTime = 0.0f;
            m_ShotIndex = last ? 0 : m_ShotIndex + 1;
            // 電波が途切れたように切り替える。森の演出も作り直し、消えた街灯や窓の明かりを戻して、また起きるようにしている
            m_CutNoise = 1.0f;
            m_Events = ForestEvents{};
        }
    }
    ApplyCamera();
    m_CutNoise = (std::max)(0.0f, m_CutNoise - deltaTime / 0.6f);
    m_Events.Update(deltaTime, m_Player->GetPosition(), m_Props);
    if (m_Grass != nullptr)
    {
        m_Grass->SetUnseenWalker(m_Events.GetWalkerPosition(), 10.0f, m_Events.GetWalkerStrength());
    }
}

void ForestBackdrop::Uninit()
{
    Core::Game* game = Core::Game::GetInstance();
    if (game == nullptr)
    {
        return;
    }
    ForestScene::RestoreAtmosphere();
    game->GetPostProcess()->SetSignalInterference(0.0f);
    game->GetPostProcess()->SetPresenceStatic(0.0f);
    game->GetCamera()->SetMouseLookEnabled(true);
    game->GetCamera()->SetArrowKeyLookEnabled(true);
    for (const std::string& name : m_ObjectNames)
    {
        game->DestroyObj(name);
    }
    m_ObjectNames.clear();
    m_Player = nullptr;
    m_Grass = nullptr;
}
