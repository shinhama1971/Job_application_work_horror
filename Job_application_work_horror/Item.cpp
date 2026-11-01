// ============================================================================
// ファイルの役割: ヒューズ（1面で集めるアイテム）の形、浮かぶ動き、拾ったときの処理を管理している。
// 主な技術: Mayaで作ったモデル（PropModel）、ゆっくり脈打つ大きさ、調べる操作、時間で回る・浮く動き
// ============================================================================

#include "Item.h"
#include "Application.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "PropModel.h"

#include <cmath>

using namespace DirectX::SimpleMath;

// ヒューズの形（緑がかったガラスの筒・上下の金具・手前に透けて見えるオレンジのフィラメント）を、Mayaで作ったモデルから読み込んでいる。
// モデルは高さ約7の実際の大きさなので、拡大率5で割っている
void Item::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();
    PropModel::Append("assets/model/props/fuse.fbx",
        {
            { "Glass", Color(0.34f, 0.46f, 0.38f, 1.0f) },
            { "Metal", Color(0.62f, 0.58f, 0.42f, 1.0f) },
            { "Filament", Color(1.0f, 0.48f, 0.10f, 1.0f) },
        },
        Vector3(5.0f, 5.0f, 5.0f), m_Vertices, m_Indices);
}

// 形とバッファ、シェーダー、マテリアルを作っている。わずかに自ら光らせ、暗い部屋でも見つけやすくしている
void Item::Init()
{
    m_Vertices.reserve(96);
    m_Indices.reserve(288);
    BuildGeometry();

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.Specular = Color(0.55f, 0.48f, 0.30f, 1.0f);
    material.Emission = Color(0.065f, 0.035f, 0.008f, 1.0f);
    material.Shininess = 32.0f;
    material.TextureEnable = FALSE;
    // PBR：ヒューズの金属の口金とガラス（半分ほど金属。懐中電灯で照らすと芯が光る）
    material.Metallic = 0.50f;
    material.Roughness = 0.30f;
    m_Material->Create(material);

    m_Scale = Vector3(5.0f, 5.0f, 5.0f);
    m_BaseY = m_Position.y;
    m_AnimationTime = 0.0f;
}

void Item::Update()
{
    if (!m_IsActive || m_IsCollected) return;

    // ゆっくり回りながら浮き沈みさせ、暗い部屋でも小さなヒューズを見つけやすくしている。
    const float deltaTime = Application::GetDeltaTime();
    m_AnimationTime += deltaTime;
    m_Rotation.y += 1.5f * deltaTime;
    m_Position.y = m_BaseY + std::sin(m_AnimationTime * 2.6f) * 0.75f;
}

// 拾ったときの処理：数を増やし、音・画面の光・振動で知らせている
void Item::Interact(Player& player)
{
    if (!m_IsActive || m_IsCollected)
    {
        return;
    }

    m_IsCollected = true;
    Core::Game::GetInstance()->AddItemCount();
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_PICKUP);
    Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(
        0.72f, 0.30f);
    Input::SetVibration(6, 0.14f);

    // 1本目を拾った瞬間、プレイヤーから-Z方向に80離れた位置に影を出している（最初の驚かせる演出）
    if (Core::Game::GetInstance()->GetItemCount() == 1)
    {
        const Vector3 shadowPosition(
            player.GetPosition().x,
            player.GetPosition().y,
            player.GetPosition().z - 80.0f
        );

        Core::Game::GetInstance()->RequestAddObject<ShadowMan>(
            [shadowPosition](ShadowMan& shadow)
            {
                shadow.SetPosition(
                    shadowPosition.x,
                    shadowPosition.y,
                    shadowPosition.z
                );
            }
        );
    }
}

// 回転と浮き沈みに加えて、大きさを少し脈打たせて描いている
void Item::Draw(Camera* cam)
{
    if (!m_IsActive || m_IsCollected) return;

    cam->SetCamera();

    Matrix r = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z
    );

    Matrix t = Matrix::CreateTranslation(m_Position);

    const float pulse =
        1.0f + (std::sin(m_AnimationTime * 3.2f) * 0.5f + 0.5f) * 0.08f;
    Matrix s = Matrix::CreateScale(
        m_Scale.x * pulse,
        m_Scale.y * pulse,
        m_Scale.z * pulse
    );

    Matrix worldmtx = s * r * t;

    Renderer::SetWorldMatrix(&worldmtx);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

// 頂点データとマテリアルを解放している
void Item::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Material.reset();
}
