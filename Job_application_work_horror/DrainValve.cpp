// ============================================================================
// ファイルの役割: 1面の西棟の排水バルブ（壁の配管に付いたハンドル）の、回した量と見た目を管理している。
// 主な技術: Interactableインターフェース、長押しで進む操作、回転する部品だけ別の行列で描く描画
// ============================================================================

#include "DrainValve.h"
#include "PropModel.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // ハンドルの中心の、壁の表面からの距離と、回る速さ（ラジアン/秒）
    constexpr float WheelOffset = 5.2f;
    constexpr float WheelTurnSpeed = 2.6f;
}

// 台座と配管（動かない部品）を先に、ハンドル（回る部品）を後に、Mayaで作ったモデルから読み込んでいる。
// 座標の単位は世界と同じで、壁の表面がz=0、部屋の側が-zになっている。ハンドルは原点を中心に作ってあり、描くときにずらして回している。
void DrainValve::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();

    const Color darkMetal(0.10f, 0.115f, 0.11f, 1.0f);
    const Color pipeMetal(0.16f, 0.18f, 0.17f, 1.0f);
    const Color wheelPaint(0.42f, 0.07f, 0.05f, 1.0f);
    const std::vector<PropModel::IndexRange> ranges = PropModel::Append(
        "assets/model/props/drain_valve.fbx",
        {
            // 動かない部品：壁の台座、継ぎ目のフランジ、手前へ出る短い配管、床から天井まで壁を伝う配管
            { "Base", darkMetal },
            { "Pipe", pipeMetal },
            // 回る部品：輪・4本のスポーク、中心の軸、輪に付いた取っ手
            { "Wheel", wheelPaint },
            { "Hub", darkMetal },
        },
        Vector3::One, m_Vertices, m_Indices);
    m_FixedIndexCount = ranges[2].Start;
}

Matrix DrainValve::MakeWorldMatrix() const
{
    return Matrix::CreateScale(m_Scale) *
        Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z) *
        Matrix::CreateTranslation(m_Position);
}

// 形とバッファ、シェーダー、マテリアルを作っている。色は頂点の色を使っている
void DrainValve::Init()
{
    BuildGeometry();
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.Specular = Color(0.16f, 0.15f, 0.13f, 1.0f);
    material.Shininess = 20.0f;
    material.TextureEnable = FALSE;
    // PBR：赤く塗ったバルブの取っ手と、塗装した配管（塗料なので金属ではなく、艶のある粗さにしている）
    material.Metallic = 0.0f;
    material.Roughness = 0.42f;
    m_Material->Create(material);

    // 画面の外かの判定に使う境界（床から天井まで伸びる配管を含めている）
    ModelBounds bounds;
    bounds.Center = Vector3(0.0f, 13.0f, -3.0f);
    bounds.Extents = Vector3(4.7f, 25.0f, 3.5f);
    bounds.SphereRadius = bounds.Extents.Length();
    bounds.IsValid = true;
    SetModelBounds(bounds);
}

// 毎フレームの処理はない（回すのはStage1WestWingがTurnを呼んだときだけ）
void DrainValve::Update()
{
}

// 調べる位置はハンドルの中心にしている
Vector3 DrainValve::GetInteractionPosition() const
{
    return Vector3::Transform(Vector3(0.0f, 0.0f, -WheelOffset), MakeWorldMatrix());
}

bool DrainValve::Turn(float deltaTime)
{
    if (IsOpened())
    {
        return false;
    }
    m_WheelAngle += deltaTime * WheelTurnSpeed;
    m_Progress = (std::min)(1.0f, m_Progress + deltaTime / TurnSeconds);
    return IsOpened();
}

// 台座と配管を描き、ハンドルだけは回した角度の行列で描いている
void DrainValve::Draw(Camera* camera)
{
    camera->SetCamera();

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();

    Matrix world = MakeWorldMatrix();
    Renderer::SetWorldMatrix(&world);
    context->DrawIndexed(m_FixedIndexCount, 0, 0);

    Matrix wheelWorld = Matrix::CreateRotationZ(m_WheelAngle) *
        Matrix::CreateTranslation(0.0f, 0.0f, -WheelOffset) * world;
    Renderer::SetWorldMatrix(&wheelWorld);
    context->DrawIndexed(
        static_cast<UINT>(m_Indices.size()) - m_FixedIndexCount, m_FixedIndexCount, 0);
}

// 頂点データを解放している
void DrainValve::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}
