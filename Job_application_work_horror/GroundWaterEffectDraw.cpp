// ============================================================================
// ファイルの役割: 水滴と波紋の描画用メッシュを作成し、カメラに映るものを描く。
// ============================================================================

#include "Ground.h"
#include "Camera.h"

#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
	constexpr float SplashSeconds = 0.20f;
	constexpr float SplashSpread = 0.95f;
	constexpr float SplashHeight = 1.15f;
	constexpr float SplashBaseY = -99.72f;
	// --- 水滴の形（先が細い板の十字）：色（薄い青の半透明）、細い先・太い元の半分の幅、半分の高さ
	const Color DropVertexColor(0.62f, 0.76f, 0.80f, 0.80f);
	constexpr float DropTailWidth = 0.025f;
	constexpr float DropHeadWidth = 0.14f;
	constexpr float DropHalfHeight = 0.58f;
	// 水滴のマテリアル：薄い青の半透明で、光沢を強くしている
	const Color DropDiffuse(0.64f, 0.78f, 0.82f, 0.82f);
	const Color DropAmbient(0.32f, 0.42f, 0.46f, 0.82f);
	const Color DropEmission(0.065f, 0.10f, 0.115f, 0.0f);
	const Color DropSpecular(0.92f, 0.98f, 1.00f, 0.82f);
	constexpr float DropShininess = 42.0f;
	// 波紋の形：頂点の色、分割数、内側の半径（外側は1）
	const Color RippleVertexColor(0.62f, 0.78f, 0.82f, 0.72f);
	constexpr int RippleSegments = 32;
	constexpr float RippleInnerRadius = 0.76f;
	// 波紋のマテリアル（作るとき）
	const Color RippleInitDiffuse(0.34f, 0.48f, 0.52f, 0.70f);
	const Color RippleInitEmission(0.035f, 0.065f, 0.075f, 0.0f);
	const Color RippleInitSpecular(0.80f, 0.90f, 0.94f, 0.70f);
	constexpr float RippleInitShininess = 64.0f;
	// 描くときの波紋の色（薄れ具合を掛ける）と光沢
	const Vector3 RippleDiffuse(0.20f, 0.34f, 0.38f);
	const Vector3 RippleEmission(0.025f, 0.055f, 0.065f);
	const Vector3 RippleSpecular(0.65f, 0.78f, 0.82f);
	constexpr float RippleShininess = 72.0f;
	// しぶきの粒の大きさ
	const Vector3 SplashDropScale(0.28f, 0.34f, 0.28f);
	// 画面に映るかを調べる球の半径（落ちている水滴・しぶき・着水の波紋）と、足音の波紋に足すゆとり
	constexpr float FallingDropCullRadius = 2.0f;
	constexpr float SplashCullRadius = 3.0f;
	constexpr float ImpactRippleCullRadius = 4.0f;
	constexpr float FootstepRippleCullMargin = 1.0f;
	// 着水の波紋：始めの半径と広がる幅、強さ。足音の波紋：始めの半径と強さ
	constexpr float ImpactRippleStartRadius = 0.35f;
	constexpr float ImpactRippleGrowth = 2.5f;
	constexpr float ImpactRippleStrength = 0.82f;
	constexpr float FootstepRippleStartRadius = 0.55f;
	constexpr float FootstepRippleStrength = 0.35f;
}

// 水滴と波紋の形・マテリアルを作り、シェーダーと同じ計算で水たまりの位置を求め、水滴を落とす場所を決めている
void WaterEffectSystem::Init()
{
	m_FallingDrops.clear();
	m_Leaks.clear();
	m_DropVertices.clear();
	m_RippleVertices.clear();
	m_FallingDrops.reserve(MaxPuddleDrops);
	m_FootstepRipples.reserve(MaxFootstepRipples);
	BuildDropMesh();
	BuildRippleMesh();

	// 水たまりは、床全体のマス目に乱数で置いている（1面）。2面は Ground::SetPuddleLayout で置き直している
	BuildPuddles(PuddleLayout{});
}

// 水滴の形とマテリアル：先が細い板を十字に組み、どの角度から見ても小さな水滴に見える形にしている。
void WaterEffectSystem::BuildDropMesh()
{
	const Color dropColor = DropVertexColor;
	const float tailWidth = DropTailWidth;
	const float headWidth = DropHeadWidth;
	const float halfHeight = DropHalfHeight;
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
	m_DropShader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	// 水滴のマテリアル：薄い青の半透明で、光沢を強くしている
	MATERIAL material{};
	material.Diffuse = DropDiffuse;
	material.Ambient = DropAmbient;
	material.Emission = DropEmission;
	material.Specular = DropSpecular;
	material.Shininess = DropShininess;
	material.TextureEnable = FALSE;
	m_DropMaterial = std::make_unique<Material>();
	m_DropMaterial->CreateRequired(material);
}

// 波紋の形：内側の半径0.76・外側の半径1の細い輪を32分割で作っている（表と裏の両面）
void WaterEffectSystem::BuildRippleMesh()
{
	const Color rippleColor = RippleVertexColor;
	constexpr int rippleSegments = RippleSegments;
	constexpr float innerRadius = RippleInnerRadius;
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
	rippleMaterial.Diffuse = RippleInitDiffuse;
	rippleMaterial.Emission = RippleInitEmission;
	rippleMaterial.Specular = RippleInitSpecular;
	rippleMaterial.Shininess = RippleInitShininess;
	rippleMaterial.TextureEnable = FALSE;
	m_RippleMaterial = std::make_unique<Material>();
	m_RippleMaterial->CreateRequired(rippleMaterial);
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
			!camera->IsSphereVisible(drop.Position, FallingDropCullRadius))
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
	DrawSplashes(camera, context);

	// 波紋は加算合成で、光が当たったように明るく重ねている
	Renderer::SetBlendState(BS_ADDITIVE);
	m_RippleVertexBuffer.SetGPU();

	// 水滴が着水したところの波紋
	for (const FallingDrop& drop : m_FallingDrops)
	{
		if (drop.ImpactTimer <= 0.0f ||
			!camera->IsSphereVisible(drop.Position, ImpactRippleCullRadius))
		{
			continue;
		}
		const float progress = 1.0f - drop.ImpactTimer / DropImpactSeconds;
		DrawRipple(
			context,
			Vector3(drop.Position.x, RippleSurfaceY, drop.Position.z),
			progress,
			ImpactRippleStartRadius + progress * ImpactRippleGrowth,
			ImpactRippleStrength);
	}

	// 足音の波紋（足を下ろした瞬間の小さな輪。そこから広がる波は、波のシミュレーションが描いている）
	for (const WaterRipple& ripple : m_FootstepRipples)
	{
		if (!camera->IsSphereVisible(ripple.Position, ripple.MaxRadius + FootstepRippleCullMargin))
		{
			continue;
		}
		const float progress = ripple.Age / ripple.Duration;
		DrawRipple(
			context,
			ripple.Position,
			progress,
			FootstepRippleStartRadius + progress * ripple.MaxRadius,
			FootstepRippleStrength);
	}

	Renderer::SetBlendState(BS_NONE);
}

// 着水から0.2秒の間、4方向へ小さなしぶきを飛ばしている（水滴の形を小さくして使い回している）
void WaterEffectSystem::DrawSplashes(Camera* camera, ID3D11DeviceContext* context)
{
	Renderer::SetBlendState(BS_ALPHABLEND);
	m_DropVertexBuffer.SetGPU();
	m_DropMaterial->SetGPU();
	for (const FallingDrop& drop : m_FallingDrops)
	{
		const float impactAge = DropImpactSeconds - drop.ImpactTimer;
		if (drop.ImpactTimer <= 0.0f || impactAge > SplashSeconds ||
			!camera->IsSphereVisible(drop.Position, SplashCullRadius))
		{
			continue;
		}

		const float progress = impactAge / SplashSeconds;
		const float spread = progress * SplashSpread;
		const float height = std::sin(progress * DirectX::XM_PI) * SplashHeight;
		const Vector2 directions[] =
		{
			Vector2(1.0f, 0.0f), Vector2(-1.0f, 0.0f),
			Vector2(0.0f, 1.0f), Vector2(0.0f, -1.0f)
		};
		for (const Vector2& direction : directions)
		{
			const Vector3 splashPosition(
				drop.Position.x + direction.x * spread,
				SplashBaseY + height,
				drop.Position.z + direction.y * spread);
			Matrix world = Matrix::CreateScale(SplashDropScale) *
				Matrix::CreateTranslation(splashPosition);
			Renderer::SetWorldMatrix(&world);
			context->Draw(static_cast<UINT>(m_DropVertices.size()), 0);
		}
	}
}

// 1つの波紋を、広がり具合（progress）に合わせて大きく・薄くして描いている
void WaterEffectSystem::DrawRipple(ID3D11DeviceContext* context, const Vector3& position,
	float progress, float radius, float strength)
{
	const float fade = (1.0f - progress) * strength;
	MATERIAL material{};
	material.Diffuse = Color(
		RippleDiffuse.x * fade, RippleDiffuse.y * fade, RippleDiffuse.z * fade, fade);
	material.Emission = Color(
		RippleEmission.x * fade, RippleEmission.y * fade, RippleEmission.z * fade, 0.0f);
	material.Specular = Color(
		RippleSpecular.x * fade, RippleSpecular.y * fade, RippleSpecular.z * fade, fade);
	material.Shininess = RippleShininess;
	material.TextureEnable = FALSE;
	m_RippleMaterial->SetMaterial(material);
	m_RippleMaterial->SetGPU();

	Matrix world = Matrix::CreateScale(radius, 1.0f, radius) *
		Matrix::CreateTranslation(position);
	Renderer::SetWorldMatrix(&world);
	context->Draw(static_cast<UINT>(m_RippleVertices.size()), 0);
}

// 水滴と波紋を描いている
void WaterEffectSystem::Draw(Camera* camera)
{
	DrawFallingDrops(camera);
	DrawWaterRipples(camera);
}
