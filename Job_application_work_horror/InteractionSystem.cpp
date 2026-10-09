// ============================================================================
// ファイルの役割: 視線の向きと距離を使って、今調べられる対象を1つ選んでいる。
// 主な技術: 視線と対象の向きの比較、点数による一番よい対象の選択、壁でさえぎられていないかの判定、インターフェースによる分離
// ============================================================================

#include "InteractionSystem.h"

#include <limits>

#include "Game.h"
#include "Input.h"
#include "Interactable.h"
#include "Player.h"
#include "Wall.h"

using namespace DirectX::SimpleMath;

// 調べられる対象の中から一番よいものを選び、Eキー（コントローラーはA）が押されたら調べている
void InteractionSystem::Update(Player& player)
{
    Interactable* previousFocus = m_FocusedInteractable;
    m_FocusedInteractable = nullptr;

    Core::Game* game = Core::Game::GetInstance();
    Camera* camera = game->GetCamera();
    const Vector3 origin = camera->GetPosition();
    const Vector3 forward = camera->GetForward();

    float bestScore = -std::numeric_limits<float>::infinity();

    // 調べられる状態で、近く、視線の正面に近く、壁にさえぎられていない対象だけを候補にしている
    for (Interactable* candidate : game->GetObjects<Interactable>())
    {
        if (candidate == nullptr || !candidate->IsInteractionEnabled())
        {
            continue;
        }

        Vector3 toCandidate = candidate->GetInteractionPosition() - origin;
        const float distance = toCandidate.Length();

        if (distance <= 0.001f || distance > MaxInteractionDistance)
        {
            continue;
        }

        toCandidate /= distance;
        const float facingDot = forward.Dot(toCandidate);

        if (facingDot < MinimumFacingDot)
        {
            continue;
        }

        if (!HasClearLineOfSight(origin, candidate->GetInteractionPosition(), distance))
        {
            continue;
        }

        // 点数：正面に近いほど高く、遠いほど低い。前のフレームで選んでいた対象は少し高くし、選ぶ対象がちらちら変わらないようにしている
        const float distanceRate = distance / MaxInteractionDistance;
        const float focusPersistence = candidate == previousFocus ? 0.08f : 0.0f;
        const float score = facingDot * 2.0f - distanceRate + focusPersistence;

        if (score > bestScore)
        {
            bestScore = score;
            m_FocusedInteractable = candidate;
        }
    }

    if (m_FocusedInteractable != nullptr &&
        (Input::GetKeyTrigger(VK_E) ||
         Input::GetButtonTrigger(XINPUT_A)))
    {
        m_FocusedInteractable->Interact(player);
    }
}

// 目から対象までの線分が、対象の手前で壁に当たっていないかを調べている。
// 壁に付いた配電盤などは壁の表面ぎりぎりにあるため、対象の4.5手前までに当たった壁だけをさえぎる物としている
bool InteractionSystem::HasClearLineOfSight(
    const Vector3& origin,
    const Vector3& target,
    float targetDistance) const
{
    Core::Game* game = Core::Game::GetInstance();
    for (const Wall* wall : game->GetObjects<Wall>())
    {
        if (wall == nullptr)
        {
            continue;
        }

        float wallDistance = 0.0f;
        if (wall->IntersectsInteractionSegment(origin, target, wallDistance) &&
            wallDistance < targetDistance - SurfaceInteractionTolerance)
        {
            return false;
        }
    }
    return true;
}

// 選んでいる対象の操作の説明を返している（無ければ空）
std::string_view InteractionSystem::GetPrompt() const
{
    return m_FocusedInteractable == nullptr
        ? std::string_view{}
        : std::string_view{ m_FocusedInteractable->GetInteractionPrompt() };
}
