// ============================================================================
// ファイルの役割: 床と、その上の水たまり・濡れた表現・水しぶきの描画を管理している。
// 主な技術: コードで作る格子状のメッシュ、拡大・回転・移動の行列、法線・UV、濡れた床のシェーダー（wetFloorPS）
// Ground.hで宣言した処理の中身（格子の作成・電力に合わせた水面の変化・描画）をここで定義している。
// ============================================================================

#include "Ground.h"
#include "Application.h"
#include "Game.h"
#include "Player.h"
#include "TechDemo.h"
#include "utility.h"
#include "Wall.h"
#include <algorithm>
#include <cmath>
using namespace DirectX::SimpleMath;

namespace
{
	// 水たまりのメッシュを床から浮かせる高さ（約2.5mm）。床と同じ平面だと深度がそろわずちらつくため、わずかに上げている
	constexpr float WaterSurfaceLift = 0.05f;
	// 水たまりのメッシュの細かさを変える距離（カメラから、メッシュを囲む円の一番近い所まで）。
	// 遠くの霧（horrorGrade.hlsli の GetHorrorDistanceFog）は110から濃くなり始めるので、それより先から省いている
	constexpr float NearWaterDistance = 130.0f;
	constexpr float MiddleWaterDistance = 280.0f;
	// 水たまり・雨漏りの多角形の辺の数
	constexpr int PuddleOutlineSegments = 32;
	constexpr int LeakOutlineSegments = 24;
}

//=======================================
// 初期化処理：50x50マスの格子を作り、濡れた床のシェーダーとテクスチャを用意している
//=======================================
void Ground::Init()
{
	m_WetTime = 0.0f;
	m_PowerReflectionBlend = 0.0f;
	m_PowerSurge = 0.0f;
	m_WasPowerRestored = false;
	m_Footprints.Clear();
	m_WaterEffects.Init();
	m_RippleSimulation.Init();

	// 頂点データ：1マスを2つの三角形（6頂点）で作り、原点を中心に並べている
	m_SizeX = 50;
	m_SizeZ = 50;
	m_Vertices.resize(6 * m_SizeX * m_SizeZ);

	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;
			m_Vertices[n + 0].position = Vector3(-0.5f + x - m_SizeX / 2, 0, 0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 1].position = Vector3(0.5f + x - m_SizeX / 2, 0, 0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 2].position = Vector3(-0.5f + x - m_SizeX / 2, 0, -0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 3].position = Vector3(-0.5f + x - m_SizeX / 2, 0, -0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 4].position = Vector3(0.5f + x - m_SizeX / 2, 0, 0.5f - z + m_SizeZ / 2);
			m_Vertices[n + 5].position = Vector3(0.5f + x - m_SizeX / 2, 0, -0.5f - z + m_SizeZ / 2);

			m_Vertices[n + 0].color = Color(1, 1, 1, 1);
			m_Vertices[n + 1].color = Color(1, 1, 1, 1);
			m_Vertices[n + 2].color = Color(1, 1, 1, 1);
			m_Vertices[n + 3].color = Color(1, 1, 1, 1);
			m_Vertices[n + 4].color = Color(1, 1, 1, 1);
			m_Vertices[n + 5].color = Color(1, 1, 1, 1);

			// テクスチャはマスごとに0〜1を割り当て、マスごとに繰り返して貼っている
			m_Vertices[n + 0].uv = Vector2(0, 0);
			m_Vertices[n + 1].uv = Vector2(1, 0);
			m_Vertices[n + 2].uv = Vector2(0, 1);
			m_Vertices[n + 3].uv = Vector2(0, 1);
			m_Vertices[n + 4].uv = Vector2(1, 0);
			m_Vertices[n + 5].uv = Vector2(1, 1);

			m_Vertices[n + 0].normal = Vector3(0, 1, 0);
			m_Vertices[n + 1].normal = Vector3(0, 1, 0);
			m_Vertices[n + 2].normal = Vector3(0, 1, 0);
			m_Vertices[n + 3].normal = Vector3(0, 1, 0);
			m_Vertices[n + 4].normal = Vector3(0, 1, 0);
			m_Vertices[n + 5].normal = Vector3(0, 1, 0);
		}
	}

	// 各三角形の法線を、2辺の外積から計算し直している（平らなので、どれも真上を向く）
	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;

			// 三角形の2辺のベクトル
			Vector3 v1 = m_Vertices[n + 1].position - m_Vertices[n + 0].position;
			Vector3 v2 = m_Vertices[n + 2].position - m_Vertices[n + 0].position;
			Vector3 normal = v1.Cross(v2);// 外積を計算している
			normal.Normalize();			  // 長さを1にしている
			m_Vertices[n + 0].normal = normal;
			m_Vertices[n + 1].normal = normal;
			m_Vertices[n + 2].normal = normal;

			// 2つめの三角形の2辺のベクトル
			v1 = m_Vertices[n + 4].position - m_Vertices[n + 3].position;
			v2 = m_Vertices[n + 5].position - m_Vertices[n + 3].position;
			normal = v1.Cross(v2);// 外積を計算している
			normal.Normalize();	  // 長さを1にしている

			m_Vertices[n + 3].normal = normal;
			m_Vertices[n + 4].normal = normal;
			m_Vertices[n + 5].normal = normal;
		}
	}

	// 頂点バッファを作っている
	m_VertexBuffer.Create(m_Vertices);

	// インデックスデータ（頂点を順番どおりに使っている）
	m_Indices.resize(6 * m_SizeX * m_SizeZ);

	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;
			m_Indices[n + 0] = n + 0;
			m_Indices[n + 1] = n + 1;
			m_Indices[n + 2] = n + 2;
			m_Indices[n + 3] = n + 3;
			m_Indices[n + 4] = n + 4;
			m_Indices[n + 5] = n + 5;
		}
	}

	// インデックスバッファを作っている
	m_IndexBuffer.Create(m_Indices);

	// 濡れた床のピクセルシェーダーを使うシェーダーを作っている
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/wetFloorPS.hlsl");

	// 濡れた床の値を渡す定数バッファを作っている
	Renderer::CreateConstantBuffer(
		sizeof(WetFloorBuffer),
		m_WetFloorBuffer.ReleaseAndGetAddressOf());

	// 水たまりの表を渡す定数バッファを作り、WaterEffectSystemで決めた値を書き込んでいる
	const WaterEffectSystem::PuddleCellBufferData& puddleCells = m_WaterEffects.GetPuddleCellData();
	Renderer::CreateConstantBuffer(
		static_cast<unsigned int>(sizeof(puddleCells)),
		m_PuddleCellBuffer.ReleaseAndGetAddressOf());
	Renderer::GetDeviceContext()->UpdateSubresource(
		m_PuddleCellBuffer.Get(), 0, nullptr, &puddleCells, 0, 0);

	// 床のテクスチャを読み込んでいる（読めなければファイル名を表示して終了している）
    constexpr const char* fieldTexturePath = "assets/texture/field.jpg";
    if (!m_Texture.Load(fieldTexturePath))
    {
        utility::ReportFatalError(
            std::string("テクスチャを読み込めませんでした。\n") + fieldTexturePath);
    }

	// マテリアルを作っている
	m_Material = std::make_unique<Material>();
	MATERIAL mtrl;
	mtrl.Diffuse = Color(1, 1, 1, 1);
	mtrl.TextureEnable = true;// テクスチャを使う
	m_Material->Create(mtrl);
	// 床の高さを-100にし、1マスを20単位に広げている（全体で1000x1000）
	m_Position.y = -100.0f;
	m_Scale.x = 20.0f;
	m_Scale.z = 20.0f;

	// 雨漏りと浸水した範囲のメッシュの頂点バッファ（毎フレーム書き換えるので、入りきる大きさで先に作っている）
	m_DynamicSurfaceCapacity = static_cast<unsigned int>(
		(WaterEffectSystem::MaxLeakCount * LeakOutlineSegments + 4) * 3);
	m_DynamicSurfaceBuffer.Create(std::vector<VERTEX_3D>(m_DynamicSurfaceCapacity));
	m_DynamicSurfaces.clear();
	m_PuddleSurfacesDirty = true;
}

//=======================================
// 水たまりのメッシュ
//=======================================
void Ground::AppendWaterPolygon(std::vector<VERTEX_3D>& vertices, const std::vector<Vector2>& outline) const
{
	Vector2 center = Vector2::Zero;
	for (const Vector2& point : outline)
	{
		center += point;
	}
	center /= static_cast<float>(outline.size());
	// 床の格子と同じUV（床の1マスでテクスチャ1枚。テクスチャは繰り返して読むので、マスをまたいで続いた値にしている）。
	// 床は回していないので、ワールドの位置から床の格子の位置へは、移動と拡大を戻すだけで求まる
	const auto makeVertex = [this](const Vector2& point)
	{
		VERTEX_3D vertex{};
		vertex.position = Vector3(point.x, m_Position.y + WaterSurfaceLift, point.y);
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
	constexpr float cellSize = 82.0f;
	constexpr float coverRadius = 0.62f * 1.04f;
	const Vector2 gridOffset(data.GridOffset.x, data.GridOffset.y);
	std::vector<Vector2> outline(PuddleOutlineSegments);
	for (int row = 0; row < WaterEffectSystem::PuddleCellCount; ++row)
	{
		for (int column = 0; column < WaterEffectSystem::PuddleCellCount; ++column)
		{
			const Vector4& cell = data.Cells[static_cast<std::size_t>(row * WaterEffectSystem::PuddleCellCount + column)];
			if (cell.x < 0.5f)
			{
				continue;
			}
			// シェーダーと同じ、楕円の向き・半径・中心のずれ
			const Vector2 randomValue(cell.y, cell.z);
			const float angle = randomValue.x * DirectX::XM_2PI;
			const float cosine = std::cos(angle);
			const float sine = std::sin(angle);
			const Vector2 axes(0.72f + randomValue.x * 0.25f, 0.43f + randomValue.y * 0.20f);
			const Vector2 shift = (randomValue - Vector2(0.5f, 0.5f)) * 0.22f;
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
		const float radius = (leak.Radius + 3.0f) * polygonScale + 0.5f;
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
	if (m_WaterEffects.HasFloodRegion())
	{
		const Vector2 minimum = m_WaterEffects.GetFloodMin() - Vector2(1.0f, 1.0f);
		const Vector2 maximum = m_WaterEffects.GetFloodMax() + Vector2(1.0f, 1.0f);
		if (minimum.x < maximum.x && minimum.y < maximum.y)
		{
			outline = { minimum, Vector2(maximum.x, minimum.y), maximum, Vector2(minimum.x, maximum.y) };
			WaterSurface surface;
			surface.Center = (minimum + maximum) * 0.5f;
			surface.Radius = Vector2::Distance(minimum, maximum) * 0.5f;
			surface.VertexStart = static_cast<unsigned int>(m_DynamicSurfaceVertices.size());
			AppendWaterPolygon(m_DynamicSurfaceVertices, outline);
			surface.VertexCount = static_cast<unsigned int>(m_DynamicSurfaceVertices.size()) - surface.VertexStart;
			m_DynamicSurfaces.push_back(surface);
		}
	}
	if (!m_DynamicSurfaceVertices.empty() && m_DynamicSurfaceVertices.size() <= m_DynamicSurfaceCapacity)
	{
		m_DynamicSurfaceBuffer.Modify(m_DynamicSurfaceVertices);
	}
	else if (m_DynamicSurfaceVertices.size() > m_DynamicSurfaceCapacity)
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
	// 技術デモ（F2）で「メッシュ（LODなし）」を選んでいるときは、見えている水たまりをすべて一番細かく描く
	if (distance < NearWaterDistance || Tools::TechDemo::GetPuddleMeshMode() == 1)
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

//=======================================
// 更新処理：経過時間を進め、電力が戻ったときの水面の変化と、水滴・波紋を更新している
//=======================================
void Ground::Update()
{
	const float deltaTime = Application::GetDeltaTime();
	m_WetTime += deltaTime;
	// 長く遊んでも浮動小数点の精度が落ちないよう、時間を戻している
	if (m_WetTime > 10000.0f)
	{
		m_WetTime = 0.0f;
	}

	const bool powerRestored =
		Core::Game::GetInstance()->IsPowerRestored();
	if (powerRestored && !m_WasPowerRestored)
	{
		// 天井照明へ電気が流れた瞬間に水面を短く乱し、電力が戻った衝撃を見た目でも伝えている。
		m_PowerSurge = 1.0f;
	}
	m_WasPowerRestored = powerRestored;

	// 電力が戻ったら、反射をゆっくり強くしている（停電したら速く弱める）
	const float targetBlend = powerRestored ? 1.0f : 0.0f;
	const float response = powerRestored ? 2.2f : 4.0f;
	m_PowerReflectionBlend +=
		(targetBlend - m_PowerReflectionBlend) * response * deltaTime;
	m_PowerReflectionBlend = std::clamp(
		m_PowerReflectionBlend, 0.0f, 1.0f);
	// 水面の乱れは約2.4秒で収まる
	m_PowerSurge = (std::max)(0.0f, m_PowerSurge - deltaTime * 0.42f);
	m_WaterEffects.Update(deltaTime);
	m_Footprints.Update(deltaTime);
	// 水たまりのメッシュ（マス目の水たまりは変わったときだけ、雨漏りと浸水した範囲は毎フレーム）
	RebuildPuddleSurfacesIfChanged();
	BuildDynamicSurfaces();

	// 浸水した範囲があれば、水面で跳ね返った懐中電灯の光を壁や天井に揺らして映している（waterCaustics.hlsli）。
	// 範囲がない面（2面など）は強さを0にし、壁のシェーダーで計算しないようにしている。
	const bool hasFlood = m_WaterEffects.HasFloodRegion();
	const DirectX::SimpleMath::Vector4 floodRect = hasFlood
		? DirectX::SimpleMath::Vector4(
			m_WaterEffects.GetFloodMin().x, m_WaterEffects.GetFloodMin().y,
			m_WaterEffects.GetFloodMax().x, m_WaterEffects.GetFloodMax().y)
		: DirectX::SimpleMath::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
	Renderer::SetWaterCaustics(
		floodRect,
		m_Position.y,
		m_WetTime,
		(hasFlood ? 1.0f : 0.0f) * Tools::TechDemo::GetWaterCausticsScale());

	// 足音と水滴のおもりを、水たまりの波へ渡している。波を進めるのは描画の最初（RunGpuSimulation）で、
	// そのための経過時間・計算の中心（カメラ）・浸水した範囲をここで覚えている。
	for (const DirectX::SimpleMath::Vector4& impulse : m_WaterEffects.TakeRippleImpulses())
	{
		m_RippleSimulation.AddImpulse(
			DirectX::SimpleMath::Vector2(impulse.x, impulse.y), impulse.z, impulse.w);
	}
	const DirectX::SimpleMath::Vector3 eye = Core::Game::GetInstance()->GetCamera()->GetPosition();
	m_PendingRippleSeconds += deltaTime;
	m_RippleCenter = DirectX::SimpleMath::Vector2(eye.x, eye.z);
	m_RippleFloodRect = floodRect;

	// 波を跳ね返す物：当たり判定があり、床に立っている箱（柱・棚・机の脚・壁など）を、カメラに近い順に選んでいる。
	// 上から見た長方形にするため、向きが0度か90度の物だけを使っている（それ以外の向きの物は床に立つ物にはない）。
	m_RippleObstacles.clear();
	for (const Wall* wall : Core::Game::GetInstance()->GetObjects<Wall>())
	{
		const DirectX::SimpleMath::Vector3 position = wall->GetPosition();
		const DirectX::SimpleMath::Vector3 scale = wall->GetScale();
		const float yaw = std::fmod(std::fabs(wall->GetRotation().y), DirectX::XM_PI);
		const bool straight = yaw < 0.01f || yaw > DirectX::XM_PI - 0.01f;
		const bool quarter = std::fabs(yaw - DirectX::XM_PIDIV2) < 0.01f;
		if (!wall->IsCollisionEnabled() || position.y - scale.y * 0.5f > m_Position.y + 1.5f ||
			(!straight && !quarter))
		{
			continue;
		}
		m_RippleObstacles.emplace_back(position.x, position.z,
			(quarter ? scale.z : scale.x) * 0.5f, (quarter ? scale.x : scale.z) * 0.5f);
	}
	const DirectX::SimpleMath::Vector2 center = m_RippleCenter;
	std::sort(m_RippleObstacles.begin(), m_RippleObstacles.end(),
		[&center](const DirectX::SimpleMath::Vector4& a, const DirectX::SimpleMath::Vector4& b)
		{
			return DirectX::SimpleMath::Vector2::DistanceSquared(DirectX::SimpleMath::Vector2(a.x, a.y), center) <
				DirectX::SimpleMath::Vector2::DistanceSquared(DirectX::SimpleMath::Vector2(b.x, b.y), center);
		});
}

// 水たまりの波を、更新で受け取った時間ぶん波の式で進めている（WaterRippleSimulation）。
// 計算する範囲はカメラのまわりだけ。できた波は、床の反射を波打たせ、壁に光の輪を映す。
// 描画の最初にGame::Drawから呼ばれ、GPU時間は「水たまりの波」の段階として測っている。
void Ground::RunGpuSimulation()
{
	const float seconds = m_PendingRippleSeconds;
	m_PendingRippleSeconds = 0.0f;
	// 技術デモ（F2）で「水たまりの波：なし」を選んでいるときは、波を計算も表示もしない
	if (!Tools::TechDemo::UsesWaterRipples())
	{
		Renderer::SetWaterRipples(nullptr, DirectX::SimpleMath::Vector2::Zero, 0.0f, 0.0f);
		return;
	}
	m_RippleSimulation.Update(seconds, m_RippleCenter, m_PuddleCellBuffer.Get(), m_RippleFloodRect, m_RippleObstacles);
	Renderer::SetWaterRipples(m_RippleSimulation.GetHeightSRV(), m_RippleSimulation.GetOrigin(),
		WaterRippleSimulation::WorldSize, 1.0f);
}

//=======================================
// 描画処理：床を濡れた床のシェーダーで描き、続けて水滴と波紋を描いている
//=======================================
void Ground::Draw(Camera* cam)
{
	RebuildPuddleSurfacesIfChanged();
	// プレイヤーの視点の行列を設定している
	cam->SetCamera();

	// 拡大・回転・移動の行列を作り、GPUへ設定している
	Matrix worldmtx = MakeWorldMatrix();
	Renderer::SetWorldMatrix(&worldmtx);

	// デバイスコンテキストを取得している
	ID3D11DeviceContext* devicecontext;
	devicecontext = Renderer::GetDeviceContext();

	// 三角形のリストとして描く
	devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_Shader.SetGPU();
	m_VertexBuffer.SetGPU();
	m_IndexBuffer.SetGPU();

	m_Texture.SetGPU();
	m_Material->SetGPU();

	// 濡れた床の値：電力が戻ると波紋と反射を少し強め、戻った瞬間は細かく揺らしている
	WetFloorBuffer wetFloor{};
	wetFloor.Time = m_WetTime;
	const float surgeWave = m_PowerSurge *
		(0.55f + 0.45f * std::sin(m_WetTime * 17.0f));
	wetFloor.RippleStrength =
		0.92f + m_PowerReflectionBlend * 0.10f + surgeWave * 0.18f;
	// 技術デモで「水面の反射なし」を選んでいるときは、反射の画像を混ぜない
	wetFloor.ReflectionStrength = Tools::TechDemo::GetReflectionMethod() != 0
		? 0.96f + m_PowerReflectionBlend * 0.22f + surgeWave * 0.12f
		: 0.0f;
	// 水に浸かった範囲がなければ、最小(1,1)が最大(0,0)より大きい値を渡して「範囲なし」にしている
	wetFloor.FloodRect = m_WaterEffects.HasFloodRegion()
		? DirectX::SimpleMath::Vector4(
			m_WaterEffects.GetFloodMin().x, m_WaterEffects.GetFloodMin().y,
			m_WaterEffects.GetFloodMax().x, m_WaterEffects.GetFloodMax().y)
		: DirectX::SimpleMath::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
	// 床に残る濡れた足跡（乾いたものは詰めて、数だけを渡している）
	m_Footprints.Fill(wetFloor.Footprints, wetFloor.FootprintInfo);
	// 天井の雨漏りの真下の水たまり（今の半径）
	const std::vector<WaterEffectSystem::Leak>& leaks = m_WaterEffects.GetLeaks();
	const int leakCount = (std::min)(static_cast<int>(leaks.size()), WaterEffectSystem::MaxLeakCount);
	for (int leakIndex = 0; leakIndex < leakCount; ++leakIndex)
	{
		const WaterEffectSystem::Leak& leak = leaks[static_cast<std::size_t>(leakIndex)];
		wetFloor.Leaks[leakIndex] = DirectX::SimpleMath::Vector4(leak.Center.x, leak.Center.y, leak.Radius, 0.0f);
	}
	wetFloor.LeakInfo = DirectX::SimpleMath::Vector4(static_cast<float>(leakCount), 0.0f, 0.0f, 0.0f);
	// 床の本体は乾いた床として描き、水たまりは後でそのメッシュに描き重ねる（DrawWaterSurfaces）。
	// 技術デモ（F2）で「床全体で計算」を選んでいるときは、前の方式（床の全部の画素で水たまりを計算し、メッシュは描かない）にしている
	const bool drawsPuddleMeshes = Tools::TechDemo::GetPuddleMeshMode() != 0;
	wetFloor.WaterSurfaceLevel = static_cast<float>(drawsPuddleMeshes ? WaterLevel::DryFloor : WaterLevel::Near);
	devicecontext->UpdateSubresource(
		m_WetFloorBuffer.Get(), 0, nullptr, &wetFloor, 0, 0);
	// ピクセルシェーダーのb10へ設定している
	ID3D11Buffer* wetFloorBuffer = m_WetFloorBuffer.Get();
	devicecontext->PSSetConstantBuffers(10, 1, &wetFloorBuffer);
	// 水たまりの表をb13へ設定している
	ID3D11Buffer* puddleCellBuffer = m_PuddleCellBuffer.Get();
	devicecontext->PSSetConstantBuffers(13, 1, &puddleCellBuffer);

	devicecontext->DrawIndexed(
		(UINT)m_Indices.size(),	// 描くインデックスの数
		0,					// インデックスバッファの最初の位置
		0);

	// 水たまり・雨漏り・浸水した範囲を、それぞれのメッシュで描き重ねている（画面の外は描かず、遠いほど軽く）
	if (drawsPuddleMeshes)
	{
		DrawWaterSurfaces(cam, false, &wetFloor);
	}

	// 天井からの水滴と、足元の波紋を描いている
	m_WaterEffects.Draw(cam);
}

//=======================================
// ワールド行列：拡大・回転・移動の順に掛けている
//=======================================
Matrix Ground::MakeWorldMatrix() const
{
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.x, m_Rotation.y, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);
	return s * r * t;
}

//=======================================
// 深度プリパス：本描画と同じ頂点シェーダー・同じ行列で、床の面の深度だけを描いている
//=======================================
void Ground::DrawDepthPrepass(Camera* cam)
{
	cam->SetCamera();
	Matrix worldmtx = MakeWorldMatrix();
	Renderer::SetWorldMatrix(&worldmtx);

	ID3D11DeviceContext* devicecontext = Renderer::GetDeviceContext();
	devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// ピクセルシェーダーを外し、深度だけを書いている
	m_Shader.SetGPU();
	devicecontext->PSSetShader(nullptr, nullptr, 0);
	m_VertexBuffer.SetGPU();
	m_IndexBuffer.SetGPU();
	devicecontext->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
	// 水たまりのメッシュの深度も描いている。メッシュは床より少し上にあるので、本描画でメッシュの下の床の画素は深度の判定で捨てられ、
	// 床の計算と水たまりの計算が同じ画素で二重に動かない
	// （技術デモで「床全体で計算」を選んでいるときは、本描画でもメッシュを描かないので、深度も描かない）
	RebuildPuddleSurfacesIfChanged();
	if (Tools::TechDemo::GetPuddleMeshMode() != 0)
	{
		DrawWaterSurfaces(cam, true, nullptr);
	}
}

//=======================================
// 終了処理：水の効果と定数バッファを解放している
//=======================================
void Ground::Uninit()
{
	// 次のシーンに水面の光の揺らぎが残らないよう、効果を消している
	Renderer::SetWaterCaustics(DirectX::SimpleMath::Vector4(1.0f, 1.0f, 0.0f, 0.0f), 0.0f, 0.0f, 0.0f);
	m_WaterEffects.Uninit();
	m_RippleSimulation.Uninit();
	Renderer::SetWaterRipples(nullptr, DirectX::SimpleMath::Vector2::Zero, 0.0f, 0.0f);
	m_WetFloorBuffer.Reset();
	m_PuddleCellBuffer.Reset();
}


//=======================================
// 頂点情報を取得している
//=======================================
std::vector<VERTEX_3D>Ground::GetVertices()
{
	std::vector<VERTEX_3D>res;
	res.resize(m_Vertices.size());
	// 拡大・回転・移動の行列を作っている
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);
	Matrix worldmtx = s * r * t;

	// 各頂点をワールド座標へ変換して入れている
	for (int i = 0; i < m_Vertices.size(); i++)
	{
		res[i].position = Vector3::Transform(m_Vertices[i].position, worldmtx);
		res[i].normal= Vector3::Transform(m_Vertices[i].normal, worldmtx);
		res[i].uv = m_Vertices[i].uv;
	}
	return res;
}

// 床のテクスチャを差し替えている
void Ground::SetTexture(const char* filename)
{
	m_Texture.Load(filename);
}
