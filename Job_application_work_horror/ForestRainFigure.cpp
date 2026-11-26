// ============================================================================
// ファイルの役割: 外の森の、雨でだけ輪郭が浮かぶ人影の進行（説明は ForestRainFigure.h を参照）。
// ============================================================================

#include "ForestRainFigure.h"

#include "Game.h"
#include "ShadowFigureGeometry.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

namespace
{
    // 小道のこの z より北へ進んだら立たせる（開始地点は z = -400）。プレイヤーの何歩先の北に、小道の中央から西へどれだけずらして立たせるか
    constexpr float TriggerZ = -375.0f;
    constexpr float FigureAhead = 110.0f;
    constexpr float FigureWestShift = 6.0f;
    // 雨の中に浮かび上がるまでの秒数
    constexpr float AppearSeconds = 1.5f;
    // 懐中電灯で照らしているとみなす、視線との角度（内積）と届く距離。この秒数だけ照らし続けると崩れる
    constexpr float LitFacing = 0.97f;
    constexpr float LitMaxDistance = 220.0f;
    constexpr float LitSecondsToCollapse = 2.0f;
    // これより近づくと、驚かせて崩れる。プレイヤーが足元のこの距離より北へ通り過ぎるか、立ってからこの秒数がたつと、静かに崩れる
    constexpr float ScareDistance = 40.0f;
    constexpr float PassBehind = 10.0f;
    constexpr float StandMaxSeconds = 40.0f;
    // 崩れ落ちる秒数
    constexpr float CollapseSeconds = 0.9f;
    // 恐怖の画面効果：照らしている間（最低＋照らした長さの幅）、照らして崩れたとき、近づいて崩れたとき。消えるまでの秒数
    constexpr float LitFearBase = 0.2f;
    constexpr float LitFearRange = 0.3f;
    constexpr float CollapseFear = 0.5f;
    constexpr float ScareFear = 0.85f;
    constexpr float FearFadeSeconds = 2.5f;
    // 崩れ落ちた水が地面を打つ音（人影が水を踏む音を低くして使う）と、近づいて崩れたときの低い衝撃音（高さと音量）
    constexpr float SplashPitch = 0.55f;
    constexpr float SplashVolume = 1.6f;
    constexpr float ScarePitch = 0.6f;
    constexpr float ScareVolume = 1.1f;
}

float ForestRainFigure::Update(float deltaTime, const Vector3& playerPosition, const Vector3& eye, const Vector3& forward,
    bool flashlightOn, float groundY)
{
    m_StateTime += deltaTime;
    m_Fear = (std::max)(0.0f, m_Fear - deltaTime / FearFadeSeconds);
    switch (m_State)
    {
    case State::Waiting:
        if (playerPosition.z > TriggerZ)
        {
            m_State = State::Standing;
            m_StateTime = 0.0f;
            m_LitTime = 0.0f;
            m_Foot = Vector3(playerPosition.x - FigureWestShift, groundY, playerPosition.z + FigureAhead);
        }
        break;
    case State::Standing:
    {
        const Vector2 offset(playerPosition.x - m_Foot.x, playerPosition.z - m_Foot.z);
        if (offset.Length() < ScareDistance)
        {
            StartCollapse(true);
            break;
        }
        if (playerPosition.z > m_Foot.z + PassBehind || m_StateTime > StandMaxSeconds)
        {
            StartCollapse(false);
            break;
        }
        // 浮かび上がりきった後、胸のあたりを懐中電灯で照らし続けると、崩れる
        const Vector3 chest = m_Foot + Vector3(0.0f, ShadowFigureGeometry::ChestHeight, 0.0f);
        Vector3 toChest = chest - eye;
        const float distance = toChest.Length();
        const bool lit = flashlightOn && distance > 0.0f && distance < LitMaxDistance &&
            forward.Dot(toChest / distance) > LitFacing && m_StateTime >= AppearSeconds;
        m_LitTime = lit ? m_LitTime + deltaTime : 0.0f;
        if (lit)
        {
            m_Fear = (std::max)(m_Fear, LitFearBase + LitFearRange * (std::min)(m_LitTime / LitSecondsToCollapse, 1.0f));
        }
        if (m_LitTime >= LitSecondsToCollapse)
        {
            StartCollapse(false);
        }
        break;
    }
    case State::Collapsing:
        if (m_StateTime >= CollapseSeconds)
        {
            m_State = State::Gone;
            m_StateTime = 0.0f;
        }
        break;
    default:
        break;
    }
    return m_Fear;
}

float ForestRainFigure::GetVisibility() const
{
    switch (m_State)
    {
    case State::Standing:
        return (std::min)(m_StateTime / AppearSeconds, 1.0f);
    case State::Collapsing:
        return m_CollapseFrom * (std::max)(1.0f - m_StateTime / CollapseSeconds, 0.0f);
    default:
        return 0.0f;
    }
}

void ForestRainFigure::StartCollapse(bool scared)
{
    // 浮かび上がりきる前に崩れるときも、今の見え方から崩していく
    m_CollapseFrom = GetVisibility();
    m_State = State::Collapsing;
    m_StateTime = 0.0f;
    Core::Game* game = Core::Game::GetInstance();
    game->PlayAudioCueAt(SOUND_CUE_WATER_STEP, m_Foot, SplashPitch, SplashVolume);
    if (scared)
    {
        game->PlayAudioCueAt(SOUND_CUE_SCARE, m_Foot, ScarePitch, ScareVolume);
    }
    m_Fear = (std::max)(m_Fear, scared ? ScareFear : CollapseFear);
}
