// ============================================================================
// ファイルの役割: 2面の壁の鏡にだけ映る「自分の姿」（プレイヤーの体）を描いている。説明は MirrorSelf.h を参照。
// 主な技術: 円柱と楕円体の体のメッシュ、頂点の色の塗り分け、部位ごとの回転（歩く動き）、鏡を描く間だけの描画、過去の姿勢の輪のバッファ
// ============================================================================

#include "MirrorSelf.h"

#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "Player.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 体の大きさ（体は高さ約2.1で作り、20倍して目の高さがカメラの高さ（足元から約40）に合うようにしている）
    constexpr float BodyScale = 20.0f;
    // 円柱と楕円体の分割数
    constexpr int CylinderSegments = 10;
    constexpr int EllipsoidRings = 7;
    constexpr int EllipsoidSegments = 12;
    // 1歩で進む距離の2倍（脚が1往復する距離）
    constexpr float StrideCycleDistance = 34.0f;
}

void MirrorSelf::AddTriangle(
    const Vector3& a, const Vector3& b, const Vector3& c,
    const Vector3& normalA, const Vector3& normalB, const Vector3& normalC, const Color& color)
{
    const unsigned int base = static_cast<unsigned int>(m_Vertices.size());
    const Vector3 positions[] = { a, b, c };
    const Vector3 normals[] = { normalA, normalB, normalC };
    for (int index = 0; index < 3; ++index)
    {
        VERTEX_3D vertex{};
        vertex.position = positions[index];
        vertex.normal = normals[index];
        vertex.color = color;
        vertex.uv = Vector2(0.0f, 0.0f);
        m_Vertices.push_back(vertex);
    }
    // 表と裏の両方の順番を入れ、どちらから見ても欠けないようにしている
    const unsigned int indices[] = { base, base + 1, base + 2, base + 2, base + 1, base };
    m_Indices.insert(m_Indices.end(), indices, indices + 6);
}

void MirrorSelf::AddCylinder(
    const Vector3& a, const Vector3& b, float radiusA, float radiusB, float depthScale, const Color& color)
{
    Vector3 axis = b - a;
    axis.Normalize();
    const Vector3 reference = std::abs(axis.y) < 0.9f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
    Vector3 side = reference.Cross(axis);
    side.Normalize();
    const Vector3 other = axis.Cross(side);
    // 輪の上の向き（奥行きだけ depthScale で縮め、胴を平たくできるようにしている）
    const auto ringDirection = [&side, &other, depthScale](int segment)
    {
        const float angle = DirectX::XM_2PI * static_cast<float>(segment) / CylinderSegments;
        Vector3 direction = side * std::cos(angle) + other * std::sin(angle);
        direction.z *= depthScale;
        return direction;
    };
    for (int segment = 0; segment < CylinderSegments; ++segment)
    {
        const Vector3 d0 = ringDirection(segment);
        const Vector3 d1 = ringDirection(segment + 1);
        Vector3 n0 = d0;
        Vector3 n1 = d1;
        n0.Normalize();
        n1.Normalize();
        const Vector3 a0 = a + d0 * radiusA;
        const Vector3 a1 = a + d1 * radiusA;
        const Vector3 b0 = b + d0 * radiusB;
        const Vector3 b1 = b + d1 * radiusB;
        AddTriangle(a0, b0, a1, n0, n0, n1, color);
        AddTriangle(a1, b0, b1, n1, n0, n1, color);
        // 両端のふた
        AddTriangle(a, a1, a0, -axis, -axis, -axis, color);
        AddTriangle(b, b0, b1, axis, axis, axis, color);
    }
}

void MirrorSelf::AddEllipsoid(const Vector3& center, const Vector3& radius, const Color& color)
{
    const auto pointAt = [&center, &radius](int ring, int segment, Vector3& normal)
    {
        const float latitude = -DirectX::XM_PIDIV2 + DirectX::XM_PI * static_cast<float>(ring) / EllipsoidRings;
        const float longitude = DirectX::XM_2PI * static_cast<float>(segment) / EllipsoidSegments;
        const Vector3 unit(
            std::cos(latitude) * std::cos(longitude),
            std::sin(latitude),
            std::cos(latitude) * std::sin(longitude));
        normal = Vector3(unit.x / radius.x, unit.y / radius.y, unit.z / radius.z);
        normal.Normalize();
        return center + Vector3(unit.x * radius.x, unit.y * radius.y, unit.z * radius.z);
    };
    for (int ring = 0; ring < EllipsoidRings; ++ring)
    {
        for (int segment = 0; segment < EllipsoidSegments; ++segment)
        {
            Vector3 n00, n01, n10, n11;
            const Vector3 p00 = pointAt(ring, segment, n00);
            const Vector3 p01 = pointAt(ring, segment + 1, n01);
            const Vector3 p10 = pointAt(ring + 1, segment, n10);
            const Vector3 p11 = pointAt(ring + 1, segment + 1, n11);
            AddTriangle(p00, p10, p01, n00, n10, n01, color);
            AddTriangle(p01, p10, p11, n01, n10, n11, color);
        }
    }
}

void MirrorSelf::BeginPart(const Vector3& pivot, int swing)
{
    BodyPart part;
    part.IndexStart = static_cast<unsigned int>(m_Indices.size());
    part.Pivot = pivot;
    part.Swing = swing;
    m_Parts.push_back(part);
}

void MirrorSelf::EndPart()
{
    BodyPart& part = m_Parts.back();
    part.IndexCount = static_cast<unsigned int>(m_Indices.size()) - part.IndexStart;
}

void MirrorSelf::Init()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Parts.clear();

    // 服・ズボン・靴・肌・髪・懐中電灯の色
    const Color jacket(0.16f, 0.17f, 0.16f, 1.0f);
    const Color pants(0.10f, 0.10f, 0.11f, 1.0f);
    const Color shoes(0.05f, 0.045f, 0.04f, 1.0f);
    const Color skin(0.34f, 0.27f, 0.22f, 1.0f);
    const Color hair(0.035f, 0.030f, 0.028f, 1.0f);
    const Color flashlight(0.06f, 0.06f, 0.07f, 1.0f);

    // 胴・頭・懐中電灯を構えた右腕（振らない）。足元が0、前が+z、右が+x
    BeginPart(Vector3(0.0f, 0.0f, 0.0f), 0);
    AddCylinder(Vector3(0.0f, 0.86f, 0.0f), Vector3(0.0f, 1.02f, 0.0f), 0.22f, 0.24f, 0.65f, pants);
    AddCylinder(Vector3(0.0f, 1.00f, 0.0f), Vector3(0.0f, 1.62f, 0.0f), 0.25f, 0.29f, 0.62f, jacket);
    AddEllipsoid(Vector3(0.0f, 1.60f, 0.0f), Vector3(0.33f, 0.08f, 0.16f), jacket);
    AddCylinder(Vector3(0.0f, 1.64f, 0.0f), Vector3(0.0f, 1.77f, 0.0f), 0.06f, 0.06f, 1.0f, skin);
    AddEllipsoid(Vector3(0.0f, 1.88f, 0.01f), Vector3(0.115f, 0.145f, 0.13f), skin);
    AddEllipsoid(Vector3(0.0f, 1.935f, -0.03f), Vector3(0.125f, 0.12f, 0.135f), hair);
    AddCylinder(Vector3(0.33f, 1.57f, 0.0f), Vector3(0.36f, 1.40f, 0.25f), 0.065f, 0.055f, 1.0f, jacket);
    AddCylinder(Vector3(0.36f, 1.40f, 0.25f), Vector3(0.30f, 1.47f, 0.55f), 0.055f, 0.045f, 1.0f, jacket);
    AddEllipsoid(Vector3(0.29f, 1.48f, 0.60f), Vector3(0.05f, 0.05f, 0.06f), skin);
    AddCylinder(Vector3(0.29f, 1.50f, 0.56f), Vector3(0.29f, 1.52f, 0.82f), 0.04f, 0.05f, 1.0f, flashlight);
    EndPart();

    // 脚（股関節を中心に前後へ振る）
    for (int side = -1; side <= 1; side += 2)
    {
        const float x = 0.12f * static_cast<float>(side);
        BeginPart(Vector3(x, 0.92f, 0.0f), side < 0 ? 1 : 2);
        AddCylinder(Vector3(x, 0.92f, 0.0f), Vector3(x, 0.08f, 0.0f), 0.10f, 0.075f, 1.0f, pants);
        AddEllipsoid(Vector3(x, 0.05f, 0.06f), Vector3(0.075f, 0.05f, 0.15f), shoes);
        EndPart();
    }

    // 左腕（肩を中心に、左脚と逆向きに振る）
    BeginPart(Vector3(-0.33f, 1.57f, 0.0f), 3);
    AddCylinder(Vector3(-0.33f, 1.57f, 0.0f), Vector3(-0.37f, 1.22f, 0.0f), 0.065f, 0.055f, 1.0f, jacket);
    AddCylinder(Vector3(-0.37f, 1.22f, 0.0f), Vector3(-0.36f, 0.92f, 0.03f), 0.055f, 0.045f, 1.0f, jacket);
    AddEllipsoid(Vector3(-0.36f, 0.88f, 0.03f), Vector3(0.05f, 0.06f, 0.05f), skin);
    EndPart();

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // 色は頂点の色で決めるため、マテリアルは白にしている
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.Ambient = Color(0.03f, 0.03f, 0.03f, 1.0f);
    material.Specular = Color(0.05f, 0.05f, 0.05f, 1.0f);
    material.Shininess = 6.0f;
    material.TextureEnable = FALSE;
    m_Material = std::make_unique<Material>();
    m_Material->Create(material);

    m_Scale = Vector3(BodyScale, BodyScale, BodyScale);
    m_HistoryHead = 0;
    m_HistoryFilled = 0;
    m_SampleTimer = 0.0f;
    m_WalkPhase = 0.0f;
    m_WalkAmount = 0.0f;
}

const MirrorSelf::PoseSample& MirrorSelf::GetSample(int samplesAgo) const
{
    const int clamped = (std::clamp)(samplesAgo, 0, (std::max)(m_HistoryFilled - 1, 0));
    return m_History[static_cast<std::size_t>((m_HistoryHead - clamped + HistoryCount) % HistoryCount)];
}

// プレイヤーの位置とカメラの向きを1秒に60回記録し、遅らせた姿勢と、その時の歩く速さから手足の振りを決めている
void MirrorSelf::Update()
{
    Core::Game* game = Core::Game::GetInstance();
    if (m_Player == nullptr)
    {
        m_Player = game->GetObj<Player>("Player");
    }
    if (m_Player == nullptr)
    {
        return;
    }
    m_Hidden = m_Player->IsHiding();

    const float deltaTime = Application::GetDeltaTime();
    PoseSample current;
    current.Position = m_Player->GetPosition();
    current.Yaw = game->GetCamera()->GetCameraDirection();
    m_SampleTimer += deltaTime;
    if (m_HistoryFilled == 0 || m_SampleTimer >= SampleInterval)
    {
        m_SampleTimer = (std::clamp)(m_SampleTimer - SampleInterval, 0.0f, SampleInterval);
        m_HistoryHead = (m_HistoryHead + 1) % HistoryCount;
        m_History[static_cast<std::size_t>(m_HistoryHead)] = current;
        m_HistoryFilled = (std::min)(m_HistoryFilled + 1, HistoryCount);
    }

    // 遅らせる秒数の分だけ前の記録を選んでいる（記録が足りなければ、一番古いもの）
    const int delaySamples = static_cast<int>(std::lround(m_DelaySeconds / SampleInterval));
    const PoseSample& pose = GetSample(delaySamples);
    m_Position = pose.Position;
    m_Rotation = Vector3(0.0f, pose.Yaw, 0.0f);

    // 歩く速さ（0.1秒前との差）から、手足を振る大きさと速さを決めている
    const PoseSample& earlier = GetSample(delaySamples + 6);
    Vector3 moved = pose.Position - earlier.Position;
    moved.y = 0.0f;
    const float speed = moved.Length() / (6.0f * SampleInterval);
    const float targetAmount = (std::clamp)(speed / 35.0f, 0.0f, 1.0f);
    m_WalkAmount += (targetAmount - m_WalkAmount) * (std::min)(deltaTime * 8.0f, 1.0f);
    m_WalkPhase += speed / StrideCycleDistance * DirectX::XM_2PI * deltaTime;
    if (m_WalkPhase > DirectX::XM_2PI * 64.0f)
    {
        m_WalkPhase -= DirectX::XM_2PI * 64.0f;
    }
}

// 鏡に映す景色を描いている間だけ、体を部位ごとに描いている（脚と左腕は、関節を中心に前後へ振る）
void MirrorSelf::Draw(Camera* camera)
{
    if (!Renderer::IsDrawingWallMirror() || m_Hidden || m_HistoryFilled == 0)
    {
        return;
    }
    camera->SetCamera();
    const Matrix body =
        Matrix::CreateScale(m_Scale) *
        Matrix::CreateFromYawPitchRoll(m_Rotation.y, 0.0f, 0.0f) *
        Matrix::CreateTranslation(m_Position);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();

    const float swing = std::sin(m_WalkPhase) * m_WalkAmount;
    for (const BodyPart& part : m_Parts)
    {
        float angle = 0.0f;
        if (part.Swing == 1)
        {
            angle = swing * 0.45f;
        }
        else if (part.Swing == 2)
        {
            angle = -swing * 0.45f;
        }
        else if (part.Swing == 3)
        {
            angle = -swing * 0.35f;
        }
        Matrix world =
            Matrix::CreateTranslation(-part.Pivot) *
            Matrix::CreateRotationX(angle) *
            Matrix::CreateTranslation(part.Pivot) *
            body;
        Renderer::SetWorldMatrix(&world);
        context->DrawIndexed(part.IndexCount, part.IndexStart, 0);
    }
}

void MirrorSelf::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Parts.clear();
    m_Material.reset();
    m_Player = nullptr;
}
