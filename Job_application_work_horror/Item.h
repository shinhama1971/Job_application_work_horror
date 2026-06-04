#pragma once

#include "Game.h"
#include "Player.h"
#include "Input.h"
class Item : public Object
{
private:
    MeshRenderer m_MeshRenderer;

    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    bool m_IsCollected = false;

    // プレイヤーに触れた判定用の距離
    float m_GetDistance = 50.0f;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

    bool IsCollected() const
    {
        return m_IsCollected;
    }
};