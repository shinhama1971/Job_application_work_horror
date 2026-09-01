// ============================================================================
// ファイルの役割: 天井照明の形状、点灯状態、故障時のちらつきを管理します。
// ============================================================================

#include "Game.h"
#include "CeilingLight.h"

#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

void CeilingLight::Init()
{
    m_Vertices.reserve(48);
    m_Indices.reserve(144);

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

    const auto addBox = [&addFace](
        const Vector3& center,
        const Vector3& half,
        const Color& color)
    {
        const float left = center.x - half.x;
        const float right = center.x + half.x;
        const float bottom = center.y - half.y;
        const float top = center.y + half.y;
        const float back = center.z - half.z;
        const float front = center.z + half.z;

        addFace({ Vector3(left, bottom, front), Vector3(left, top, front),
                  Vector3(right, bottom, front), Vector3(right, top, front) },
                Vector3(0.0f, 0.0f, 1.0f), color);
        addFace({ Vector3(right, bottom, back), Vector3(right, top, back),
                  Vector3(left, bottom, back), Vector3(left, top, back) },
                Vector3(0.0f, 0.0f, -1.0f), color);
        addFace({ Vector3(right, bottom, front), Vector3(right, top, front),
                  Vector3(right, bottom, back), Vector3(right, top, back) },
                Vector3(1.0f, 0.0f, 0.0f), color);
        addFace({ Vector3(left, bottom, back), Vector3(left, top, back),
                  Vector3(left, bottom, front), Vector3(left, top, front) },
                Vector3(-1.0f, 0.0f, 0.0f), color);
        addFace({ Vector3(left, top, front), Vector3(left, top, back),
                  Vector3(right, top, front), Vector3(right, top, back) },
                Vector3(0.0f, 1.0f, 0.0f), color);
        addFace({ Vector3(left, bottom, back), Vector3(left, bottom, front),
                  Vector3(right, bottom, back), Vector3(right, bottom, front) },
                Vector3(0.0f, -1.0f, 0.0f), color);
    };

    addBox(Vector3::Zero, Vector3(0.50f, 0.14f, 0.50f),
           Color(0.16f, 0.17f, 0.16f, 1.0f));
    m_BodyIndexCount = m_Indices.size();

    addBox(Vector3(0.0f, -0.18f, 0.0f), Vector3(0.40f, 0.055f, 0.34f),
           Color(0.72f, 0.70f, 0.62f, 1.0f));

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    MATERIAL body{};
    body.Diffuse = Color(0.42f, 0.44f, 0.41f, 1.0f);
    body.Specular = Color(0.12f, 0.12f, 0.10f, 1.0f);
    body.Shininess = 12.0f;
    body.TextureEnable = FALSE;
    m_BodyMaterial = std::make_unique<Material>();
    m_BodyMaterial->Create(body);

    MATERIAL panel{};
    panel.Diffuse = Color(0.25f, 0.24f, 0.20f, 1.0f);
    panel.TextureEnable = FALSE;
    m_LightMaterial = std::make_unique<Material>();
    m_LightMaterial->Create(panel);
}

void CeilingLight::Update()
{
    constexpr float deltaTime = 1.0f / 60.0f;
    m_Time += deltaTime;

    const bool powerRestored =
        Core::Game::GetInstance()->IsPowerRestored();

    if (powerRestored && !m_WasPowerRestored)
    {
        m_PowerOnTimer = 0.0f;
    }
    else if (powerRestored)
    {
        m_PowerOnTimer += deltaTime;
    }
    else
    {
        m_PowerOnTimer = 0.0f;
    }

    m_WasPowerRestored = powerRestored;

    float targetBrightness = 0.0f;
    if (powerRestored)
    {
        // Start fixtures one after another, then make each fluorescent tube
        // stutter briefly before it reaches full output.
        const float startupDelay =
            std::fmod(std::fabs(m_FlickerOffset), 5.0f) * 0.10f;
        const float startupTime = m_PowerOnTimer - startupDelay;

        if (startupTime >= 0.0f && startupTime < 0.58f)
        {
            const int pulse = static_cast<int>(startupTime * 24.0f);
            const bool tubeIsOn =
                (pulse == 0) || (pulse == 3) || (pulse == 4) ||
                (pulse >= 7 && (pulse % 3) != 1);
            const float warmup =
                0.40f + (startupTime / 0.58f) * 0.60f;
            targetBrightness = tubeIsOn ? warmup : 0.025f;
        }
        else if (startupTime >= 0.58f)
        {
            // Each old fluorescent fixture has a slightly different ballast.
            // A rare voltage dip breaks the perfectly constant game-light look.
            const float fixtureWear = std::fmod(
                std::fabs(m_FlickerOffset) * 0.371f + 0.17f,
                1.0f);
            const float electricalHum =
                std::sin((m_Time + m_FlickerOffset) *
                    (15.0f + fixtureWear * 4.0f)) * 0.012f;
            const float highFrequencyBuzz =
                std::sin((m_Time * 43.0f) + m_FlickerOffset * 7.0f) * 0.004f;
            const float dipCycle = 8.5f + fixtureWear * 5.5f;
            const float dipPhase = std::fmod(
                m_Time + std::fabs(m_FlickerOffset) * 1.91f,
                dipCycle);
            const bool rareVoltageDip =
                dipPhase < 0.045f + fixtureWear * 0.035f;
            const float voltage = rareVoltageDip
                ? 0.28f + fixtureWear * 0.18f
                : 1.0f;
            targetBrightness =
                (1.0f + electricalHum + highFrequencyBuzz) * voltage;
        }
    }
    else if (m_IsEmergencyLight)
    {
        const float flickerTime = m_Time + m_FlickerOffset;
        const float unstable =
            std::sin(flickerTime * 13.0f) *
            std::sin(flickerTime * 29.0f);
        const bool shortBlackout =
            std::fmod(flickerTime, 4.3f) < 0.12f;
        targetBrightness = shortBlackout
            ? 0.015f
            : 0.22f + (unstable + 1.0f) * 0.08f;
    }

    // A faulted fluorescent tube still provides occasional guidance, but its
    // ballast drops out for irregular intervals. Explicit event flickers are
    // applied afterwards so scripted scares remain readable.
    if (m_IsFaulted && powerRestored)
    {
        const float faultTime = m_Time + std::fabs(m_FlickerOffset) * 0.73f;
        const float faultCycle = 3.1f +
            std::fmod(std::fabs(m_FlickerOffset) * 0.41f, 1.4f);
        const float faultPhase = std::fmod(faultTime, faultCycle);
        const float ballastNoise =
            std::sin(faultTime * 17.0f) * std::sin(faultTime * 31.0f);
        const bool longDropout = faultPhase < 0.36f;
        const bool unstableDropout =
            faultPhase < 1.15f && ballastNoise < -0.32f;
        targetBrightness = (longDropout || unstableDropout)
            ? 0.018f
            : targetBrightness * 0.58f;
    }

    if (m_EventFlickerTimer > 0.0f)
    {
        m_EventFlickerTimer = (std::max)(
            0.0f,
            m_EventFlickerTimer - deltaTime);
        const float elapsed =
            m_EventFlickerDuration - m_EventFlickerTimer;
        const int pulse = static_cast<int>(elapsed * 26.0f);
        const bool tubeOn =
            (pulse == 0) || (pulse == 2) ||
            (pulse >= 4 && (pulse % 3) != 1);
        const float flareBrightness =
            0.38f + m_EventFlickerStrength * 0.48f;
        targetBrightness = tubeOn
            ? (std::max)(targetBrightness, flareBrightness)
            : 0.008f;
    }

    if (m_IsForcedOff)
    {
        targetBrightness = 0.0f;
    }

    const bool startingUp = powerRestored && m_PowerOnTimer < 1.1f;
    const bool voltageDip =
        powerRestored && !startingUp && targetBrightness < 0.75f;
    const float response = powerRestored
        ? (startingUp ? 0.48f : (voltageDip ? 0.24f : 0.08f))
        : 0.32f;
    m_Brightness += (targetBrightness - m_Brightness) * response;
}

void CeilingLight::Draw(Camera* camera)
{
    camera->SetCamera();

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    const Matrix scale = Matrix::CreateScale(m_Scale);
    const Matrix translation = Matrix::CreateTranslation(m_Position);
    Matrix world = scale * rotation * translation;
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();

    m_BodyMaterial->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_BodyIndexCount), 0, 0);

    MATERIAL panel{};
    panel.Diffuse = Color(0.22f, 0.21f, 0.18f, 1.0f);
    if (Core::Game::GetInstance()->IsPowerRestored())
    {
        if (m_IsFaulted)
        {
            panel.Emission = Color(
                0.52f * m_Brightness,
                0.62f * m_Brightness,
                0.45f * m_Brightness,
                1.0f);
        }
        else
        {
            panel.Emission = Color(
                0.68f * m_Brightness,
                0.76f * m_Brightness,
                0.88f * m_Brightness,
                1.0f);
        }
    }
    else
    {
        panel.Emission = Color(
            0.75f * m_Brightness,
            0.025f * m_Brightness,
            0.015f * m_Brightness,
            1.0f);
    }
    panel.TextureEnable = FALSE;
    m_LightMaterial->SetMaterial(panel);
    m_LightMaterial->SetGPU();

    const UINT panelIndexCount =
        static_cast<UINT>(m_Indices.size() - m_BodyIndexCount);
    context->DrawIndexed(
        panelIndexCount,
        static_cast<UINT>(m_BodyIndexCount),
        0);
}

void CeilingLight::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}
