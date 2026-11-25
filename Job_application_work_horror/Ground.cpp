// ============================================================================
// ファイルの役割: 床と、その上の水たまり・濡れた表現・水しぶきの描画を管理している。
// 主な技術: コードで作る格子状のメッシュ、拡大・回転・移動の行列、法線・UV、濡れた床のシェーダー（wetFloorPS）
// Ground.hで宣言した処理の中身（格子の作成・電力に合わせた水面の変化・描画）をここで定義している。
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
	// 浸水範囲は長方形。水面は中心から各辺に三角形を1枚ずつ張る。
	constexpr int FloodOutlineSegments = 4;
	constexpr int FloodRectangleCapacity = 2;
	constexpr int WaterTriangleVertexCount = 3;
	// 床の格子のマス数（縦横）、床の高さ、1マスの大きさ（全体で1000x1000）
	constexpr int GridCellCount = 50;
	constexpr int VerticesPerCell = 6;
	constexpr float FloorY = -100.0f;
	constexpr float CellSize = 20.0f;
	// 時間を戻す秒数（長く遊んでも浮動小数点の精度が落ちないように）
	constexpr float WetTimeWrapSeconds = 10000.0f;
	// 電力が戻ったときの水面：反射を強める速さ（戻ったとき・停電したとき）、乱れが収まる速さ（約2.4秒）、
	// 乱れの揺れ（速さ・いつもの割合と揺れの幅）
	constexpr float PowerBlendRiseRate = 2.2f;
	constexpr float PowerBlendFallRate = 4.0f;
	constexpr float PowerSurgeDecayRate = 0.42f;
	constexpr float SurgeWaveRate = 17.0f;
	constexpr float SurgeWaveBase = 0.55f;
	constexpr float SurgeWaveRange = 0.45f;
	// 波紋と反射の強さ：いつもの強さ、電力が戻ったときに足す強さ、電力が戻った瞬間の乱れで足す強さ
	constexpr float RippleStrengthBase = 0.92f;
	constexpr float RippleStrengthPower = 0.10f;
	constexpr float RippleStrengthSurge = 0.18f;
	constexpr float ReflectionStrengthBase = 0.96f;
	constexpr float ReflectionStrengthPower = 0.22f;
	constexpr float ReflectionStrengthSurge = 0.12f;
	// 波を止める壁：床からこの高さより上にある壁は数えない。まっすぐ・横向きとみなす角度の誤差（ラジアン）
	constexpr float RippleObstacleMaxHeight = 1.5f;
	constexpr float AxisAlignedTolerance = 0.01f;
	// シェーダーの番号（wetFloorPS.hlsl の register と合わせる）：床の定数 b10、水たまりの表 b13
	constexpr UINT WetFloorBufferSlot = SIGNAL_LOST_WET_FLOOR_BUFFER_SLOT;
	constexpr UINT PuddleCellBufferSlot = SIGNAL_LOST_PUDDLE_CELL_BUFFER_SLOT;
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

	BuildFloorGrid();
	CreateFloorResources();

	// 床の高さを-100にし、1マスを20単位に広げている（全体で1000x1000）
	m_Position.y = FloorY;
	m_Scale.x = CellSize;
	m_Scale.z = CellSize;

	// 雨漏りと浸水した範囲のメッシュの頂点バッファ（毎フレーム書き換えるので、入りきる大きさで先に作っている）
	m_DynamicSurfaceCapacity = static_cast<unsigned int>(
		(WaterEffectSystem::MaxLeakCount * LeakOutlineSegments + FloodOutlineSegments * FloodRectangleCapacity) * WaterTriangleVertexCount);
	m_DynamicSurfaceBuffer.Create(std::vector<VERTEX_3D>(m_DynamicSurfaceCapacity));
	m_DynamicSurfaces.clear();
	m_PuddleSurfacesDirty = true;
}

//=======================================
// 床の格子：50x50マスの頂点とインデックスを作り、バッファにしている
//=======================================
void Ground::BuildFloorGrid()
{
	// 頂点データ：1マスを2つの三角形（6頂点）で作り、原点を中心に並べている
	m_SizeX = GridCellCount;
	m_SizeZ = GridCellCount;
	m_Vertices.resize(VerticesPerCell * m_SizeX * m_SizeZ);

	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * VerticesPerCell + x * VerticesPerCell;
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
			int n = z * m_SizeX * VerticesPerCell + x * VerticesPerCell;

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
	m_Indices.resize(VerticesPerCell * m_SizeX * m_SizeZ);

	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * VerticesPerCell + x * VerticesPerCell;
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
}

//=======================================
// 床を描く道具：濡れた床のシェーダー・定数バッファ・テクスチャ・マテリアルを作っている
//=======================================
void Ground::CreateFloorResources()
{
	// 濡れた床のピクセルシェーダーを使うシェーダーを作っている
	m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/wetFloorPS.hlsl");

	// 濡れた床の値を渡す定数バッファを作っている
	Renderer::CreateRequiredConstantBuffer(
		sizeof(WetFloorBuffer),
		m_WetFloorBuffer.ReleaseAndGetAddressOf(), "濡れた床");

	// 水たまりの表を渡す定数バッファを作り、WaterEffectSystemで決めた値を書き込んでいる
	const WaterEffectSystem::PuddleCellBufferData& puddleCells = m_WaterEffects.GetPuddleCellData();
	Renderer::CreateRequiredConstantBuffer(
		static_cast<unsigned int>(sizeof(puddleCells)),
		m_PuddleCellBuffer.ReleaseAndGetAddressOf(), "水たまりのセル");
	Renderer::GetDeviceContext()->UpdateSubresource(
		m_PuddleCellBuffer.Get(), 0, nullptr, &puddleCells, 0, 0);

	// 床のテクスチャを読み込んでいる（読めなければファイル名を表示して終了している）
    constexpr const char* fieldTexturePath = "assets/texture/field.jpg";
    if (!m_Texture.Load(fieldTexturePath))
    {
        Utility::ReportFatalError(
            std::string("テクスチャを読み込めませんでした。\n") + fieldTexturePath);
    }

	// マテリアルを作っている
	m_Material = std::make_unique<Material>();
	MATERIAL mtrl;
	mtrl.Diffuse = Color(1, 1, 1, 1);
	mtrl.TextureEnable = true;// テクスチャを使う
	m_Material->CreateRequired(mtrl);
}

//=======================================
// 更新処理：経過時間を進め、電力が戻ったときの水面の変化と、水滴・波紋を更新している
//=======================================
void Ground::Update()
{
	// 水面・水滴・波紋は世界の時計で進めている（時間が止まった部屋では、落ちる水滴も波も止まる）
	const float deltaTime = Application::GetWorldDeltaTime();
	m_WetTime += deltaTime;
	// 長く遊んでも浮動小数点の精度が落ちないよう、時間を戻している
	if (m_WetTime > WetTimeWrapSeconds)
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
	const float response = powerRestored ? PowerBlendRiseRate : PowerBlendFallRate;
	m_PowerReflectionBlend +=
		(targetBlend - m_PowerReflectionBlend) * response * deltaTime;
	m_PowerReflectionBlend = std::clamp(
		m_PowerReflectionBlend, 0.0f, 1.0f);
	// 水面の乱れは約2.4秒で収まる
	m_PowerSurge = (std::max)(0.0f, m_PowerSurge - deltaTime * PowerSurgeDecayRate);
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
		m_Position.y + m_DrainTrace.w,
		m_WetTime,
		(hasFlood ? 1.0f : 0.0f) * Tools::TechDemo::WaterCausticsScale,
		m_DrainTrace);

	// 足音と水滴のおもりを、水たまりの波へ渡している。波を進めるのは描画の最初（RunGpuSimulation）で、
	// そのための経過時間・計算の中心（カメラ）・浸水した範囲をここで覚えている。
	for (const DirectX::SimpleMath::Vector4& impulse : m_WaterEffects.TakeRippleImpulses())
	{
		m_RippleSimulation.AddImpulse(
			DirectX::SimpleMath::Vector2(impulse.x, impulse.y), impulse.z, impulse.w);
	}
	AddRainRipples(deltaTime);
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
		const bool straight = yaw < AxisAlignedTolerance || yaw > DirectX::XM_PI - AxisAlignedTolerance;
		const bool quarter = std::fabs(yaw - DirectX::XM_PIDIV2) < AxisAlignedTolerance;
		if (!wall->IsCollisionEnabled() || position.y - scale.y * 0.5f > m_Position.y + RippleObstacleMaxHeight ||
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
	// 比較用の描画設定で「水たまりの波：なし」を選んでいるときは、波を計算も表示もしない
	if (!Tools::TechDemo::UseWaterRipples)
	{
		Renderer::SetWaterRipples(nullptr, DirectX::SimpleMath::Vector2::Zero, 0.0f, 0.0f);
		return;
	}
	m_RippleSimulation.Update(seconds, m_RippleCenter, m_PuddleCellBuffer.Get(), m_RippleFloodRect, m_RippleObstacles);
	Renderer::SetWaterRipples(m_RippleSimulation.GetHeightSRV(), m_RippleSimulation.GetOrigin(),
		m_RippleSimulation.GetWorldSize(), 1.0f);
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
		(SurgeWaveBase + SurgeWaveRange * std::sin(m_WetTime * SurgeWaveRate));
	wetFloor.RippleStrength =
		RippleStrengthBase + m_PowerReflectionBlend * RippleStrengthPower + surgeWave * RippleStrengthSurge;
	// 技術デモで「水面の反射なし」を選んでいるときは、反射の画像を混ぜない
	wetFloor.ReflectionStrength = Tools::TechDemo::GetReflectionMethod() != Tools::TechDemo::ReflectionMethod::Off
		? ReflectionStrengthBase + m_PowerReflectionBlend * ReflectionStrengthPower + surgeWave * ReflectionStrengthSurge
		: 0.0f;
	// 水に浸かった範囲がなければ、最小(1,1)が最大(0,0)より大きい値を渡して「範囲なし」にしている
	wetFloor.FloodRect = m_WaterEffects.HasFloodRegion()
		? DirectX::SimpleMath::Vector4(
			m_WaterEffects.GetFloodMin().x, m_WaterEffects.GetFloodMin().y,
			m_WaterEffects.GetFloodMax().x, m_WaterEffects.GetFloodMax().y)
		: DirectX::SimpleMath::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
	// 床に残る濡れた足跡（乾いたものは詰めて、数だけを渡している）
	wetFloor.FloodInletRect = m_FloodInletRect;
	m_Footprints.Fill(wetFloor.Footprints, wetFloor.FootprintInfo);
	// 天井の雨漏りの真下の水たまり（今の半径）
	const std::vector<WaterEffectSystem::Leak>& leaks = m_WaterEffects.GetLeaks();
	const int leakCount = (std::min)(static_cast<int>(leaks.size()), WaterEffectSystem::MaxLeakCount);
	for (int leakIndex = 0; leakIndex < leakCount; ++leakIndex)
	{
		const WaterEffectSystem::Leak& leak = leaks[static_cast<std::size_t>(leakIndex)];
		wetFloor.Leaks[leakIndex] = DirectX::SimpleMath::Vector4(leak.Center.x, leak.Center.y, leak.Radius, 0.0f);
	}
	constexpr auto puddleMeshMode = Tools::TechDemo::DefaultPuddleMeshMode;
	wetFloor.LeakInfo = DirectX::SimpleMath::Vector4(
		static_cast<float>(leakCount), puddleMeshMode == Tools::TechDemo::PuddleMeshMode::MeshLod ? 1.0f : 0.0f,
		m_FloodRedness, m_FloodInletRedness);
	// 床の本体は乾いた床として描き、水たまりは後でそのメッシュに描き重ねる（DrawWaterSurfaces）。
	// 比較用の描画設定で「床全体で計算」を選んでいるときは、前の方式（床の全部の画素で水たまりを計算し、メッシュは描かない）にしている
	const bool drawsPuddleMeshes = puddleMeshMode != Tools::TechDemo::PuddleMeshMode::FullFloor;
	wetFloor.WaterSurfaceLevel = static_cast<float>(drawsPuddleMeshes ? WaterLevel::DryFloor : WaterLevel::Near);
	devicecontext->UpdateSubresource(
		m_WetFloorBuffer.Get(), 0, nullptr, &wetFloor, 0, 0);
	// ピクセルシェーダーのb10へ設定している
	ID3D11Buffer* wetFloorBuffer = m_WetFloorBuffer.Get();
	devicecontext->PSSetConstantBuffers(WetFloorBufferSlot, 1, &wetFloorBuffer);
	// 水たまりの表をb13へ設定している
	ID3D11Buffer* puddleCellBuffer = m_PuddleCellBuffer.Get();
	devicecontext->PSSetConstantBuffers(PuddleCellBufferSlot, 1, &puddleCellBuffer);

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

void Ground::DrawCorridorFloodWave()
{
	m_FloodWave.Draw(m_Position.y + WaterSurfaceLift, m_WetTime);
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
	if constexpr (Tools::TechDemo::DefaultPuddleMeshMode != Tools::TechDemo::PuddleMeshMode::FullFloor)
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
	m_FloodWave.Reset();
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
