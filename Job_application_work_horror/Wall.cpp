// ============================================================================
// ファイルの役割: 箱の形をした壁（と、同じ形を使う棚・配管・目印などの小物）の形・色・当たり判定・影の有無を管理している。
// 主な技術: コードで作る箱のメッシュ、AABBの当たり判定、一番近い面への押し戻し、拡大・回転・移動の行列
// ============================================================================

#include "Wall.h"
#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "ShadowMap.h"
#include "Renderer.h"
#include "PropModel.h"
#include "Utility.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

using namespace DirectX::SimpleMath;

namespace
{
    // 水面と鏡に箱として映す小物の大きさ（上から見た向きでの半分の大きさ）：低すぎる物（床の縁取り・板）、
    // 大きすぎる物（床や天井のような板。部屋の形と重なってちらつく）、細すぎる物は映さない
    constexpr float ReflectionProxyMinHeight = 3.0f;
    constexpr float ReflectionProxyMaxWidth = 40.0f;
    constexpr float ReflectionProxyMinWidth = 0.8f;

    // --- 普段の色：くすんだ灰色、光沢は弱い
    const Color DefaultDiffuse(0.34f, 0.36f, 0.33f, 1.0f);
    const Color DefaultAmbient(0.03f, 0.035f, 0.03f, 1.0f);
    const Color DefaultSpecular(0.04f, 0.04f, 0.04f, 1.0f);
    constexpr float DefaultShininess = 4.0f;
    // 色を指定したとき（SetAppearance）の環境光と映り込みの色、光沢の下限
    const Color AppearanceAmbient(0.025f, 0.025f, 0.025f, 1.0f);
    const Color AppearanceSpecular(0.10f, 0.11f, 0.10f, 1.0f);
    constexpr float MinimumShininess = 1.0f;
    // 崩れて消える・現れる秒数の下限（0で割らないため）
    constexpr float MinimumDissolveSeconds = 0.01f;
    // これより透けていない物だけを、水面と鏡に箱として映す
    constexpr float OpaqueAlpha = 0.999f;
    // 光る色の一番強い成分がこれ以下なら、周りを照らさない
    constexpr float MinimumGlowPeak = 0.01f;

}

// 原点を中心にした1x1x1の箱（6面）を作り、灰色のコンクリートのマテリアルを用意している
void Wall::Init()
{
    BuildBoxMesh();
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_BoxVertices = m_Vertices;
    m_BoxIndices = m_Indices;
    m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");
    m_NaturalSurface = NaturalSurface::None;

    m_Material = std::make_unique<Material>();
    // 普段の色：くすんだ灰色、光沢は弱い
    m_SurfaceMaterial.Diffuse = DefaultDiffuse;
    m_SurfaceMaterial.Ambient = DefaultAmbient;
    m_SurfaceMaterial.Specular = DefaultSpecular;
    m_SurfaceMaterial.Emission = Color(0.0f, 0.0f, 0.0f, 1.0f);
    m_SurfaceMaterial.Shininess = DefaultShininess;
    m_SurfaceMaterial.TextureEnable = FALSE;
    m_Material->CreateRequired(m_SurfaceMaterial);
    m_UnlitPartMaterial = std::make_unique<Material>();
    m_UnlitPartMaterial->CreateRequired(m_SurfaceMaterial);
}

// 1x1x1の箱の6面（前・後ろ・右・左・上・下）の頂点とインデックスを作っている
void Wall::BuildBoxMesh()
{
    m_Vertices.clear();
    m_Indices.clear();
    // 6面 × 4頂点、6面 × 表と裏の2枚 × 2つの三角形 × 3
    constexpr size_t BoxVertexCount = 24;
    constexpr size_t BoxIndexCount = 72;
    m_Vertices.reserve(BoxVertexCount);
    m_Indices.reserve(BoxIndexCount);

    AddBoxFace(
        { Vector3(-0.5f, -0.5f, 0.5f), Vector3(-0.5f, 0.5f, 0.5f),
          Vector3(0.5f, -0.5f, 0.5f), Vector3(0.5f, 0.5f, 0.5f) },
        Vector3(0.0f, 0.0f, 1.0f)
    );
    AddBoxFace(
        { Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, -0.5f),
          Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, 0.5f, -0.5f) },
        Vector3(0.0f, 0.0f, -1.0f)
    );
    AddBoxFace(
        { Vector3(0.5f, -0.5f, 0.5f), Vector3(0.5f, 0.5f, 0.5f),
          Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, -0.5f) },
        Vector3(1.0f, 0.0f, 0.0f)
    );
    AddBoxFace(
        { Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, 0.5f, -0.5f),
          Vector3(-0.5f, -0.5f, 0.5f), Vector3(-0.5f, 0.5f, 0.5f) },
        Vector3(-1.0f, 0.0f, 0.0f)
    );
    AddBoxFace(
        { Vector3(-0.5f, 0.5f, 0.5f), Vector3(-0.5f, 0.5f, -0.5f),
          Vector3(0.5f, 0.5f, 0.5f), Vector3(0.5f, 0.5f, -0.5f) },
        Vector3(0.0f, 1.0f, 0.0f)
    );
    AddBoxFace(
        { Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, -0.5f, 0.5f),
          Vector3(0.5f, -0.5f, -0.5f), Vector3(0.5f, -0.5f, 0.5f) },
        Vector3(0.0f, -1.0f, 0.0f)
    );
}

// 四角形の面を1枚追加している
void Wall::AddBoxFace(const std::array<Vector3, 4>& positions, const Vector3& normal)
{
    const Color vertexColor(1.0f, 1.0f, 1.0f, 1.0f);
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
        std::begin(faceIndices),
        std::end(faceIndices)
    );
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
    m_DissolveSpeed = 1.0f / (std::max)(seconds, MinimumDissolveSeconds);
}

void Wall::DissolveOut(float seconds)
{
    if (!m_Visible)
    {
        return;
    }
    m_DissolveTarget = 1.0f;
    m_DissolveSpeed = 1.0f / (std::max)(seconds, MinimumDissolveSeconds);
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
bool Wall::GetReflectionProxy(ReflectionProxy& proxy) const
{
    if (!m_Visible || m_Dissolve > 0.0f || m_IsMirror ||
        m_SurfaceMaterial.WeatheringSurface || m_SurfaceMaterial.Diffuse.w < OpaqueAlpha)
    {
        return false;
    }
    // 形が箱から大きく外れる見た目のモデルは、1つの箱で映すと水面に四角い箱が浮かんで見える。
    // 箱としては映さず、画面に見えている所はSSRで本物の形を、画面の外は環境マップ（本物のモデルを撮った景色）を映す
    if (m_VisualModel == "pump" || m_VisualModel == "tank" || m_VisualModel == "debris")
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
    if (extent.y < ReflectionProxyMinHeight || extent.x > ReflectionProxyMaxWidth || extent.z > ReflectionProxyMaxWidth ||
        (std::max)(extent.x, extent.z) < ReflectionProxyMinWidth)
    {
        return false;
    }
    proxy = { m_Position, extent, m_SurfaceMaterial.Diffuse, Vector2::Zero };
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
    if (m_Breathing)
    {
        BindBreathing();
    }

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
    // 呼吸する壁も、影には膨らむ前の平らな面を描いている。膨らみは廊下側（灯りのある側）へだけ出るので、
    // 本描画の面はいつも影の面より灯りに近く、自分の影に入らない（変形した面を影に描くと、影の解像度の差で縞や欠けが出ていた）
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
    if (m_Breathing)
    {
        BindBreathing();
    }
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

// プレイヤーの円が箱にめり込んでいたら押し出している。回転は考えず、上から見た長方形として扱っている
void Wall::ResolveCollision(Vector3& position, float radius) const
{
    if (!m_Visible || !m_CollisionEnabled)
    {
        return;
    }

    if (m_Breathing && m_BreathAmplitude > 0.0f)
    {
        ResolveBreathingCollision(position, radius);
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
    m_SurfaceMaterial.Ambient = m_HasSurfaceParams ? m_SurfaceParams : AppearanceAmbient;
    m_SurfaceMaterial.Specular = AppearanceSpecular;
    m_SurfaceMaterial.Emission = emission;
    m_SurfaceMaterial.Shininess = (std::max)(shininess, MinimumShininess);
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
    if (!m_Visible || m_GlowRange <= 0.0f || m_GlowStrength <= 0.0f || m_EmissionScale <= MinGlowEmissionScale)
    {
        return;
    }

    const Color& emission = m_SurfaceMaterial.Emission;
    // 光る色の一番強い成分で割って色だけを取り出し、強さは別に渡している
    const float peak = (std::max)(emission.R(), (std::max)(emission.G(), emission.B()));
    if (peak <= MinimumGlowPeak)
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
