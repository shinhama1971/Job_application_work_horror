#pragma once

#include <algorithm>

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

class CeilingLight : public Object
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_BodyMaterial;
    std::unique_ptr<Material> m_LightMaterial;

    size_t m_BodyIndexCount = 0;
    float m_Time = 0.0f;
    float m_FlickerOffset = 0.0f;
    float m_Brightness = 0.0f;
    float m_PowerOnTimer = 0.0f;
    float m_EventFlickerTimer = 0.0f;
    float m_EventFlickerDuration = 0.0f;
    float m_EventFlickerStrength = 0.0f;
    bool m_IsEmergencyLight = false;
    bool m_IsFaulted = false;
    bool m_IsForcedOff = false;
    bool m_WasPowerRestored = false;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    void Uninit() override;

    float GetBrightness() const { return m_Brightness; }
    bool IsEmergencyLight() const { return m_IsEmergencyLight; }
    bool IsFaulted() const { return m_IsFaulted; }
    bool IsForcedOff() const { return m_IsForcedOff; }

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

    void SetScale(float x, float y, float z)
    {
        m_Scale = DirectX::SimpleMath::Vector3(x, y, z);
    }

    void SetEmergencyLight(bool emergency, float flickerOffset)
    {
        m_IsEmergencyLight = emergency;
        m_FlickerOffset = flickerOffset;
    }

    void SetFaulted(bool faulted)
    {
        m_IsFaulted = faulted;
    }

    void SetForcedOff(bool forcedOff)
    {
        m_IsForcedOff = forcedOff;
    }

    void TriggerEventFlicker(float duration, float strength)
    {
        m_EventFlickerDuration = (std::max)(duration, 0.05f);
        m_EventFlickerTimer = m_EventFlickerDuration;
        m_EventFlickerStrength = (std::clamp)(strength, 0.0f, 1.0f);
    }
};
