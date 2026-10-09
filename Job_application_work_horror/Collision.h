// ============================================================================
// ファイルの役割: 線・線分・平面・三角形・球・AABBの当たり判定と、最も近い点の計算を提供している。
// 主な技術: AABB、球、線分、平面、三角形の内外判定、最近接点
// ============================================================================

//================================
// 衝突判定の宣言（バージョン1.0）
//================================
#pragma once

#include <simplemath.h>

namespace Collision
{
	// ライン（両方向に無限に続く直線）
	struct Line {
		DirectX::SimpleMath::Vector3 point; // 直線が通る1点
		DirectX::SimpleMath::Vector3 vec; // 直線の向き
	};

	// プレーン（無限に広がる平面）
	struct Plane {
		DirectX::SimpleMath::Vector3 point; // 平面上の1点
		DirectX::SimpleMath::Vector3 normal; // 平面の法線ベクトル
		// （未使用）平面の方程式 ax + by + cz + d = 0 の d
	};

	// セグメント（始点と終点のある線分）
	struct Segment {
		DirectX::SimpleMath::Vector3 start; // 始点
		DirectX::SimpleMath::Vector3 end; // 終点
	};

	// 三角形ポリゴン（3頂点で囲まれた有限の平面）
	struct Polygon {
		const DirectX::SimpleMath::Vector3 p0; // 頂点0
		const DirectX::SimpleMath::Vector3 p1; // 頂点1
		const DirectX::SimpleMath::Vector3 p2; // 頂点2
	};

	// 球
	struct Sphere {
		DirectX::SimpleMath::Vector3 center; // 中心
		float radius; // 半径
	};


	// AABB（各軸に平行な箱）。
	// 最小の角と最大の角の2点で表している
	struct AABB {
		// x・y・zそれぞれが最も小さい角
		DirectX::SimpleMath::Vector3 min;
		// x・y・zそれぞれが最も大きい角
		DirectX::SimpleMath::Vector3 max;
	};

	// 当たり判定（contactを渡す版は、当たった位置も返している）
	bool CheckHit(const Line& line, const Plane& plane); // 直線と平面
	bool CheckHit(const Segment& segment, const Plane& plane); // 線分と平面
	bool CheckHit(const Line& line, const Polygon& polygon); // 直線と三角形
	bool CheckHit(const Line& line, const Polygon& polygon, DirectX::SimpleMath::Vector3& contact); // 直線と三角形（交点も返す）
	bool CheckHit(const Segment& segment, const Polygon& polygon); // 線分と三角形
	bool CheckHit(const Segment& segment, const Polygon& polygon, DirectX::SimpleMath::Vector3& contact); // 線分と三角形（交点も返す）
	bool CheckHit(const Sphere& sphere, const Plane& plane); // 球と平面
	bool CheckHit(const Sphere& sphere, const Polygon& polygon); // 球と三角形
	bool CheckHit(const Sphere& sphere, const Polygon& polygon, DirectX::SimpleMath::Vector3& contact); // 球と三角形（接触点も返す）
	bool CheckHit(Sphere sphere1, Sphere sphere2); // 球と球
	bool CheckHit(Sphere sphere1, Sphere sphere2, DirectX::SimpleMath::Vector3& contact); // 球と球（接触点も返す）
	bool CheckHit(AABB p1, AABB p2); // AABBとAABB

	// 内積・外積
	float Dot(const DirectX::SimpleMath::Vector3& v1, const DirectX::SimpleMath::Vector3& v2);
	DirectX::SimpleMath::Vector3 Cross(const DirectX::SimpleMath::Vector3& v1, const DirectX::SimpleMath::Vector3& v2);


	// 点に最も近い線分上の点
	DirectX::SimpleMath::Vector3 ClosestPointOnSegment(const DirectX::SimpleMath::Vector3& point, const Segment& segment);
	// 点と線分の距離の2乗（contactには最も近い線分上の点を返している）
	float DistanceSquaredPointToSegment(const DirectX::SimpleMath::Vector3& point, const Segment& segment);
	float DistanceSquaredPointToSegment(const DirectX::SimpleMath::Vector3& point, const Segment& segment, DirectX::SimpleMath::Vector3& contact);
	// 点と線分の距離
	float DistancePointToSegment(const DirectX::SimpleMath::Vector3& point, const Segment& segment);
	float DistancePointToSegment(const DirectX::SimpleMath::Vector3& point, const Segment& segment, DirectX::SimpleMath::Vector3& contact);
	// 点と平面の距離
	float DistancePointToPlane(const DirectX::SimpleMath::Vector3& point, const Plane& plane);
	// 点から平面へ下ろした垂線の足
	DirectX::SimpleMath::Vector3 ProjectPointToPlane(const DirectX::SimpleMath::Vector3& point, const Plane& plane);
	// 平面上の点が三角形の内側にあるか
	bool PointInTriangle(const DirectX::SimpleMath::Vector3& point, const Polygon& polygon);
	// 点に最も近い三角形上の点
	DirectX::SimpleMath::Vector3 ClosestPointOnTriangle(const DirectX::SimpleMath::Vector3& point, const Polygon& polygon);
	// 三角形の法線（単位ベクトル）
	DirectX::SimpleMath::Vector3 GetNormal(const Polygon& polygon);

	// 中心と幅・高さ・奥行きからAABBを作っている
	AABB SetAABB(DirectX::SimpleMath::Vector3 centerposition, float width, float height, float depth);
}