#pragma once

#include <string_view>
#include <SimpleMath.h>

class Interactable;
class Player;

class InteractionSystem
{
public:
    void Update(Player& player);
    Interactable* GetFocusedInteractable() const { return m_FocusedInteractable; }
    std::string_view GetPrompt() const;

private:
    static constexpr float MaxInteractionDistance = 55.0f;
    static constexpr float MinimumFacingDot = 0.72f;
    static constexpr float SurfaceInteractionTolerance = 4.5f;

    Interactable* m_FocusedInteractable = nullptr;
    bool HasClearLineOfSight(
        const DirectX::SimpleMath::Vector3& origin,
        const DirectX::SimpleMath::Vector3& target,
        float targetDistance) const;
};
