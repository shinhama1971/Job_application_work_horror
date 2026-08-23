#pragma once

#include "Object.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "Interactable.h"

class BatteryItem : public Object, public Interactable
{
private:
    MeshRenderer m_MeshRenderer;

    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    bool m_IsCollected = false;
    bool m_IsActive = true;
    float m_RecoverValue = 30.0f;

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
    const char* GetInteractionPrompt() const override { return "電池を拾う"; }
    void Interact(Player& player) override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

    void SetActive(bool active)
    {
        m_IsActive = active;
    }
};
