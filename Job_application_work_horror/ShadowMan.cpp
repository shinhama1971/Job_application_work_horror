// ============================================================================
// ファイルの役割: 人影の出現時間、プレイヤーを追う動き、見られたときに崩れて消える演出を管理している。
// 主な技術: 簡単な敵の状態管理、プレイヤーへ近づく動き、黒い人影の表現、ディゾルブ（崩れて消える表現）
// ============================================================================

#include "Game.h"
#include "Application.h"
#include "ShadowMan.h"
#include "Player.h"
#include "input.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

// 状態を初期化し、箱を組み合わせて人の形（両足・胴・両腕・頭）を作っている
void ShadowMan::Init()
{
	m_Age = 0.0f;
	m_ObservedAmount = 0.0f;
	m_ReactedToGaze = false;
	m_GazeScareEnabled = false;
	m_ChaseEnabled = false;
	m_IsActive = true;
	m_DeactivateOnExpire = false;
	m_OnObserved = nullptr;
	m_LifeTime = 4.0f;
	m_ChaseSpeed = 0.0f;
	m_ChaseStopDistance = 28.0f;
	m_ChaseStepDistance = 0.0f;

    m_Vertices.reserve(144);
    m_Indices.reserve(432);

    // ほとんど黒に近い色
    const Color shadowColor(0.025f, 0.03f, 0.027f, 1.0f);

    // 四角形の面を1枚追加している。表と裏の両方から見えるよう、三角形を両面分（12個のインデックス）入れている
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

    // 中心と半分の大きさを指定して、6面の箱を追加している
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

    // 両足・胴・両腕・頭
    addBox(Vector3(-0.18f, 0.45f, 0.0f), Vector3(0.12f, 0.45f, 0.12f));
    addBox(Vector3(0.18f, 0.45f, 0.0f), Vector3(0.12f, 0.45f, 0.12f));
    addBox(Vector3(0.0f, 1.25f, 0.0f), Vector3(0.36f, 0.42f, 0.16f));
    addBox(Vector3(-0.48f, 1.24f, 0.0f), Vector3(0.10f, 0.45f, 0.10f));
    addBox(Vector3(0.48f, 1.24f, 0.0f), Vector3(0.10f, 0.45f, 0.10f));
    addBox(Vector3(0.0f, 1.86f, 0.0f), Vector3(0.22f, 0.22f, 0.20f));

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    // 崩れて消える表現のピクセルシェーダーを使っている
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

    // 人の背丈くらいの大きさ（高さ16倍で約30）
    m_Scale = Vector3(8.0f, 16.0f, 8.0f);
}

// 表示時間を減らし、追ってくる・プレイヤーの方を向く・見られたかの判定をしている
void ShadowMan::Update()
{
    if (!m_IsActive)
    {
        return;
    }

    const float deltaTime = Application::GetDeltaTime();
    m_Age += deltaTime;
    m_LifeTime -= deltaTime;
    // 時間切れになったら、非表示にするか破棄している
    if (m_LifeTime <= 0.0f)
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
    if (m_Player == nullptr)
    {
        m_Player = game->GetObj<Player>("Player");
    }
    Player* player = m_Player;
    if (player == nullptr)
    {
        return;
    }

    Vector3 toPlayer = player->GetPosition() - m_Position;
    // 追ってくる設定なら、水平方向にプレイヤーへ近づいている（止まる距離より近づかない）
    if (m_ChaseEnabled)
    {
        Vector3 horizontalDirection(toPlayer.x, 0.0f, toPlayer.z);
        const float horizontalDistance = horizontalDirection.Length();
        if (horizontalDistance > m_ChaseStopDistance &&
            horizontalDistance > 0.001f)
        {
            horizontalDirection /= horizontalDistance;
            const float travel = (std::min)(
                m_ChaseSpeed * deltaTime,
                horizontalDistance - m_ChaseStopDistance);
            m_Position += horizontalDirection * travel;
            toPlayer = player->GetPosition() - m_Position;

            // 一定の歩幅ごとに、この人影の足元から足音を鳴らしている。
            // 立体音響で、見えていなくても背後のどこから近づいてくるかが分かる。
            constexpr float ChaseStrideLength = 22.0f;
            m_ChaseStepDistance += travel;
            if (m_ChaseStepDistance >= ChaseStrideLength)
            {
                m_ChaseStepDistance -= ChaseStrideLength;
                m_ChaseStepLeft = !m_ChaseStepLeft;
                // プレイヤーより低く重い足音にし、左右の足で少しだけ音の高さを変えている。
                game->PlayAudioCueAt(
                    SOUND_CUE_FOOTSTEP,
                    m_Position + Vector3(0.0f, 4.0f, 0.0f),
                    m_ChaseStepLeft ? 0.72f : 0.77f,
                    1.7f);
            }
        }
    }
    // いつもプレイヤーの方を向いている
    if (toPlayer.LengthSquared() > 0.0001f)
    {
        m_Rotation.y = std::atan2(toPlayer.x, toPlayer.z);
    }

	// 見られたか：ライトを点けていて、220より近く、視線の中心（内積0.955超）に影の胸のあたりがあるとき
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

	// 見られている間は崩れていき、表示時間も早く減っている。最初に見られた瞬間に画面と振動で驚かせている
	if (illuminatedByGaze)
	{
		m_ObservedAmount += 3.6f * deltaTime;
		m_LifeTime -= 2.0f * deltaTime;

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
	// 目を離すと、崩れ具合が少しずつ戻る
	else
	{
		m_ObservedAmount -= 1.08f * deltaTime;
	}

	m_ObservedAmount = (std::clamp)(m_ObservedAmount, 0.0f, 1.0f);
	// ほとんど崩れたら、0.4秒以内に消している
	if (m_ObservedAmount > 0.82f)
	{
		m_LifeTime = (std::min)(m_LifeTime, 0.4f);
	}
}

// 現れる・消える・見られて崩れる度合いを合わせて、崩れて消える表現で描いている
void ShadowMan::Draw(Camera* camera)
{
    if (!m_IsActive || m_LifeTime <= 0.0f)
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

    // 0.35秒かけて現れ、残り0.5秒で消えていく
    const float appear = (std::min)(m_Age / 0.35f, 1.0f);
    const float disappear = (std::min)(
        m_LifeTime / 0.5f,
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
    // b7は普通のマテリアルのシェーダーでは、デバッグ表示の設定にも使っている。
    // 人影を描いた後に戻さないと、次のフレームの部屋全体が黒くなる場合がある。
    Microsoft::WRL::ComPtr<ID3D11Buffer> previousBuffer;
    context->PSGetConstantBuffers(7, 1, previousBuffer.GetAddressOf());
    context->PSSetConstantBuffers(7, 1, &dissolveBuffer);
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
    ID3D11Buffer* restoredBuffer = previousBuffer.Get();
    context->PSSetConstantBuffers(7, 1, &restoredBuffer);
}

// 見られたときの処理と、定数バッファ・頂点データを解放している
void ShadowMan::Uninit()
{
	m_OnObserved = nullptr;
    m_DissolveBuffer.Reset();
    m_Vertices.clear();
    m_Indices.clear();
}
