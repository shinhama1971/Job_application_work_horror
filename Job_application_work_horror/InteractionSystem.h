#pragma once

#include <string_view>

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

    Interactable* m_FocusedInteractable = nullptr;
};
