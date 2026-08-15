#include "Ground.h"
#include "stb_image.h"
#include "Game.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
using namespace DirectX::SimpleMath;

//=======================================
//初期化処理
//=======================================
void Ground::Init()
{
	m_WetTime = 0.0f;
	m_PowerReflectionBlend = 0.0f;
	m_PowerSurge = 0.0f;
	m_WasPowerRestored = false;
	BuildFallingDrops();

	// 頂点データ
	m_SizeX = 50;
	m_SizeZ = 50;
	/*m_SizeX=10
	  m_SizeZ=30　　縦長の地形になる*/
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

	//読み込む画像ファイルのパス
	const char* filename = "assets/texture/teran.png";

	//画像データを格納するポインタ
	unsigned char* imageDate = nullptr;
	int width, height, channels;

	//グレースケール(1チャネル)で画像を読み込む
	imageDate = stbi_load(filename, &width, &height, &channels, 1);
	if (imageDate)
	{
		for (int z = 0; z <= m_SizeZ; z++)
		{
			for (int x = 0; x <= m_SizeX; x++)
			{
				//高さを計算
				int picX = (int)(1 + x * (float)(width - 2) / m_SizeX);//左右ピクセルを無視
				int picY = (int)(1 + z * (float)(height - 2) / m_SizeZ);//上下1ピクセルを無視
				unsigned char pixelValue = imageDate[picY * width + picX];
				float h = (float)pixelValue / 4.0f;//土地のデコボコ具合を調整Y座標

				//頂点座標に高さを代入
				int n = z * m_SizeX * 6 + x * 6;
				if (x != m_SizeX && z != m_SizeZ)
				{
					m_Vertices[n].position.y = h;
				}

				if (x != 0 && z != m_SizeZ)//左隣のポリゴン
				{
					m_Vertices[n - 2].position.y = h;
					m_Vertices[n - 5].position.y = h;
				}

				if (x != m_SizeX && z != 0)//上隣のポリゴン
				{
					m_Vertices[n - m_SizeX * 6 + 2].position.y = h;
					m_Vertices[n - m_SizeX * 6 + 3].position.y = h;
				}

				if (x != 0 && z != 0)//左上隣のポリゴン
				{
					m_Vertices[n - m_SizeX * 6 - 1].position.y = h;
				}
			}
		}

		//メモリを解放
		stbi_image_free(imageDate);
	}

	//法線ベクトルを更新
	for (int z = 0; z < m_SizeZ; z++)
	{
		for (int x = 0; x < m_SizeX; x++)
		{
			int n = z * m_SizeX * 6 + x * 6;

			//2つのベクトルを計算
			Vector3 v1 = m_Vertices[n + 1].position - m_Vertices[n + 0].position;
			Vector3 v2 = m_Vertices[n + 2].position - m_Vertices[n + 0].position;
			Vector3 normal = v1.Cross(v2);//外積を計算
			normal.Normalize();			  //正規化
			m_Vertices[n + 0].normal = normal;
			m_Vertices[n + 1].normal = normal;
			m_Vertices[n + 2].normal = normal;

			//2つのベクトルを計算
			v1 = m_Vertices[n + 4].position - m_Vertices[n + 3].position;
			v2 = m_Vertices[n + 5].position - m_Vertices[n + 3].position;
			normal = v1.Cross(v2);//外積を計算
			normal.Normalize();	  //正規化

			m_Vertices[n + 3].normal = normal;
			m_Vertices[n + 4].normal = normal;
			m_Vertices[n + 5].normal = normal;
		}
	}

	// 頂点バッファ生成
	m_VertexBuffer.Create(m_Vertices);

	// インデックデータ
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

	// インデックスバッファ生成
	m_IndexBuffer.Create(m_Indices);

	// シェーダオブジェクト生成
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/wetFloorPS.hlsl");

	Renderer::CreateConstantBuffer(
		sizeof(WetFloorBuffer),
		m_WetFloorBuffer.ReleaseAndGetAddressOf());

	//テクスチャロード
	bool sts = m_Texture.Load("assets/texture/field.jpg");
	assert(sts == true);

	//マテリアル情報取得
	m_Material = std::make_unique<Material>();
	MATERIAL mtrl;
	mtrl.Diffuse = Color(1, 1, 1, 1);
	mtrl.TextureEnable = true;//テクスチャを使うか否かのフラグ
	m_Material->Create(mtrl);
	//Groundの位置や大きさを調整
	m_Position.y = -100.0f;
	m_Scale.x = 20.0f;
	m_Scale.z = 20.0f;
}

//=======================================
//更新処理
//=======================================
void Ground::Update()
{
	constexpr float deltaTime = 1.0f / 60.0f;
	m_WetTime += deltaTime;
	if (m_WetTime > 10000.0f)
	{
		m_WetTime = 0.0f;
	}

	const bool powerRestored =
		Core::Game::GetInstance()->IsPowerRestored();
	if (powerRestored && !m_WasPowerRestored)
	{
		// Briefly disturb the water when the ceiling fixtures surge on.
		m_PowerSurge = 1.0f;
	}
	m_WasPowerRestored = powerRestored;

	const float targetBlend = powerRestored ? 1.0f : 0.0f;
	const float response = powerRestored ? 2.2f : 4.0f;
	m_PowerReflectionBlend +=
		(targetBlend - m_PowerReflectionBlend) * response * deltaTime;
	m_PowerReflectionBlend = std::clamp(
		m_PowerReflectionBlend, 0.0f, 1.0f);
	m_PowerSurge = (std::max)(0.0f, m_PowerSurge - deltaTime * 0.42f);
	UpdateFallingDrops(deltaTime);
	UpdateFootstepRipples(deltaTime);
}

//=======================================
//描画処理
//=======================================
void Ground::Draw(Camera* cam)
{
	//カメラを選択する
	cam->SetCamera();

	// SRT情報作成
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.x, m_Rotation.y, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);

	Matrix worldmtx;
	worldmtx = s * r * t;
	Renderer::SetWorldMatrix(&worldmtx); // GPUにセット

	// 描画の処理
	ID3D11DeviceContext* devicecontext;
	devicecontext = Renderer::GetDeviceContext();

	// トポロジーをセット（プリミティブタイプ）
	devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_Shader.SetGPU();
	m_VertexBuffer.SetGPU();
	m_IndexBuffer.SetGPU();

	m_Texture.SetGPU();
	m_Material->SetGPU();

	WetFloorBuffer wetFloor{};
	wetFloor.Time = m_WetTime;
	const float surgeWave = m_PowerSurge *
		(0.55f + 0.45f * std::sin(m_WetTime * 17.0f));
	wetFloor.RippleStrength =
		0.92f + m_PowerReflectionBlend * 0.10f + surgeWave * 0.18f;
	wetFloor.ReflectionStrength =
		0.96f + m_PowerReflectionBlend * 0.22f + surgeWave * 0.12f;
	devicecontext->UpdateSubresource(
		m_WetFloorBuffer.Get(), 0, nullptr, &wetFloor, 0, 0);
	ID3D11Buffer* wetFloorBuffer = m_WetFloorBuffer.Get();
	devicecontext->PSSetConstantBuffers(10, 1, &wetFloorBuffer);

	devicecontext->DrawIndexed(
		(UINT)m_Indices.size(),	// 描画するインデックス数
		0,					// 最初のインデックスバッファの位置
		0);

	DrawFallingDrops(cam);
	DrawWaterRipples(cam);
}

//=======================================
//終了処理
//=======================================
void Ground::Uninit()
{
	m_FallingDrops.clear();
	m_PuddleCenters.clear();
	m_DropVertices.clear();
	m_DropMaterial.reset();
	m_RippleVertices.clear();
	m_FootstepRipples.clear();
	m_RippleMaterial.reset();
	m_WetFloorBuffer.Reset();
}

void Ground::BuildFallingDrops()
{
	m_FallingDrops.clear();
	m_PuddleCenters.clear();
	m_DropVertices.clear();
	m_RippleVertices.clear();
	m_FallingDrops.reserve(24);
	m_PuddleCenters.reserve(40);
	m_FootstepRipples.reserve(8);

	// A tapered crossed streak reads as a small water drop from every angle.
	const Color dropColor(0.62f, 0.76f, 0.80f, 0.80f);
	const float tailWidth = 0.025f;
	const float headWidth = 0.14f;
	const float halfHeight = 0.58f;
	const auto addQuad = [this, &dropColor](
		const Vector3& a, const Vector3& b,
		const Vector3& c, const Vector3& d,
		const Vector3& normal)
	{
		const Vector3 positions[] = { a, b, c, c, b, d };
		for (const Vector3& position : positions)
		{
			VERTEX_3D vertex{};
			vertex.position = position;
			vertex.normal = normal;
			vertex.color = dropColor;
			m_DropVertices.push_back(vertex);
		}
	};

	addQuad(
		Vector3(-headWidth, -halfHeight, 0.0f),
		Vector3(-tailWidth, halfHeight, 0.0f),
		Vector3(headWidth, -halfHeight, 0.0f),
		Vector3(tailWidth, halfHeight, 0.0f),
		Vector3(0.0f, 0.0f, 1.0f));
	addQuad(
		Vector3(0.0f, -halfHeight, -headWidth),
		Vector3(0.0f, halfHeight, -tailWidth),
		Vector3(0.0f, -halfHeight, headWidth),
		Vector3(0.0f, halfHeight, tailWidth),
		Vector3(1.0f, 0.0f, 0.0f));

	m_DropVertexBuffer.Create(m_DropVertices);
	m_DropShader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	MATERIAL material{};
	material.Diffuse = Color(0.64f, 0.78f, 0.82f, 0.82f);
	material.Ambient = Color(0.32f, 0.42f, 0.46f, 0.82f);
	material.Emission = Color(0.065f, 0.10f, 0.115f, 0.0f);
	material.Specular = Color(0.92f, 0.98f, 1.00f, 0.82f);
	material.Shininess = 42.0f;
	material.TextureEnable = FALSE;
	m_DropMaterial = std::make_unique<Material>();
	m_DropMaterial->Create(material);

	const Color rippleColor(0.62f, 0.78f, 0.82f, 0.72f);
	constexpr int rippleSegments = 32;
	constexpr float innerRadius = 0.76f;
	for (int segment = 0; segment < rippleSegments; ++segment)
	{
		const float angle0 = DirectX::XM_2PI *
			static_cast<float>(segment) / static_cast<float>(rippleSegments);
		const float angle1 = DirectX::XM_2PI *
			static_cast<float>(segment + 1) / static_cast<float>(rippleSegments);
		const Vector3 inner0(std::cos(angle0) * innerRadius, 0.0f,
			std::sin(angle0) * innerRadius);
		const Vector3 outer0(std::cos(angle0), 0.0f, std::sin(angle0));
		const Vector3 inner1(std::cos(angle1) * innerRadius, 0.0f,
			std::sin(angle1) * innerRadius);
		const Vector3 outer1(std::cos(angle1), 0.0f, std::sin(angle1));
		const Vector3 positions[] =
		{
			inner0, outer0, inner1, inner1, outer0, outer1,
			inner1, outer0, inner0, outer1, outer0, inner1
		};
		for (const Vector3& position : positions)
		{
			VERTEX_3D vertex{};
			vertex.position = position;
			vertex.normal = Vector3::Up;
			vertex.color = rippleColor;
			m_RippleVertices.push_back(vertex);
		}
	}
	m_RippleVertexBuffer.Create(m_RippleVertices);
	MATERIAL rippleMaterial{};
	rippleMaterial.Diffuse = Color(0.34f, 0.48f, 0.52f, 0.70f);
	rippleMaterial.Emission = Color(0.035f, 0.065f, 0.075f, 0.0f);
	rippleMaterial.Specular = Color(0.80f, 0.90f, 0.94f, 0.70f);
	rippleMaterial.Shininess = 64.0f;
	rippleMaterial.TextureEnable = FALSE;
	m_RippleMaterial = std::make_unique<Material>();
	m_RippleMaterial->Create(rippleMaterial);

	const auto fraction = [](float value)
	{
		return value - std::floor(value);
	};
	const auto hash21 = [&fraction](float x, float y)
	{
		return fraction(std::sin(x * 127.1f + y * 311.7f) * 43758.5453f);
	};
	const auto hash22 = [&hash21](float x, float y)
	{
		return Vector2(
			hash21(x + 17.3f, y + 41.7f),
			hash21(x + 93.1f, y + 11.8f));
	};

	struct DropCandidate
	{
		int CellX;
		int CellZ;
		Vector2 RandomValue;
		Vector2 PuddleCenter;
		float DistanceSquared;
	};

	constexpr float cellSize = 82.0f;
	std::vector<DropCandidate> candidates;
	for (int cellZ = -4; cellZ <= 4; ++cellZ)
	{
		for (int cellX = -4; cellX <= 4; ++cellX)
		{
			const float puddleSeed = hash21(
				static_cast<float>(cellX) + 53.4f,
				static_cast<float>(cellZ) + 27.9f);
			if (puddleSeed < 0.62f)
			{
				continue;
			}

			const Vector2 randomValue = hash22(
				static_cast<float>(cellX),
				static_cast<float>(cellZ));
			const Vector2 puddleCenter =
				(Vector2(static_cast<float>(cellX), static_cast<float>(cellZ)) +
				 Vector2(0.5f, 0.5f) +
				 (randomValue - Vector2(0.5f, 0.5f)) * 0.22f) * cellSize;
			m_PuddleCenters.push_back(puddleCenter);
			// The playable area is centred slightly toward positive Z. Sorting
			// prevents the limited pool from being consumed by map-edge cells.
			const Vector2 fromGameplayCenter =
				puddleCenter - Vector2(0.0f, 70.0f);
			candidates.push_back({
				cellX,
				cellZ,
				randomValue,
				puddleCenter,
				fromGameplayCenter.x * fromGameplayCenter.x +
				fromGameplayCenter.y * fromGameplayCenter.y });
		}
	}

	std::sort(
		candidates.begin(),
		candidates.end(),
		[](const DropCandidate& left, const DropCandidate& right)
		{
			return left.DistanceSquared < right.DistanceSquared;
		});

	const size_t dropCount = (std::min)(candidates.size(), size_t(24));
	for (size_t candidateIndex = 0; candidateIndex < dropCount; ++candidateIndex)
	{
		const DropCandidate& candidate = candidates[candidateIndex];
		const float cellX = static_cast<float>(candidate.CellX);
		const float cellZ = static_cast<float>(candidate.CellZ);
		const Vector2& randomValue = candidate.RandomValue;
		const Vector2& puddleCenter = candidate.PuddleCenter;

		FallingDrop drop{};
		drop.Seed = hash21(cellX + 9.7f, cellZ + 71.3f);
		drop.Position = Vector3(
			puddleCenter.x + (drop.Seed - 0.5f) * 9.0f,
			-52.0f - randomValue.y * 22.0f,
			puddleCenter.y + (randomValue.x - 0.5f) * 9.0f);
		drop.Speed = 19.0f + drop.Seed * 13.0f;
		drop.WaitTimer = randomValue.x * 1.4f;
		drop.Active = drop.WaitTimer <= 0.01f;
		m_FallingDrops.push_back(drop);
	}
}

void Ground::UpdateFallingDrops(float deltaTime)
{
	for (FallingDrop& drop : m_FallingDrops)
	{
		drop.ImpactTimer = (std::max)(0.0f, drop.ImpactTimer - deltaTime);

		if (!drop.Active)
		{
			drop.WaitTimer -= deltaTime;
			if (drop.WaitTimer <= 0.0f)
			{
				drop.Active = true;
				drop.Position.y = -52.0f - drop.Seed * 14.0f;
			}
			continue;
		}

		drop.Position.y -= drop.Speed * deltaTime;
		if (drop.Position.y <= -99.35f)
		{
			drop.Position.y = -99.35f;
			drop.Active = false;
			drop.ImpactTimer = 0.55f;
			drop.WaitTimer = 0.65f + drop.Seed * 1.9f;
		}
	}
}

bool Ground::IsInsidePuddle(const Vector3& position) const
{
	for (const Vector2& center : m_PuddleCenters)
	{
		const float offsetX = position.x - center.x;
		const float offsetZ = position.z - center.y;
		if ((offsetX * offsetX) / (24.0f * 24.0f) +
			(offsetZ * offsetZ) / (17.0f * 17.0f) <= 1.0f)
		{
			return true;
		}
	}
	return false;
}

void Ground::UpdateFootstepRipples(float deltaTime)
{
	m_FootstepRippleCooldown = (std::max)(
		0.0f, m_FootstepRippleCooldown - deltaTime);
	m_LensSplashCooldown = (std::max)(
		0.0f, m_LensSplashCooldown - deltaTime);
	for (WaterRipple& ripple : m_FootstepRipples)
	{
		ripple.Age += deltaTime;
	}
	std::erase_if(m_FootstepRipples, [](const WaterRipple& ripple)
	{
		return ripple.Age >= ripple.Duration;
	});

	Player* player = Core::Game::GetInstance()->GetObj<Player>("Player");
	if (player == nullptr)
	{
		m_HasLastPlayerPosition = false;
		return;
	}

	const Vector3 playerPosition = player->GetPosition();
	if (!m_HasLastPlayerPosition)
	{
		m_LastPlayerPosition = playerPosition;
		m_HasLastPlayerPosition = true;
		return;
	}

	const float movementX = playerPosition.x - m_LastPlayerPosition.x;
	const float movementZ = playerPosition.z - m_LastPlayerPosition.z;
	const float movementSquared = movementX * movementX + movementZ * movementZ;
	m_LastPlayerPosition = playerPosition;
	if (movementSquared < 0.015f || m_FootstepRippleCooldown > 0.0f ||
		!IsInsidePuddle(playerPosition))
	{
		return;
	}

	WaterRipple ripple{};
	ripple.Position = Vector3(playerPosition.x, -99.96f, playerPosition.z);
	ripple.Duration = player->IsSprinting() ? 0.82f : 0.68f;
	ripple.MaxRadius = player->IsSprinting() ? 4.4f : 3.2f;
	m_FootstepRipples.push_back(ripple);
	if (m_FootstepRipples.size() > 8)
	{
		m_FootstepRipples.erase(m_FootstepRipples.begin());
	}
	m_FootstepRippleCooldown = player->IsSprinting() ? 0.22f : 0.36f;

	if (m_LensSplashCooldown <= 0.0f)
	{
		const float moistureStrength = player->IsSprinting() ? 0.92f : 0.58f;
		const float moistureDuration = player->IsSprinting() ? 5.2f : 3.6f;
		Core::Game::GetInstance()->GetPostProcess()->TriggerLensMoisture(
			moistureStrength,
			moistureDuration);
		m_LensSplashCooldown = player->IsSprinting() ? 1.15f : 1.75f;
	}
}

void Ground::DrawFallingDrops(Camera* camera)
{
	if (m_DropVertices.empty() || m_DropMaterial == nullptr)
	{
		return;
	}

	camera->SetCamera();
	ID3D11DeviceContext* context = Renderer::GetDeviceContext();
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Renderer::SetBlendState(BS_ALPHABLEND);
	m_DropShader.SetGPU();
	m_DropVertexBuffer.SetGPU();
	m_DropMaterial->SetGPU();

	for (const FallingDrop& drop : m_FallingDrops)
	{
		if (!drop.Active)
		{
			continue;
		}

		Matrix world = Matrix::CreateTranslation(drop.Position);
		Renderer::SetWorldMatrix(&world);
		context->Draw(static_cast<UINT>(m_DropVertices.size()), 0);
	}

	Renderer::SetBlendState(BS_NONE);
}

void Ground::DrawWaterRipples(Camera* camera)
{
	if (m_RippleVertices.empty() || m_RippleMaterial == nullptr ||
		m_DropMaterial == nullptr)
	{
		return;
	}

	camera->SetCamera();
	ID3D11DeviceContext* context = Renderer::GetDeviceContext();
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_DropShader.SetGPU();

	Renderer::SetBlendState(BS_ALPHABLEND);
	m_DropVertexBuffer.SetGPU();
	m_DropMaterial->SetGPU();
	for (const FallingDrop& drop : m_FallingDrops)
	{
		const float impactAge = 0.55f - drop.ImpactTimer;
		if (drop.ImpactTimer <= 0.0f || impactAge > 0.20f)
		{
			continue;
		}

		const float progress = impactAge / 0.20f;
		const float spread = progress * 0.95f;
		const float height = std::sin(progress * DirectX::XM_PI) * 1.15f;
		const Vector2 directions[] =
		{
			Vector2(1.0f, 0.0f), Vector2(-1.0f, 0.0f),
			Vector2(0.0f, 1.0f), Vector2(0.0f, -1.0f)
		};
		for (const Vector2& direction : directions)
		{
			const Vector3 splashPosition(
				drop.Position.x + direction.x * spread,
				-99.72f + height,
				drop.Position.z + direction.y * spread);
			Matrix world = Matrix::CreateScale(0.28f, 0.34f, 0.28f) *
				Matrix::CreateTranslation(splashPosition);
			Renderer::SetWorldMatrix(&world);
			context->Draw(static_cast<UINT>(m_DropVertices.size()), 0);
		}
	}

	Renderer::SetBlendState(BS_ADDITIVE);
	m_RippleVertexBuffer.SetGPU();
	const auto drawRipple = [this, context](
		const Vector3& position,
		float progress,
		float radius,
		float strength)
	{
		const float fade = (1.0f - progress) * strength;
		MATERIAL material{};
		material.Diffuse = Color(
			0.20f * fade, 0.34f * fade, 0.38f * fade, fade);
		material.Emission = Color(
			0.025f * fade, 0.055f * fade, 0.065f * fade, 0.0f);
		material.Specular = Color(
			0.65f * fade, 0.78f * fade, 0.82f * fade, fade);
		material.Shininess = 72.0f;
		material.TextureEnable = FALSE;
		m_RippleMaterial->SetMaterial(material);
		m_RippleMaterial->SetGPU();

		Matrix world = Matrix::CreateScale(radius, 1.0f, radius) *
			Matrix::CreateTranslation(position);
		Renderer::SetWorldMatrix(&world);
		context->Draw(static_cast<UINT>(m_RippleVertices.size()), 0);
	};

	for (const FallingDrop& drop : m_FallingDrops)
	{
		if (drop.ImpactTimer <= 0.0f)
		{
			continue;
		}
		const float progress = 1.0f - drop.ImpactTimer / 0.55f;
		drawRipple(
			Vector3(drop.Position.x, -99.96f, drop.Position.z),
			progress,
			0.35f + progress * 2.5f,
			0.82f);
	}

	for (const WaterRipple& ripple : m_FootstepRipples)
	{
		const float progress = ripple.Age / ripple.Duration;
		drawRipple(
			ripple.Position,
			progress,
			0.55f + progress * ripple.MaxRadius,
			0.62f);
	}

	Renderer::SetBlendState(BS_NONE);
}

//=======================================
//頂点情報を取得
//=======================================
std::vector<VERTEX_3D>Ground::GetVertices()
{
	std::vector<VERTEX_3D>res;
	res.resize(m_Vertices.size());
	// SRT情報作成
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);
	Matrix worldmtx = s * r * t;

	//ワールド変換してデータを代入
	for (int i = 0; i < m_Vertices.size(); i++)
	{
		res[i].position = Vector3::Transform(m_Vertices[i].position, worldmtx);
		res[i].normal= Vector3::Transform(m_Vertices[i].normal, worldmtx);
		res[i].uv = m_Vertices[i].uv;
	}
	return res;
}

void Ground::SetTexture(const char* filename)
{
	m_Texture.Load(filename);
}