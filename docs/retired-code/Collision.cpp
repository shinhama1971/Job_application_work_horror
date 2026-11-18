// ============================================================================
// ファイルの役割: 線・線分・平面・三角形・球・AABBの当たり判定と、最も近い点の計算を実装している。
// 主な技術: AABB、球、線分、平面、三角形の内外判定、最近接点
// ============================================================================

//================================
// 衝突判定の実装（バージョン1.0）
//================================
#include "Collision.h"
#include <algorithm>

using namespace DirectX::SimpleMath;

namespace Collision
{
	//==================================
	// ■CheckHit関数
	// 直線（無限の長さ）と平面（無限の広さ）が交わるかを判定している
	//==================================
	bool CheckHit(const Line& line, const Plane& plane)
	{
		// 無限の直線が無限の平面に当たらないのは、直線と平面が平行なときだけ。
		// 直線の向きと平面の法線が垂直（内積が0）なら平行。ただし直線が平面の上にあれば当たっているとしている
		if (Dot((plane.point - line.point), plane.normal) == 0 || Dot(line.vec, plane.normal) != 0)
		{
			return true;
		}
		return false;
	}

	//==================================
	// ■CheckHit関数
	// 線分と平面（無限の広さ）が交わるかを判定している
	//==================================
	bool CheckHit(const Segment& segment, const Plane& plane)
	{
		// 始点と終点が平面の反対側にある（法線方向の距離の符号が違う）か、どちらかが平面上にあれば交わっている
		if (Dot((segment.start - plane.point), plane.normal) * Dot((segment.end - plane.point), plane.normal) <= 0)
		{
			return true;
		}
		return false;
	}

	//==================================
	// ■CheckHit関数
	// 直線（無限の長さ）と三角形が交わるかを判定している
	//==================================
	bool CheckHit(const Line& line, const Polygon& polygon)
	{
		Vector3 p;
		return CheckHit(line, polygon, p);
	}
	// 交点（contact）も求める版
	bool CheckHit(const Line& line, const Polygon& polygon, DirectX::SimpleMath::Vector3& contact)
	{
		// 三角形の法線を計算している
		Vector3 normal = GetNormal(polygon);

		// 直線と法線が垂直（直線が平面と平行）なら交わらない
		float denom = Dot(normal, line.vec);
		if (fabs(denom) < 1e-6f) {
			return false; // 交差なし
		}

		// 直線と、三角形を含む平面との交点を計算している
		float d = Dot(normal, polygon.p0);
		float t = (d - Dot(normal, line.point)) / denom;
		contact = line.point + t * line.vec;

		// 交点が三角形の内側にあるかを判定している
		return PointInTriangle(contact, polygon);
	}

	//==================================
	// ■CheckHit関数
	// 線分と三角形が交わるかを判定している
	//==================================
	bool CheckHit(const Segment& segment, const Polygon& polygon)
	{
		Vector3 p;
		return CheckHit(segment, polygon, p);
	}
	// 交点（contact）も求める版。まず三角形を含む平面と線分が交わるかを調べている
	bool CheckHit(const Segment& segment, const Polygon& polygon, Vector3& contact)
	{
		Plane plane(polygon.p0, GetNormal(polygon));

		if (CheckHit(segment, plane))
		{
			// 線分と平面の交点の位置（始点からの割合t）を計算している
			float denom = plane.normal.Dot(segment.end - segment.start);
			float t = plane.normal.Dot(plane.point - segment.start) / denom;

			// tが0〜1の範囲なら、交点は線分の上にある
			if (t >= 0.0f && t <= 1.0f) {
				contact = segment.start + t * (segment.end - segment.start); // 交点の座標

				// 交点が三角形の内側にあるかを判定している
				return PointInTriangle(contact, polygon);
			}
		}
		return false;

	}

	//==================================
	// ■CheckHit関数
	// 球と平面が当たっているかを判定している
	//==================================
	bool CheckHit(const Sphere& sphere, const Plane& plane)
	{
		// 球の中心から平面までの距離を計算している
		float distance = DistancePointToPlane(sphere.center, plane);

		// 半径以内なら当たっている
		return (distance <= sphere.radius);
	}

	//==================================
	// ■CheckHit関数
	// 球と三角形が当たっているかを判定している
	//==================================
	bool CheckHit(const Sphere& sphere, const Polygon& polygon)
	{
		Vector3 p;
		return CheckHit(sphere, polygon, p);
	}
	// 接触点（contact）も求める版
	bool CheckHit(const Sphere& sphere, const Polygon& polygon, Vector3& contact)
	{
		Plane plane(polygon.p0, GetNormal(polygon));

		// 球の中心から、三角形を含む平面までの距離を計算している
		float distance = DistancePointToPlane(sphere.center, plane);

		// 半径以内なら、さらに三角形の範囲内かを調べている
		if (distance <= sphere.radius)
		{
			// 球の中心を平面へ投影した点を求めている
			contact = ProjectPointToPlane(sphere.center, plane);

			// その点が三角形の内側なら当たっている
			if (PointInTriangle(contact, polygon))
			{
				return true;
			}

			// 三角形の外側なら、3つの辺それぞれとの距離が半径以内かを調べている（辺の近くをかすめた場合）
			if (DistancePointToSegment(sphere.center, { polygon.p0 , polygon.p1 }, contact) <= sphere.radius)
			{
				return true;
			}

			if (DistancePointToSegment(sphere.center, { polygon.p1 , polygon.p2 }, contact) <= sphere.radius)
			{
				return true;
			}

			if (DistancePointToSegment(sphere.center, { polygon.p2 , polygon.p0 }, contact) <= sphere.radius)
			{
				return true;
			}
		}

		return false;
	}

	//==================================
	// ■CheckHit関数
	// 球と球が当たっているかを判定している
	//==================================
	bool CheckHit(Sphere sphere1, Sphere sphere2)
	{
		Vector3 p;
		return CheckHit(sphere1, sphere2, p);
	}
	// 接触点（contact）も求める版。中心間の距離の2乗と、半径の和の2乗を比べている（平方根を省くため）
	bool CheckHit(Sphere sphere1, Sphere sphere2, Vector3& contact)
	{
		float len2 = (sphere1.center - sphere2.center).LengthSquared();
		float r2 = (sphere1.radius + sphere2.radius) * (sphere1.radius + sphere2.radius);
		if (r2 > len2) {

			// 接触点を計算している（sphere1が後からsphere2にぶつかってきたとして、sphere2の表面の点にしている）
			Vector3 v = (sphere1.center - sphere2.center);
			v.Normalize();
			contact = sphere2.center + v * sphere2.radius;

			return true;
		}
		return false;
	}

	//==================================
	// ■CheckHit関数
	// AABBとAABBが重なっているかを判定している（どれか1つの軸で離れていれば重なっていない）
	//==================================
	bool CheckHit(AABB p1, AABB p2) {

		// X軸で離れているか
		if (p1.max.x < p2.min.x) {
			return false;
		}

		if (p1.min.x > p2.max.x) {
			return false;
		}

		// Y軸で離れているか
		if (p1.max.y < p2.min.y) {
			return false;
		}

		if (p1.min.y > p2.max.y) {
			return false;
		}

		// Z軸で離れているか
		if (p1.max.z < p2.min.z) {
			return false;
		}

		if (p1.min.z > p2.max.z) {
			return false;
		}

		return true;
	}


	//==================================
	// ■Dot関数
	// 2つのベクトルの内積を求めている
	//==================================
	float Dot(const Vector3& v1, const Vector3& v2)
	{
		return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
	}

	//==================================
	// ■Cross関数
	// 2つのベクトルの外積を求めている
	//==================================
	Vector3 Cross(const Vector3& v1, const Vector3& v2)
	{
		return Vector3(
			v1.y * v2.z - v1.z * v2.y,	// x成分
			v1.z * v2.x - v1.x * v2.z,	// y成分
			v1.x * v2.y - v1.y * v2.x);	// z成分
	}

	//==================================
	// ■ClosestPointOnSegment関数
	// 点に最も近い線分上の点を求めている
	//==================================
	Vector3 ClosestPointOnSegment(const Vector3& point, const Segment& segment)
	{
		// 線分のベクトル（終点 - 始点）
		Vector3 vec = segment.end - segment.start;

		// 線分の長さの2乗
		double r2 = vec.x * vec.x + vec.y * vec.y + vec.z * vec.z;

		// 点から始点へのベクトルと線分のベクトルの内積から、点が線分のどのあたりに投影されるかを求めている
		double tt = -Dot(vec, (segment.start - point));

		// 点が始点よりも外側に投影される場合
		if (tt < 0)
		{
			// 始点が最も近い
			return segment.start;
		}
		// 点が終点よりも外側に投影される場合
		else if (tt > r2)
		{
			// 終点が最も近い
			return segment.end;
		}
		// 点が線分の上に投影される場合、投影した位置を求めている
		else
		{
			Vector3 ab = segment.end - segment.start; // 線分のベクトル
			float lengthSq = ab.LengthSquared(); // 線分の長さの2乗

			// 線分の長さが0（両端が同じ点）の場合
			if (lengthSq == 0.0f) {
				return segment.start; // 端点を返している
			}

			// 点を線分へ投影した位置を、始点からの割合t（0〜1）で求めている
			float t = (point - segment.start).Dot(ab) / lengthSq;
			t = std::clamp(t, 0.0f, 1.0f);

			// 線分上の最も近い点を返している
			return (segment.start + t * ab);
		}
	}

	//==================================
	// ■DistanceSquaredPointToSegment関数
	// 点と線分の距離の2乗を求めている（contactには最も近い線分上の点を返している）
	//==================================
	float DistanceSquaredPointToSegment(const Vector3& point, const Segment& segment)
	{
		Vector3 p;
		return DistanceSquaredPointToSegment(point, segment, p);
	}
	float DistanceSquaredPointToSegment(const Vector3& point, const Segment& segment, Vector3& contact)
	{
		contact = ClosestPointOnSegment(point, segment);

		return (point - contact).LengthSquared();
	}

	//==================================
	// ■DistancePointToSegment関数
	// 点と線分の距離を求めている（contactには最も近い線分上の点を返している）
	//==================================
	float DistancePointToSegment(const Vector3& point, const Segment& segment)
	{
		Vector3 p;
		return DistancePointToSegment(point, segment, p);
	}
	float DistancePointToSegment(const Vector3& point, const Segment& segment, Vector3& contact)
	{
		contact = ClosestPointOnSegment(point, segment);

		return (point - contact).Length();
	}

	//==================================
	// ■DistancePointToPlane関数
	// 点と平面の距離を求めている（法線が単位ベクトルでなくても正しく求まるよう、法線の長さの2乗で割っている）
	//==================================
	float DistancePointToPlane(const Vector3& point, const Plane& plane)
	{
		return fabs(Dot((point - plane.point), plane.normal) / Dot(plane.normal, plane.normal));
	}

	//==================================
	// ■ProjectPointToPlane関数
	// 点から平面へ下ろした垂線の足（平面上で最も近い点）を求めている
	//==================================
	Vector3 ProjectPointToPlane(const Vector3& point, const Plane& plane)
	{
		double t = -Dot((point - plane.point), plane.normal) / Dot(plane.normal, plane.normal);

		// 垂線の足の座標を計算している
		return  point + (plane.normal * (float)t);
	}

	//==================================
	// ■PointInTriangle関数
	// 平面上の点が三角形の内側にあるかを判定している
	//==================================
	bool PointInTriangle(const Vector3& point, const Polygon& polygon)
	{
		// 3辺のベクトル
		Vector3 ab = polygon.p1 - polygon.p0;
		Vector3 bc = polygon.p2 - polygon.p1;
		Vector3 ca = polygon.p0 - polygon.p2;

		// 各頂点から点へのベクトル
		Vector3 ap = point - polygon.p0;
		Vector3 bp = point - polygon.p1;
		Vector3 cp = point - polygon.p2;

		// 各辺と点へのベクトルの外積（点が辺のどちら側にあるかで向きが変わる）
		Vector3	n1 = Cross(ab, ap);
		Vector3	n2 = Cross(bc, bp);
		Vector3	n3 = Cross(ca, cp);

		// 三角形の法線ベクトル
		Vector3	normal = Cross(ab, bc);

		float dot = n1.Dot(normal);
		if (dot < 0) return false; // 法線と逆向き＝点がこの辺の外側にある

		dot = n2.Dot(normal);
		if (dot < 0) return false; // 法線と逆向き＝点がこの辺の外側にある

		dot = n3.Dot(normal);
		if (dot < 0) return false; // 法線と逆向き＝点がこの辺の外側にある

		return true;
	}

	//==================================
	// ■ClosestPointOnTriangle関数
	// 点に最も近い三角形上の点を求めている
	//==================================
	Vector3 ClosestPointOnTriangle(const Vector3& point, const Polygon& polygon)
	{
		Plane plane(polygon.p0, GetNormal(polygon));

		// 点を三角形を含む平面へ投影している
		Vector3 p = ProjectPointToPlane(point, plane);

		// 投影した点が三角形の内側なら、それが最も近い点
		if (PointInTriangle(p, polygon))
		{
			return p;
		}

		// 外側なら、3つの辺それぞれで最も近い点を求め、その中で一番近いものを選んでいる
		Vector3 p1, p2, p3;
		float d1 = DistanceSquaredPointToSegment(point, { polygon.p0 , polygon.p1 }, p1);
		float d2 = DistanceSquaredPointToSegment(point, { polygon.p1 , polygon.p2 }, p2);
		float d3 = DistanceSquaredPointToSegment(point, { polygon.p2 , polygon.p0 }, p3);

		if (d1 < d2)
		{
			if (d1 < d3)
			{
				return p1;
			}
			else
			{
				return p3;
			}
		}
		else
		{
			if (d2 < d3)
			{
				return p2;
			}
			else
			{
				return p3;
			}
		}
	}

	//==================================
	// 三角形の法線（単位ベクトル）を、2辺の外積から計算している
	//==================================
	Vector3 GetNormal(const Polygon& polygon)
	{
		Vector3 n = Cross((polygon.p1 - polygon.p0), (polygon.p2 - polygon.p0));
		n.Normalize();
		return n;
	}

	//==================================
	// 中心と幅・高さ・奥行きからAABBを作っている（負の値が来ても正の大きさとして扱っている）
	//==================================
	AABB SetAABB(Vector3 centerposition, float width, float height, float depth)
	{
		AABB aabb{};

		width = fabs(width);
		height = fabs(height);
		depth = fabs(depth);

		aabb.min.x = centerposition.x - width / 2.0f;
		aabb.min.y = centerposition.y - height / 2.0f;
		aabb.min.z = centerposition.z - depth / 2.0f;

		aabb.max.x = centerposition.x + width / 2.0f;
		aabb.max.y = centerposition.y + height / 2.0f;
		aabb.max.z = centerposition.z + depth / 2.0f;

		return aabb;
	}

}