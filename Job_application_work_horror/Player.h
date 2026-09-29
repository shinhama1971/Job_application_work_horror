// ============================================================================
// ファイルの役割: 一人称移動、視点、懐中電灯、電池、インタラクションを管理します。
// 主な技術: FPS制御、衝突応答、ヘッドボブ、スポットライト、入力統合
// ============================================================================

#pragma once

#include "Object.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "FlashlightSystem.h"
#include "PlayerMovement.h"

class Player : public Object
{
private:
    // ===== 移動と当たり判定 =====
    // 座標の単位はステージ共通。PLAYER_HEIGHTはカメラではなく衝突体の高さです。
    PlayerMovement m_Movement;
    static constexpr float PLAYER_RADIUS = 4.0f;
    static constexpr float PLAYER_HEIGHT = 34.0f;
    static constexpr float MIN_Y_POSITION = -99.0f;

    float m_Radius = PLAYER_RADIUS;
    float m_AmbienceTimer = 0.0f;
    float m_FootstepTimer = 0.0f;
    int m_FootstepIndex = 0;
    float m_SurfaceNoisePulse = 0.0f;
    bool m_WetSurfaceOverride = false;

    // ===== 懐中電灯と電池 =====
    // 電池残量が閾値を下回ると、完全消灯の前に不安定な点滅へ移行します。
    FlashlightSystem m_Flashlight;
    float m_FlashlightNearSurfaceBlend = 0.0f;

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
    bool m_SprintAllowed = true;
public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    // カメラの水平方向の向き（上下の傾きを除いた単位ベクトル）です。
    DirectX::SimpleMath::Vector3 GetForward() const;
    void AddBattery(float value)
    {
        m_Flashlight.AddBattery(value);
    }
    // HUDとアイテム判定が参照する現在の電池残量（0～100）。
    float GetBattery() const
    {
        return m_Flashlight.GetBattery();
    }

    // 電池が少なくなった通知をHUDに出す残り時間です（0なら表示しない）。
    float GetBatteryNoticeTimer() const
    {
        return m_Flashlight.GetBatteryNoticeTimer();
    }
 
    // ワープ時は慣性が残らないよう、座標と同時に速度もリセットします。
    void SetPosition(DirectX::SimpleMath::Vector3 pos)
    {
        m_Position = pos;
        m_Movement.ResetVelocity();
    }
    
    bool IsFPS() const { return m_IsFPS; }

    // falseの間はPlayerの更新を止め、移動と操作を受け付けません（捕獲演出や監視映像を見ている間など）。
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
        return m_Movement.IsSprinting();
    }

    // falseの間は走る入力を無視し、歩きだけにします（2面の周回中など）。
    void SetSprintAllowed(bool allowed)
    {
        m_SprintAllowed = allowed;
    }

    bool IsSprintAllowed() const
    {
        return m_SprintAllowed;
    }

    bool IsMovingHorizontally() const
    {
        return m_Movement.IsMovingHorizontally();
    }

    // Sceneが持つ特殊な床（水たまり等）を次のUpdateへ通知します。
    void SetWetSurface(bool wet) { m_WetSurfaceOverride = wet; }
    // 直前の足音がどれだけ大きかったか（0〜1）。水たまりや走りで大きくなり、2面の危険度に加算されます。
    float GetSurfaceNoisePulse() const { return m_SurfaceNoisePulse; }

    float GetStamina() const
    {
        return m_Movement.GetStamina();
    }

    void RestoreStamina()
    {
        m_Movement.RestoreStamina();
    }

    bool IsFlashlightOn() const
    {
        return m_Flashlight.IsOn();
    }
};
