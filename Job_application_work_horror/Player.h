// ============================================================================
// ファイルの役割: 一人称の移動、視点、懐中電灯、電池、足音、ロッカーに隠れる操作を管理している。
// 主な技術: 一人称の操作、壁との当たり判定と押し戻し、歩くときの頭の揺れ、スポットライト、キーボードとコントローラーの入力の統合
// ============================================================================

#pragma once

#include "Object.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "FlashlightSystem.h"
#include "PlayerMovement.h"

// プレイヤー。足元の位置を持ち、毎フレーム入力から移動し、目の高さにカメラを置いて懐中電灯の光を作っている。
class Player : public Object
{
private:
    // ===== 移動と当たり判定 =====
    // 座標の単位はステージ共通。PLAYER_HEIGHTはカメラではなく、当たり判定の体の高さ。
    PlayerMovement m_Movement;
    // 当たり判定の半径・体の高さ・足元の最低の高さ（床の高さ）
    static constexpr float PLAYER_RADIUS = 4.0f;
    static constexpr float PLAYER_HEIGHT = 34.0f;
    static constexpr float MIN_Y_POSITION = -99.0f;

    // 当たり判定の半径、経過時間（手持ちライトの揺れ・呼吸の揺れに使っている）、次の足音までの秒数、足音の番号（左右で音の高さを変える）
    float m_Radius = PLAYER_RADIUS;
    float m_AmbienceTimer = 0.0f;
    float m_FootstepTimer = 0.0f;
    int m_FootstepIndex = 0;
    // 直前の足音の大きさ（2面の危険度に使っている）、Sceneから知らされた水の床の上か
    float m_SurfaceNoisePulse = 0.0f;
    bool m_WetSurfaceOverride = false;

    // ===== 懐中電灯と電池 =====
    // 電池の残りがしきい値を下回ると、完全に消える前に不安定な点滅へ移っている。
    FlashlightSystem m_Flashlight;
    // 壁や床が近いほど1に近づく値（近いときに光を弱めて白飛びを防ぐのに使っている）
    float m_FlashlightNearSurfaceBlend = 0.0f;

    // ===== ライティングの調整値 =====
    // 点灯しているときの色。Diffuseは照らす光、Ambientは最低限残す環境光。
    float m_LightDiffuseR = 1.72f;
    float m_LightDiffuseG = 1.62f;
    float m_LightDiffuseB = 1.42f;
    float m_LightAmbientR = 0.1f;
    float m_LightAmbientG = 0.1f;
    float m_LightAmbientB = 0.12f;

    // 消灯しているときの色。真っ暗で進めなくならないための、最低限の明るさを持っている。
    float m_DarkDiffuseR = 0.3f;
    float m_DarkDiffuseG = 0.3f;
    float m_DarkDiffuseB = 0.35f;
    float m_DarkAmbientR = 0.06f;
    float m_DarkAmbientG = 0.06f;
    float m_DarkAmbientB = 0.08f;
    // 足元から目までの高さ
    float m_CameraHeightOffset = 40.0f;

    // ===== プレイヤーのメッシュの描画資源（三人称のデバッグ表示だけで使っている） =====
    MeshRenderer m_MeshRenderer;
    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    // ===== カメラと操作の状態 =====
    bool m_IsFPS = true;                    // trueなら一人称視点
    bool m_SpawnAdjusted = false;           // 最初の位置の床への補正が終わったか
    // 操作できるか、走ってよいか
    bool m_CanControl = true;
    bool m_SprintAllowed = true;

    // ===== 隠れ場所（ロッカー） =====
    // 隠れている間は移動できず、懐中電灯も点けられない。視点は扉の正面から左右に少しだけ動かせる。
    static constexpr float HidingLookRange = 0.65f;     // 扉の正面から左右に見回せる角度（ラジアン）
    static constexpr float HidingInputDelay = 0.35f;    // 入った直後・出た直後に、同じキーで出入りしないための待ち時間
    // 隠れているか
    bool m_IsHiding = false;
    DirectX::SimpleMath::Vector3 m_HidePosition;        // 隠れている間の足元の位置
    DirectX::SimpleMath::Vector3 m_HideExitPosition;    // 外へ出たときに立つ位置
    float m_HideFacing = 0.0f;                          // 扉の正面の向き（ヨー）
    // 次に出入りの操作を受け付けるまでの秒数
    float m_HidingInputTimer = 0.0f;

    // 隠れている間の見回せる範囲の制限と、外へ出る操作
    void UpdateHiding(Camera& camera, float deltaTime);
public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    // カメラの水平方向の向き（上下の傾きを除いた単位ベクトル）を返している。
    DirectX::SimpleMath::Vector3 GetForward() const;
    // 電池を回復している
    void AddBattery(float value)
    {
        m_Flashlight.AddBattery(value);
    }
    // HUDとアイテムの判定が参照する、今の電池の残り（0〜100）を返している。
    float GetBattery() const
    {
        return m_Flashlight.GetBattery();
    }

    // 電池を拾った通知をHUDに出す残り時間を返している（0なら表示しない）。
    float GetBatteryNoticeTimer() const
    {
        return m_Flashlight.GetBatteryNoticeTimer();
    }
 
    // ワープしたときに勢いが残らないよう、座標と一緒に速度も0にしている。
    void SetPosition(DirectX::SimpleMath::Vector3 pos)
    {
        m_Position = pos;
        m_Movement.ResetVelocity();
    }
    
    // 一人称視点か
    bool IsFPS() const { return m_IsFPS; }

    // falseの間はPlayerの更新を止め、移動と操作を受け付けない（捕まった演出や監視映像を見ている間など）。
    void SetCanControl(bool enable)
    {
        m_CanControl = enable;
    }

    bool CanControl() const
    {
        return m_CanControl;
    }

    // 走っているか
    bool IsSprinting() const
    {
        return m_Movement.IsSprinting();
    }

    // falseの間は走る入力を無視し、歩きだけにしている（2面の周回中など）。
    void SetSprintAllowed(bool allowed)
    {
        m_SprintAllowed = allowed;
    }

    bool IsSprintAllowed() const
    {
        return m_SprintAllowed;
    }

    // 実際に水平方向へ動いているか
    bool IsMovingHorizontally() const
    {
        return m_Movement.IsMovingHorizontally();
    }

    // Sceneが持つ特別な濡れた床（2面の水たまりなど）の上にいることを、次のUpdateへ伝えている。
    void SetWetSurface(bool wet) { m_WetSurfaceOverride = wet; }
    // 直前の足音がどれだけ大きかったか（0〜1）。水たまりや走っているときに大きくなり、2面の危険度に足されている。
    float GetSurfaceNoisePulse() const { return m_SurfaceNoisePulse; }

    // スタミナの残りを返す・満タンに戻している
    float GetStamina() const
    {
        return m_Movement.GetStamina();
    }

    void RestoreStamina()
    {
        m_Movement.RestoreStamina();
    }

    // 懐中電灯が点いているか
    bool IsFlashlightOn() const
    {
        return m_Flashlight.IsOn();
    }

    // ロッカーに入っている。hidePositionは隠れている間の足元、exitPositionは出たときに立つ位置、
    // facingは扉の正面の向き（ヨー）。懐中電灯は自動で消している。
    void EnterHiding(
        const DirectX::SimpleMath::Vector3& hidePosition,
        const DirectX::SimpleMath::Vector3& exitPosition,
        float facing);
    // 隠れているか
    bool IsHiding() const { return m_IsHiding; }
    // 懐中電灯を点けた状態・消した状態にしている（自動撮影モードで使っている。音は鳴らさない）。
    void SetFlashlightOn(bool on)
    {
        if (m_Flashlight.IsOn() != on)
        {
            m_Flashlight.Toggle();
        }
    }
    // 捕まったときなど、その場でロッカーから出た扱いにしている（位置は呼び出し側が決めている）。
    void ForceExitHiding()
    {
        m_IsHiding = false;
        m_HidingInputTimer = HidingInputDelay;
    }
};
