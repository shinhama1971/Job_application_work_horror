// ============================================================================
// ファイルの役割: 中に隠れられるロッカーの扉（見た目と「隠れる」操作）を管理している。
// 主な技術: Interactableインターフェース、Mayaで作ったモデル（PropModel）、使えるかどうかの切り替え
// ============================================================================

#include "Locker.h"
#include "Game.h"
#include "Player.h"
#include "PropModel.h"

#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 隠れる位置は扉のすぐ前にしている（扉は隠れている間は描かないので、視界をさえぎらない）。
    // ロッカーの本体（Wall）の中に視点を入れると、本体の面に視界をふさがれるためである。
    constexpr float HideOffset = 2.5f;
    // 出たときに立つ位置（扉の正面から12離れた所）
    constexpr float ExitOffset = 12.0f;
}

// 扉の板・枠・取っ手と、目の高さの通気口の羽根を、Mayaで作ったモデル（tools/maya/build_props.py）から読み込んでいる。
// ローカル座標では扉の面は x=0、正面は +X 向きにしている（幅15・高さ44）。
void Locker::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();

    // 扉の板・枠・すき間・取っ手の色（くすんだ緑がかった灰色の金属）
    PropModel::Append("assets/model/props/locker_door.fbx",
        {
            { "Panel", Color(0.24f, 0.28f, 0.26f, 1.0f) },
            { "Frame", Color(0.13f, 0.15f, 0.14f, 1.0f) },
            { "Slit", Color(0.015f, 0.018f, 0.016f, 1.0f) },
            { "Handle", Color(0.42f, 0.42f, 0.38f, 1.0f) },
        },
        Vector3::One, m_Vertices, m_Indices);
}

void Locker::Init()
{
    BuildGeometry();
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // 色は頂点の色で付けるため、マテリアルは白にしている（シェーダーは頂点の色×マテリアルの色で塗っている）。
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.Ambient = Color(0.3f, 0.3f, 0.3f, 1.0f);
    material.Specular = Color(0.30f, 0.32f, 0.30f, 1.0f);
    material.Shininess = 24.0f;
    material.TextureEnable = FALSE;
    // PBR：塗装の剥げかけた鋼板のロッカー（叩かれてへこんだ金属の鈍い光沢）
    material.Metallic = 0.50f;
    material.Roughness = 0.45f;
    m_Material = std::make_unique<Material>();
    m_Material->Create(material);
}

// 扉の位置と向きを決め、隠れる位置と出る位置を扉の前に決めている。
// メッシュは+X向きに作ってあるので、ヨーからπ/2引いて扉の正面をfacingの向きに合わせている
void Locker::Place(const Vector3& doorCenter, float facing)
{
    m_Position = doorCenter;
    m_Facing = facing;
    m_Rotation = Vector3(0.0f, facing - DirectX::XM_PIDIV2, 0.0f);

    // 扉の正面の向き。ヨーの向きはPlayerのforward（sin, cos）と同じ決め方にしている。
    const Vector3 front(std::sin(facing), 0.0f, std::cos(facing));
    m_HidePosition = Vector3(doorCenter.x, -99.0f, doorCenter.z) + front * HideOffset;
    m_ExitPosition = Vector3(doorCenter.x, -99.0f, doorCenter.z) + front * ExitOffset;
}

// 調べるときの位置は、扉の面から少し前にしている
Vector3 Locker::GetInteractionPosition() const
{
    const Vector3 front(std::sin(m_Facing), 0.0f, std::cos(m_Facing));
    return m_Position + front * 0.8f;
}

// 調べたらプレイヤーを中に隠れさせ、扉の音（少し高く小さい音）を鳴らしている
void Locker::Interact(Player& player)
{
    if (!IsInteractionEnabled())
    {
        return;
    }
    m_Occupant = &player;
    player.EnterHiding(m_HidePosition, m_ExitPosition, m_Facing);
    Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_DOOR, m_Position, 1.35f, 0.55f);
}

void Locker::Update()
{
    // プレイヤーが自分で外へ出たら、空いた状態に戻している。
    if (m_Occupant != nullptr && !m_Occupant->IsHiding())
    {
        m_Occupant = nullptr;
    }
}

void Locker::Draw(Camera* cam)
{
    // 中に隠れている間は扉を描いていない（視点が扉のすぐ前にあり、扉に視界をふさがれるため）。
    if (m_Occupant != nullptr)
    {
        return;
    }

    cam->SetCamera();
    Matrix world = Matrix::CreateFromYawPitchRoll(m_Rotation.y, 0.0f, 0.0f) *
        Matrix::CreateTranslation(m_Position);
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

// 頂点データとマテリアルを解放している
void Locker::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Material.reset();
}
