// ============================================================================
// ファイルの役割: 箱の形をした壁（と、同じ形を使う棚・配管・目印などの小物）の形・色・当たり判定・影の有無を管理している。
// 主な技術: コードで作る箱のメッシュ、AABBの当たり判定、一番近い面への押し戻し、拡大・回転・移動の行列
// ============================================================================

#include "Wall.h"
#include "Camera.h"
#include "Game.h"
#include "Renderer.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

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
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    // 普段の色：くすんだ灰色、光沢は弱い
    m_SurfaceMaterial.Diffuse = Color(0.34f, 0.36f, 0.33f, 1.0f);
    m_SurfaceMaterial.Ambient = Color(0.03f, 0.035f, 0.03f, 1.0f);
    m_SurfaceMaterial.Specular = Color(0.04f, 0.04f, 0.04f, 1.0f);
    m_SurfaceMaterial.Emission = Color(0.0f, 0.0f, 0.0f, 1.0f);
    m_SurfaceMaterial.Shininess = 4.0f;
    m_SurfaceMaterial.TextureEnable = FALSE;
    m_Material->Create(m_SurfaceMaterial);
}

// 動かないので、毎フレームの処理はない
void Wall::Update()
{
}

// 箱を描いている（非表示なら何もしない）
void Wall::Draw(Camera* cam)
{
    if (!m_Visible)
    {
        return;
    }

    cam->SetCamera();

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z
    );
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

    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

// 影を作るための描画（深度だけ）
void Wall::DrawShadow()
{
    if (!m_Visible || !m_CastsShadow)
    {
        return;
    }

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z);
    Matrix world = Matrix::CreateScale(m_Scale) * rotation *
        Matrix::CreateTranslation(m_Position);
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Core::Game::GetInstance()->GetShadowMap()->SetShader();
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
    m_SurfaceMaterial.Diffuse = diffuse;
    m_SurfaceMaterial.Ambient = Color(0.025f, 0.025f, 0.025f, 1.0f);
    m_SurfaceMaterial.Specular = Color(0.10f, 0.11f, 0.10f, 1.0f);
    m_SurfaceMaterial.Emission = emission;
    m_SurfaceMaterial.Shininess = (std::max)(shininess, 1.0f);
    m_SurfaceMaterial.TextureEnable = FALSE;

    if (m_Material != nullptr)
    {
        m_Material->SetMaterial(m_SurfaceMaterial);
    }
}

// 壁の古さを描く面かどうかを、マテリアルの印として変えている
void Wall::SetWeatheringSurface(bool enabled)
{
    m_SurfaceMaterial.WeatheringSurface = enabled ? TRUE : FALSE;
    if (m_Material != nullptr)
    {
        m_Material->SetMaterial(m_SurfaceMaterial);
    }
}

// 光る色をそのまま光の色にし、明るさは光る強さに比例させている。
void Wall::CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
{
    if (!m_Visible || m_GlowRange <= 0.0f || m_GlowStrength <= 0.0f)
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
        peak * m_GlowStrength);
    lights.push_back(light);
}
