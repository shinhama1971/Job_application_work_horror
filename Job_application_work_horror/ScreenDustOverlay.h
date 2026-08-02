#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

class ScreenDustOverlay : public Object
{
private:
    struct TimeBuffer
    {
        float time;
        float power;
        float dummy1;
        float dummy2;
    };

    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;

    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;

    Shader m_Shader;
    std::unique_ptr<Material> m_Material;

    ID3D11Buffer* m_TimeBuffer = nullptr;

    float m_Time = 0.0f;
    float m_Power = 0.7f;

    bool m_IsActive = false;
    float m_Timer = 0.0f;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    void SetPower(float power)
    {
        m_Power = power;
    }

    void SetActive(bool active)
    {
        m_IsActive = active;
    }

    void SetTimer(float timer)
    {
        m_Timer = timer;
    }
};