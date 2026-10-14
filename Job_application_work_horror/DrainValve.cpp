// ============================================================================
// ファイルの役割: 1面の西棟の排水バルブ（壁の配管に付いたハンドル）の、回した量と見た目を管理している。
// 主な技術: Interactableインターフェース、長押しで進む操作、回転する部品だけ別の行列で描く描画
// ============================================================================

#include "DrainValve.h"

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

// 台座と配管（動かない部品）を先に、ハンドル（回る部品）を後に頂点で組み立てている。
// 座標の単位は世界と同じで、壁の表面がz=0、部屋の側が-zになっている。
void DrainValve::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();

    // 四角形の面を1枚追加している。表と裏の両方から見えるよう、三角形を両面分（12個のインデックス）入れている
    const auto addFace = [this](
        const std::array<Vector3, 4>& positions,
        const Vector3& normal,
        const Color& color)
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
            vertex.color = color;
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

    // 中心・半分の大きさ・z軸まわりの角度を指定して、6面の箱を追加している（ハンドルの輪を斜めの箱で作るため）
    const auto addBox = [&addFace](
        const Vector3& center,
        const Vector3& half,
        float angleZ,
        const Color& color)
    {
        const Matrix rotation = Matrix::CreateRotationZ(angleZ);
        // 箱の角を、符号(-1か1)の組で指定して求めている
        const auto corner = [&](float signX, float signY, float signZ)
        {
            return Vector3::Transform(
                Vector3(half.x * signX, half.y * signY, half.z * signZ), rotation) + center;
        };
        const auto normal = [&](const Vector3& direction)
        {
            return Vector3::TransformNormal(direction, rotation);
        };

        addFace({ corner(-1, -1, 1), corner(-1, 1, 1), corner(1, -1, 1), corner(1, 1, 1) },
                normal(Vector3(0.0f, 0.0f, 1.0f)), color);
        addFace({ corner(1, -1, -1), corner(1, 1, -1), corner(-1, -1, -1), corner(-1, 1, -1) },
                normal(Vector3(0.0f, 0.0f, -1.0f)), color);
        addFace({ corner(1, -1, 1), corner(1, 1, 1), corner(1, -1, -1), corner(1, 1, -1) },
                normal(Vector3(1.0f, 0.0f, 0.0f)), color);
        addFace({ corner(-1, -1, -1), corner(-1, 1, -1), corner(-1, -1, 1), corner(-1, 1, 1) },
                normal(Vector3(-1.0f, 0.0f, 0.0f)), color);
        addFace({ corner(-1, 1, 1), corner(-1, 1, -1), corner(1, 1, 1), corner(1, 1, -1) },
                normal(Vector3(0.0f, 1.0f, 0.0f)), color);
        addFace({ corner(-1, -1, -1), corner(-1, -1, 1), corner(1, -1, -1), corner(1, -1, 1) },
                normal(Vector3(0.0f, -1.0f, 0.0f)), color);
    };

    const Color darkMetal(0.10f, 0.115f, 0.11f, 1.0f);
    const Color pipeMetal(0.16f, 0.18f, 0.17f, 1.0f);
    const Color wheelPaint(0.42f, 0.07f, 0.05f, 1.0f);

    // 動かない部品：壁の台座、手前へ出る短い配管、床から天井まで壁を伝う配管
    addBox(Vector3(0.0f, 0.0f, -0.4f), Vector3(4.5f, 4.5f, 0.4f), 0.0f, darkMetal);
    addBox(Vector3(0.0f, 0.0f, -2.8f), Vector3(1.4f, 1.4f, 2.0f), 0.0f, pipeMetal);
    addBox(Vector3(0.0f, 19.0f, -1.6f), Vector3(1.2f, 19.0f, 1.2f), 0.0f, pipeMetal);
    addBox(Vector3(0.0f, -6.0f, -1.6f), Vector3(1.2f, 6.0f, 1.2f), 0.0f, pipeMetal);
    m_FixedIndexCount = static_cast<unsigned int>(m_Indices.size());

    // 回る部品：輪（10本の短い箱）、十字の握り、中心の軸、輪に付いた取っ手
    constexpr int RimSegments = 10;
    constexpr float RimRadius = 4.2f;
    for (int segment = 0; segment < RimSegments; ++segment)
    {
        const float angle = DirectX::XM_2PI * static_cast<float>(segment) / RimSegments;
        addBox(Vector3(std::cos(angle) * RimRadius, std::sin(angle) * RimRadius, 0.0f),
            Vector3(0.42f, 1.45f, 0.42f), angle, wheelPaint);
    }
    addBox(Vector3::Zero, Vector3(RimRadius, 0.32f, 0.28f), DirectX::XM_PIDIV4, wheelPaint);
    addBox(Vector3::Zero, Vector3(RimRadius, 0.32f, 0.28f), -DirectX::XM_PIDIV4, wheelPaint);
    addBox(Vector3::Zero, Vector3(0.9f, 0.9f, 0.6f), 0.0f, darkMetal);
    addBox(Vector3(0.0f, RimRadius, -0.9f), Vector3(0.35f, 0.35f, 0.9f), 0.0f, darkMetal);
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
