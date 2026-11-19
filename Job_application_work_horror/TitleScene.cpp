// ============================================================================
// ファイルの役割: タイトル画面の入力と表示、ゲーム開始への切り替えを管理している。
// 主な技術: Sceneの継承、2DのUI、入力によるシーンの切り替え、後ろに映す森（ForestBackdrop）と、場面の切り替えのノイズ
// （説明は TitleScene.h を参照）
// ============================================================================

#include "TitleScene.h"
#include "TitleStageChoices.h"

#include "Application.h"
#include "Game.h"
#include "Input.h"
#include "PostProcess.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

int TitleScene::s_SelectedStage = 0;

namespace
{
    // 小道の入口 → 街灯と水たまり → 草むらを何かが通る → 研究所の窓の明かりが消える、を繰り返している。
    // 草むらの場面は、z = 40 を越えた所から始め、すぐに「見えない何か」が前を横切る（ForestEvents）。
    // 研究所の場面は、z = 140 を越えたところで2階の窓の明かりが消える
    const std::vector<ForestBackdrop::Shot> TitleShots =
    {
        { Vector3(2.0f, -99.0f, -300.0f), Vector3(6.0f, -99.0f, -252.0f), 0.10f, -0.05f, -0.06f, -0.10f, 13.0f },
        { Vector3(-8.0f, -99.0f, -114.0f), Vector3(-4.0f, -99.0f, -98.0f), -0.38f, -0.55f, -0.10f, -0.14f, 12.0f },
        { Vector3(0.0f, -99.0f, 40.5f), Vector3(0.0f, -99.0f, 48.0f), 0.06f, -0.04f, -0.34f, -0.40f, 11.0f },
        { Vector3(0.0f, -99.0f, 126.0f), Vector3(0.0f, -99.0f, 150.0f), 0.0f, 0.0f, 0.02f, 0.07f, 13.0f },
    };

    // sin を使う決まった式で 0〜1 の値を作る係数
    constexpr float HashFrequency = 12.9898f;
    constexpr float HashAmplitude = 43758.5453f;

    // ときどき来る短い乱れ：この秒数ごとに、この確率で、この秒数だけ、この強さで乱れる（乱数の種のずらし）
    constexpr float GlitchWindowSeconds = 3.7f;
    constexpr float GlitchSeedOffset = 0.37f;
    constexpr float GlitchChance = 0.55f;
    constexpr float GlitchSeconds = 0.18f;
    constexpr float GlitchStrength = 0.55f;
    // 題名のノイズを、画面全体の電波障害とラジオの雑音の乱れへ渡す割合
    constexpr float InterferenceScale = 0.85f;
    constexpr float PresenceStaticScale = 0.55f;
    // 左スティック：この量より倒したら1回動かし、この量より戻したら次を受け付ける
    constexpr float StickSelectThreshold = 0.6f;
    constexpr float StickReleaseThreshold = 0.3f;
    // 選ぶ面が変わったときの短い振動
    constexpr int SelectRumbleFrames = 1;
    constexpr float SelectRumbleStrength = 0.05f;

    float Hash(float value)
    {
        const float s = std::sin(value * HashFrequency) * HashAmplitude;
        return s - std::floor(s);
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
    m_Backdrop.Init(TitleShots, true);
}

float TitleScene::GetNoise() const
{
    // 場面が変わった瞬間の強い乱れ
    float noise = m_Backdrop.GetCutNoise();
    // ときどき来る短い乱れ（約3.7秒ごとに、半分くらいの確率で0.18秒だけ）
    const float index = std::floor(m_TitleTime / GlitchWindowSeconds);
    const float local = m_TitleTime - index * GlitchWindowSeconds;
    if (Hash(index + GlitchSeedOffset) < GlitchChance && local < GlitchSeconds)
    {
        noise = (std::max)(noise, GlitchStrength * (1.0f - local / GlitchSeconds));
    }
    return std::clamp(noise, 0.0f, 1.0f);
}

// Q（B）でゲームを終える（終了の確認が出る）。↑↓←→（W・A・S・D、十字キー、左スティック）か1・2・3のキーで始める面を選び、
// Enter（AかSTART）で選んだ面を始めている。2面から始めたプレイは、クリアしてもベスト記録に残さない（Game::ChangeScene）。
void TitleScene::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    m_TitleTime += deltaTime;

    // 後ろの森：カメラを動かし、街灯のちらつきなどの演出と、草むらを通る「見えない何か」を進めている
    m_Backdrop.Update(deltaTime);
    // 題名のノイズに合わせて、画面全体にも電波障害の横線と色ずれを出している
    const float noise = GetNoise();
    Effect::PostProcess* postProcess = Core::Game::GetInstance()->GetPostProcess();
    postProcess->SetSignalInterference(noise * InterferenceScale);
    postProcess->SetPresenceStatic(noise * PresenceStaticScale);

    if (Input::GetKeyTrigger(VK_Q) ||
        Input::GetButtonTrigger(XINPUT_B))
    {
        PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
        return;
    }

    // 左スティックは、倒した瞬間に1回だけ動かしている（戻すまで次は動かさない）
    const float stickX = Input::GetLeftAnalogStick().x;
    const bool stickLeft = m_StickReleased && stickX < -StickSelectThreshold;
    const bool stickRight = m_StickReleased && stickX > StickSelectThreshold;
    if (stickLeft || stickRight)
    {
        m_StickReleased = false;
    }
    else if (std::abs(stickX) < StickReleaseThreshold)
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
        s_SelectedStage = (std::min)(TitleStageChoices::Count - 1, s_SelectedStage + 1);
    }
    // 数字キー（1から順に）で、その番号の面を直接選んでいる
    for (int stage = 0; stage < TitleStageChoices::Count; ++stage)
    {
        if (Input::GetKeyTrigger(static_cast<BYTE>(VK_1 + stage)))
        {
            s_SelectedStage = stage;
        }
    }
    if (s_SelectedStage != previousStage)
    {
        Input::SetVibration(SelectRumbleFrames, SelectRumbleStrength);
    }

    if (Input::GetKeyTrigger(VK_RETURN) ||
        Input::GetButtonTrigger(XINPUT_A) ||
        Input::GetButtonTrigger(XINPUT_START))
    {
        // 1面は、雨の森を歩いて研究所に入ってから始める（森で研究棟からの無線を聞き、玄関で目的の画面を見る）。
        // 3つ目は、森を飛ばして、導入（受信記録と目的の画面）から1面を始める。4つ目（一番下）は、試作中の3面から始める
        Core::Game::GetInstance()->RequestSceneChange(TitleStageChoices::Entries[s_SelectedStage].Scene);
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

// HUDと後ろの森を片付けている（森の画面効果・画面の乱れ・視点の操作も普段に戻る）
void TitleScene::Uninit()
{
    m_Hud.Uninit();
    m_Backdrop.Uninit();
}
