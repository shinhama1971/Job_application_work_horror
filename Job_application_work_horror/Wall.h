#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "Material.h"

class Camera;

class Wall : public Object
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;

    IndexBuffer m_IndexBuffer;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;

    std::unique_ptr<Material> m_Material;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    void SetScale(float x, float y, float z)
    {
        m_Scale = DirectX::SimpleMath::Vector3(x, y, z);
    }

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }
};