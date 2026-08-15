#include "InteractionSystem.h"

#include <limits>

#include "Game.h"
#include "Input.h"
#include "Interactable.h"
#include "Player.h"
#include "Wall.h"

using namespace DirectX::SimpleMath;

void InteractionSystem::Update(Player& player)
{
    Interactable* previousFocus = m_FocusedInteractable;
    m_FocusedInteractable = nullptr;

    Core::Game* game = Core::Game::GetInstance();
    Camera* camera = game->GetCamera();
    const Vector3 origin = camera->GetPosition();
    const Vector3 forward = camera->GetForward();

    float bestScore = -std::numeric_limits<float>::infinity();

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

std::string_view InteractionSystem::GetPrompt() const
{
    return m_FocusedInteractable == nullptr
        ? std::string_view{}
        : std::string_view{ m_FocusedInteractable->GetInteractionPrompt() };
}
