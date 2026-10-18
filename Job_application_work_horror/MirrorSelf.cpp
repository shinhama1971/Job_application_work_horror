// ============================================================================
// ファイルの役割: 2面の壁の鏡にだけ映る「自分の姿」（プレイヤーの体）を描いている。説明は MirrorSelf.h を参照。
// 主な技術: 箱を組み合わせた体のメッシュ、頂点の色の塗り分け、鏡を描く間だけの描画、過去の姿勢の輪のバッファ
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

void MirrorSelf::Init()
{
    m_Vertices.clear();
    m_Indices.clear();

    // 中心と半分の大きさを指定して、色を付けた6面の箱を追加している（表と裏の両方から見える）
    const auto addBox = [this](const Vector3& center, const Vector3& half, const Color& color)
    {
        const Vector3 minimum = center - half;
        const Vector3 maximum = center + half;
        const auto addFace = [this, &color](const std::array<Vector3, 4>& positions, const Vector3& normal)
        {
            const unsigned int base = static_cast<unsigned int>(m_Vertices.size());
            const std::array<Vector2, 4> uvs =
            {
                Vector2(0.0f, 1.0f), Vector2(0.0f, 0.0f), Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f)
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
                base + 0, base + 1, base + 2, base + 2, base + 1, base + 3,
                base + 2, base + 1, base + 0, base + 3, base + 1, base + 2
            };
            m_Indices.insert(m_Indices.end(), indices, indices + 12);
        };
        const float l = minimum.x, r = maximum.x, b = minimum.y, t = maximum.y, k = minimum.z, f = maximum.z;
        addFace({ Vector3(l, b, f), Vector3(l, t, f), Vector3(r, b, f), Vector3(r, t, f) }, Vector3(0.0f, 0.0f, 1.0f));
        addFace({ Vector3(r, b, k), Vector3(r, t, k), Vector3(l, b, k), Vector3(l, t, k) }, Vector3(0.0f, 0.0f, -1.0f));
        addFace({ Vector3(r, b, f), Vector3(r, t, f), Vector3(r, b, k), Vector3(r, t, k) }, Vector3(1.0f, 0.0f, 0.0f));
        addFace({ Vector3(l, b, k), Vector3(l, t, k), Vector3(l, b, f), Vector3(l, t, f) }, Vector3(-1.0f, 0.0f, 0.0f));
        addFace({ Vector3(l, t, f), Vector3(l, t, k), Vector3(r, t, f), Vector3(r, t, k) }, Vector3(0.0f, 1.0f, 0.0f));
        addFace({ Vector3(l, b, k), Vector3(l, b, f), Vector3(r, b, k), Vector3(r, b, f) }, Vector3(0.0f, -1.0f, 0.0f));
    };

    // 服・ズボン・靴・肌・髪・懐中電灯の色
    const Color jacket(0.16f, 0.17f, 0.16f, 1.0f);
    const Color pants(0.10f, 0.10f, 0.11f, 1.0f);
    const Color shoes(0.05f, 0.045f, 0.04f, 1.0f);
    const Color skin(0.34f, 0.27f, 0.22f, 1.0f);
    const Color hair(0.035f, 0.030f, 0.028f, 1.0f);
    const Color flashlight(0.06f, 0.06f, 0.07f, 1.0f);

    // 体（高さ約2.1。足元が0、前が+z）。20倍して、目の高さがカメラの高さ（足元から約40）に合うようにしている
    addBox(Vector3(-0.15f, 0.48f, 0.0f), Vector3(0.11f, 0.48f, 0.11f), pants);
    addBox(Vector3(0.15f, 0.48f, 0.0f), Vector3(0.11f, 0.48f, 0.11f), pants);
    addBox(Vector3(-0.15f, 0.04f, 0.05f), Vector3(0.12f, 0.04f, 0.17f), shoes);
    addBox(Vector3(0.15f, 0.04f, 0.05f), Vector3(0.12f, 0.04f, 0.17f), shoes);
    addBox(Vector3(0.0f, 1.30f, 0.0f), Vector3(0.30f, 0.38f, 0.15f), jacket);
    // 左腕は下げ、右腕は懐中電灯を前に構えている
    addBox(Vector3(-0.40f, 1.25f, 0.0f), Vector3(0.08f, 0.36f, 0.08f), jacket);
    addBox(Vector3(0.36f, 1.52f, 0.30f), Vector3(0.075f, 0.075f, 0.32f), jacket);
    addBox(Vector3(0.36f, 1.52f, 0.66f), Vector3(0.06f, 0.06f, 0.06f), skin);
    addBox(Vector3(0.36f, 1.55f, 0.80f), Vector3(0.05f, 0.05f, 0.12f), flashlight);
    // 首と頭（後ろと上は髪）
    addBox(Vector3(0.0f, 1.72f, 0.0f), Vector3(0.07f, 0.06f, 0.07f), skin);
    addBox(Vector3(0.0f, 1.95f, 0.0f), Vector3(0.14f, 0.17f, 0.16f), skin);
    addBox(Vector3(0.0f, 2.09f, -0.02f), Vector3(0.15f, 0.06f, 0.17f), hair);
    addBox(Vector3(0.0f, 1.96f, -0.13f), Vector3(0.15f, 0.15f, 0.05f), hair);

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

    m_Scale = Vector3(20.0f, 20.0f, 20.0f);
    m_HistoryHead = 0;
    m_HistoryFilled = 0;
    m_SampleTimer = 0.0f;
}

// プレイヤーの位置とカメラの向きを、1秒に60回記録している（遅れて動くときに昔の姿勢を使うため）
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

    PoseSample current;
    current.Position = m_Player->GetPosition();
    current.Yaw = game->GetCamera()->GetCameraDirection();
    m_SampleTimer += Application::GetDeltaTime();
    if (m_HistoryFilled == 0 || m_SampleTimer >= SampleInterval)
    {
        m_SampleTimer = (std::min)(m_SampleTimer - SampleInterval, SampleInterval);
        m_SampleTimer = (std::max)(m_SampleTimer, 0.0f);
        m_HistoryHead = (m_HistoryHead + 1) % HistoryCount;
        m_History[static_cast<std::size_t>(m_HistoryHead)] = current;
        m_HistoryFilled = (std::min)(m_HistoryFilled + 1, HistoryCount);
    }

    // 遅らせる秒数の分だけ前の記録を選んでいる（記録が足りなければ、一番古いもの）
    const int delaySamples = (std::min)(
        static_cast<int>(std::lround(m_DelaySeconds / SampleInterval)), m_HistoryFilled - 1);
    const PoseSample& pose = m_History[static_cast<std::size_t>(
        (m_HistoryHead - (std::max)(delaySamples, 0) + HistoryCount) % HistoryCount)];
    m_Position = pose.Position;
    m_Rotation = Vector3(0.0f, pose.Yaw, 0.0f);
}

// 鏡に映す景色を描いている間だけ、体を描いている
void MirrorSelf::Draw(Camera* camera)
{
    if (!Renderer::IsDrawingWallMirror() || m_Hidden || m_HistoryFilled == 0)
    {
        return;
    }
    camera->SetCamera();
    Matrix world =
        Matrix::CreateScale(m_Scale) *
        Matrix::CreateFromYawPitchRoll(m_Rotation.y, 0.0f, 0.0f) *
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

void MirrorSelf::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Material.reset();
    m_Player = nullptr;
}
