// ============================================================================
// ファイルの役割: 床の水たまり・雨漏り・浸水した範囲のメッシュを作り、水面の細かさを選んで描いている。
// （Ground.cpp から分けた。説明は元のファイルとヘッダーを参照）
// ============================================================================

#include "Ground.h"
#include "shader/renderingConfig.hlsli"
#include "Application.h"
#include "Game.h"
#include "Player.h"
#include "TechDemo.h"
#include "Utility.h"
#include "Wall.h"
#include "MathUtility.h"
#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
	// 排水を始める前の、浸水した水面の高さ（排水の跡の高さを、ここから床の高さへ下げていく）
	constexpr float InitialFloodHeight = 1.25f;
	// 描画グループとデバッグ表示を分ける距離。反射の距離は wetFloorPS が水面の画素ごとに調べる。
	constexpr float NearWaterDistance = 170.0f;
	constexpr float MiddleWaterDistance = 340.0f;
	// 水たまりの多角形の辺の数
	constexpr int PuddleOutlineSegments = 32;
	// 多角形で覆う水面は、雨漏りの半径より外側まで広げて縁の欠けを防ぐ。
	constexpr float LeakRadiusMargin = 3.0f;
	constexpr float LeakOutlineMargin = 0.5f;
}

//=======================================
// 水たまりのメッシュ
//=======================================
void Ground::SetDrainTrace(const Vector2& column, float edgeZ, float progress)
{
	const float height = WaterSurfaceLift + (InitialFloodHeight - WaterSurfaceLift) * (1.0f - progress);
	m_DrainTrace = Vector4(column.x, column.y, edgeZ, height);
}

void Ground::AppendWaterPolygon(std::vector<VERTEX_3D>& vertices,
	const std::vector<Vector2>& outline, float extraHeight) const
{
	Vector2 center = Vector2::Zero;
	for (const Vector2& point : outline)
	{
		center += point;
	}
	center /= static_cast<float>(outline.size());
	// 床の格子と同じUV（床の1マスでテクスチャ1枚。テクスチャは繰り返して読むので、マスをまたいで続いた値にしている）。
	// 床は回していないので、ワールドの位置から床の格子の位置へは、移動と拡大を戻すだけで求まる
	const auto makeVertex = [this, extraHeight](const Vector2& point)
	{
		VERTEX_3D vertex{};
		vertex.position = Vector3(point.x, m_Position.y + WaterSurfaceLift + extraHeight, point.y);
		vertex.normal = Vector3(0.0f, 1.0f, 0.0f);
		vertex.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
		vertex.uv = Vector2(
			(point.x - m_Position.x) / m_Scale.x + 0.5f,
			0.5f - (point.y - m_Position.z) / m_Scale.z);
		return vertex;
	};
	for (std::size_t index = 0; index < outline.size(); ++index)
	{
		const Vector2& a = outline[index];
		const Vector2& b = outline[(index + 1) % outline.size()];
		// 床の格子と同じく、法線が上を向く並び（2辺の外積の y が正）にしている
		const float upward = (a.y - center.y) * (b.x - center.x) - (a.x - center.x) * (b.y - center.y);
		vertices.push_back(makeVertex(center));
		vertices.push_back(makeVertex(upward > 0.0f ? a : b));
		vertices.push_back(makeVertex(upward > 0.0f ? b : a));
	}
}

// マス目の水たまり1つにつき1つ、水たまりの形を覆う楕円の多角形を作っている。
// puddleMask.hlsli は、楕円の中心からの距離（楕円の半径で割った値）に、ノイズと波形のゆがみ（-0.177〜0.177）を足して岸の線（0.44）を決めている。
// そのため水たまりと岸は、楕円の距離 0.617 の内側に必ず収まる。多角形が楕円の内側に入る分も見込み、少し大きく（0.645）覆っている
void Ground::BuildPuddleSurfaces()
{
	m_PuddleSurfaceVertices.clear();
	m_PuddleSurfaces.clear();
	const WaterEffectSystem::PuddleCellBufferData& data = m_WaterEffects.GetPuddleCellData();
	constexpr float cellSize = WaterEffectSystem::PuddleCellSize;
	constexpr float coverRadius = WaterEffectSystem::PuddleCoverRadius;
	const Vector2 gridOffset(data.GridOffset.x, data.GridOffset.y);
	std::vector<Vector2> outline(PuddleOutlineSegments);
	for (int row = 0; row < WaterEffectSystem::PuddleCellCount; ++row)
	{
		for (int column = 0; column < WaterEffectSystem::PuddleCellCount; ++column)
		{
			const Vector4& cell = data.Cells[static_cast<std::size_t>(row * WaterEffectSystem::PuddleCellCount + column)];
			if (cell.x < WaterEffectSystem::PuddleCellPresentThreshold)
			{
				continue;
			}
			// シェーダーと同じ、楕円の向き・半径・中心のずれ
			const Vector2 randomValue(cell.y, cell.z);
			const float angle = randomValue.x * DirectX::XM_2PI;
			const float cosine = std::cos(angle);
			const float sine = std::sin(angle);
			const Vector2 axes(WaterEffectSystem::PuddleAxisX + randomValue.x * WaterEffectSystem::PuddleAxisXRange,
				WaterEffectSystem::PuddleAxisZ + randomValue.y * WaterEffectSystem::PuddleAxisZRange);
			const Vector2 shift = (randomValue - Vector2(0.5f, 0.5f)) * WaterEffectSystem::PuddleCenterJitter;
			const Vector2 cellCenter(
				static_cast<float>(WaterEffectSystem::PuddleCellMin + column) + 0.5f,
				static_cast<float>(WaterEffectSystem::PuddleCellMin + row) + 0.5f);
			for (int point = 0; point < PuddleOutlineSegments; ++point)
			{
				// 回した楕円の上の点を、シェーダーの回転（rotated = 回転 × マス目の中の位置）を戻して、ワールドの位置にしている
				const float t = DirectX::XM_2PI * static_cast<float>(point) / static_cast<float>(PuddleOutlineSegments);
				const float rotatedX = std::cos(t) * axes.x * coverRadius;
				const float rotatedY = std::sin(t) * axes.y * coverRadius;
				const Vector2 local(rotatedX * cosine + rotatedY * sine, -rotatedX * sine + rotatedY * cosine);
				outline[static_cast<std::size_t>(point)] = (cellCenter + shift + local) * cellSize - gridOffset;
			}
			WaterSurface surface;
			surface.Center = (cellCenter + shift) * cellSize - gridOffset;
			surface.Radius = (std::max)(axes.x, axes.y) * coverRadius * cellSize;
			surface.VertexStart = static_cast<unsigned int>(m_PuddleSurfaceVertices.size());
			AppendWaterPolygon(m_PuddleSurfaceVertices, outline);
			surface.VertexCount = static_cast<unsigned int>(m_PuddleSurfaceVertices.size()) - surface.VertexStart;
			m_PuddleSurfaces.push_back(surface);
		}
	}
	if (!m_PuddleSurfaceVertices.empty())
	{
		m_PuddleSurfaceBuffer.Create(m_PuddleSurfaceVertices);
	}
	m_PuddleSurfacesDirty = false;
	m_PuddleSurfacesPosition = m_Position;
	m_PuddleSurfacesScale = m_Scale;
}

void Ground::RebuildPuddleSurfacesIfChanged()
{
	if (m_PuddleSurfacesDirty || m_PuddleSurfacesPosition != m_Position || m_PuddleSurfacesScale != m_Scale)
	{
		BuildPuddleSurfaces();
	}
}

// 雨漏りの水たまり（今の半径の円）と、浸水した範囲（長方形）のメッシュを作っている。
// 床のシェーダーが雨漏りの水たまりを計算する範囲（半径 + 3）と、浸水した範囲がなめらかに始まる所（0.5外側）を覆っている
void Ground::BuildDynamicSurfaces()
{
	m_DynamicSurfaceVertices.clear();
	m_DynamicSurfaces.clear();
	std::vector<Vector2> outline;
	const std::vector<WaterEffectSystem::Leak>& leaks = m_WaterEffects.GetLeaks();
	const int leakCount = (std::min)(static_cast<int>(leaks.size()), WaterEffectSystem::MaxLeakCount);
	const float polygonScale = 1.0f / std::cos(DirectX::XM_PI / static_cast<float>(LeakOutlineSegments));
	for (int leakIndex = 0; leakIndex < leakCount; ++leakIndex)
	{
		const WaterEffectSystem::Leak& leak = leaks[static_cast<std::size_t>(leakIndex)];
		const float radius = (leak.Radius + LeakRadiusMargin) * polygonScale + LeakOutlineMargin;
		outline.resize(LeakOutlineSegments);
		for (int point = 0; point < LeakOutlineSegments; ++point)
		{
			const float t = DirectX::XM_2PI * static_cast<float>(point) / static_cast<float>(LeakOutlineSegments);
			outline[static_cast<std::size_t>(point)] = leak.Center + Vector2(std::cos(t), std::sin(t)) * radius;
		}
		WaterSurface surface;
		surface.Center = leak.Center;
		surface.Radius = radius;
		surface.VertexStart = static_cast<unsigned int>(m_DynamicSurfaceVertices.size());
		AppendWaterPolygon(m_DynamicSurfaceVertices, outline);
		surface.VertexCount = static_cast<unsigned int>(m_DynamicSurfaceVertices.size()) - surface.VertexStart;
		m_DynamicSurfaces.push_back(surface);
	}
	const auto appendFlood = [&](Vector2 minimum, Vector2 maximum, float extraHeight)
	{
		const Vector2 margin(1.0f, 1.0f);
		minimum -= margin;
		maximum += margin;
		outline = { minimum, Vector2(maximum.x, minimum.y), maximum, Vector2(minimum.x, maximum.y) };
		WaterSurface surface;
		surface.Center = (minimum + maximum) * 0.5f;
		surface.Radius = Vector2::Distance(minimum, maximum) * 0.5f;
		surface.VertexStart = static_cast<unsigned int>(m_DynamicSurfaceVertices.size());
		AppendWaterPolygon(m_DynamicSurfaceVertices, outline, extraHeight);
		surface.VertexCount = static_cast<unsigned int>(m_DynamicSurfaceVertices.size()) - surface.VertexStart;
		m_DynamicSurfaces.push_back(surface);
	};
	if (m_WaterEffects.HasFloodRegion())
	{
		appendFlood(m_WaterEffects.GetFloodMin(), m_WaterEffects.GetFloodMax(),
			(std::max)(0.0f, m_DrainTrace.w - WaterSurfaceLift));
	}
	if (m_FloodInletRect.x < m_FloodInletRect.z)
	{
		appendFlood(Vector2(m_FloodInletRect.x, m_FloodInletRect.y),
			Vector2(m_FloodInletRect.z, m_FloodInletRect.w), 0.0f);
	}
	// 転送に失敗したフレームでは、新しい描画範囲で古い頂点を読まない。
	if (!m_DynamicSurfaceVertices.empty() && !m_DynamicSurfaceBuffer.Modify(m_DynamicSurfaceVertices))
	{
		m_DynamicSurfaces.clear();
	}
}

Ground::WaterLevel Ground::SelectWaterLevel(const WaterSurface& surface, Camera* cam) const
{
	const Vector3 center(surface.Center.x, m_Position.y, surface.Center.y);
	// プレイヤーの視点では、画面の外の水たまりを描かない（深度プリパスと本描画で同じ判定になる）。
	// 監視映像や環境マップのように画角の違う視点では、見えているのに消えることがないよう、画面の外かは調べていない
	if (cam == Core::Game::GetInstance()->GetCamera() && !cam->IsSphereVisible(center, surface.Radius))
	{
		return WaterLevel::DryFloor;
	}
	const float distance = (std::max)(0.0f, Vector3::Distance(cam->GetPosition(), center) - surface.Radius);
	// 比較用の描画設定で「メッシュ（LODなし）」を選んでいるときは、見えている水たまりをすべて一番細かく描く
	if (distance < NearWaterDistance || Tools::TechDemo::DefaultPuddleMeshMode == Tools::TechDemo::PuddleMeshMode::DetailedMesh)
	{
		return WaterLevel::Near;
	}
	return distance < MiddleWaterDistance ? WaterLevel::Middle : WaterLevel::Far;
}

void Ground::DrawWaterSurfaces(Camera* cam, bool depthOnly, WetFloorBuffer* wetFloor)
{
	if (m_PuddleSurfaces.empty() && m_DynamicSurfaces.empty())
	{
		return;
	}
	ID3D11DeviceContext* devicecontext = Renderer::GetDeviceContext();
	// メッシュの頂点はワールドの位置で作っているので、ワールド行列は単位行列にしている
	Matrix identity = Matrix::Identity;
	Renderer::SetWorldMatrix(&identity);

	std::vector<WaterLevel> puddleLevels(m_PuddleSurfaces.size());
	std::vector<WaterLevel> dynamicLevels(m_DynamicSurfaces.size());
	for (std::size_t index = 0; index < m_PuddleSurfaces.size(); ++index)
	{
		puddleLevels[index] = SelectWaterLevel(m_PuddleSurfaces[index], cam);
	}
	for (std::size_t index = 0; index < m_DynamicSurfaces.size(); ++index)
	{
		dynamicLevels[index] = SelectWaterLevel(m_DynamicSurfaces[index], cam);
	}
	const bool countsForDebug = !depthOnly && cam == Core::Game::GetInstance()->GetCamera();
	if (countsForDebug)
	{
		std::fill(std::begin(m_DrawnSurfaceCounts), std::end(m_DrawnSurfaceCounts), 0);
	}

	// 細かさごとにまとめて描いている（b10 を書き換えるのは、細かさの数だけ）
	for (const WaterLevel level : { WaterLevel::Near, WaterLevel::Middle, WaterLevel::Far })
	{
		const bool hasPuddle = std::find(puddleLevels.begin(), puddleLevels.end(), level) != puddleLevels.end();
		const bool hasDynamic = std::find(dynamicLevels.begin(), dynamicLevels.end(), level) != dynamicLevels.end();
		if (!hasPuddle && !hasDynamic)
		{
			continue;
		}
		if (!depthOnly && wetFloor != nullptr)
		{
			wetFloor->WaterSurfaceLevel = static_cast<float>(level);
			devicecontext->UpdateSubresource(m_WetFloorBuffer.Get(), 0, nullptr, wetFloor, 0, 0);
		}
		const auto drawSurfaces = [&](VertexBuffer<VERTEX_3D>& buffer, const std::vector<WaterSurface>& surfaces,
			const std::vector<WaterLevel>& levels)
		{
			buffer.SetGPU();
			for (std::size_t index = 0; index < surfaces.size(); ++index)
			{
				if (levels[index] == level)
				{
					devicecontext->Draw(surfaces[index].VertexCount, surfaces[index].VertexStart);
					if (countsForDebug)
					{
						++m_DrawnSurfaceCounts[static_cast<int>(level)];
					}
				}
			}
		};
		if (hasPuddle)
		{
			drawSurfaces(m_PuddleSurfaceBuffer, m_PuddleSurfaces, puddleLevels);
		}
		if (hasDynamic)
		{
			drawSurfaces(m_DynamicSurfaceBuffer, m_DynamicSurfaces, dynamicLevels);
		}
	}
}
