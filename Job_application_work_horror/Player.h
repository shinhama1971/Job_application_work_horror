// ============================================================================
// ファイルの役割: 一人称移動、視点、懐中電灯、電池、インタラクションを管理します。
// ============================================================================

#pragma once

#include "Object.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "MeshRenderer.h"
#include "Material.h"

class Player : public Object
{
private:
    // ===== 移動と当たり判定 =====
    // 座標の単位はステージ共通。PLAYER_HEIGHTはカメラではなく衝突体の高さです。
    DirectX::SimpleMath::Vector3 m_Velocity =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
    static constexpr float DEFAULT_MOVE_SPEED = 0.5f;
    static constexpr float GRAVITY = 0.01f;
    static constexpr float PLAYER_RADIUS = 4.0f;
    static constexpr float PLAYER_HEIGHT = 34.0f;
    static constexpr float MIN_Y_POSITION = -99.0f;

    float m_MoveSpeed = DEFAULT_MOVE_SPEED;
    float m_Radius = PLAYER_RADIUS;
    static constexpr float SPRINT_SPEED_MULTIPLIER = 1.65f;
    float m_HeadBobTimer = 0.0f;
    float m_HeadBobOffset = 0.0f;
    float m_HeadBobSideOffset = 0.0f;
    float m_AmbienceTimer = 0.0f;
    float m_FootstepTimer = 0.0f;
    int m_FootstepIndex = 0;
    float m_SurfaceNoisePulse = 0.0f;
    bool m_WetSurfaceOverride = false;
    float m_Stamina = 100.0f;
    float m_StaminaRecoveryDelay = 0.0f;
    bool m_SprintExhausted = false;
    static constexpr float MAX_STAMINA = 100.0f;
    static constexpr float STAMINA_DRAIN_PER_FRAME = 0.22f;
    static constexpr float STAMINA_RECOVERY_PER_FRAME = 0.38f;

    // ===== 懐中電灯と電池 =====
    // 電池残量が閾値を下回ると、完全消灯の前に不安定な点滅へ移行します。
    bool m_FlashLightOn = true;
    float m_Battery = 100.0f;
    static constexpr float MAX_BATTERY = 100.0f;
    static constexpr float BATTERY_CONSUMPTION_RATE = 0.02f;
    static constexpr float BATTERY_FLICKER_THRESHOLD = 20.0f;
    static constexpr int FLICKER_FRAME_INTERVAL = 10;
    int m_FlickerTimer = 0;
    float m_BatteryNoticeTimer = 0.0f;
    int m_LowBatteryWarningLevel = 0;
    bool m_WasFlashlightVoltageDrop = false;
    float m_FlashlightNearSurfaceBlend = 0.0f;
    float m_FlashlightPowerBlend = 1.0f;

    // ===== ライティング調整値 =====
    // 点灯時の色。Diffuseは照射光、Ambientは最低限残す環境光です。
    float m_LightDiffuseR = 1.72f;
    float m_LightDiffuseG = 1.62f;
    float m_LightDiffuseB = 1.42f;
    float m_LightAmbientR = 0.1f;
    float m_LightAmbientG = 0.1f;
    float m_LightAmbientB = 0.12f;

    // 消灯時の色。真っ暗で進行不能にならないための最低照度を保持します。
    float m_DarkDiffuseR = 0.3f;
    float m_DarkDiffuseG = 0.3f;
    float m_DarkDiffuseB = 0.35f;
    float m_DarkAmbientR = 0.06f;
    float m_DarkAmbientG = 0.06f;
    float m_DarkAmbientB = 0.08f;
    float m_CameraHeightOffset = 40.0f;

    // ===== プレイヤーメッシュの描画資源 =====
    MeshRenderer m_MeshRenderer;
    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    // ===== カメラと操作状態 =====
    bool m_IsFPS = true;                    // trueなら一人称視点
    bool m_SpawnAdjusted = false;           // 初期位置の床補正が完了したか
    bool m_CanControl = true;
    bool m_IsSprinting = false;
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
        m_BatteryNoticeTimer = 2.2f;
        m_LowBatteryWarningLevel = m_Battery <= 20.0f ? 1 : 0;
    }
    // HUDとアイテム判定が参照する現在の電池残量（0～100）。
    float GetBattery() const
    {
        return m_Battery;
    }

    float GetBatteryNoticeTimer() const
    {
        return m_BatteryNoticeTimer;
    }
 
    // ワープ時は慣性が残らないよう、座標と同時に速度もリセットします。
    void SetPosition(DirectX::SimpleMath::Vector3 pos)
    {
        m_Position = pos;
        m_Velocity = DirectX::SimpleMath::Vector3::Zero;
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

    bool IsSprinting() const
    {
        return m_IsSprinting;
    }

    bool IsMovingHorizontally() const
    {
        return m_Velocity.x * m_Velocity.x + m_Velocity.z * m_Velocity.z > 0.0025f;
    }

    // Sceneが持つ特殊な床（水たまり等）を次のUpdateへ通知します。
    void SetWetSurface(bool wet) { m_WetSurfaceOverride = wet; }
    float GetSurfaceNoisePulse() const { return m_SurfaceNoisePulse; }

    float GetStamina() const
    {
        return m_Stamina;
    }

    void RestoreStamina()
    {
        m_Stamina = MAX_STAMINA;
        m_StaminaRecoveryDelay = 0.0f;
        m_SprintExhausted = false;
    }

    bool IsFlashlightOn() const
    {
        return m_FlashLightOn;
    }
};
