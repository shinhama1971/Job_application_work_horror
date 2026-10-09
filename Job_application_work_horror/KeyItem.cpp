// ============================================================================
// ファイルの役割: 1面に落ちている鍵（隠し部屋の扉の鍵・西棟の扉の鍵）の見た目と、拾ったときの処理を管理している。
// 主な技術: Interactableインターフェース、箱を組み合わせた形をコードで生成、時間で回る・浮く動き
// ============================================================================

#include "KeyItem.h"
#include "Application.h"
#include "Game.h"
#include "Input.h"

#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

// 古い真鍮の鍵の形を、箱の組み合わせで作っている（持ち手の輪・軸・歯）。
void KeyItem::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();

    const Color white(1.0f, 1.0f, 1.0f, 1.0f);
    // 中心と半分の大きさを指定して、6面の箱を追加している
    const auto addBox = [this, &white](const Vector3& center, const Vector3& half)
    {
        const std::array<Vector3, 6> normals =
        {
            Vector3(1.0f, 0.0f, 0.0f), Vector3(-1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, 0.0f, -1.0f)
        };
        for (const Vector3& normal : normals)
        {
            // 面の2本の辺の向きを、法線と直交する軸から作っている。
            const Vector3 axisU = std::abs(normal.y) > 0.5f
                ? Vector3(1.0f, 0.0f, 0.0f)
                : Vector3(0.0f, 1.0f, 0.0f);
            const Vector3 axisV = normal.Cross(axisU);
            const Vector3 faceCenter = center + normal * Vector3(half.x, half.y, half.z);
            const Vector3 u = axisU * Vector3(half.x, half.y, half.z);
            const Vector3 v = axisV * Vector3(half.x, half.y, half.z);

            const unsigned int base = static_cast<unsigned int>(m_Vertices.size());
            m_Vertices.push_back({ faceCenter - u - v, normal, white, Vector2(0.0f, 1.0f) });
            m_Vertices.push_back({ faceCenter - u + v, normal, white, Vector2(0.0f, 0.0f) });
            m_Vertices.push_back({ faceCenter + u - v, normal, white, Vector2(1.0f, 1.0f) });
            m_Vertices.push_back({ faceCenter + u + v, normal, white, Vector2(1.0f, 0.0f) });
            // 表と裏の両方の順番を持たせ、向きに関係なく表示している（Wallと同じ方針）。
            const unsigned int faceIndices[] =
            {
                base, base + 1, base + 2, base + 2, base + 1, base + 3,
                base + 2, base + 1, base, base + 3, base + 1, base + 2
            };
            m_Indices.insert(m_Indices.end(), faceIndices, faceIndices + 12);
        }
    };

    // 持ち手の輪（4本の棒で作った四角い輪）
    addBox(Vector3(-0.95f, 0.30f, 0.0f), Vector3(0.30f, 0.06f, 0.06f));
    addBox(Vector3(-0.95f, -0.30f, 0.0f), Vector3(0.30f, 0.06f, 0.06f));
    addBox(Vector3(-1.25f, 0.0f, 0.0f), Vector3(0.06f, 0.30f, 0.06f));
    addBox(Vector3(-0.65f, 0.0f, 0.0f), Vector3(0.06f, 0.30f, 0.06f));
    // 軸
    addBox(Vector3(0.25f, 0.0f, 0.0f), Vector3(0.85f, 0.06f, 0.06f));
    // 歯（長さの違う2本）
    addBox(Vector3(0.80f, -0.17f, 0.0f), Vector3(0.07f, 0.12f, 0.05f));
    addBox(Vector3(1.02f, -0.13f, 0.0f), Vector3(0.07f, 0.08f, 0.05f));
}

void KeyItem::Init()
{
    BuildGeometry();
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // くすんだ真鍮。暗い部屋でもライトを当てれば光るよう、弱い自己発光と強い鏡面反射を持たせている。
    MATERIAL material{};
    material.Diffuse = Color(0.46f, 0.34f, 0.12f, 1.0f);
    material.Ambient = Color(0.15f, 0.11f, 0.04f, 1.0f);
    material.Specular = Color(0.95f, 0.80f, 0.45f, 1.0f);
    material.Emission = Color(0.030f, 0.020f, 0.006f, 1.0f);
    material.Shininess = 64.0f;
    material.TextureEnable = FALSE;
    m_Material = std::make_unique<Material>();
    m_Material->Create(material);

    // 置き場所の高さを、上下に浮く動きの中心として覚えている
    m_Scale = Vector3(4.0f, 4.0f, 4.0f);
    m_BaseY = m_Position.y;
    m_AnimationTime = 0.0f;
}

// 拾われるまで、ゆっくり回りながら上下に浮かせている
void KeyItem::Update()
{
    if (!m_IsActive || m_IsCollected)
    {
        return;
    }
    const float deltaTime = Application::GetDeltaTime();
    m_AnimationTime += deltaTime;
    m_Rotation.y += 1.1f * deltaTime;
    m_Position.y = m_BaseY + std::sin(m_AnimationTime * 2.2f) * 0.30f;
}

// 拾ったときの処理：拾った音（少し高い音）・画面の光・振動で知らせている。扉を開けるのはSceneが担当している
void KeyItem::Interact(Player& player)
{
    (void)player;
    if (!m_IsActive || m_IsCollected)
    {
        return;
    }
    m_IsCollected = true;
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_PICKUP, 1.25f);
    Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(0.56f, 0.28f);
    Input::SetVibration(5, 0.12f);
}

// 回転と浮き沈みを反映して描いている
void KeyItem::Draw(Camera* cam)
{
    if (!m_IsActive || m_IsCollected)
    {
        return;
    }

    cam->SetCamera();
    Matrix world = Matrix::CreateScale(m_Scale) *
        Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z) *
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
void KeyItem::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Material.reset();
}
