// ============================================================================
// ファイルの役割: プレイヤーが調べられるオブジェクト共通のインターフェースです。
// ============================================================================

#pragma once

#include <SimpleMath.h>

class Player;

class Interactable
{
public:
    virtual ~Interactable() = default;

    virtual bool IsInteractionEnabled() const = 0;
    virtual DirectX::SimpleMath::Vector3 GetInteractionPosition() const = 0;
    virtual const char* GetInteractionPrompt() const = 0;
    virtual void Interact(Player& player) = 0;
};
