#pragma once

#include "Object.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "MeshRenderer.h"
#include "Material.h"

class Player : public Object
{
private:
    // ===== 物理演算・移動 =====
    DirectX::SimpleMath::Vector3 m_Velocity =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
    static constexpr float DEFAULT_MOVE_SPEED = 0.5f;
    static constexpr float GRAVITY = 0.01f;
    static constexpr float PLAYER_RADIUS = 1.0f;
    static constexpr float PLAYER_HEIGHT = 2.0f;
    static constexpr float MIN_Y_POSITION = -99.0f;

    float m_MoveSpeed = DEFAULT_MOVE_SPEED;
    float m_Radius = PLAYER_RADIUS;

    // ===== 懐中電灯システム =====
    bool m_FlashLightOn = true;
    float m_Battery = 100.0f;
    static constexpr float MAX_BATTERY = 100.0f;
    static constexpr float BATTERY_CONSUMPTION_RATE = 0.02f;
    static constexpr float BATTERY_FLICKER_THRESHOLD = 20.0f;
    static constexpr int FLICKER_FRAME_INTERVAL = 10;
    int m_FlickerTimer = 0;

    // ===== ライティング（バイオハザード風） =====
    // ライトON時の設定
    float m_LightDiffuseR = 1.8f;
    float m_LightDiffuseG = 1.6f;
    float m_LightDiffuseB = 1.2f;
    float m_LightAmbientR = 0.1f;
    float m_LightAmbientG = 0.1f;
    float m_LightAmbientB = 0.12f;

    // ライトOFF時の設定
    float m_DarkDiffuseR = 0.3f;
    float m_DarkDiffuseG = 0.3f;
    float m_DarkDiffuseB = 0.35f;
    float m_DarkAmbientR = 0.06f;
    float m_DarkAmbientG = 0.06f;
    float m_DarkAmbientB = 0.08f;
    float m_CameraHeightOffset = 1.8f;

    // ===== メッシュ・レンダリング =====
    MeshRenderer m_MeshRenderer;
    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    // ===== カメラモード =====
    bool m_IsFPS = true;                    // true: 一人称, false: 三人称
    bool m_SpawnAdjusted = false;           // スポーン位置調整フラグ
    bool m_CanControl = true;
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

    void SetCanControl(bool enable)
    {
        m_CanControl = enable;
    }

    bool CanControl() const
    {
        return m_CanControl;
    }
};