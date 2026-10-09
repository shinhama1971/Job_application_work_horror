// ============================================================================
// ファイルの役割: すべてのゲームオブジェクトに共通する処理（ワールド座標の境界球の計算）を実装している。
// 主な技術: 拡大・回転・移動の変換、境界球
// ============================================================================
#include "Object.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

// ローカル座標の境界の中心をワールド座標へ変換し、半径は3軸の大きさのうち最大のもので拡大している（どの向きに回しても球に収まる）
WorldBoundingSphere Object::GetWorldBoundingSphere() const
{
	const Matrix world = Matrix::CreateScale(m_Scale) *
		Matrix::CreateFromYawPitchRoll(
			m_Rotation.y, m_Rotation.x, m_Rotation.z) *
		Matrix::CreateTranslation(m_Position);

	WorldBoundingSphere sphere;
	sphere.Center = Vector3::Transform(m_ModelBounds->Center, world);
	const float maximumScale = (std::max)(
		std::abs(m_Scale.x),
		(std::max)(std::abs(m_Scale.y), std::abs(m_Scale.z)));
	sphere.Radius = m_ModelBounds->SphereRadius * maximumScale;
	return sphere;
}
