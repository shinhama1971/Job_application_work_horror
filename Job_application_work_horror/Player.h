#pragma once

#include "Object.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "MeshRenderer.h"
#include "Material.h"

class Player : public Object
{
private:
    DirectX::SimpleMath::Vector3 m_Velocity =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

    //懐中電灯作成
    bool m_FlashLightOn = true;
    float m_Battery = 100.0f;

    MeshRenderer m_MeshRenderer;

    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    bool m_IsFPS = true;
    bool m_SpawnAdjusted = false;

    float m_MoveSpeed = 0.5f;
    float m_Radius = 1.0f;
    int m_FlickerTimer = 0;

    // ライト調整用（バイオハザード風）
    float m_LightDiffuseR = 1.8f;
    float m_LightDiffuseG = 1.6f;
    float m_LightDiffuseB = 1.2f;
    float m_LightAmbientR = 0.1f;
    float m_LightAmbientG = 0.1f;
    float m_LightAmbientB = 0.12f;
    float m_DarkDiffuseR = 0.3f;
    float m_DarkDiffuseG = 0.3f;
    float m_DarkDiffuseB = 0.35f;
    float m_DarkAmbientR = 0.06f;
    float m_DarkAmbientG = 0.06f;
    float m_DarkAmbientB = 0.08f;
    float m_CameraHeightOffset = 1.8f;
   
public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    DirectX::SimpleMath::Vector3 GetForward() const;
    void AddBattery(float value)
    {
        m_Battery += value;

        if (m_Battery > 100.0f)
        {
            m_Battery = 100.0f;
        }
    }
	// バッテリー残量の取得
    float GetBattery() const
    {
        return m_Battery;
    }
 
	// プレイヤーの位置を設定
    void SetPosition(DirectX::SimpleMath::Vector3 pos)
    {
        m_Position = pos;
    }
    
    bool IsFPS() const { return m_IsFPS; }
};