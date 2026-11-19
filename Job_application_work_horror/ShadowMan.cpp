// ============================================================================
// ファイルの役割: 人影の出現時間、プレイヤーを追う動き、見られたときに崩れて消える演出を管理している。
// 主な技術: 簡単な敵の状態管理、プレイヤーへ近づく動き、黒い人影の表現、ディゾルブ（崩れて消える表現）
// ============================================================================

#include "Game.h"
#include "PostProcess.h"
#include "Application.h"
#include "ShadowMan.h"
#include "Player.h"
#include "input.h"
#include "PropModel.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
	// 人影の大きさ（高さ16倍で、人の背丈くらいの約30）。モデルはこの大きさを1にした空間で読み込む
	const Vector3 ShadowSize(8.0f, 16.0f, 8.0f);
	// 最初に表示する秒数と、追うときに止まる距離（EnableChase で変えられる）
	constexpr float DefaultLifeSeconds = 4.0f;
	constexpr float DefaultChaseStopDistance = 28.0f;
	// 足音を鳴らす高さと、左右の足の音の高さ、音量
	constexpr float FootstepHeight = 4.0f;
	constexpr float FootstepPitchLeft = 0.72f;
	constexpr float FootstepPitchRight = 0.77f;
	constexpr float FootstepVolume = 1.7f;
	// 見られたか：現れてからこの秒数が過ぎ、ライトを点けていて、この距離より近く、視線の中心（向きとの内積がこれ以上）に
	// 影の胸（足もとからこの高さ）があるとき
	constexpr float GazeIgnoreSeconds = 0.18f;
	constexpr float GazeDistance = 220.0f;
	constexpr float GazeFacing = 0.955f;
	constexpr float ChestHeight = 17.0f;
	// 見られている間に崩れる速さと、表示時間が減る速さ。目を離したときに戻る速さ
	constexpr float ObservedRate = 3.6f;
	constexpr float ObservedLifeDrain = 2.0f;
	constexpr float ObservedRecoverRate = 1.08f;
	// ほとんど崩れたら（この割合を超えたら）、この秒数以内に消す
	constexpr float VanishObservedAmount = 0.82f;
	constexpr float VanishSeconds = 0.4f;
	// 現れるのにかける秒数、消えるのにかける秒数、見られて崩れる割合の最大、崩れる縁の幅
	constexpr float AppearSeconds = 0.35f;
	constexpr float DisappearSeconds = 0.5f;
	constexpr float GazeDissolveMax = 0.88f;
	constexpr float DissolveEdgeWidth = 0.085f;
	// 影のマテリアルの色（暗い灰色の体は、シェーダーで溶けるように描く）
	const Color ShadowDiffuse(0.70f, 0.74f, 0.70f, 1.0f);
	// 向きや距離を計算しないほど近いとみなす長さ（2乗・そのまま）
	constexpr float MinimumLengthSquared = 0.0001f;
	constexpr float MinimumDistance = 0.001f;
	// 見られて驚かせるときの振動
	constexpr int GazeScareRumbleFrames = 12;
	constexpr float GazeScareRumbleStrength = 0.32f;
	// 崩れる度合いを渡すシェーダーの番号（shadowDissolvePS.hlsl の b7。デバッグ表示の定数と同じ番号なので、描いた後に戻す）
	constexpr UINT DissolveBufferSlot = 7;
}

// 状態を初期化し、Mayaで作った人の形のモデルを読み込んでいる
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
	m_LifeTime = DefaultLifeSeconds;
	m_ChaseSpeed = 0.0f;
	m_ChaseStopDistance = DefaultChaseStopDistance;
	m_ChaseStepDistance = 0.0f;

    // 細長い手足と、前へ傾いた頭の人の形（tools/maya/build_props.py）。ほとんど黒に近い色にしている。
    // モデルは身長約31の実際の大きさなので、ワールド行列で掛ける拡大率（下の m_Scale）で割って読み込んでいる
    PropModel::Append("assets/model/props/shadow_man.fbx",
        { { "Body", Color(0.025f, 0.03f, 0.027f, 1.0f) } },
        ShadowSize, m_Vertices, m_Indices);

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    // 崩れて消える表現のピクセルシェーダーを使っている
    m_Shader.CreateRequired(
        "shader/litTextureVS.hlsl",
        "shader/shadowDissolvePS.hlsl");

    Renderer::CreateRequiredConstantBuffer(
        sizeof(DissolveBuffer),
        m_DissolveBuffer.ReleaseAndGetAddressOf(), "影の溶解演出");

    MATERIAL material{};
    material.Diffuse = ShadowDiffuse;
    material.Specular = Color(0.0f, 0.0f, 0.0f, 1.0f);
    material.Shininess = 1.0f;
    material.TextureEnable = FALSE;
    m_Material = std::make_unique<Material>();
    m_Material->CreateRequired(material);

    // 人の背丈くらいの大きさ（高さ16倍で約30）
    m_Scale = ShadowSize;
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

    // 位置を外から決めている影は、時間切れ・追う動き・見られて崩れる反応をせず、プレイヤーの方を向くだけにしている
    if (m_ExternallyControlled)
    {
        if (m_Player == nullptr)
        {
            m_Player = Core::Game::GetInstance()->GetObj<Player>("Player");
        }
        if (m_Player != nullptr)
        {
            const Vector3 toPlayer = m_Player->GetPosition() - m_Position;
            if (toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z > MinimumLengthSquared)
            {
                m_Rotation.y = std::atan2(toPlayer.x, toPlayer.z);
            }
        }
        return;
    }

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
            horizontalDistance > MinimumDistance)
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
                    m_Position + Vector3(0.0f, FootstepHeight, 0.0f),
                    m_ChaseStepLeft ? FootstepPitchLeft : FootstepPitchRight,
                    FootstepVolume);
            }
        }
    }
    // いつもプレイヤーの方を向いている
    if (toPlayer.LengthSquared() > MinimumLengthSquared)
    {
        m_Rotation.y = std::atan2(toPlayer.x, toPlayer.z);
    }

	// 見られたか：ライトを点けていて、220より近く、視線の中心（内積0.955超）に影の胸のあたりがあるとき
	const Vector3 shadowCenter = m_Position + Vector3(0.0f, ChestHeight, 0.0f);
	Vector3 cameraToShadow = shadowCenter - game->GetCamera()->GetPosition();
	const float distanceToShadow = cameraToShadow.Length();
	if (distanceToShadow > MinimumDistance)
	{
		cameraToShadow /= distanceToShadow;
	}

	const float gazeAlignment =
		game->GetCamera()->GetForward().Dot(cameraToShadow);
	const bool illuminatedByGaze =
		m_Age > GazeIgnoreSeconds &&
		player->IsFlashlightOn() &&
		distanceToShadow < GazeDistance &&
		gazeAlignment > GazeFacing;

	// 見られている間は崩れていき、表示時間も早く減っている。最初に見られた瞬間に画面と振動で驚かせている
	if (illuminatedByGaze)
	{
		m_ObservedAmount += ObservedRate * deltaTime;
		m_LifeTime -= ObservedLifeDrain * deltaTime;

		if (!m_ReactedToGaze)
		{
			m_ReactedToGaze = true;
			// 視線で驚かせる演出が有効なら、通常の発見時より画面の衝撃を強く長くする。
			constexpr float GazeScarePulseStrength = 0.62f;
			constexpr float ObservedFadePulseStrength = 0.20f;
			constexpr float GazeScarePulseSeconds = 0.48f;
			constexpr float ObservedFadePulseSeconds = 0.28f;
			const float pulseStrength = m_GazeScareEnabled ? GazeScarePulseStrength : ObservedFadePulseStrength;
			const float pulseDuration = m_GazeScareEnabled ? GazeScarePulseSeconds : ObservedFadePulseSeconds;
			game->GetPostProcess()->TriggerHorrorPulse(
				pulseStrength,
				pulseDuration);

			if (m_GazeScareEnabled)
			{
				Input::SetVibration(GazeScareRumbleFrames, GazeScareRumbleStrength);
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
		m_ObservedAmount -= ObservedRecoverRate * deltaTime;
	}

	m_ObservedAmount = (std::clamp)(m_ObservedAmount, 0.0f, 1.0f);
	// ほとんど崩れたら、0.4秒以内に消している
	if (m_ObservedAmount > VanishObservedAmount)
	{
		m_LifeTime = (std::min)(m_LifeTime, VanishSeconds);
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
    const float appear = (std::min)(m_Age / AppearSeconds, 1.0f);
    const float disappear = (std::min)(
        m_LifeTime / DisappearSeconds,
        1.0f);

    DissolveBuffer dissolve{};
    dissolve.Time = m_Age;
	const float gazeDissolve = 1.0f - m_ObservedAmount * GazeDissolveMax;
    dissolve.Visibility = (std::min)(
		(std::min)(appear, disappear),
		gazeDissolve);
    dissolve.EdgeWidth = DissolveEdgeWidth;
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
    context->PSGetConstantBuffers(DissolveBufferSlot, 1, previousBuffer.GetAddressOf());
    context->PSSetConstantBuffers(DissolveBufferSlot, 1, &dissolveBuffer);
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
    ID3D11Buffer* restoredBuffer = previousBuffer.Get();
    context->PSSetConstantBuffers(DissolveBufferSlot, 1, &restoredBuffer);
}

// 見られたときの処理と、定数バッファ・頂点データを解放している
void ShadowMan::Uninit()
{
	m_OnObserved = nullptr;
    m_DissolveBuffer.Reset();
    m_Vertices.clear();
    m_Indices.clear();
}
