// ============================================================================
// ファイルの役割: 一人称視点の位置、向き、ビュー行列、射影行列を管理します。
// 主な技術: DirectXMath、ビュー行列、透視投影、マウスルック、オイラー角制御
// ============================================================================

#pragma once

#include <Windows.h>
#include <SimpleMath.h>

class Camera
{
private:
    POINT m_LastMousePos{};
    bool m_FirstMouse = true;

    bool m_MouseLookEnable = true;
    float m_LookSensitivityScale = 1.0f;

    DirectX::SimpleMath::Vector3 m_Position =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

    DirectX::SimpleMath::Vector3 m_Rotation =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

    DirectX::SimpleMath::Vector3 m_Scale =
        DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

    DirectX::SimpleMath::Vector3 m_Target{};
    DirectX::SimpleMath::Matrix m_ViewMatrix{};

    bool m_UseOverrideMatrices = false;
    DirectX::SimpleMath::Matrix m_OverrideView =
        DirectX::SimpleMath::Matrix::Identity;
    DirectX::SimpleMath::Matrix m_OverrideProjection =
        DirectX::SimpleMath::Matrix::Identity;

    float m_CameraDirection = 0.0f;
    float m_CameraPitch = 0.0f;


public:
    void Init();
    void Update();
    // 描画に使う行列をシェーダーへ設定します。0=プレイヤー視点の3D（上書き行列があればそれを使用）、
    // それ以外=画面中央原点の2D正射影です。
    void SetCamera(int mode = 0);
    void Uninit();

    // ポーズメニューの視点感度を反映します（0.55〜1.55倍に制限）。
    void SetLookSensitivityScale(float scale)
    {
        m_LookSensitivityScale = scale < 0.55f
            ? 0.55f
            : (scale > 1.55f ? 1.55f : scale);
    }

    void SetTarget(DirectX::SimpleMath::Vector3 target);

    // プレイヤー視点のビュー・射影行列を返します（左手系、画角60度、near 1、far 1000）。
    // タイルベースのライトカリングも同じ行列を使います。
    void GetMainMatrices(
        DirectX::SimpleMath::Matrix& view,
        DirectX::SimpleMath::Matrix& projection) const;
    // 反射や監視カメラなど別視点で描く間だけ、SetCamera(0)が使う行列を差し替えます。
    // 描き終えたらClearOverrideMatricesで元に戻します。
    void SetOverrideMatrices(
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& projection);
    void ClearOverrideMatrices();

    void SetPosition(DirectX::SimpleMath::Vector3 pos)
    {
        m_Position = pos;
    }

    DirectX::SimpleMath::Vector3 GetPosition() const
    {
        return m_Position;
    }

    // 水平方向の向き（ヨー、ラジアン）です。
    float GetCameraDirection() const
    {
        return m_CameraDirection;
    }

    // 上下の向き（ピッチ、ラジアン）です。
    float GetCameraPitch() const
    {
        return m_CameraPitch;
    }

    // ヨーとピッチから求めた、視線の向きの単位ベクトルです。
    DirectX::SimpleMath::Vector3 GetForward() const;

    // 境界球が現在のカメラ視野に入るかを保守的に判定します。
    // 少し広めの余白を含め、画面端で大型オブジェクトが急に消えるのを防ぎます。
    bool IsSphereVisible(
        const DirectX::SimpleMath::Vector3& center,
        float radius,
        bool testVertical = true) const;
};
