#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"
#include "Interactable.h"

class FuseBox : public Object, public Interactable
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;

    bool m_IsPowered = false;
    bool m_IsExitControl = false;
    bool m_IsManualControl = false;
    bool m_ManualInteractionAllowed = false;
    const char* m_ManualPrompt = "スイッチを操作する";

    void BuildGeometry();

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    void Uninit() override;

    bool IsInteractionEnabled() const override
    {
        return !m_IsPowered &&
            (!m_IsManualControl || m_ManualInteractionAllowed);
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override
    {
        return m_Position;
    }
    const char* GetInteractionPrompt() const override;
    void Interact(Player& player) override;

    void SetExitControl(bool enabled) { m_IsExitControl = enabled; }
    void SetManualControl(const char* prompt)
    {
        m_IsManualControl = true;
        m_ManualPrompt = prompt;
    }
    void SetManualInteractionAllowed(bool allowed)
    {
        m_ManualInteractionAllowed = allowed;
    }
    void ResetActivation();
    bool IsActivated() const { return m_IsPowered; }

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }
};
