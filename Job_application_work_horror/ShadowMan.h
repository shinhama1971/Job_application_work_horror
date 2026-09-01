// ============================================================================
// ファイルの役割: 遠景の人影、出現時間、消失・ディゾルブ演出を管理します。
// ============================================================================

#pragma once

#include <algorithm>
#include <functional>
#include <utility>
#include <wrl/client.h>
#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

class ShadowMan : public Object
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;

    struct DissolveBuffer
    {
        float Time;
        float Visibility;
        float EdgeWidth;
        float Padding;
    };

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DissolveBuffer;
    float m_Age = 0.0f;
    float m_ObservedAmount = 0.0f;
    bool m_ReactedToGaze = false;
    bool m_GazeScareEnabled = false;
    bool m_ChaseEnabled = false;
    bool m_IsActive = true;
    bool m_DeactivateOnExpire = false;
    std::function<void()> m_OnObserved;

    int m_LifeTimer = 120;
    float m_ChaseSpeed = 0.0f;
    float m_ChaseStopDistance = 28.0f;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    void Uninit() override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

    void EnableGazeScare(float lifetimeSeconds = 6.0f)
    {
        m_GazeScareEnabled = true;
        const int requestedFrames = static_cast<int>(lifetimeSeconds * 60.0f);
        m_LifeTimer = requestedFrames > 30 ? requestedFrames : 30;
    }

    void SetOnObserved(std::function<void()> callback)
    {
        m_OnObserved = std::move(callback);
    }

    void SetActive(bool active)
    {
        if (active && !m_IsActive)
        {
            m_Age = 0.0f;
            m_ObservedAmount = 0.0f;
            m_ReactedToGaze = false;
            m_GazeScareEnabled = false;
            m_ChaseEnabled = false;
            m_ChaseSpeed = 0.0f;
            m_LifeTimer = 120;
            m_OnObserved = nullptr;
        }
        if (!active)
        {
            m_ChaseEnabled = false;
        }
        m_IsActive = active;
    }

    bool IsActive() const
    {
        return m_IsActive;
    }

    void EnableChase(float speed, float stopDistance)
    {
        m_ChaseEnabled = true;
        m_ChaseSpeed = (std::max)(speed, 0.0f);
        m_ChaseStopDistance = (std::max)(stopDistance, 8.0f);
    }

    void SetDeactivateOnExpire(bool deactivate)
    {
        m_DeactivateOnExpire = deactivate;
    }
};
