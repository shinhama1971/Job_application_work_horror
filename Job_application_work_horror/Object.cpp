// ============================================================================
// ファイルの役割: 全ゲームオブジェクト共通の座標、姿勢、寿命、仮想関数を定義します。
// 主な技術: 基底クラス、仮想関数、ライフサイクル管理
// ============================================================================
#include "Object.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

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
