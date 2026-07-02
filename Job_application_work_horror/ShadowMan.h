#pragma once

#include "Object.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "MeshRenderer.h"
#include "Material.h"

class ShadowMan : public Object
{
private:
    MeshRenderer m_MeshRenderer;

    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    int m_LifeTimer = 120; // 60FPSなら約2秒

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }
};