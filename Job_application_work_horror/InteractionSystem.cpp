#include "InteractionSystem.h"

#include <limits>

#include "Game.h"
#include "Input.h"
#include "Interactable.h"
#include "Player.h"

using namespace DirectX::SimpleMath;

void InteractionSystem::Update(Player& player)
{
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

        const float distanceRate = distance / MaxInteractionDistance;
        const float score = facingDot * 2.0f - distanceRate;

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

std::string_view InteractionSystem::GetPrompt() const
{
    return m_FocusedInteractable == nullptr
        ? std::string_view{}
        : std::string_view{ m_FocusedInteractable->GetInteractionPrompt() };
}
