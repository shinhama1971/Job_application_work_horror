// ============================================================================
// ファイルの役割: タイトル画面の入力と表示、ゲーム開始への切り替え、後ろに映す森のカメラを管理している。
// 主な技術: Sceneの継承、2DのUI、入力によるシーンの切り替え、ゆっくり進むカメラの場面の切り替えと、切り替えのノイズ
// （説明は TitleScene.h を参照）
// ============================================================================

#include "TitleScene.h"

#include "Application.h"
#include "Camera.h"
#include "ForestGrass.h"
#include "ForestScene.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "PostProcess.h"
#include "SceneLayoutBuilder.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

int TitleScene::s_SelectedStage = 0;

namespace
{
    // 後ろに映す場面1つ分：足元の位置・水平の向き（+Z が0）・上下の向きを、始めから終わりへ、なめらかに動かしている
    struct Shot
    {
        Vector3 From;
        Vector3 To;
        float YawFrom;
        float YawTo;
        float PitchFrom;
        float PitchTo;
        float Seconds;
    };

    // 小道の入口 → 街灯と水たまり → 草むらを何かが通る → 研究所の窓の明かりが消える、を繰り返している。
    // 草むらの場面は、z = 40 を越えた所から始め、すぐに「見えない何か」が前を横切る（ForestEvents）。
    // 研究所の場面は、z = 140 を越えたところで2階の窓の明かりが消える
    const Shot Shots[] =
    {
        { Vector3(2.0f, -99.0f, -300.0f), Vector3(6.0f, -99.0f, -252.0f), 0.10f, -0.05f, -0.06f, -0.10f, 13.0f },
        { Vector3(-8.0f, -99.0f, -114.0f), Vector3(-4.0f, -99.0f, -98.0f), -0.38f, -0.55f, -0.10f, -0.14f, 12.0f },
        { Vector3(0.0f, -99.0f, 40.5f), Vector3(0.0f, -99.0f, 48.0f), 0.06f, -0.04f, -0.34f, -0.40f, 11.0f },
        { Vector3(0.0f, -99.0f, 126.0f), Vector3(0.0f, -99.0f, 150.0f), 0.0f, 0.0f, 0.02f, 0.07f, 13.0f },
    };
    constexpr int ShotCount = static_cast<int>(sizeof(Shots) / sizeof(Shots[0]));

    float Hash(float value)
    {
        const float s = std::sin(value * 12.9898f) * 43758.5453f;
        return s - std::floor(s);
    }

    float SmoothStep(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

// 作るときに初期化している
TitleScene::TitleScene()
{
    Init();
}

// 壊すときに後片付けをしている
TitleScene::~TitleScene()
{
    Uninit();
}

// 経過時間を0にし、HUDと後ろの森を準備している
void TitleScene::Init()
{
    m_TitleTime = 0.0f;
    m_Hud.Init();

    // 後ろに映す森は、森の場面（ForestScene）と同じ作り方で作っている
    Core::Game* game = Core::Game::GetInstance();
    ForestScene::ApplyAtmosphere();
    const PropLayout layout = ForestScene::LoadLayout();
    SceneLayoutBuilder builder(*game, m_ObjectNames);
    ForestScene::World world = ForestScene::BuildWorld(builder, layout);
    m_Player = world.PlayerObject;
    m_Grass = world.Grass;
    m_Props = std::move(world.Props);

    // プレイヤーは入力を読まず、位置と視点の向きはこのシーンから動かす（マウスと矢印キーでも視点を回さない）
    m_Player->SetAttractMode(true);
    game->GetCamera()->SetMouseLookEnabled(false);
    game->GetCamera()->SetArrowKeyLookEnabled(false);
    m_ShotIndex = 0;
    m_ShotTime = 0.0f;
    m_CutNoise = 1.0f;
    UpdateCamera(0.0f);
    m_Player->Update();
}

// 今の場面のカメラを動かし、場面の終わりで次の場面へ切り替えている
void TitleScene::UpdateCamera(float deltaTime)
{
    m_ShotTime += deltaTime;
    if (m_ShotTime >= Shots[m_ShotIndex].Seconds)
    {
        m_ShotTime = 0.0f;
        m_ShotIndex = (m_ShotIndex + 1) % ShotCount;
        // 電波が途切れたように切り替える。森の演出も作り直し、消えた街灯や窓の明かりを戻して、また起きるようにしている
        m_CutNoise = 1.0f;
        m_Events = ForestEvents{};
    }
    const Shot& shot = Shots[m_ShotIndex];
    const float progress = SmoothStep(m_ShotTime / shot.Seconds);
    m_Player->SetPosition(Vector3::Lerp(shot.From, shot.To, progress));
    // 手持ちのカメラのように、ほんの少し揺らしている
    const float sway = std::sin(m_TitleTime * 0.7f) * 0.012f + std::sin(m_TitleTime * 1.9f) * 0.004f;
    Camera* camera = Core::Game::GetInstance()->GetCamera();
    camera->SetCameraDirection(shot.YawFrom + (shot.YawTo - shot.YawFrom) * progress + sway);
    camera->SetCameraPitch(shot.PitchFrom + (shot.PitchTo - shot.PitchFrom) * progress + sway * 0.5f);
    // 懐中電灯の電池が切れないようにしている
    m_Player->AddBattery(100.0f);
}

float TitleScene::GetNoise() const
{
    // 場面が変わった瞬間の強い乱れ
    float noise = m_CutNoise;
    // ときどき来る短い乱れ（約3.7秒ごとに、半分くらいの確率で0.18秒だけ）
    constexpr float window = 3.7f;
    const float index = std::floor(m_TitleTime / window);
    const float local = m_TitleTime - index * window;
    if (Hash(index + 0.37f) < 0.55f && local < 0.18f)
    {
        noise = (std::max)(noise, 0.55f * (1.0f - local / 0.18f));
    }
    return std::clamp(noise, 0.0f, 1.0f);
}

// Q（B）でゲームを終える（終了の確認が出る）。←→（A・D、十字キー、左スティック）か1・2・3のキーで始める面を選び、
// Enter（AかSTART）で選んだ面を始めている。2面から始めたプレイは、クリアしてもベスト記録に残さない（Game::ChangeScene）。
void TitleScene::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    m_TitleTime += deltaTime;

    // 後ろの森：カメラを動かし、街灯のちらつきなどの演出と、草むらを通る「見えない何か」を草へ渡している
    UpdateCamera(deltaTime);
    m_CutNoise = (std::max)(0.0f, m_CutNoise - deltaTime / 0.6f);
    m_Events.Update(deltaTime, m_Player->GetPosition(), m_Props);
    if (m_Grass != nullptr)
    {
        m_Grass->SetUnseenWalker(m_Events.GetWalkerPosition(), 10.0f, m_Events.GetWalkerStrength());
    }
    // 題名のノイズに合わせて、画面全体にも電波障害の横線と色ずれを出している
    const float noise = GetNoise();
    Effect::PostProcess* postProcess = Core::Game::GetInstance()->GetPostProcess();
    postProcess->SetSignalInterference(noise * 0.85f);
    postProcess->SetPresenceStatic(noise * 0.55f);

    if (Input::GetKeyTrigger(VK_Q) ||
        Input::GetButtonTrigger(XINPUT_B))
    {
        PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
        return;
    }

    // 左スティックは、倒した瞬間に1回だけ動かしている（戻すまで次は動かさない）
    const float stickX = Input::GetLeftAnalogStick().x;
    const bool stickLeft = m_StickReleased && stickX < -0.6f;
    const bool stickRight = m_StickReleased && stickX > 0.6f;
    if (stickLeft || stickRight)
    {
        m_StickReleased = false;
    }
    else if (std::abs(stickX) < 0.3f)
    {
        m_StickReleased = true;
    }

    // 面の並びは縦なので、↑↓でも選べるようにしている（←→・A・D もこれまでどおり使える）
    const int previousStage = s_SelectedStage;
    if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A) || Input::GetKeyTrigger(VK_UP) ||
        Input::GetKeyTrigger(VK_W) || Input::GetButtonTrigger(XINPUT_LEFT) || Input::GetButtonTrigger(XINPUT_UP) || stickLeft)
    {
        s_SelectedStage = (std::max)(0, s_SelectedStage - 1);
    }
    if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D) || Input::GetKeyTrigger(VK_DOWN) ||
        Input::GetKeyTrigger(VK_S) || Input::GetButtonTrigger(XINPUT_RIGHT) || Input::GetButtonTrigger(XINPUT_DOWN) || stickRight)
    {
        s_SelectedStage = (std::min)(2, s_SelectedStage + 1);
    }
    if (Input::GetKeyTrigger(VK_1))
    {
        s_SelectedStage = 0;
    }
    if (Input::GetKeyTrigger(VK_2))
    {
        s_SelectedStage = 1;
    }
    if (Input::GetKeyTrigger(VK_3))
    {
        s_SelectedStage = 2;
    }
    if (s_SelectedStage != previousStage)
    {
        Input::SetVibration(1, 0.05f);
    }

    if (Input::GetKeyTrigger(VK_RETURN) ||
        Input::GetButtonTrigger(XINPUT_A) ||
        Input::GetButtonTrigger(XINPUT_START))
    {
        // 外の森（試作）は、まだ1面にはつながらない（森の中を歩いて研究所まで行ける。Esc のポーズメニューからタイトルへ戻る）
        constexpr SceneName stages[] = { SceneName::Stage, SceneName::Stage2, SceneName::Forest };
        Core::Game::GetInstance()->RequestSceneChange(stages[s_SelectedStage]);
    }
}

// 題名・操作の説明・ベスト記録・始める面の選択を、後ろの森の上に描いている
void TitleScene::Draw(Camera* camera)
{
    (void)camera;
    Core::Game* game = Core::Game::GetInstance();
    m_Hud.DrawTitle(
        m_TitleTime,
        game->HasClearRecord(),
        game->GetBestClearTimeSeconds(),
        game->GetBestCaughtCount(),
        s_SelectedStage,
        GetNoise());
}

// HUDを片付け、森の画面効果・画面の乱れ・視点の操作を普段に戻し、後ろの森を破棄している
void TitleScene::Uninit()
{
    m_Hud.Uninit();
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
}
