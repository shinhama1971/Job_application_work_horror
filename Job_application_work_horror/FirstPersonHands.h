// ============================================================================
// ファイルの役割: 一人称で画面の右下に映る「懐中電灯を持った右手」のモデルの、動きと描画を管理している。
// 主な技術: Mayaで作ったFBX（tools/maya/build_flashlight_hand.py、ModelCache経由）、カメラに付いた手元の空間、
//           視点を回したときに遅れて付いてくる揺れ（ばね）、歩く・走る・息の揺れ、ビューポートの深度の範囲を手前に寄せる描画
// ・手は壁にめり込まないよう、ビューポートの深度の範囲を 0〜0.02 にして描き、いつも壁より手前にしている
//   （本描画の深度は near=1 なので、0.02 より手前に壁が来ることはない）。
// ・手元だけの射影行列（画角を本描画より狭く、near を 0.1）を b9 で渡し、手の形がゆがまないようにしている。
// ・懐中電灯の光が前の壁で跳ね返る光（Playerが求めた前の壁までの距離から）で、手と懐中電灯を前から照らしている。
// ============================================================================

#pragma once

#include "Material.h"
#include "ModelCache.h"
#include "Shader.h"

#include <SimpleMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <vector>

class Camera;

// Player が持ち、Player::Update の最後に動きを、Player::Draw で描いている
class FirstPersonHands
{
public:
    // 1フレーム分の、手の動きを決める入力（Playerが渡している）
    struct FrameInput
    {
        float DeltaTime = 0.0f;
        // カメラの向き（ヨー・ピッチ。ラジアン）
        float Yaw = 0.0f;
        float Pitch = 0.0f;
        // 歩くときの頭の揺れ（上下・左右）と、動く速さの割合（0〜1）
        float HeadBobVertical = 0.0f;
        float HeadBobSide = 0.0f;
        float SpeedRate = 0.0f;
        bool Sprinting = false;
        // 懐中電灯の状態：レンズの光（0〜1）、電池の弱り（0〜1）、電池の少なさで光が揺れる強さ（0〜1）
        float LensGlow = 0.0f;
        float BatteryWeakness = 0.0f;
        float BatteryStress = 0.0f;
        // このフレームでスイッチを押したか、光が一瞬暗くなり始めたか
        bool Toggled = false;
        bool VoltageDrop = false;
        // 前の面の近さ（0〜1。Playerの m_FlashlightNearSurfaceBlend）
        float NearSurface = 0.0f;
        // 手を下げて画面の外へ出すか（ロッカーに隠れている間、操作できない演出の間）
        bool Lowered = false;
        // 前の面で跳ね返って手を照らす光（色と強さ）
        DirectX::SimpleMath::Vector3 Bounce = DirectX::SimpleMath::Vector3::Zero;
    };

    void Init();
    void Update(const FrameInput& input);
    // プレイヤーの視点の本描画の中で呼んでいる（監視映像など、ほかの視点では呼ばない）
    void Draw(Camera& camera);
    void Uninit();

    // 自動撮影で、揺れの途中ではなく落ち着いた姿勢から写すため、揺れを止めている
    void ResetMotion();

private:
    // viewModel.hlsli の ViewModelBuffer と同じ並び（b9）
    struct VIEW_MODEL_BUFFER
    {
        DirectX::SimpleMath::Matrix Projection;
        DirectX::SimpleMath::Vector4 Eye;
        DirectX::SimpleMath::Vector4 Bounce;
        DirectX::SimpleMath::Vector4 BounceDirection;
        DirectX::SimpleMath::Vector4 State;
    };
    static_assert(sizeof(VIEW_MODEL_BUFFER) % 16 == 0, "定数バッファは16バイトの倍数にしている");

    // ばねで目標へ近づける値（位置と速さ）。視点の回転に遅れて付いてくる揺れに使っている
    struct Spring
    {
        float Value = 0.0f;
        float Velocity = 0.0f;
        void Update(float target, float stiffness, float damping, float deltaTime);
    };

    std::shared_ptr<ModelData> m_ModelData;
    std::vector<std::unique_ptr<Material>> m_Materials;
    Shader m_FlashlightShader;
    Shader m_SkinShader;
    Shader m_ClothShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ViewModelBuffer;

    // 手元の空間 → ビュー空間の行列（Update で作り、Draw でカメラの逆行列を掛けてワールドにしている）
    DirectX::SimpleMath::Matrix m_ViewPose = DirectX::SimpleMath::Matrix::Identity;

    // 動きの状態
    float m_Time = 0.0f;
    float m_PreviousYaw = 0.0f;
    float m_PreviousPitch = 0.0f;
    bool m_HasPreviousLook = false;
    Spring m_LookSwayYaw;
    Spring m_LookSwayPitch;
    Spring m_BobX;
    Spring m_BobY;
    float m_Sprint = 0.0f;       // 走っている度合い（0〜1）
    float m_Lower = 1.0f;        // 手を下げている度合い（1で画面の外）
    float m_Click = 0.0f;        // スイッチを押したときの、小さく前へ押す動き
    float m_Jolt = 0.0f;         // 光が一瞬暗くなったときに、懐中電灯を振る動き
    float m_NearSurface = 0.0f;
    float m_LensGlow = 0.0f;
    float m_BatteryWeakness = 0.0f;
    DirectX::SimpleMath::Vector3 m_Bounce = DirectX::SimpleMath::Vector3::Zero;
};
