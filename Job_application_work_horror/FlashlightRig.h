// ============================================================================
// ファイルの役割: 懐中電灯の影を作る位置（手元）と、影を作る向きを決めている。
// 主な技術: ビュー空間とワールド空間の変換（カメラの右・上・前の3つの向き）
// 懐中電灯の光（明るさの円）は、壁の文字を読みやすいよう、これまでどおり視線の中央から照らしている。
// 影まで目の位置から作ると、物の影がちょうどその物の真後ろに隠れて画面に映らない。
// そこで影だけを、目より少し右下（手元）から作り、棚や配管の影が壁や床に映るようにしている。
// 光と影の出どころが数単位ずれるが、プレイヤーには光源の位置が見えないため、手で持ったライトの影に見える。
// ============================================================================

#pragma once

#include <SimpleMath.h>

namespace FlashlightRig
{
    // 影を作る手元の位置（ビュー空間：xが右、yが上、zが前）。1単位は約5cm（部屋の高さが約50単位）。
    inline const DirectX::SimpleMath::Vector3 HandOffset(5.5f, -6.5f, 4.0f);
    // 影を作る向き：手元から、画面の中央のこの距離の先へ向けている（光の円とほぼ同じ範囲を覆う）
    inline constexpr float AimDistance = 150.0f;

    // カメラの位置と前の向きから、ワールド空間の手元の位置と、影を作る向きを求めている。
    // 右と上の向きは、カメラのビュー行列（XMMatrixLookAtLH、上は+Y）と同じ求め方にしている。
    inline void GetShadowPose(
        const DirectX::SimpleMath::Vector3& eye,
        const DirectX::SimpleMath::Vector3& forward,
        DirectX::SimpleMath::Vector3& origin,
        DirectX::SimpleMath::Vector3& direction)
    {
        DirectX::SimpleMath::Vector3 front = forward;
        front.Normalize();
        DirectX::SimpleMath::Vector3 right =
            DirectX::SimpleMath::Vector3::Up.Cross(front);
        right.Normalize();
        const DirectX::SimpleMath::Vector3 up = front.Cross(right);

        origin = eye + right * HandOffset.x + up * HandOffset.y + front * HandOffset.z;
        direction = eye + front * AimDistance - origin;
        direction.Normalize();
    }
}
