// ============================================================================
// ファイルの役割: 箱の形をした壁（と、同じ形を使う棚・配管・目印などの小物）の形・色・当たり判定・影の有無を管理している。
// 主な技術: コードで作る箱のメッシュ、AABBの当たり判定、一番近い面への押し戻し、拡大・回転・移動の行列
// ============================================================================

#include "Wall.h"
#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "Renderer.h"
#include "PropModel.h"
#include "utility.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 見た目だけモデルにするときの部位。UsesSurfaceColor の部位は配置ファイルの色（SetAppearance）で塗り、
    // それ以外（書類・取っ手・すき間の暗がりなど）はここで決めた色で塗っている
    // Glows の部位（警告灯のレンズ・電球）だけに、配置ファイルの光る色を付けている（枠や笠は光らせない）
    struct VisualModelPart
    {
        const char* Material;
        Color PartColor;
        bool UsesSurfaceColor;
        bool Glows = false;
    };

    // モデル1つ分：配置ファイルで使う名前、ファイル、標準の大きさ（長い物は x が長い向き）、箱の一番長い向きへ回すか、部位
    struct VisualModel
    {
        const char* Name;
        const char* Path;
        Vector3 Size;
        bool FollowsLongestAxis;
        std::vector<VisualModelPart> Parts;
    };

    const VisualModelPart MainPart{ "Main", Color(1.0f, 1.0f, 1.0f, 1.0f), true };
    const VisualModelPart DarkPart{ "Dark", Color(0.012f, 0.012f, 0.012f, 1.0f), false };
    const VisualModelPart SlitPart{ "Slit", Color(0.015f, 0.016f, 0.015f, 1.0f), false };
    const VisualModelPart HandlePart{ "Handle", Color(0.42f, 0.42f, 0.38f, 1.0f), false };
    // 警告灯とライト：光る部位（配置ファイルの色で塗って光らせる）と、光らない金属の枠
    const VisualModelPart LensPart{ "Lens", Color(1.0f, 1.0f, 1.0f, 1.0f), true, true };
    const VisualModelPart HousingPart{ "Housing", Color(0.075f, 0.08f, 0.078f, 1.0f), false };

    const std::vector<VisualModel>& GetVisualModels()
    {
        static const std::vector<VisualModel> models =
        {
            // 書類棚（棚板・柱・筋かいと、ファイル・段ボール箱・紙の束）
            { "shelf_long", "assets/model/props/wall_shelf_long.fbx", Vector3(90.0f, 44.0f, 6.0f), true,
                { MainPart, { "Binder", Color(0.10f, 0.12f, 0.17f, 1.0f), false },
                  { "Box", Color(0.30f, 0.24f, 0.16f, 1.0f), false }, { "Paper", Color(0.46f, 0.44f, 0.40f, 1.0f), false } } },
            { "shelf_short", "assets/model/props/wall_shelf_short.fbx", Vector3(30.0f, 44.0f, 10.0f), true,
                { MainPart, { "Binder", Color(0.10f, 0.12f, 0.17f, 1.0f), false },
                  { "Box", Color(0.30f, 0.24f, 0.16f, 1.0f), false }, { "Paper", Color(0.46f, 0.44f, 0.40f, 1.0f), false } } },
            // 机の天板（幕板と引き出し）と脚
            { "desk_top", "assets/model/props/wall_desk_top.fbx", Vector3(40.0f, 2.0f, 24.0f), false,
                { MainPart, { "Metal", Color(0.40f, 0.38f, 0.33f, 1.0f), false } } },
            { "desk_leg", "assets/model/props/wall_desk_leg.fbx", Vector3(2.0f, 23.0f, 2.0f), false, { MainPart } },
            // 板を打ち付けた木箱と、崩れた資材の山
            { "crate", "assets/model/props/wall_crate.fbx", Vector3(14.0f, 14.0f, 14.0f), false, { MainPart, DarkPart } },
            { "debris", "assets/model/props/wall_debris.fbx", Vector3(40.0f, 30.0f, 30.0f), false,
                { MainPart, { "Concrete", Color(0.21f, 0.20f, 0.19f, 1.0f), false } } },
            // 鋼の梁・管の柱（長い向きへ回す）
            { "beam", "assets/model/props/wall_beam.fbx", Vector3(60.0f, 6.0f, 6.0f), true, { MainPart } },
            { "pipe_pole", "assets/model/props/wall_pipe_pole.fbx", Vector3(50.0f, 3.0f, 3.0f), true, { MainPart } },
            // 引き出しのキャビネット・壁際の配電箱
            { "cabinet", "assets/model/props/wall_cabinet.fbx", Vector3(28.0f, 24.0f, 12.0f), false, { MainPart, HandlePart } },
            { "service_box", "assets/model/props/wall_service_box.fbx", Vector3(10.0f, 30.0f, 24.0f), false,
                { MainPart, HandlePart, SlitPart } },
            // 西棟のポンプ・タンク・排水口
            { "pump", "assets/model/props/wall_pump.fbx", Vector3(30.0f, 26.0f, 26.0f), false,
                { MainPart, { "Paint", Color(0.12f, 0.18f, 0.15f, 1.0f), false } } },
            { "tank", "assets/model/props/wall_tank.fbx", Vector3(20.0f, 34.0f, 30.0f), false, { MainPart } },
            { "drain", "assets/model/props/wall_drain.fbx", Vector3(16.0f, 0.8f, 16.0f), false, { MainPart, DarkPart } },
            // 柱（上下の台つき）
            { "column", "assets/model/props/wall_column.fbx", Vector3(10.0f, 38.0f, 10.0f), false, { MainPart } },
            // 2面のロッカーの本体と、鏡・肖像画・偽の扉の額縁の棒
            { "locker_body", "assets/model/props/wall_locker_body.fbx", Vector3(10.0f, 44.0f, 16.0f), false, { MainPart, SlitPart } },
            { "frame_rail", "assets/model/props/wall_frame_rail.fbx", Vector3(20.0f, 2.0f, 1.8f), true, { MainPart } },
            // 壁の警告灯（光るレンズを金属の枠と格子で囲む。壁に沿った長い向きへ回す）
            { "warning_lamp", "assets/model/props/wall_warning_lamp.fbx", Vector3(10.0f, 4.0f, 1.0f), true,
                { HousingPart, LensPart } },
            // 安全地帯の裸電球（ソケットと光る電球）
            { "safe_bulb", "assets/model/props/wall_safe_bulb.fbx", Vector3(2.6f, 3.2f, 2.6f), false, { HousingPart, LensPart } },
            // 卓上ライト（緑の琺瑯の笠と、下から見える光る電球・笠の裏）、腕、台
            { "desk_lamp_shade", "assets/model/props/wall_desk_lamp_shade.fbx", Vector3(5.0f, 3.0f, 5.0f), false,
                { HousingPart, { "Shade", Color(0.10f, 0.16f, 0.12f, 1.0f), false }, LensPart } },
            // 天井沿いの長い配管（継ぎ目のフランジと吊り金具。長い向きへ回す）
            { "pipe_run", "assets/model/props/wall_pipe_run.fbx", Vector3(300.0f, 3.0f, 3.0f), true, { MainPart } },
            // 扉の上の表示灯（光る表示面を金属の枠で囲む）と、電球を吊るすコード
            { "door_indicator", "assets/model/props/wall_door_indicator.fbx", Vector3(10.0f, 5.0f, 1.0f), true,
                { HousingPart, LensPart } },
            { "hanging_cord", "assets/model/props/wall_hanging_cord.fbx", Vector3(0.4f, 2.5f, 0.4f), false, { MainPart } },
            // 外の森（forest_props.txt）：枯れ木2種（標準の大きさは幹の当たり判定の箱。枝はその外へ広がる）、倒木（長い向きへ回す）、岩、枯れた茂み
            { "forest_tree_dead", "assets/model/props/forest_tree_dead.fbx", Vector3(8.0f, 130.0f, 8.0f), false, { MainPart } },
            { "forest_tree_fork", "assets/model/props/forest_tree_fork.fbx", Vector3(8.0f, 130.0f, 8.0f), false, { MainPart } },
            { "forest_fallen_log", "assets/model/props/forest_fallen_log.fbx", Vector3(60.0f, 8.0f, 8.0f), true, { MainPart } },
            { "forest_rock", "assets/model/props/forest_rock.fbx", Vector3(14.0f, 9.0f, 12.0f), false, { MainPart } },
            { "forest_brush", "assets/model/props/forest_brush.fbx", Vector3(20.0f, 12.0f, 20.0f), false, { MainPart } },
            { "desk_lamp_arm", "assets/model/props/wall_desk_lamp_arm.fbx", Vector3(0.8f, 9.0f, 0.8f), false, { MainPart } },
            { "desk_lamp_base", "assets/model/props/wall_desk_lamp_base.fbx", Vector3(4.0f, 1.0f, 4.0f), false,
                { MainPart, HousingPart } },
        };
        return models;
    }

    const VisualModel* FindVisualModel(const std::string& name)
    {
        for (const VisualModel& model : GetVisualModels())
        {
            if (name == model.Name)
            {
                return &model;
            }
        }
        return nullptr;
    }
}

// 原点を中心にした1x1x1の箱（6面）を作り、灰色のコンクリートのマテリアルを用意している
void Wall::Init()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Vertices.reserve(24);
    m_Indices.reserve(72);

    const Color vertexColor(1.0f, 1.0f, 1.0f, 1.0f);

    // 四角形の面を1枚追加している
    const auto addFace = [this, &vertexColor](
        const std::array<Vector3, 4>& positions,
        const Vector3& normal)
    {
        const unsigned int base = static_cast<unsigned int>(m_Vertices.size());
        const std::array<Vector2, 4> uvs =
        {
            Vector2(0.0f, 1.0f),
            Vector2(0.0f, 0.0f),
            Vector2(1.0f, 1.0f),
            Vector2(1.0f, 0.0f)
        };

        for (size_t i = 0; i < positions.size(); ++i)
        {
            VERTEX_3D vertex{};
            vertex.position = positions[i];
            vertex.normal = normal;
            vertex.color = vertexColor;
            vertex.uv = uvs[i];
            m_Vertices.push_back(vertex);
        }

        // 表と裏の両方の頂点の順番を持たせ、どちら側からでも壁が見えるようにしている。
        // 部屋の配置を調整している途中でも、裏の面が欠けないようにするためである。
        const unsigned int faceIndices[] =
        {
            base + 0, base + 1, base + 2,
            base + 2, base + 1, base + 3,
            base + 2, base + 1, base + 0,
            base + 3, base + 1, base + 2
        };
        m_Indices.insert(
            m_Indices.end(),
            faceIndices,
            faceIndices + 12
        );
    };

    // 前・後ろ・右・左・上・下の6面
    addFace(
        { Vector3(-0.5f, -0.5f, 0.5f), Vector3(-0.5f, 0.5f, 0.5f),
          Vector3(0.5f, -0.5f, 0.5f), Vector3(0.5f, 0.5f, 0.5f) },
        Vector3(0.0f, 0.0f, 1.0f)
    );
    addFace(
        { Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, -0.5f),
          Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, 0.5f, -0.5f) },
        Vector3(0.0f, 0.0f, -1.0f)
    );
    addFace(
        { Vector3(0.5f, -0.5f, 0.5f), Vector3(0.5f, 0.5f, 0.5f),
          Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, -0.5f) },
        Vector3(1.0f, 0.0f, 0.0f)
    );
    addFace(
        { Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, 0.5f, -0.5f),
          Vector3(-0.5f, -0.5f, 0.5f), Vector3(-0.5f, 0.5f, 0.5f) },
        Vector3(-1.0f, 0.0f, 0.0f)
    );
    addFace(
        { Vector3(-0.5f, 0.5f, 0.5f), Vector3(-0.5f, 0.5f, -0.5f),
          Vector3(0.5f, 0.5f, 0.5f), Vector3(0.5f, 0.5f, -0.5f) },
        Vector3(0.0f, 1.0f, 0.0f)
    );
    addFace(
        { Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, -0.5f, 0.5f),
          Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, -0.5f, 0.5f) },
        Vector3(0.0f, -1.0f, 0.0f)
    );

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_BoxVertices = m_Vertices;
    m_BoxIndices = m_Indices;
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");
    m_NaturalSurface = NaturalSurface::None;

    m_Material = std::make_unique<Material>();
    // 普段の色：くすんだ灰色、光沢は弱い
    m_SurfaceMaterial.Diffuse = Color(0.34f, 0.36f, 0.33f, 1.0f);
    m_SurfaceMaterial.Ambient = Color(0.03f, 0.035f, 0.03f, 1.0f);
    m_SurfaceMaterial.Specular = Color(0.04f, 0.04f, 0.04f, 1.0f);
    m_SurfaceMaterial.Emission = Color(0.0f, 0.0f, 0.0f, 1.0f);
    m_SurfaceMaterial.Shininess = 4.0f;
    m_SurfaceMaterial.TextureEnable = FALSE;
    m_Material->Create(m_SurfaceMaterial);
    m_UnlitPartMaterial = std::make_unique<Material>();
    m_UnlitPartMaterial->Create(m_SurfaceMaterial);
}

// 崩れて現れる・消える途中なら、崩れ具合を進めている（消え終わったら非表示にしている）
void Wall::Update()
{
    if (m_DissolveSpeed <= 0.0f)
    {
        return;
    }
    const float step = m_DissolveSpeed * Application::GetDeltaTime();
    const float dissolve = m_Dissolve < m_DissolveTarget
        ? (std::min)(m_Dissolve + step, m_DissolveTarget)
        : (std::max)(m_Dissolve - step, m_DissolveTarget);
    ApplyDissolve(dissolve);
    if (m_Dissolve == m_DissolveTarget)
    {
        m_DissolveSpeed = 0.0f;
        if (m_DissolveTarget >= 1.0f)
        {
            // 消え終わった：非表示にし、次に表示するときは元の形から始めている
            m_Visible = false;
            ApplyDissolve(0.0f);
        }
    }
}

void Wall::SetVisible(bool visible)
{
    m_Visible = visible;
    m_DissolveSpeed = 0.0f;
    if (m_Dissolve != 0.0f)
    {
        ApplyDissolve(0.0f);
    }
}

void Wall::DissolveIn(float seconds)
{
    // 消えている物は、崩れきった形から現れ始めている（表示中なら今の崩れ具合から戻している）
    if (!m_Visible)
    {
        m_Visible = true;
        ApplyDissolve(1.0f);
    }
    m_DissolveTarget = 0.0f;
    m_DissolveSpeed = 1.0f / (std::max)(seconds, 0.01f);
}

void Wall::DissolveOut(float seconds)
{
    if (!m_Visible)
    {
        return;
    }
    m_DissolveTarget = 1.0f;
    m_DissolveSpeed = 1.0f / (std::max)(seconds, 0.01f);
}

void Wall::ApplyDissolve(float dissolve)
{
    m_Dissolve = dissolve;
    m_SurfaceMaterial.Dissolve = dissolve;
    if (m_Material != nullptr)
    {
        UploadMaterial();
    }
}

// 拡大・回転・移動の順に掛けたワールド行列を返している
Matrix Wall::MakeWorldMatrix() const
{
    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z
    );
    const Matrix scale = Matrix::CreateScale(m_Scale);
    const Matrix translation = Matrix::CreateTranslation(m_Position);
    return scale * rotation * translation;
}

// レイ反射で水面に映す小物の箱を返している。回した箱は、それを囲む軸に沿った箱にしている
bool Wall::GetReflectionProxy(Vector3& minimum, Vector3& maximum, Color& color) const
{
    if (!m_Visible || m_Dissolve > 0.0f || m_IsMirror ||
        m_SurfaceMaterial.WeatheringSurface || m_SurfaceMaterial.Diffuse.w < 0.999f)
    {
        return false;
    }
    const Matrix rotation = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
    const Vector3 half(std::abs(m_Scale.x) * 0.5f, std::abs(m_Scale.y) * 0.5f, std::abs(m_Scale.z) * 0.5f);
    const Vector3 extent(
        std::abs(rotation._11) * half.x + std::abs(rotation._21) * half.y + std::abs(rotation._31) * half.z,
        std::abs(rotation._12) * half.x + std::abs(rotation._22) * half.y + std::abs(rotation._32) * half.z,
        std::abs(rotation._13) * half.x + std::abs(rotation._23) * half.y + std::abs(rotation._33) * half.z);
    // 低い物（床の縁取り・板）と、床や天井のような大きな板は映さない（部屋の形と重なり、ちらつくため）
    if (extent.y < 3.0f || extent.x > 40.0f || extent.z > 40.0f || (std::max)(extent.x, extent.z) < 0.8f)
    {
        return false;
    }
    minimum = m_Position - extent;
    maximum = m_Position + extent;
    color = m_SurfaceMaterial.Diffuse;
    return true;
}

// 箱を描いている（非表示なら何もしない）
void Wall::Draw(Camera* cam)
{
    if (!m_Visible)
    {
        return;
    }

    cam->SetCamera();

    Matrix world = MakeWorldMatrix();
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();

    // モデルの光らない部位（枠・笠・台）を、光る色を付けないマテリアルで先に描いている
    if (m_GlowIndexStart > 0)
    {
        m_UnlitPartMaterial->SetGPU();
        context->DrawIndexed(m_GlowIndexStart, 0, 0);
    }
    m_Material->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()) - m_GlowIndexStart, m_GlowIndexStart, 0);
}

// 影を作るための描画（深度だけ）
void Wall::DrawShadow()
{
    if (!m_Visible || !m_CastsShadow)
    {
        return;
    }

    Matrix world = MakeWorldMatrix();
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Core::Game::GetInstance()->GetShadowMap()->SetShader();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

// 深度プリパス：本描画と同じ頂点シェーダー・同じ行列で、深度だけを描いている（ピクセルシェーダーは外している）
void Wall::DrawDepthPrepass(Camera* cam)
{
    if (!WritesDepthPrepass())
    {
        return;
    }

    cam->SetCamera();

    Matrix world = MakeWorldMatrix();
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    context->PSSetShader(nullptr, nullptr, 0);
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

// プレイヤーの円が箱にめり込んでいたら押し出している。回転は考えず、上から見た長方形として扱っている
void Wall::ResolveCollision(Vector3& position, float radius) const
{
    if (!m_Visible || !m_CollisionEnabled)
    {
        return;
    }

    const float halfX = std::abs(m_Scale.x) * 0.5f;
    const float halfZ = std::abs(m_Scale.z) * 0.5f;

    const float minX = m_Position.x - halfX;
    const float maxX = m_Position.x + halfX;
    const float minZ = m_Position.z - halfZ;
    const float maxZ = m_Position.z + halfZ;

    // 長方形の中で、プレイヤーに最も近い点を求め、中心が外にあれば半径の分だけ離している
    const float closestX = std::clamp(position.x, minX, maxX);
    const float closestZ = std::clamp(position.z, minZ, maxZ);

    const float deltaX = position.x - closestX;
    const float deltaZ = position.z - closestZ;
    const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
    const float radiusSquared = radius * radius;

    if (distanceSquared >= radiusSquared)
    {
        return;
    }

    constexpr float epsilon = 0.000001f;
    if (distanceSquared > epsilon)
    {
        const float distance = std::sqrt(distanceSquared);
        const float pushDistance = radius - distance;
        position.x += deltaX / distance * pushDistance;
        position.z += deltaZ / distance * pushDistance;
        return;
    }

    // プレイヤーの中心が壁の中にある場合は、最も近い面の外へ押し出している。
    // 壁の中に現れたり、1フレームで大きく動いたりしても、安全に戻れるようにしている。
    const float distanceToLeft = position.x - minX;
    const float distanceToRight = maxX - position.x;
    const float distanceToNear = position.z - minZ;
    const float distanceToFar = maxZ - position.z;

    const float nearestFace = (std::min)(
        (std::min)(distanceToLeft, distanceToRight),
        (std::min)(distanceToNear, distanceToFar)
    );

    if (nearestFace == distanceToLeft)
    {
        position.x = minX - radius;
    }
    else if (nearestFace == distanceToRight)
    {
        position.x = maxX + radius;
    }
    else if (nearestFace == distanceToNear)
    {
        position.z = minZ - radius;
    }
    else
    {
        position.z = maxZ + radius;
    }
}

// 線分が箱に当たるかを、3つの軸それぞれで線分が箱の幅の中にある区間を絞り込んで調べている（スラブ法）
bool Wall::IntersectsInteractionSegment(
    const Vector3& start,
    const Vector3& end,
    float& hitDistance) const
{
    if (!m_Visible || !m_CollisionEnabled)
    {
        return false;
    }

    const Vector3 halfExtent(
        std::abs(m_Scale.x) * 0.5f,
        std::abs(m_Scale.y) * 0.5f,
        std::abs(m_Scale.z) * 0.5f);
    const Vector3 boxMin = m_Position - halfExtent;
    const Vector3 boxMax = m_Position + halfExtent;
    const Vector3 direction = end - start;

    float minimumTime = 0.0f;
    float maximumTime = 1.0f;
    const auto clipAxis = [&minimumTime, &maximumTime](
        float origin,
        float delta,
        float minimum,
        float maximum)
    {
        constexpr float epsilon = 0.000001f;
        if (std::abs(delta) <= epsilon)
        {
            return origin >= minimum && origin <= maximum;
        }

        float enterTime = (minimum - origin) / delta;
        float exitTime = (maximum - origin) / delta;
        if (enterTime > exitTime)
        {
            std::swap(enterTime, exitTime);
        }
        minimumTime = (std::max)(minimumTime, enterTime);
        maximumTime = (std::min)(maximumTime, exitTime);
        return minimumTime <= maximumTime;
    };

    if (!clipAxis(start.x, direction.x, boxMin.x, boxMax.x) ||
        !clipAxis(start.y, direction.y, boxMin.y, boxMax.y) ||
        !clipAxis(start.z, direction.z, boxMin.z, boxMax.z))
    {
        return false;
    }

    // 当たった位置（線分の始点からの距離）を返している
    const float segmentLength = direction.Length();
    hitDistance = segmentLength * (std::clamp)(minimumTime, 0.0f, 1.0f);
    return true;
}

// 頂点データを解放している
void Wall::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}

// 色・自己発光の色・光沢を変えている（環境光と鏡面反射の色は小物用の値にそろえている）
void Wall::SetAppearance(
    const Color& diffuse,
    const Color& emission,
    float shininess)
{
    const bool colorChanged = m_SurfaceMaterial.Diffuse.x != diffuse.x ||
        m_SurfaceMaterial.Diffuse.y != diffuse.y || m_SurfaceMaterial.Diffuse.z != diffuse.z;
    m_SurfaceMaterial.Diffuse = diffuse;
    // 絵を計算で描く面（肖像画・時計）では、環境の色の値を絵へ渡す値として使っている（SetPortraitEyes・SetClockHands）
    m_SurfaceMaterial.Ambient = m_HasSurfaceParams ? m_SurfaceParams : Color(0.025f, 0.025f, 0.025f, 1.0f);
    m_SurfaceMaterial.Specular = Color(0.10f, 0.11f, 0.10f, 1.0f);
    m_SurfaceMaterial.Emission = emission;
    m_SurfaceMaterial.Shininess = (std::max)(shininess, 1.0f);
    m_SurfaceMaterial.TextureEnable = FALSE;

    if (m_Material != nullptr)
    {
        UploadMaterial();
    }
    // モデルの見た目では、配置ファイルの色を頂点の色で付けているので、色が変わったら頂点を作り直している
    if (colorChanged && !m_VisualModel.empty())
    {
        RebuildVisualModel();
    }
}

// マテリアルをGPUへ送っている。モデルの見た目では、部位ごとの色を頂点の色で付けるため、マテリアルの色を白にしている
// （透明度・光る色・粗さなどは、そのまま使う）
void Wall::UploadMaterial()
{
    if (m_Material == nullptr)
    {
        return;
    }
    MATERIAL material = m_SurfaceMaterial;
    // 光る色に倍率を掛けている（街灯のちらつきなど。SetEmissionScale）
    material.Emission = Color(m_SurfaceMaterial.Emission.x * m_EmissionScale, m_SurfaceMaterial.Emission.y * m_EmissionScale,
        m_SurfaceMaterial.Emission.z * m_EmissionScale, m_SurfaceMaterial.Emission.w);
    if (!m_VisualModel.empty())
    {
        material.Diffuse = Color(1.0f, 1.0f, 1.0f, m_SurfaceMaterial.Diffuse.w);
    }
    // 樹皮の材質（barkPS）は、木目の向きを、使っていない環境の色で受け取っている（箱の回転をワールドへ当てた向き）
    if (m_NaturalSurface == NaturalSurface::Bark)
    {
        Vector3 grain = Vector3::TransformNormal(m_GrainAxis,
            Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z));
        grain.Normalize();
        material.Ambient = Color(grain.x, grain.y, grain.z, 1.0f);
    }
    m_Material->SetMaterial(material);
    // 光らない部位用：同じ値で、光る色だけを消している
    if (m_UnlitPartMaterial != nullptr)
    {
        material.Emission = Color(0.0f, 0.0f, 0.0f, 1.0f);
        m_UnlitPartMaterial->SetMaterial(material);
    }
}

void Wall::SetEmissionScale(float scale)
{
    scale = (std::max)(scale, 0.0f);
    // 変わったときだけ、マテリアルを送り直している（毎フレーム同じ値なら何もしない）
    if (scale == m_EmissionScale)
    {
        return;
    }
    m_EmissionScale = scale;
    UploadMaterial();
}

bool Wall::IsKnownVisualModel(const std::string& name)
{
    return FindVisualModel(name) != nullptr;
}

void Wall::SetVisualModel(const std::string& name)
{
    if (!name.empty() && FindVisualModel(name) == nullptr)
    {
        utility::ReportFatalError("Wall に知らないモデルの名前が指定されました: " + name);
    }
    m_VisualModel = name;
    // 外の森の岩は岩の材質（rockPS。色むら・ひび・苔・鉱物の粒・地面際の湿り）、枯れ木・倒木・茂みは樹皮の材質
    // （barkPS。木目に沿った溝・剥がれ・苔）で描いている（どちらもPBR）。配置を読み直して変わったときは、シェーダーを替え直している
    const NaturalSurface surface = name == "forest_rock" ? NaturalSurface::Rock
        : (name == "forest_tree_dead" || name == "forest_tree_fork" || name == "forest_fallen_log" || name == "forest_brush")
            ? NaturalSurface::Bark : NaturalSurface::None;
    if (surface != m_NaturalSurface)
    {
        m_NaturalSurface = surface;
        m_Shader.Create("shader/litTextureVS.hlsl",
            surface == NaturalSurface::Rock ? "shader/rockPS.hlsl"
            : surface == NaturalSurface::Bark ? "shader/barkPS.hlsl" : "shader/litTexturePS.hlsl");
    }
    RebuildVisualModel();
    UploadMaterial();
}

// 見た目の頂点を作り直している。モデルは、箱の一番長い向きへ回してから、箱の標準の大きさで割り、1×1×1の箱の空間にしている
// （ワールド行列の拡大で、置いた箱の大きさに伸びる）
void Wall::RebuildVisualModel()
{
    m_GlowIndexStart = 0;
    if (m_VisualModel.empty())
    {
        m_Vertices = m_BoxVertices;
        m_Indices = m_BoxIndices;
    }
    else
    {
        const VisualModel& model = *FindVisualModel(m_VisualModel);
        Matrix orientation = Matrix::Identity;
        Vector3 nominal = model.Size;
        if (model.FollowsLongestAxis)
        {
            const Vector3 size(std::abs(m_Scale.x), std::abs(m_Scale.y), std::abs(m_Scale.z));
            if (size.z > size.x && size.z >= size.y)
            {
                // モデルの x を z へ（y軸まわりに回す）
                orientation = Matrix(
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    -1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);
                nominal = Vector3(model.Size.z, model.Size.y, model.Size.x);
            }
            else if (size.y > size.x && size.y > size.z)
            {
                // モデルの x を y へ（z軸まわりに回す）
                orientation = Matrix(
                    0.0f, 1.0f, 0.0f, 0.0f,
                    -1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);
                nominal = Vector3(model.Size.y, model.Size.x, model.Size.z);
            }
        }
        // 樹皮の木目の向き：長い向きへ回すモデル（倒木）はモデルの x、それ以外（木・茂み）は上
        m_GrainAxis = Vector3::TransformNormal(
            model.FollowsLongestAxis ? Vector3(1.0f, 0.0f, 0.0f) : Vector3(0.0f, 1.0f, 0.0f), orientation);
        // 光らない部位を先に、光る部位を後に並べ、光る部位の始まりを覚えている
        std::vector<PropModel::Part> parts;
        bool hasGlowPart = false;
        std::size_t unlitPartCount = 0;
        for (const bool glowing : { false, true })
        {
            for (const VisualModelPart& part : model.Parts)
            {
                if (part.Glows != glowing)
                {
                    continue;
                }
                hasGlowPart = hasGlowPart || part.Glows;
                unlitPartCount += part.Glows ? 0 : 1;
                const Color& surface = m_SurfaceMaterial.Diffuse;
                parts.push_back({ part.Material, part.UsesSurfaceColor
                    ? Color(surface.x, surface.y, surface.z, 1.0f) : part.PartColor });
            }
        }
        m_Vertices.clear();
        m_Indices.clear();
        const std::vector<PropModel::IndexRange> ranges =
            PropModel::Append(model.Path, parts, nominal, m_Vertices, m_Indices, orientation);
        if (hasGlowPart)
        {
            m_GlowIndexStart = ranges[unlitPartCount].Start;
        }
    }
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
}

// PBRの表面の仕上げ（金属らしさと粗さ）を、マテリアルの値として変えている
void Wall::SetSurfaceFinish(float metallic, float roughness)
{
    m_SurfaceMaterial.Metallic = metallic;
    m_SurfaceMaterial.Roughness = roughness;
    if (m_Material != nullptr)
    {
        UploadMaterial();
    }
}

// 壁の古さを描く面かどうかを、マテリアルの印として変えている
void Wall::SetWeatheringSurface(bool enabled)
{
    m_SurfaceMaterial.WeatheringSurface = enabled ? TRUE : FALSE;
    if (m_Material != nullptr)
    {
        UploadMaterial();
    }
}

// 光る色をそのまま光の色にし、明るさは光る強さに比例させている。
void Wall::CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
{
    if (!m_Visible || m_GlowRange <= 0.0f || m_GlowStrength <= 0.0f || m_EmissionScale <= 0.001f)
    {
        return;
    }

    const Color& emission = m_SurfaceMaterial.Emission;
    // 光る色の一番強い成分で割って色だけを取り出し、強さは別に渡している
    const float peak = (std::max)(emission.R(), (std::max)(emission.G(), emission.B()));
    if (peak <= 0.01f)
    {
        return;
    }

    ENVIRONMENT_POINT_LIGHT light{};
    light.PositionRange = Vector4(m_Position.x, m_Position.y, m_Position.z, m_GlowRange);
    light.ColorIntensity = Vector4(
        emission.R() / peak, emission.G() / peak, emission.B() / peak,
        peak * m_GlowStrength * m_EmissionScale);
    lights.push_back(light);
}

void Wall::SetSurfaceParams(const Color& params)
{
    m_SurfaceParams = params;
    m_SurfaceMaterial.Ambient = m_SurfaceParams;
    if (m_Material != nullptr)
    {
        UploadMaterial();
    }
}

void Wall::SetPortraitEyes(float openness, float gazeX, float gazeY, float glow)
{
    SetSurfaceParams(Color(openness, gazeX, gazeY, glow));
}

void Wall::SetClockHands(float hourAngle, float minuteAngle)
{
    SetSurfaceParams(Color(hourAngle, minuteAngle, 0.0f, 0.0f));
}
