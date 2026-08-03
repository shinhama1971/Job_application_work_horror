#pragma once

#include "Object.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "Interactable.h"

class Door : public Object, public Interactable
{
private:
    MeshRenderer m_MeshRenderer;

    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    bool m_IsOpen = false;
    bool m_IsOpening = false;

    DirectX::SimpleMath::Vector3 m_StartPosition;
    DirectX::SimpleMath::Vector3 m_OpenPosition;

    float m_OpenSpeed = 1.0f;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    bool IsInteractionEnabled() const override { return !m_IsOpen && !m_IsOpening; }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override;
    void Interact(Player& player) override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);

        m_StartPosition = m_Position;

        // 横に80移動して開く
        m_OpenPosition =
            m_Position + DirectX::SimpleMath::Vector3(80.0f, 0.0f, 0.0f);
    }

    bool IsOpen() const
    {
        return m_IsOpen;
    }
};
