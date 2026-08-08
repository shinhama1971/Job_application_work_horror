#pragma once

#include "Game.h"
#include "Player.h"
#include "Input.h"
#include "Interactable.h"

class Item : public Object, public Interactable
{
private:
    MeshRenderer m_MeshRenderer;

    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    bool m_IsCollected = false;
    bool m_IsActive = true;

    // プレイヤーに触れた判定用の距離

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    bool IsInteractionEnabled() const override
    {
        return m_IsActive && !m_IsCollected;
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override { return "Collect fuse"; }
    void Interact(Player& player) override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

    bool IsCollected() const
    {
        return m_IsCollected;
    }

    void SetActive(bool active)
    {
        m_IsActive = active;
    }

    bool IsActive() const
    {
        return m_IsActive;
    }
};
