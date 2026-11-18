// ============================================================================
// ファイルの役割: 天井から吊るした、揺れる裸電球。説明は HangingBulb.h を参照。
// 主な技術: 減衰する2方向の振り子、すきま風の揺れ、揺さぶり、電球の位置の点光源、コードの円柱と、Mayaで作ったソケットと電球のモデル
// ============================================================================

#include "HangingBulb.h"

#include "Application.h"
#include "Camera.h"
#include "Renderer.h"
#include "PropModel.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 振り子の揺れの速さ（1秒あたりのラジアン。周期は約2.4秒）と、揺れの減り方（1秒あたり）
    constexpr float SwingFrequency = DirectX::XM_2PI / 2.4f;
    constexpr float SwingDamping = 0.28f;
    // 電球の半径、光の届く範囲と強さ、色（暖かい白熱電球）
    constexpr float BulbRadius = 0.9f;
    constexpr float LightRange = 95.0f;
    constexpr float LightIntensity = 1.6f;
    const Color BulbColor(1.0f, 0.68f, 0.38f, 1.0f);
    // コードの円柱の分割数と、太さ（半径）
    constexpr int Segments = 10;
    constexpr float CordRadius = 0.08f;
    // 作る頂点の数の見込み（コードとモデルの分。先に入れ物の大きさを決めておく）
    constexpr std::size_t ReservedVertexCount = 512;
    // モデルの頂点の色（白。色はマテリアルで決める）
    const Color ModelVertexColor(1.0f, 1.0f, 1.0f, 1.0f);
    // コード（黒い布の被覆）と電球のガラスのマテリアル。電球の光る強さは、色にこの倍率を掛ける
    const Color CordDiffuse(0.05f, 0.045f, 0.04f, 1.0f);
    const Color CordAmbient(0.03f, 0.03f, 0.03f, 1.0f);
    const Color CordSpecular(0.08f, 0.08f, 0.08f, 1.0f);
    constexpr float CordShininess = 12.0f;
    const Color BulbGlassDiffuse(0.9f, 0.85f, 0.75f, 1.0f);
    constexpr float BulbEmissionScale = 1.6f;
    // ソケットの長さ（コードの先から電球の上端まで）
    constexpr float SocketLength = 1.1f;

    // 押されたときに揺れる向きを決める乱数（時間と種に掛ける倍率）
    constexpr float KickTimeScale = 3.71f;
    constexpr float KickSeedScale = 5.3f;
    // 1フレームの時間の上限（止まった後に一気に揺れないように）
    constexpr float MaxDeltaTime = 0.05f;
    // すきま風：いつもの強さと、影が近いときに足す強さ。向きを変える2つの揺れ（速さと種の倍率）と、2つ目の揺れの割合
    constexpr float DraftBase = 0.020f;
    constexpr float DraftDisturbance = 0.16f;
    constexpr float DraftRateX = 0.71f;
    constexpr float DraftRateX2 = 2.3f;
    constexpr float DraftSeedScaleX2 = 2.0f;
    constexpr float DraftSecondaryRatio = 0.4f;
    constexpr float DraftRateZ = 0.53f;
    constexpr float DraftSeedScaleZ = 1.7f;
    // 揺れの角度の上限（ラジアン）
    constexpr float MaxSwingAngle = 0.7f;
    // 接触の悪いソケットの明るさの揺れ：2つの速さの sin を掛けた揺れ、強く揺れる判定、
    // 揺れの速さに掛ける倍率と暗くなる上限、弱いときの割合
    constexpr float ContactRateA = 37.0f;
    constexpr float ContactRateB = 13.0f;
    constexpr float ContactThreshold = 0.6f;
    constexpr float SwingDimScale = 0.5f;
    constexpr float MaxSwingDim = 0.35f;
    constexpr float WeakContactRatio = 0.15f;

    // 三角形を1つ足している（表と裏の両方の順番を入れている）
    void AddTriangle(std::vector<VERTEX_3D>& vertices, std::vector<unsigned int>& indices,
        const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& na, const Vector3& nb, const Vector3& nc)
    {
        const unsigned int base = static_cast<unsigned int>(vertices.size());
        const Vector3 positions[] = { a, b, c };
        const Vector3 normals[] = { na, nb, nc };
        for (int index = 0; index < 3; ++index)
        {
            VERTEX_3D vertex{};
            vertex.position = positions[index];
            vertex.normal = normals[index];
            vertex.color = ModelVertexColor;
            vertices.push_back(vertex);
        }
        const unsigned int faces[] = { base, base + 1, base + 2, base + 2, base + 1, base };
        indices.insert(indices.end(), faces, faces + 6);
    }

    // y軸に沿った円柱（上の高さtop、下の高さbottom、半径radius）を足している
    void AddVerticalCylinder(std::vector<VERTEX_3D>& vertices, std::vector<unsigned int>& indices,
        float top, float bottom, float radius)
    {
        for (int segment = 0; segment < Segments; ++segment)
        {
            const float angle0 = DirectX::XM_2PI * static_cast<float>(segment) / Segments;
            const float angle1 = DirectX::XM_2PI * static_cast<float>(segment + 1) / Segments;
            const Vector3 n0(std::cos(angle0), 0.0f, std::sin(angle0));
            const Vector3 n1(std::cos(angle1), 0.0f, std::sin(angle1));
            const Vector3 t0 = n0 * radius + Vector3(0.0f, top, 0.0f);
            const Vector3 t1 = n1 * radius + Vector3(0.0f, top, 0.0f);
            const Vector3 b0 = n0 * radius + Vector3(0.0f, bottom, 0.0f);
            const Vector3 b1 = n1 * radius + Vector3(0.0f, bottom, 0.0f);
            AddTriangle(vertices, indices, t0, b0, t1, n0, n0, n1);
            AddTriangle(vertices, indices, t1, b0, b1, n1, n0, n1);
            AddTriangle(vertices, indices, Vector3(0.0f, bottom, 0.0f), b1, b0,
                Vector3(0.0f, -1.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f));
        }
    }
}

void HangingBulb::Init()
{
    m_Vertices.clear();
    m_Indices.clear();
    // 付け根（原点）から下へ、コード・ソケットの順に作り、その後ろに電球を作っている（電球だけ光らせるため）
    m_Vertices.reserve(ReservedVertexCount);
    AddVerticalCylinder(m_Vertices, m_Indices, 0.0f, -m_CordLength, CordRadius);
    // ソケットと電球は、Mayaで作ったモデル（上端が原点。tools/maya/build_props.py）を、コードの長さだけ下へずらしている
    const size_t modelStart = m_Vertices.size();
    const std::vector<PropModel::IndexRange> ranges = PropModel::Append(
        "assets/model/props/hanging_bulb.fbx",
        { { "Socket", ModelVertexColor }, { "Bulb", ModelVertexColor } },
        Vector3::One, m_Vertices, m_Indices);
    for (size_t index = modelStart; index < m_Vertices.size(); ++index)
    {
        m_Vertices[index].position.y -= m_CordLength;
    }
    m_CordIndexCount = ranges[1].Start;

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    MATERIAL cord{};
    cord.Diffuse = CordDiffuse;
    cord.Ambient = CordAmbient;
    cord.Specular = CordSpecular;
    cord.Shininess = CordShininess;
    cord.TextureEnable = FALSE;
    m_CordMaterial = std::make_unique<Material>();
    m_CordMaterial->CreateRequired(cord);

    MATERIAL bulb = cord;
    bulb.Diffuse = BulbGlassDiffuse;
    bulb.Emission = BulbColor * BulbEmissionScale;
    m_BulbMaterial = std::make_unique<Material>();
    m_BulbMaterial->CreateRequired(bulb);
}

void HangingBulb::Place(const Vector3& anchor, float seed)
{
    m_Position = anchor;
    m_Seed = seed;
}

void HangingBulb::Kick(float strength)
{
    // 向きは、種と時間から決めた乱数（毎回違う向きに揺れる）
    const float angle = std::fmod(m_Time * KickTimeScale + m_Seed * KickSeedScale, DirectX::XM_2PI);
    m_VelocityX += std::cos(angle) * strength;
    m_VelocityZ += std::sin(angle) * strength;
}

void HangingBulb::Update()
{
    const float deltaTime = (std::min)(Application::GetDeltaTime(), MaxDeltaTime);
    m_Time += deltaTime;

    // すきま風：ゆっくり向きの変わる小さな力。影が近いほど強くなる
    const float draft = DraftBase + m_Disturbance * DraftDisturbance;
    const float forceX = std::sin(m_Time * DraftRateX + m_Seed) * draft +
        std::sin(m_Time * DraftRateX2 + m_Seed * DraftSeedScaleX2) * draft * DraftSecondaryRatio;
    const float forceZ = std::cos(m_Time * DraftRateZ + m_Seed * DraftSeedScaleZ) * draft;

    // 減衰する振り子：角加速度 = -ω²・角度 - 減衰・角速度 + 力
    const float stiffness = SwingFrequency * SwingFrequency;
    m_VelocityX += (-stiffness * m_AngleX - SwingDamping * m_VelocityX + forceX) * deltaTime;
    m_VelocityZ += (-stiffness * m_AngleZ - SwingDamping * m_VelocityZ + forceZ) * deltaTime;
    m_AngleX = (std::clamp)(m_AngleX + m_VelocityX * deltaTime, -MaxSwingAngle, MaxSwingAngle);
    m_AngleZ = (std::clamp)(m_AngleZ + m_VelocityZ * deltaTime, -MaxSwingAngle, MaxSwingAngle);

    // 大きく揺れている間は、接触の悪いソケットのように明るさが少し揺れる
    const float swing = std::sqrt(m_VelocityX * m_VelocityX + m_VelocityZ * m_VelocityZ);
    const float contact = std::sin(m_Time * ContactRateA + m_Seed) * std::sin(m_Time * ContactRateB);
    m_Brightness = 1.0f - (std::min)(swing * SwingDimScale, MaxSwingDim) * (contact > ContactThreshold ? 1.0f : WeakContactRatio);
}

Matrix HangingBulb::MakeWorldMatrix() const
{
    // 付け根を中心に、x軸とz軸のまわりへ回している（z方向の角度はx軸まわり、x方向の角度はz軸まわり）
    return Matrix::CreateRotationX(m_AngleZ) * Matrix::CreateRotationZ(-m_AngleX) * Matrix::CreateTranslation(m_Position);
}

Vector3 HangingBulb::GetBulbPosition() const
{
    return Vector3::Transform(Vector3(0.0f, -m_CordLength - SocketLength - BulbRadius, 0.0f), MakeWorldMatrix());
}

float HangingBulb::GetLightRange() const
{
    return LightRange;
}

void HangingBulb::Draw(Camera* camera)
{
    camera->SetCamera();
    Matrix world = MakeWorldMatrix();
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();

    m_CordMaterial->SetGPU();
    context->DrawIndexed(m_CordIndexCount, 0, 0);

    // 電球は、明るさの揺れに合わせて光る強さを変えている
    MATERIAL bulb{};
    bulb.Diffuse = BulbGlassDiffuse;
    bulb.Ambient = CordAmbient;
    bulb.Specular = CordSpecular;
    bulb.Emission = BulbColor * (BulbEmissionScale * m_Brightness);
    bulb.Shininess = CordShininess;
    bulb.TextureEnable = FALSE;
    m_BulbMaterial->SetMaterial(bulb);
    m_BulbMaterial->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()) - m_CordIndexCount, m_CordIndexCount, 0);
}

void HangingBulb::CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
{
    ENVIRONMENT_POINT_LIGHT light{};
    const Vector3 bulb = GetBulbPosition();
    light.PositionRange = Vector4(bulb.x, bulb.y, bulb.z, LightRange);
    light.ColorIntensity = Vector4(BulbColor.x, BulbColor.y, BulbColor.z, LightIntensity * m_Brightness);
    lights.push_back(light);
}

void HangingBulb::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_CordMaterial.reset();
    m_BulbMaterial.reset();
}
