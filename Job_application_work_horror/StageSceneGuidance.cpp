// ============================================================================
// ファイルの役割: 1面のヒューズの取得経路、誘導灯、出口表示を更新する。
// ============================================================================

#include "StageScene.h"
#include "Stage1FuseProgress.h"
#include "Application.h"
#include "CeilingLight.h"
#include "EventFeedback.h"
#include "FuseBox.h"
#include "Game.h"
#include "Item.h"
#include "Player.h"
#include "Wall.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 空だった箱へヒューズが戻るときの画面の脈打ち
    constexpr EventFeedback::PulseSpec KeypadFuseReturnPulse{ 0.14f, 0.30f };
    // 出口の前兆で、扉のランプを速く点滅させる秒数
    constexpr float ExitOmenBlinkSeconds = 3.2f;
    // 暗証番号の部屋の空のヒューズの箱：この距離まで近づいたら知らせを出し、この秒数だけ出す
    constexpr float EmptyFuseCaseNoticeDistance = 30.0f;
    constexpr float EmptyFuseCaseNoticeSeconds = 3.2f;
    // 箱にヒューズが戻るときの、金属の箱に物を置くような低い音（配管を叩く音を低くして使っている）の高さと音量
    constexpr float KeypadFuseReturnPitch = 0.62f;
    constexpr float KeypadFuseReturnVolume = 1.3f;
    // 次に進む方向の天井灯：今の灯りからこの距離より離れるまでは、次の灯りへ進めない
    constexpr float RouteLightAdvanceDistance = 35.0f;
    // 照明が無いときの距離の2乗（どの灯よりも遠いとみなす）
    constexpr float MissingRouteLightDistanceSquared = 1.0e20f;
    // --- 出口の表示と扉のランプ：脈打ちの明るさ（基本＋揺れ）と速さ（電力が戻った後は落ち着いて、前は警報のように速く）、色、光沢
    constexpr float ExitSignPulseBase = 0.78f;
    constexpr float ExitSignPulseAmount = 0.16f;
    constexpr float ExitSignCalmPulseSpeed = 3.2f;
    constexpr float ExitSignAlarmPulseSpeed = 7.4f;
    const Color ExitSignReadyDiffuse(0.025f, 0.24f, 0.06f, 1.0f);
    const Vector3 ExitSignReadyEmission(0.006f, 0.36f, 0.025f);
    constexpr float ExitSignReadyShininess = 34.0f;
    const Color ExitSignWaitingDiffuse(0.24f, 0.025f, 0.018f, 1.0f);
    const Vector3 ExitSignWaitingEmission(0.32f, 0.004f, 0.002f);
    constexpr float ExitSignWaitingShininess = 30.0f;
    // 扉のランプ：出口の前兆の間ほど速く大きく点滅させる
    constexpr float IndicatorPulseBase = 0.72f;
    constexpr float IndicatorPulseSpeed = 3.2f;
    constexpr float IndicatorOmenPulseSpeed = 8.0f;
    constexpr float IndicatorPulseAmount = 0.10f;
    constexpr float IndicatorOmenPulseAmount = 0.14f;
    const Color IndicatorReadyDiffuse(0.025f, 0.20f, 0.055f, 1.0f);
    const Vector3 IndicatorReadyEmission(0.005f, 0.24f, 0.025f);
    constexpr float IndicatorReadyShininess = 30.0f;
    constexpr float IndicatorWaitingRed = 0.28f;
    constexpr float IndicatorWaitingShininess = 28.0f;
}

bool StageScene::IsThirdFuseStage() const
{
    const Core::Game* game = Core::Game::GetInstance();
    return game->GetItemCount() == Stage1FuseProgress::BeforeLastFuse &&
        m_CorridorLoopCount >= Stage1FuseProgress::LoopsToUnlockLastFuse && !game->IsPowerRestored();
}

// 3本目のヒューズは、次の2つのどちらで手に入れてもよい（一本道にしないため）。
// ・西棟：右の倉庫の鍵 → 西側の扉 → 排水バルブ3つで水を抜く → 奥のポンプ室（Stage1WestWing）
// ・暗証番号の扉の先：壁の文字から4桁の番号を探す → 扉を開ける → 閉じ込められ、影に追われながら鍵を探す → 部屋の奥
// 暗証番号の部屋のヒューズは、3本目を探す段階になるまで出さない（先に取れると、2本目の左の倉庫の進行と食い違うため）。
// 寄り道で先に暗証番号の扉を開けていれば、3本目はその部屋へ取りに行くだけになる（探索のごほうび）。
// 先に開けた人が空の部屋で迷わないよう、ヒューズの置き場所には空の箱（配置ファイルの PropKeypadFuseCase）を置き、
// 壁の掲示で箱を調べ直す条件を伝え、近づくと「箱は空だ」と知らせる。3本目の段階になった瞬間に、箱に物を置く音を部屋の方から鳴らし、
// 進む先の照明と目的の表示で、空だった箱に何かが戻ったことを伝えている（ループ廊下を2周した合図にもなる）。
// どちらか一方を取ったら、もう一方は消して、4本目として拾えないようにしている。
void StageScene::UpdateThirdFuseRoutes()
{
    Item* westWingFuse = m_Objects.thirdFuse;
    Item* keypadFuse = m_Objects.keypadRoomFuse;
    if (westWingFuse == nullptr || keypadFuse == nullptr)
    {
        return;
    }
    if (keypadFuse->IsCollected())
    {
        // 暗証番号の部屋で取った：西棟のヒューズは、水を抜いても出さない
        westWingFuse->SetActive(false);
        return;
    }
    if (westWingFuse->IsCollected())
    {
        keypadFuse->SetActive(false);
        return;
    }

    const bool thirdFuseStage = IsThirdFuseStage();
    Core::Game* game = Core::Game::GetInstance();
    // 3本目の段階より前に部屋を開けていた人へ：段階が来た瞬間に、空だった箱へヒューズが戻る音を鳴らしている
    if (thirdFuseStage && !m_WasThirdFuseStage && m_KeypadDoor.IsSolved())
    {
        m_KeypadFuseReturned = true;
        game->PlayAudioCueAt(SOUND_CUE_PIPE_KNOCK, keypadFuse->GetPosition(), KeypadFuseReturnPitch, KeypadFuseReturnVolume);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), KeypadFuseReturnPulse);
    }
    m_WasThirdFuseStage = thirdFuseStage;

    // 3本目の段階より前に、開けた部屋の空の箱へ近づいたら、そのときだけ「箱は空だ」と知らせている
    const Player* player = m_Objects.player;
    const bool nearCase = m_KeypadDoor.IsSolved() && !thirdFuseStage && player != nullptr &&
        Vector3::DistanceSquared(player->GetPosition(), keypadFuse->GetPosition()) <
            EmptyFuseCaseNoticeDistance * EmptyFuseCaseNoticeDistance;
    if (nearCase && !m_NearEmptyFuseCase)
    {
        m_EmptyFuseCaseNoticeTimer = EmptyFuseCaseNoticeSeconds;
    }
    m_NearEmptyFuseCase = nearCase;
    m_EmptyFuseCaseNoticeTimer = (std::max)(0.0f, m_EmptyFuseCaseNoticeTimer - Application::GetDeltaTime());

    keypadFuse->SetActive(thirdFuseStage);
}
// 進む先の天井照明を1灯だけ、切れかけの電球のように不規則にちかちかさせている（ほかの照明は落ち着いて点いたまま）。
// 明滅と周囲に落ちる光を、次に向かう場所の目印にする。
// 経路の選択は進行を参照するだけで、鍵・アイテム・扉の状態は変更しない。
// 進行に合わせて、次に向かう場所の照明の組を選んでいる（どれも当てはまらないときは Stage1RouteLight::Count）
Stage1RouteLight StageScene::SelectRouteLight() const
{
    const Core::Game* game = Core::Game::GetInstance();
    const int fuseCount = game->GetItemCount();
    const bool powered = game->IsPowerRestored();
    // 閉じ込めや照明の演出の最中は、演出の明滅と混ざらないよう、どれもちかちかさせない
    const bool allowed = m_Objects.player != nullptr && m_Objects.player->CanControl() &&
        !m_HiddenRoom.IsTrapped() && !m_ScareLightSequence.IsActive();
    const Stage1WestWing::Step westWingStep = m_WestWing.GetStep();
    Stage1RouteLight selected = Stage1RouteLight::Count;
    if (allowed)
    {
        if (powered)
        {
            if (m_PowerSequence.IsExitComplete())
            {
                selected = Stage1RouteLight::ExitDoor;
            }
            else if (m_Objects.exitPowerPanel != nullptr && !m_Objects.exitPowerPanel->IsActivated())
            {
                selected = Stage1RouteLight::ExitPower;
            }
        }
        else if (fuseCount <= 0)
        {
            selected = Stage1RouteLight::StartSearch;
        }
        else if ((fuseCount == 1 && m_CorridorLoopCount < 1) ||
            (fuseCount == Stage1FuseProgress::BeforeLastFuse &&
                m_CorridorLoopCount < Stage1FuseProgress::LoopsToUnlockLastFuse))
        {
            selected = Stage1RouteLight::CorridorDoor;
        }
        else if (fuseCount == 1)
        {
            selected = Stage1RouteLight::LeftStorage;
        }
        else if (fuseCount >= Core::GameState::RequiredFuseCount)
        {
            selected = Stage1RouteLight::MainPower;
        }
        else if (m_KeypadFuseReturned && westWingStep == Stage1WestWing::Step::FindKey)
        {
            // 先に開けていた暗証番号の部屋の箱に、ヒューズが戻った（西棟へ向かい始めていなければ、そちらへ案内する）
            selected = Stage1RouteLight::KeypadRoom;
        }
        else if (westWingStep == Stage1WestWing::Step::FindKey)
        {
            selected = Stage1RouteLight::RightStorage;
        }
        else if (westWingStep == Stage1WestWing::Step::OpenDoor)
        {
            selected = Stage1RouteLight::WestWing;
        }
        else if (westWingStep == Stage1WestWing::Step::FindFuse)
        {
            selected = Stage1RouteLight::PumpRoom;
        }
        // 排水中は、バルブの赤いランプが目印になるので、ちかちかさせない。
    }
    return selected;
}

// 選んだ照明の組のうち、プレイヤーが近づいた先の1灯だけをちかちかさせている
void StageScene::UpdateRouteLights()
{
    const std::size_t route = static_cast<std::size_t>(SelectRouteLight());
    if (route != m_ActiveRoute)
    {
        m_ActiveRoute = route;
        m_RouteFixtureIndex = 0;
    }

    // いったん全部の候補を「落ち着いて点いている」状態にし、進む先の1灯だけを後でちかちかさせる
    for (const std::vector<CeilingLight*>& fixtures : m_Objects.routeLights)
    {
        for (CeilingLight* light : fixtures)
        {
            if (light != nullptr)
            {
                light->SetRouteFlicker(false);
            }
        }
    }
    if (route >= m_Objects.routeLights.size() || m_Objects.routeLights[route].empty())
    {
        return;
    }
    const std::vector<CeilingLight*>& fixtures = m_Objects.routeLights[route];

    // 時間で次々に移さず、ちかちかしている灯の下まで歩いて来たときだけ、その先の灯へ移している。
    const Vector3 playerPosition = m_Objects.player->GetPosition();
    const auto distanceSquared = [&playerPosition](const CeilingLight* light)
    {
        if (light == nullptr)
        {
            return MissingRouteLightDistanceSquared;
        }
        const Vector3 position = light->GetPosition();
        const float x = position.x - playerPosition.x;
        const float z = position.z - playerPosition.z;
        return x * x + z * z;
    };
    while (m_RouteFixtureIndex + 1 < fixtures.size())
    {
        const float currentDistance = distanceSquared(fixtures[m_RouteFixtureIndex]);
        const float nextDistance = distanceSquared(fixtures[m_RouteFixtureIndex + 1]);
        if (currentDistance > RouteLightAdvanceDistance * RouteLightAdvanceDistance && nextDistance >= currentDistance)
        {
            break;
        }
        ++m_RouteFixtureIndex;
    }
    if (CeilingLight* light = fixtures[m_RouteFixtureIndex])
    {
        light->SetRouteFlicker(true);
    }
}

// 扉のランプは、出口の前兆の間は速く点滅させている
void StageScene::UpdateExitSigns()
{
    const bool powerRestored = Core::Game::GetInstance()->IsPowerRestored();
    const bool exitPowerReady = m_PowerSequence.IsExitComplete();
    const auto signPulse = [this](float speed)
    {
        return ExitSignPulseBase + std::sin(m_StageVisualTimer * speed) * ExitSignPulseAmount;
    };

    Wall* stageExitSign = m_Objects.exitSign;
    if (stageExitSign != nullptr)
    {
        const float pulse = signPulse(powerRestored ? ExitSignCalmPulseSpeed : ExitSignAlarmPulseSpeed);
        if (exitPowerReady)
        {
            stageExitSign->SetAppearance(
                ExitSignReadyDiffuse,
                Color(ExitSignReadyEmission.x, ExitSignReadyEmission.y * pulse, ExitSignReadyEmission.z, 1.0f),
                ExitSignReadyShininess);
        }
        else
        {
            stageExitSign->SetAppearance(
                ExitSignWaitingDiffuse,
                Color(ExitSignWaitingEmission.x * pulse, ExitSignWaitingEmission.y, ExitSignWaitingEmission.z, 1.0f),
                ExitSignWaitingShininess);
        }
    }
    if (m_Objects.startDoorSign != nullptr)
    {
        const float pulse = signPulse(ExitSignAlarmPulseSpeed);
        m_Objects.startDoorSign->SetAppearance(
            ExitSignWaitingDiffuse,
            Color(ExitSignWaitingEmission.x * pulse, ExitSignWaitingEmission.y, ExitSignWaitingEmission.z, 1.0f),
            ExitSignWaitingShininess);
    }

    Wall* exitIndicator = m_Objects.doorIndicator;
    if (exitIndicator == nullptr)
    {
        return;
    }
    const float omenRate = m_ExitOmenSequence.IsTriggered()
        ? 1.0f - (std::clamp)(
            m_ExitOmenSequence.GetTimer() / ExitOmenBlinkSeconds, 0.0f, 1.0f)
        : 0.0f;
    const float indicatorPulse =
        IndicatorPulseBase + std::sin(m_StageVisualTimer *
            (IndicatorPulseSpeed + omenRate * IndicatorOmenPulseSpeed)) *
            (IndicatorPulseAmount + omenRate * IndicatorOmenPulseAmount);
    if (exitPowerReady)
    {
        exitIndicator->SetAppearance(
            IndicatorReadyDiffuse,
            Color(IndicatorReadyEmission.x, IndicatorReadyEmission.y * indicatorPulse, IndicatorReadyEmission.z, 1.0f),
            IndicatorReadyShininess);
    }
    else
    {
        exitIndicator->SetAppearance(
            ExitSignWaitingDiffuse,
            Color(IndicatorWaitingRed * indicatorPulse, ExitSignWaitingEmission.y, ExitSignWaitingEmission.z, 1.0f),
            IndicatorWaitingShininess);
    }
}
