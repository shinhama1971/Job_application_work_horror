// ============================================================================
// ファイルの役割: 水たまりの位置、天井から落ちる水滴、着水と足音の波紋の作成と描画を管理している。
// 主な技術: 決まった数の使い回し（オブジェクトプール）、距離順の並べ替え、アルファ合成・加算合成、時間で薄れる表現
// ============================================================================

#include "Ground.h"

#include "Game.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

// 水滴と波紋の形・マテリアルを作り、シェーダーと同じ計算で水たまりの位置を求め、水滴を落とす場所を決めている
void WaterEffectSystem::Init()
{
	m_FallingDrops.clear();
	m_PuddleCenters.clear();
	m_DropVertices.clear();
	m_RippleVertices.clear();
	m_FallingDrops.reserve(24);
	m_PuddleCenters.reserve(40);
	m_FootstepRipples.reserve(8);

	// 先が細い板を十字に組み、どの角度から見ても小さな水滴に見える形にしている。
	const Color dropColor(0.62f, 0.76f, 0.80f, 0.80f);
	const float tailWidth = 0.025f;
	const float headWidth = 0.14f;
	const float halfHeight = 0.58f;
	// 4頂点で四角形（三角形2つ）を追加している
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

	// 水滴のマテリアル：薄い青の半透明で、光沢を強くしている
	MATERIAL material{};
	material.Diffuse = Color(0.64f, 0.78f, 0.82f, 0.82f);
	material.Ambient = Color(0.32f, 0.42f, 0.46f, 0.82f);
	material.Emission = Color(0.065f, 0.10f, 0.115f, 0.0f);
	material.Specular = Color(0.92f, 0.98f, 1.00f, 0.82f);
	material.Shininess = 42.0f;
	material.TextureEnable = FALSE;
	m_DropMaterial = std::make_unique<Material>();
	m_DropMaterial->Create(material);

	// 波紋の形：内側の半径0.76・外側の半径1の細い輪を32分割で作っている（表と裏の両面）
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
	// 波紋のマテリアル（色の濃さは描くときに薄れ具合で毎回変えている）
	MATERIAL rippleMaterial{};
	rippleMaterial.Diffuse = Color(0.34f, 0.48f, 0.52f, 0.70f);
	rippleMaterial.Emission = Color(0.035f, 0.065f, 0.075f, 0.0f);
	rippleMaterial.Specular = Color(0.80f, 0.90f, 0.94f, 0.70f);
	rippleMaterial.Shininess = 64.0f;
	rippleMaterial.TextureEnable = FALSE;
	m_RippleMaterial = std::make_unique<Material>();
	m_RippleMaterial->Create(rippleMaterial);

	// 0〜1の疑似乱数。wetFloorPS.hlslの水たまりと同じ式を使い、見た目と判定の位置をそろえている
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

	// 水滴を落とす場所の候補（水たまりがあるマス）
	struct DropCandidate
	{
		int CellX;
		int CellZ;
		Vector2 RandomValue;
		Vector2 PuddleCenter;
		float DistanceSquared;
	};

	// 床を82x82のマスに分け、乱数がしきい値を超えたマスにだけ水たまりを置いている（シェーダーと同じ判定）
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
			// 遊ぶ範囲はZの正の方へ少し寄っているため、候補をその中心からの距離順に並べている。
			// 限られた数の水滴が、マップの端のマスだけで埋まらないようにしている。
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

	// 近い順に最大24個の水滴を、水たまりの真上から落としている（落ちる速さと待ち時間はマスごとに変えている）
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

// 水滴を落とし、床に着いたら着水の時間を始め、少し待ってからまた上から落としている
void WaterEffectSystem::UpdateFallingDrops(float deltaTime)
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

// その位置が水たまり（楕円）か、水に浸かった範囲の中かを返している
bool WaterEffectSystem::IsInsidePuddle(const Vector3& position) const
{
	if (IsInsideFloodRegion(position))
	{
		return true;
	}
	for (const Vector2& center : m_PuddleCenters)
	{
		const float offsetX = position.x - center.x;
		const float offsetZ = position.z - center.y;
		// 水たまりは横24・縦17の楕円として判定している
		if ((offsetX * offsetX) / (24.0f * 24.0f) +
			(offsetZ * offsetZ) / (17.0f * 17.0f) <= 1.0f)
		{
			return true;
		}
	}
	return false;
}

bool WaterEffectSystem::IsAnyPuddleVisible(const Camera& camera) const
{
	if (m_HasFloodRegion)
	{
		// 水に浸かった範囲を長い方向に4つへ分け、それぞれを囲む球で判定している。
		// 範囲全体を1つの大きな球で囲むと、ほかの場所を見ているときも反射の描画が動いてしまうためである。
		constexpr int SegmentCount = 4;
		const Vector2 size = m_FloodMax - m_FloodMin;
		const bool alongZ = size.y >= size.x;
		const Vector2 segmentSize = alongZ
			? Vector2(size.x, size.y / SegmentCount)
			: Vector2(size.x / SegmentCount, size.y);
		const float radius = segmentSize.Length() * 0.5f;
		for (int index = 0; index < SegmentCount; ++index)
		{
			const float offset = (static_cast<float>(index) + 0.5f) / SegmentCount;
			const Vector2 center = alongZ
				? Vector2(m_FloodMin.x + size.x * 0.5f, m_FloodMin.y + size.y * offset)
				: Vector2(m_FloodMin.x + size.x * offset, m_FloodMin.y + size.y * 0.5f);
			if (camera.IsSphereVisible(Vector3(center.x, -99.35f, center.y), radius))
			{
				return true;
			}
		}
	}
	for (const Vector2& center : m_PuddleCenters)
	{
		// 水たまりは最大でおよそ48x34。余白を含めた境界球で、画面の端で欠けるのを防いでいる。
		if (camera.IsSphereVisible(
			Vector3(center.x, -99.35f, center.y),
			31.0f))
		{
			return true;
		}
	}
	return false;
}

// 足音の波紋を古くし、消える時間を過ぎたものを取り除いている
void WaterEffectSystem::UpdateFootstepRipples(float deltaTime)
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

	// 波紋を作るのは、Playerの足音のタイミングから呼んでいる。
	// これにより、壁へ向かって歩いた場合やフレームが落ちたときも、音と波紋がずれない。
}

// 足元が水の中なら波紋を1つ作り、たまにレンズに水しぶきが付いた効果を出している。
// 間隔を空けるために作らなかったときも、水の中ならtrueを返している（足音を水音にするため）
bool WaterEffectSystem::TriggerFootstepRipple(
	const Vector3& playerPosition,
	bool sprinting)
{
	if (m_FootstepRippleCooldown > 0.0f || !IsInsidePuddle(playerPosition))
	{
		return IsInsidePuddle(playerPosition);
	}

	WaterRipple ripple{};
	ripple.Position = Vector3(playerPosition.x, -99.96f, playerPosition.z);
	ripple.Duration = sprinting ? 0.82f : 0.68f;
	ripple.MaxRadius = sprinting ? 4.4f : 3.2f;
	m_FootstepRipples.push_back(ripple);
	// 足音の波紋は最大8個まで（古いものから消している）
	if (m_FootstepRipples.size() > 8)
	{
		m_FootstepRipples.erase(m_FootstepRipples.begin());
	}
	m_FootstepRippleCooldown = sprinting ? 0.22f : 0.36f;

	// 走っているほど強く長く、レンズの曇りを出している（続けて出しすぎないよう間隔を空けている）
	if (m_LensSplashCooldown <= 0.0f)
	{
		const float moistureStrength = sprinting ? 0.92f : 0.58f;
		const float moistureDuration = sprinting ? 5.2f : 3.6f;
		Core::Game::GetInstance()->GetPostProcess()->TriggerLensMoisture(
			moistureStrength,
			moistureDuration);
		m_LensSplashCooldown = sprinting ? 1.15f : 1.75f;
	}
	return true;
}

// 落ちている水滴を、半透明で描いている（画面に映っているものだけ）
void WaterEffectSystem::DrawFallingDrops(Camera* camera)
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
		if (!drop.Active ||
			!camera->IsSphereVisible(drop.Position, 2.0f))
		{
			continue;
		}

		Matrix world = Matrix::CreateTranslation(drop.Position);
		Renderer::SetWorldMatrix(&world);
		context->Draw(static_cast<UINT>(m_DropVertices.size()), 0);
	}

	Renderer::SetBlendState(BS_NONE);
}

// 着水したところの、小さなしぶきと広がる波紋を描いている
void WaterEffectSystem::DrawWaterRipples(Camera* camera)
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
	// 着水から0.2秒の間、4方向へ小さなしぶきを飛ばしている（水滴の形を小さくして使い回している）
	for (const FallingDrop& drop : m_FallingDrops)
	{
		const float impactAge = 0.55f - drop.ImpactTimer;
		if (drop.ImpactTimer <= 0.0f || impactAge > 0.20f ||
			!camera->IsSphereVisible(drop.Position, 3.0f))
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

	// 波紋は加算合成で、光が当たったように明るく重ねている
	Renderer::SetBlendState(BS_ADDITIVE);
	m_RippleVertexBuffer.SetGPU();
	// 1つの波紋を、広がり具合（progress）に合わせて大きく・薄くして描いている
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

	// 水滴が着水したところの波紋
	for (const FallingDrop& drop : m_FallingDrops)
	{
		if (drop.ImpactTimer <= 0.0f ||
			!camera->IsSphereVisible(drop.Position, 4.0f))
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

	// 足音の波紋
	for (const WaterRipple& ripple : m_FootstepRipples)
	{
		if (!camera->IsSphereVisible(ripple.Position, ripple.MaxRadius + 1.0f))
		{
			continue;
		}
		const float progress = ripple.Age / ripple.Duration;
		drawRipple(
			ripple.Position,
			progress,
			0.55f + progress * ripple.MaxRadius,
			0.62f);
	}

	Renderer::SetBlendState(BS_NONE);
}

// 水滴と足音の波紋を更新している
void WaterEffectSystem::Update(float deltaTime)
{
	UpdateFallingDrops(deltaTime);
	UpdateFootstepRipples(deltaTime);
}

// 水滴と波紋を描いている
void WaterEffectSystem::Draw(Camera* camera)
{
	DrawFallingDrops(camera);
	DrawWaterRipples(camera);
}

// 作ったものをすべて解放している
void WaterEffectSystem::Uninit()
{
	m_FallingDrops.clear();
	m_PuddleCenters.clear();
	m_DropVertices.clear();
	m_DropMaterial.reset();
	m_RippleVertices.clear();
	m_FootstepRipples.clear();
	m_RippleMaterial.reset();
}

// Groundの関数は、水の効果の処理をそのまま呼んでいる
bool Ground::TriggerFootstepRipple(
	const Vector3& position,
	bool sprinting)
{
	return m_WaterEffects.TriggerFootstepRipple(position, sprinting);
}

bool Ground::IsInsidePuddle(const Vector3& position) const
{
	return m_WaterEffects.IsInsidePuddle(position);
}

bool Ground::IsAnyPuddleVisible(const Camera& camera) const
{
	return m_WaterEffects.IsAnyPuddleVisible(camera);
}
