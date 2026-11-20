// ============================================================================
// ファイルの役割: 一人称視点の位置・向きと、描画に使うビュー行列・射影行列を管理している。
// 主な技術: DirectXMath、ビュー行列、透視投影、マウスルック、ヨー・ピッチによる向きの制御
// ============================================================================

#pragma once

#include <Windows.h>
#include <SimpleMath.h>

// プレイヤーの目の位置と向きを持ち、そこから描画用の行列を作るカメラ。
// 位置はPlayerが毎フレーム設定し、向きはこのクラスが入力から更新している。
class Camera
{
private:
    // ポーズメニューの視点感度の倍率の範囲
    static constexpr float MinLookSensitivityScale = 0.55f;
    static constexpr float MaxLookSensitivityScale = 1.55f;

    // マウス視点で、前フレームのカーソル位置と、切り替え直後の1回目かどうか
    POINT m_LastMousePos{};
    bool m_FirstMouse = true;

    // マウス視点が有効か、ポーズメニューで設定した視点感度の倍率
    bool m_MouseLookEnable = true;
    float m_LookSensitivityScale = 1.0f;
    // 方向キーで視点を回すか（技術デモで方向キーを使う間は回さない）
    bool m_ArrowKeyLookEnable = true;

    // カメラの位置（プレイヤーの目の高さ）
    DirectX::SimpleMath::Vector3 m_Position =
        DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

    // 反射・監視カメラの映像を描く間だけ使う、差し替え用の行列
    bool m_UseOverrideMatrices = false;
    DirectX::SimpleMath::Matrix m_OverrideView =
        DirectX::SimpleMath::Matrix::Identity;
    DirectX::SimpleMath::Matrix m_OverrideProjection =
        DirectX::SimpleMath::Matrix::Identity;

    // ヨー（水平の向き）とピッチ（上下の向き）。単位はラジアン
    float m_CameraDirection = 0.0f;
    float m_CameraPitch = 0.0f;

    // 見え方だけを、カメラの位置を中心に z 方向（南北）へ引き伸ばす倍率（1で元のまま）。
    // 1面のループ廊下で、振り返った廊下が伸びて見える演出に使っている。当たり判定と光の計算は元の位置のまま
    float m_WorldStretchZ = 1.0f;

public:
    void Init();
    void Update();
    // 描画に使う行列をシェーダーへ設定している。0=プレイヤー視点の3D（差し替えた行列があればそれを使う）、
    // それ以外=画面中央を原点とする2Dの正射影。
    void SetCamera(int mode = 0);
    void Uninit();

    // ポーズメニューの視点感度を反映している（0.55〜1.55倍に制限している）。
    void SetLookSensitivityScale(float scale)
    {
        m_LookSensitivityScale = scale < MinLookSensitivityScale
            ? MinLookSensitivityScale
            : (scale > MaxLookSensitivityScale ? MaxLookSensitivityScale : scale);
    }

    // フォーカスが戻ってから最初のマウス移動量は視点に反映しない。
    void ResetMouseLook() { m_FirstMouse = true; }

    // プレイヤー視点のビュー・射影行列を返している（左手系、画角60度、near 1、far 1000）。
    // タイルベースのライトカリングも同じ行列を使っている。
    void GetMainMatrices(
        DirectX::SimpleMath::Matrix& view,
        DirectX::SimpleMath::Matrix& projection) const;
    // 反射や監視カメラなど別の視点で描く間だけ、SetCamera(0)が使う行列を差し替えている。
    // 描き終えたらClearOverrideMatricesで元に戻している。
    void SetOverrideMatrices(
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& projection);
    void ClearOverrideMatrices();

    // 見え方だけを z 方向へ引き伸ばす倍率を決めている（1で元のまま。1より小さい値は1にしている）。
    // シーンが毎フレーム決め直し、シーンを切り替えたら1に戻している。
    void SetWorldStretchZ(float scale)
    {
        m_WorldStretchZ = scale < 1.0f ? 1.0f : scale;
    }

    float GetWorldStretchZ() const
    {
        return m_WorldStretchZ;
    }

    // カメラの位置を設定・取得している（Playerが目の高さに合わせて毎フレーム設定している）
    void SetPosition(DirectX::SimpleMath::Vector3 pos)
    {
        m_Position = pos;
    }

    DirectX::SimpleMath::Vector3 GetPosition() const
    {
        return m_Position;
    }

    // 水平方向の向き（ヨー、ラジアン）を返している。
    float GetCameraDirection() const
    {
        return m_CameraDirection;
    }

    // 隠れ場所に入ったときなど、向きを直接決めるときに使っている。
    void SetCameraDirection(float direction)
    {
        m_CameraDirection = direction;
    }

    // 上下の向きを直接決めている
    void SetCameraPitch(float pitch)
    {
        m_CameraPitch = pitch;
    }

    // falseの間はマウスで視点を動かさない（自動撮影モードで使っている）。
    void SetMouseLookEnabled(bool enabled)
    {
        if (enabled != m_MouseLookEnable)
        {
            ResetMouseLook();
        }
        m_MouseLookEnable = enabled;
    }

    // falseの間は方向キーで視点を回さない（技術デモで方向キーを方式の切り替えに使っている）。
    void SetArrowKeyLookEnabled(bool enabled)
    {
        m_ArrowKeyLookEnable = enabled;
    }

    // 上下の向き（ピッチ、ラジアン）を返している。
    float GetCameraPitch() const
    {
        return m_CameraPitch;
    }

    // ヨーとピッチから求めた、視線の向きの単位ベクトルを返している。
    DirectX::SimpleMath::Vector3 GetForward() const;

    // 境界球が今のカメラの視野に入るかを、見える側に寄せて判定している。
    // 少し広めの余白を含め、画面の端で大きな物が急に消えるのを防いでいる。testVerticalがfalseなら上下は判定しない。
    bool IsSphereVisible(
        const DirectX::SimpleMath::Vector3& center,
        float radius,
        bool testVertical = true) const;
};
