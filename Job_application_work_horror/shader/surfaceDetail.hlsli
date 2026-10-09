// ============================================================================
// 共通の処理: シェーダーで計算した「高さ」から、凹凸のある法線を作っている（法線マップを使わないバンプ）。
// 画面上の偏微分（ddx/ddy）で高さの傾きを求めるため、どんな形の面にもそのまま使える。
// litTexturePS（壁）と wetFloorPS（床）で共通に使っている。
// ============================================================================
#ifndef SURFACE_DETAIL_INCLUDED
#define SURFACE_DETAIL_INCLUDED

// normal は元のワールドの法線、height はその画素の高さ（へこみは負）、strength は凹凸の強さ。
float3 ApplyHeightToNormal(float3 worldPosition, float3 normal, float height, float strength)
{
    // 隣の画素との高さの差と、位置の差から、面に沿った高さの傾きを求めて、法線を傾けている
    const float heightDx = ddx(height);
    const float heightDy = ddy(height);
    const float3 positionDx = ddx(worldPosition);
    const float3 positionDy = ddy(worldPosition);
    const float3 gradientX = cross(positionDy, normal);
    const float3 gradientY = cross(normal, positionDx);
    const float determinant = dot(positionDx, gradientX);
    const float inverseDeterminant =
        (determinant < 0.0f ? -1.0f : 1.0f) /
        max(abs(determinant), 0.0001f);
    const float3 surfaceGradient =
        (gradientX * heightDx + gradientY * heightDy) *
        inverseDeterminant;
    return normalize(normal - surfaceGradient * strength);
}

#endif
