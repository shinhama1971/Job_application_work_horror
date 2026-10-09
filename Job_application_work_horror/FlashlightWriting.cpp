// ============================================================================
// ファイルの役割: 懐中電灯で照らした部分だけ浮かび上がる、壁に書かれた文字。
// 主な技術: 半透明の板ポリゴン、懐中電灯の配光を使うピクセルシェーダー（flashlightRevealPS）、
//           視線・距離・さえぎる物による「読んでいるか」の判定
// ============================================================================

#include "FlashlightWriting.h"

#include "Application.h"
#include "Door.h"
#include "Game.h"
#include "Wall.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

void FlashlightWriting::Init()
{
    // 原点を中心にした1x1の板。法線は-Z向きで、+Z向きに見たとき文字が正しく読めるUVにしている。
    const Vector3 normal(0.0f, 0.0f, -1.0f);
    const Vector3 positions[4] =
    {
        Vector3(-0.5f, 0.5f, 0.0f), Vector3(0.5f, 0.5f, 0.0f),
        Vector3(-0.5f, -0.5f, 0.0f), Vector3(0.5f, -0.5f, 0.0f)
    };
    const Vector2 uvs[4] =
    {
        Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f),
        Vector2(0.0f, 1.0f), Vector2(1.0f, 1.0f)
    };
    m_Vertices.clear();
    for (int i = 0; i < 4; ++i)
    {
        VERTEX_3D vertex{};
        vertex.position = positions[i];
        vertex.normal = normal;
        vertex.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
        vertex.uv = uvs[i];
        m_Vertices.push_back(vertex);
    }
    // 壁（Wall）と同じく表と裏の両方の順番を持たせ、カリングの設定に左右されないようにしている。
    m_Indices = { 0, 1, 2, 2, 1, 3, 2, 1, 0, 3, 1, 2 };

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    // 照らした部分だけ文字を見せる専用のピクセルシェーダーを使っている
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/flashlightRevealPS.hlsl");

    MATERIAL material{};
    material.Diffuse = m_InkColor;
    material.TextureEnable = TRUE;
    m_Material = std::make_unique<Material>();
    m_Material->Create(material);
}

// 文字の画像を読み込んでいる（書き換え用の画像は、指定があって読み込めたときだけ使っている）
void FlashlightWriting::SetTextures(const std::string& primaryPath, const std::string& alternatePath)
{
    m_Texture.Load(primaryPath);
    m_HasAlternate = !alternatePath.empty() && m_AlternateTexture.Load(alternatePath);
    m_ShowingAlternate = false;
}

// 壁の表面に合わせて、位置・向き・大きさを決めている（法線は水平にそろえている）
void FlashlightWriting::Place(
    const Vector3& surfaceCenter,
    const Vector3& outwardNormal,
    float width,
    float height)
{
    m_Normal = outwardNormal;
    m_Normal.y = 0.0f;
    m_Normal.Normalize();
    m_Position = surfaceCenter + m_Normal * SurfaceOffset;
    // 板の法線(-Z)を、ヨー回転で outwardNormal に合わせている。
    m_Rotation = Vector3(0.0f, std::atan2(-m_Normal.x, -m_Normal.z), 0.0f);
    m_Scale = Vector3(width, height, 1.0f);
}

// 書き換え後の文字に切り替え、濃さを0から目標まで戻すことで、じわりと現している
void FlashlightWriting::ShowAlternate()
{
    if (!m_HasAlternate || m_ShowingAlternate)
    {
        return;
    }
    m_ShowingAlternate = true;
    m_Presence = 0.0f;
}

// 濃さを1秒あたり一定の量で、目標の濃さへ近づけている
void FlashlightWriting::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    m_Time += deltaTime;
    const float step = PresenceFadeSpeed * deltaTime;
    m_Presence = m_Presence < m_TargetPresence
        ? (std::min)(m_Presence + step, m_TargetPresence)
        : (std::max)(m_Presence - step, m_TargetPresence);
}

// 文字の板を描いている。濃さは透明度として、経過時間はEmissionのxに入れてシェーダーへ渡している
void FlashlightWriting::Draw(Camera* cam)
{
    if (m_Presence <= 0.001f)
    {
        return;
    }

    cam->SetCamera();
    Matrix world = Matrix::CreateScale(m_Scale) *
        Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z) *
        Matrix::CreateTranslation(m_Position);
    Renderer::SetWorldMatrix(&world);

    MATERIAL material{};
    material.Diffuse = Color(m_InkColor.R(), m_InkColor.G(), m_InkColor.B(), m_Presence);
    material.Emission = Color(m_Time, 0.0f, 0.0f, 0.0f);
    material.TextureEnable = TRUE;
    m_Material->SetMaterial(material);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    if (m_ShowingAlternate)
    {
        m_AlternateTexture.SetGPU();
    }
    else
    {
        m_Texture.SetGPU();
    }

    // 壁の上に重ねる半透明の文字なので、深度は書かずにアルファブレンドで合成している。
    Renderer::SetBlendState(BS_ALPHABLEND);
    Renderer::SetDepthEnable(false);
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
    Renderer::SetDepthEnable(true);
    Renderer::SetBlendState(BS_NONE);
}

// 読める条件：ライトが点いている・文字が十分濃い・距離が近い・視線の中心付近にある・正面に近い方向から見ている
bool FlashlightWriting::IsBeingRead(
    const Vector3& cameraPosition,
    const Vector3& cameraForward,
    bool flashlightOn) const
{
    if (!flashlightOn || m_Presence < 0.5f)
    {
        return false;
    }

    Vector3 toWriting = m_Position - cameraPosition;
    const float distance = toWriting.Length();
    if (distance > ReadDistance || distance < 0.001f)
    {
        return false;
    }
    toWriting /= distance;
    if (cameraForward.Dot(toWriting) < ReadAlignment ||
        -toWriting.Dot(m_Normal) < ReadFacing)
    {
        return false;
    }

    // 壁や閉じた扉の向こうから照らしても読めないようにしている。
    // 線分の終点は文字の少し手前にし、文字が貼られた壁自身には当たらないようにしている。
    Core::Game* game = Core::Game::GetInstance();
    const Vector3 end = m_Position + m_Normal * 2.0f;
    for (const Wall* wall : game->GetObjects<Wall>())
    {
        float hitDistance = 0.0f;
        if (wall->IntersectsInteractionSegment(cameraPosition, end, hitDistance))
        {
            return false;
        }
    }
    for (const Door* door : game->GetObjects<Door>())
    {
        if (door->BlocksSoundSegment(cameraPosition, end))
        {
            return false;
        }
    }
    return true;
}

// 頂点データを解放している
void FlashlightWriting::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}
