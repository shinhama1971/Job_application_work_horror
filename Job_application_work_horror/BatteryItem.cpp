// ============================================================================
// ファイルの役割: 懐中電灯の電池を回復するアイテムの形・見た目・取ったときの演出を管理している。
// 主な技術: Interactableインターフェース、円柱の頂点をコードで生成、マテリアルの使い分け、時間で回る・浮く動き
// ============================================================================

#include "BatteryItem.h"
#include "Application.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"

#include <cmath>

using namespace DirectX::SimpleMath;

// 電池の形（本体の円柱・両端の金属の帯・プラス極の突起・残量を示す緑の帯）を頂点で組み立てている。
// モデルファイルを使わず、円柱を重ねて作っている。
void BatteryItem::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();

    // 中心の高さ・半分の高さ・半径を指定して、側面と上下のふたを持つ円柱を1つ追加している。
    const auto addCylinder = [this](
        float centerY,
        float halfHeight,
        float radius,
        const Color& color)
    {
        // 円周を20分割している（小さいアイテムなので、これで十分丸く見える）
        constexpr unsigned int segments = 20;
        constexpr float circle = 6.28318530718f;
        const unsigned int sideBase = static_cast<unsigned int>(m_Vertices.size());

        // 側面：分割した角度ごとに、下の頂点と上の頂点を1組ずつ置いている（法線は外向き）
        for (unsigned int segment = 0; segment <= segments; ++segment)
        {
            const float angle = circle * static_cast<float>(segment) /
                static_cast<float>(segments);
            const float x = std::cos(angle) * radius;
            const float z = std::sin(angle) * radius;
            const Vector3 normal(std::cos(angle), 0.0f, std::sin(angle));
            const float u = static_cast<float>(segment) /
                static_cast<float>(segments);

            m_Vertices.push_back({ Vector3(x, centerY - halfHeight, z), normal,
                color, Vector2(u, 1.0f) });
            m_Vertices.push_back({ Vector3(x, centerY + halfHeight, z), normal,
                color, Vector2(u, 0.0f) });
        }

        // 側面：隣り合う2組の頂点で四角形（三角形2つ）を作っている
        for (unsigned int segment = 0; segment < segments; ++segment)
        {
            const unsigned int base = sideBase + segment * 2;
            const unsigned int sideIndices[] =
            {
                base, base + 1, base + 2,
                base + 2, base + 1, base + 3
            };
            m_Indices.insert(m_Indices.end(), sideIndices, sideIndices + 6);
        }

        // 上下のふたの中心の頂点
        const unsigned int bottomCenter = static_cast<unsigned int>(m_Vertices.size());
        m_Vertices.push_back({ Vector3(0.0f, centerY - halfHeight, 0.0f),
            Vector3(0.0f, -1.0f, 0.0f), color, Vector2(0.5f, 0.5f) });
        const unsigned int topCenter = static_cast<unsigned int>(m_Vertices.size());
        m_Vertices.push_back({ Vector3(0.0f, centerY + halfHeight, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f), color, Vector2(0.5f, 0.5f) });

        // ふたの縁の頂点。法線を上下に向けるため、側面とは別に頂点を置いている（角が丸く見えないように）
        const unsigned int capBase = static_cast<unsigned int>(m_Vertices.size());
        for (unsigned int segment = 0; segment <= segments; ++segment)
        {
            const float angle = circle * static_cast<float>(segment) /
                static_cast<float>(segments);
            const float x = std::cos(angle) * radius;
            const float z = std::sin(angle) * radius;
            const Vector2 uv(x / (radius * 2.0f) + 0.5f,
                z / (radius * 2.0f) + 0.5f);
            m_Vertices.push_back({ Vector3(x, centerY - halfHeight, z),
                Vector3(0.0f, -1.0f, 0.0f), color, uv });
            m_Vertices.push_back({ Vector3(x, centerY + halfHeight, z),
                Vector3(0.0f, 1.0f, 0.0f), color, uv });
        }

        // ふた：中心と縁の2頂点で扇形の三角形を作っている。上と下で頂点の順番を逆にし、どちらも外側を向く面にしている
        for (unsigned int segment = 0; segment < segments; ++segment)
        {
            const unsigned int rim = capBase + segment * 2;
            const unsigned int capIndices[] =
            {
                bottomCenter, rim + 2, rim,
                topCenter, rim + 1, rim + 3
            };
            m_Indices.insert(m_Indices.end(), capIndices, capIndices + 6);
        }
    };

    // 色はマテリアルで付けるため、頂点の色は白にしている
    const Color white(1.0f, 1.0f, 1.0f, 1.0f);
    // 本体（電池の筒）
    addCylinder(0.0f, 0.56f, 0.235f, white);
    m_BodyIndexCount = static_cast<unsigned int>(m_Indices.size());

    // 両端の金属の帯と、プラス極の突起。描くときにマテリアルを変えるため、インデックスの範囲を覚えている
    m_MetalIndexStart = m_BodyIndexCount;
    addCylinder(-0.60f, 0.045f, 0.245f, white);
    addCylinder(0.60f, 0.045f, 0.245f, white);
    addCylinder(0.685f, 0.040f, 0.115f, white);
    m_MetalIndexCount = static_cast<unsigned int>(m_Indices.size()) -
        m_MetalIndexStart;

    // 残量を示す緑の帯（少し外側に出して、本体の上に重ねている）
    m_ChargeIndexStart = static_cast<unsigned int>(m_Indices.size());
    addCylinder(0.24f, 0.075f, 0.246f, white);
    m_ChargeIndexCount = static_cast<unsigned int>(m_Indices.size()) -
        m_ChargeIndexStart;
}

// 頂点バッファ・シェーダー・3種類のマテリアルを作っている
void BatteryItem::Init()
{
    BuildGeometry();
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // 拡散色から環境光の色（拡散色の32%）を作り、マテリアルを1つ作る関数
    const auto createMaterial = [](
        const Color& diffuse,
        const Color& specular,
        const Color& emission,
        float shininess)
    {
        auto material = std::make_unique<Material>();
        MATERIAL data{};
        data.Ambient = Color(
            diffuse.x * 0.32f,
            diffuse.y * 0.32f,
            diffuse.z * 0.32f,
            diffuse.w);
        data.Diffuse = diffuse;
        data.Specular = specular;
        data.Emission = emission;
        data.Shininess = shininess;
        data.TextureEnable = FALSE;
        material->Create(data);
        return material;
    };

    // 本体：暗い緑がかった黒
    m_BodyMaterial = createMaterial(
        Color(0.055f, 0.070f, 0.060f, 1.0f),
        Color(0.28f, 0.32f, 0.28f, 1.0f),
        Color(0.002f, 0.006f, 0.003f, 1.0f), 42.0f);
    // 金属部分：明るい銀色で、光沢を強くしている
    m_MetalMaterial = createMaterial(
        Color(0.52f, 0.54f, 0.48f, 1.0f),
        Color(0.88f, 0.90f, 0.82f, 1.0f),
        Color(0.008f, 0.009f, 0.006f, 1.0f), 76.0f);
    // 残量の帯：緑色でわずかに自ら光らせ、暗い部屋でも見つけやすくしている
    m_ChargeMaterial = createMaterial(
        Color(0.10f, 0.82f, 0.28f, 1.0f),
        Color(0.50f, 1.0f, 0.60f, 1.0f),
        Color(0.035f, 0.28f, 0.075f, 1.0f), 54.0f);

    // 置き場所の高さを、上下に浮く動きの中心として覚えている
    m_Scale = Vector3(4.2f, 4.2f, 4.2f);
    m_BaseY = m_Position.y;
    m_AnimationTime = 0.0f;
}

// 拾われるまで、ゆっくり回りながら上下に浮かせている（アイテムだと分かりやすくするため）
void BatteryItem::Update()
{
    if (!m_IsActive || m_IsCollected) return;

    const float deltaTime = Application::GetDeltaTime();
    m_AnimationTime += deltaTime;
    m_Rotation.y += 1.32f * deltaTime;
    m_Position.y = m_BaseY + std::sin(m_AnimationTime * 2.4f) * 0.38f;
}

// 調べたときの処理：懐中電灯の電池を回復し、取った音・画面の光・振動で知らせている
// 電池が満タンのときは拾わない（後で必要になったときのために残している）
void BatteryItem::Interact(Player& player)
{
    if (!m_IsActive || m_IsCollected || player.GetBattery() >= 100.0f)
    {
        return;
    }

    player.AddBattery(m_RecoverValue);
    m_IsCollected = true;
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_PICKUP);
    Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(
        0.62f, 0.34f);
    Input::SetVibration(6, 0.13f);
}

// 回転・浮き上がりに加えて、大きさをわずかに脈打たせて描いている
void BatteryItem::Draw(Camera* cam)
{
    if (!m_IsActive || m_IsCollected) return;

    cam->SetCamera();

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    const Matrix translation = Matrix::CreateTranslation(m_Position);
    const float pulse = 1.0f +
        (std::sin(m_AnimationTime * 3.0f) * 0.5f + 0.5f) * 0.035f;
    const Matrix scale = Matrix::CreateScale(
        m_Scale.x * pulse, m_Scale.y * pulse, m_Scale.z * pulse);
    Matrix world = scale * rotation * translation;
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();

    // 本体・金属・残量の帯を、それぞれのマテリアルで描き分けている
    m_BodyMaterial->SetGPU();
    context->DrawIndexed(m_BodyIndexCount, 0, 0);
    m_MetalMaterial->SetGPU();
    context->DrawIndexed(m_MetalIndexCount, m_MetalIndexStart, 0);
    m_ChargeMaterial->SetGPU();
    context->DrawIndexed(m_ChargeIndexCount, m_ChargeIndexStart, 0);
}

// 頂点データとマテリアルを解放している
void BatteryItem::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_BodyMaterial.reset();
    m_MetalMaterial.reset();
    m_ChargeMaterial.reset();
}
