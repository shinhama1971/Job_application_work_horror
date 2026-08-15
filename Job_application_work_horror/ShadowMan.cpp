#include "Game.h"
#include "ShadowMan.h"
#include "Player.h"
#include "input.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

void ShadowMan::Init()
{
	m_Age = 0.0f;
	m_ObservedAmount = 0.0f;
	m_ReactedToGaze = false;
	m_GazeScareEnabled = false;
	m_IsActive = true;
	m_DeactivateOnExpire = false;
	m_OnObserved = nullptr;
	m_LifeTimer = 120;

    m_Vertices.reserve(144);
    m_Indices.reserve(432);

    const Color shadowColor(0.025f, 0.03f, 0.027f, 1.0f);

    const auto addFace = [this, &shadowColor](
        const std::array<Vector3, 4>& positions,
        const Vector3& normal)
    {
        const unsigned int base = static_cast<unsigned int>(m_Vertices.size());
        const std::array<Vector2, 4> uvs =
        {
            Vector2(0.0f, 1.0f), Vector2(0.0f, 0.0f),
            Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f)
        };

        for (size_t index = 0; index < positions.size(); ++index)
        {
            VERTEX_3D vertex{};
            vertex.position = positions[index];
            vertex.normal = normal;
            vertex.color = shadowColor;
            vertex.uv = uvs[index];
            m_Vertices.push_back(vertex);
        }

        const unsigned int indices[] =
        {
            base + 0, base + 1, base + 2,
            base + 2, base + 1, base + 3,
            base + 2, base + 1, base + 0,
            base + 3, base + 1, base + 2
        };
        m_Indices.insert(m_Indices.end(), indices, indices + 12);
    };

    const auto addBox = [&addFace](const Vector3& center, const Vector3& half)
    {
        const float left = center.x - half.x;
        const float right = center.x + half.x;
        const float bottom = center.y - half.y;
        const float top = center.y + half.y;
        const float back = center.z - half.z;
        const float front = center.z + half.z;

        addFace({ Vector3(left, bottom, front), Vector3(left, top, front),
                  Vector3(right, bottom, front), Vector3(right, top, front) },
                Vector3(0.0f, 0.0f, 1.0f));
        addFace({ Vector3(right, bottom, back), Vector3(right, top, back),
                  Vector3(left, bottom, back), Vector3(left, top, back) },
                Vector3(0.0f, 0.0f, -1.0f));
        addFace({ Vector3(right, bottom, front), Vector3(right, top, front),
                  Vector3(right, bottom, back), Vector3(right, top, back) },
                Vector3(1.0f, 0.0f, 0.0f));
        addFace({ Vector3(left, bottom, back), Vector3(left, top, back),
                  Vector3(left, bottom, front), Vector3(left, top, front) },
                Vector3(-1.0f, 0.0f, 0.0f));
        addFace({ Vector3(left, top, front), Vector3(left, top, back),
                  Vector3(right, top, front), Vector3(right, top, back) },
                Vector3(0.0f, 1.0f, 0.0f));
        addFace({ Vector3(left, bottom, back), Vector3(left, bottom, front),
                  Vector3(right, bottom, back), Vector3(right, bottom, front) },
                Vector3(0.0f, -1.0f, 0.0f));
    };

    addBox(Vector3(-0.18f, 0.45f, 0.0f), Vector3(0.12f, 0.45f, 0.12f));
    addBox(Vector3(0.18f, 0.45f, 0.0f), Vector3(0.12f, 0.45f, 0.12f));
    addBox(Vector3(0.0f, 1.25f, 0.0f), Vector3(0.36f, 0.42f, 0.16f));
    addBox(Vector3(-0.48f, 1.24f, 0.0f), Vector3(0.10f, 0.45f, 0.10f));
    addBox(Vector3(0.48f, 1.24f, 0.0f), Vector3(0.10f, 0.45f, 0.10f));
    addBox(Vector3(0.0f, 1.86f, 0.0f), Vector3(0.22f, 0.22f, 0.20f));

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create(
        "shader/litTextureVS.hlsl",
        "shader/shadowDissolvePS.hlsl");

    Renderer::CreateConstantBuffer(
        sizeof(DissolveBuffer),
        m_DissolveBuffer.ReleaseAndGetAddressOf());

    MATERIAL material{};
    material.Diffuse = Color(0.70f, 0.74f, 0.70f, 1.0f);
    material.Specular = Color(0.0f, 0.0f, 0.0f, 1.0f);
    material.Shininess = 1.0f;
    material.TextureEnable = FALSE;
    m_Material = std::make_unique<Material>();
    m_Material->Create(material);

    m_Scale = Vector3(8.0f, 16.0f, 8.0f);
}

void ShadowMan::Update()
{
    if (!m_IsActive)
    {
        return;
    }

    m_Age += 1.0f / 60.0f;
    --m_LifeTimer;
    if (m_LifeTimer <= 0)
    {
        if (m_DeactivateOnExpire)
        {
            m_IsActive = false;
        }
        else
        {
            Destroy();
        }
        return;
    }

    Core::Game* game = Core::Game::GetInstance();
    Player* player = game->GetObj<Player>("Player");
    if (player == nullptr)
    {
        return;
    }

    const Vector3 toPlayer = player->GetPosition() - m_Position;
    if (toPlayer.LengthSquared() > 0.0001f)
    {
        m_Rotation.y = std::atan2(toPlayer.x, toPlayer.z);
    }

	const Vector3 shadowCenter = m_Position + Vector3(0.0f, 17.0f, 0.0f);
	Vector3 cameraToShadow = shadowCenter - game->GetCamera()->GetPosition();
	const float distanceToShadow = cameraToShadow.Length();
	if (distanceToShadow > 0.001f)
	{
		cameraToShadow /= distanceToShadow;
	}

	const float gazeAlignment =
		game->GetCamera()->GetForward().Dot(cameraToShadow);
	const bool illuminatedByGaze =
		m_Age > 0.18f &&
		player->IsFlashlightOn() &&
		distanceToShadow < 220.0f &&
		gazeAlignment > 0.955f;

	if (illuminatedByGaze)
	{
		m_ObservedAmount += 0.060f;
		m_LifeTimer -= 2;

		if (!m_ReactedToGaze)
		{
			m_ReactedToGaze = true;
			const float pulseStrength = m_GazeScareEnabled ? 0.62f : 0.20f;
			const float pulseDuration = m_GazeScareEnabled ? 0.48f : 0.28f;
			game->GetPostProcess()->TriggerHorrorPulse(
				pulseStrength,
				pulseDuration);

			if (m_GazeScareEnabled)
			{
				Input::SetVibration(12, 0.32f);
			}

			if (m_OnObserved)
			{
				m_OnObserved();
			}
		}
	}
	else
	{
		m_ObservedAmount -= 0.018f;
	}

	m_ObservedAmount = (std::clamp)(m_ObservedAmount, 0.0f, 1.0f);
	if (m_ObservedAmount > 0.82f)
	{
		m_LifeTimer = (std::min)(m_LifeTimer, 24);
	}
}

void ShadowMan::Draw(Camera* camera)
{
    if (!m_IsActive || m_LifeTimer <= 0)
    {
        return;
    }

    camera->SetCamera();

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    const Matrix scale = Matrix::CreateScale(m_Scale);
    const Matrix translation = Matrix::CreateTranslation(m_Position);
    Matrix world = scale * rotation * translation;
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();

    const float appear = (std::min)(m_Age / 0.35f, 1.0f);
    const float disappear = (std::min)(
        static_cast<float>(m_LifeTimer) / 30.0f,
        1.0f);

    DissolveBuffer dissolve{};
    dissolve.Time = m_Age;
	const float gazeDissolve = 1.0f - m_ObservedAmount * 0.88f;
    dissolve.Visibility = (std::min)(
		(std::min)(appear, disappear),
		gazeDissolve);
    dissolve.EdgeWidth = 0.085f;
    context->UpdateSubresource(
        m_DissolveBuffer.Get(),
        0,
        nullptr,
        &dissolve,
        0,
        0);
    ID3D11Buffer* dissolveBuffer = m_DissolveBuffer.Get();
    context->PSSetConstantBuffers(7, 1, &dissolveBuffer);
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

void ShadowMan::Uninit()
{
	m_OnObserved = nullptr;
    m_DissolveBuffer.Reset();
    m_Vertices.clear();
    m_Indices.clear();
}
