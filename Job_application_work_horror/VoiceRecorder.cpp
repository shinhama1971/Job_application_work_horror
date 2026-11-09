// ============================================================================
// ファイルの役割: 1面のボイスレコーダーの見た目と、調べたときの処理（説明は VoiceRecorder.h を参照）。
// ============================================================================

#include "VoiceRecorder.h"
#include "Application.h"
#include "Game.h"
#include "Input.h"

#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 聞く前のランプ（赤）と、聞いた後のランプ（緑）の色
    const Color UnplayedLampColor(1.0f, 0.10f, 0.06f, 1.0f);
    const Color PlayedLampColor(0.16f, 0.95f, 0.30f, 1.0f);
}

// 箱を1つ足している。壁（Wall）と同じく、表と裏の両方の順番で三角形を並べ、どちらから見ても欠けないようにしている
void VoiceRecorder::AppendBox(const Vector3& center, const Vector3& size)
{
    const Vector3 h = size * 0.5f;
    const Color white(1.0f, 1.0f, 1.0f, 1.0f);
    const auto addFace = [this, &white](const std::array<Vector3, 4>& positions, const Vector3& normal)
    {
        const unsigned int base = static_cast<unsigned int>(m_Vertices.size());
        const std::array<Vector2, 4> uvs =
        {
            Vector2(0.0f, 1.0f), Vector2(0.0f, 0.0f), Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f)
        };
        for (size_t i = 0; i < positions.size(); ++i)
        {
            VERTEX_3D vertex{};
            vertex.position = positions[i];
            vertex.normal = normal;
            vertex.color = white;
            vertex.uv = uvs[i];
            m_Vertices.push_back(vertex);
        }
        const unsigned int faceIndices[] =
        {
            base + 0, base + 1, base + 2,
            base + 2, base + 1, base + 3,
            base + 2, base + 1, base + 0,
            base + 3, base + 1, base + 2
        };
        m_Indices.insert(m_Indices.end(), faceIndices, faceIndices + 12);
    };
    const Vector3 c = center;
    // 前・後ろ・右・左・上・下の6面
    addFace({ c + Vector3(-h.x, -h.y, h.z), c + Vector3(-h.x, h.y, h.z),
              c + Vector3(h.x, -h.y, h.z), c + Vector3(h.x, h.y, h.z) }, Vector3(0.0f, 0.0f, 1.0f));
    addFace({ c + Vector3(h.x, -h.y, -h.z), c + Vector3(h.x, h.y, -h.z),
              c + Vector3(-h.x, -h.y, -h.z), c + Vector3(-h.x, h.y, -h.z) }, Vector3(0.0f, 0.0f, -1.0f));
    addFace({ c + Vector3(h.x, -h.y, h.z), c + Vector3(h.x, h.y, h.z),
              c + Vector3(h.x, -h.y, -h.z), c + Vector3(h.x, h.y, -h.z) }, Vector3(1.0f, 0.0f, 0.0f));
    addFace({ c + Vector3(-h.x, -h.y, -h.z), c + Vector3(-h.x, h.y, -h.z),
              c + Vector3(-h.x, -h.y, h.z), c + Vector3(-h.x, h.y, h.z) }, Vector3(-1.0f, 0.0f, 0.0f));
    addFace({ c + Vector3(-h.x, h.y, h.z), c + Vector3(-h.x, h.y, -h.z),
              c + Vector3(h.x, h.y, h.z), c + Vector3(h.x, h.y, -h.z) }, Vector3(0.0f, 1.0f, 0.0f));
    addFace({ c + Vector3(-h.x, -h.y, -h.z), c + Vector3(-h.x, -h.y, h.z),
              c + Vector3(h.x, -h.y, -h.z), c + Vector3(h.x, -h.y, h.z) }, Vector3(0.0f, -1.0f, 0.0f));
}

// 手のひらほどの録音機（幅4.4・高さ1.2・奥行き2.6。置き場所の点が底の中心）。
// 上の左にスピーカーの網、右に3つのボタン、手前の右の角に録音中を示すランプを付けている
void VoiceRecorder::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();
    const auto beginPart = [this](Part part)
    {
        m_PartStart[part] = static_cast<unsigned int>(m_Indices.size());
    };
    const auto endPart = [this](Part part)
    {
        m_PartCount[part] = static_cast<unsigned int>(m_Indices.size()) - m_PartStart[part];
    };

    beginPart(Body);
    AppendBox(Vector3(0.0f, 0.6f, 0.0f), Vector3(4.4f, 1.2f, 2.6f));
    endPart(Body);

    beginPart(Grille);
    AppendBox(Vector3(-0.95f, 1.22f, 0.15f), Vector3(2.0f, 0.06f, 1.9f));
    endPart(Grille);

    beginPart(Button);
    for (int i = 0; i < 3; ++i)
    {
        AppendBox(Vector3(0.75f + static_cast<float>(i) * 0.55f, 1.27f, 0.45f), Vector3(0.42f, 0.16f, 0.7f));
    }
    endPart(Button);

    beginPart(Lamp);
    AppendBox(Vector3(1.75f, 1.26f, -0.85f), Vector3(0.36f, 0.14f, 0.36f));
    endPart(Lamp);
}

void VoiceRecorder::Init()
{
    BuildGeometry();
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // 金属でない物のマテリアルを1つ作る関数（PBRの粗さを指定している）
    const auto createMaterial = [](const Color& diffuse, const Color& emission, float roughness)
    {
        MATERIAL data{};
        data.Diffuse = diffuse;
        data.Ambient = Color(diffuse.x * 0.32f, diffuse.y * 0.32f, diffuse.z * 0.32f, 1.0f);
        data.Specular = Color(0.30f, 0.30f, 0.30f, 1.0f);
        data.Emission = emission;
        data.Shininess = 40.0f;
        data.TextureEnable = FALSE;
        data.Metallic = 0.0f;
        data.Roughness = roughness;
        auto material = std::make_unique<Material>();
        material->Create(data);
        return material;
    };
    const Color noEmission(0.0f, 0.0f, 0.0f, 1.0f);
    // 本体：使い込んだ黒っぽい樹脂。スピーカーの網：さらに暗いざらざらの面。ボタン：灰色の樹脂
    m_BodyMaterial = createMaterial(Color(0.060f, 0.062f, 0.066f, 1.0f), noEmission, 0.46f);
    m_GrilleMaterial = createMaterial(Color(0.020f, 0.021f, 0.022f, 1.0f), noEmission, 0.85f);
    m_ButtonMaterial = createMaterial(Color(0.22f, 0.22f, 0.21f, 1.0f), noEmission, 0.55f);
    // ランプ：光る色は描くたびに点滅に合わせて変えている
    m_LampMaterial = createMaterial(Color(0.30f, 0.04f, 0.03f, 1.0f), noEmission, 0.25f);

    m_BlinkTime = 0.0f;
}

// 聞く前は、約1.6秒ごとに短く光る（録音の残っている機械の待機の点滅）。聞いた後は緑で弱く点いたまま
float VoiceRecorder::GetLampLevel() const
{
    if (m_Played)
    {
        return 0.35f;
    }
    const float phase = std::fmod(m_BlinkTime, 1.6f);
    return phase < 0.55f ? std::sin(phase / 0.55f * 3.14159265f) : 0.0f;
}

void VoiceRecorder::Update()
{
    m_BlinkTime += Application::GetDeltaTime();
}

// 調べたときの処理：再生ボタンを押した音（懐中電灯のスイッチの音を低くしたもの）と小さな振動で知らせ、字幕は Scene に任せている
void VoiceRecorder::Interact(Player& player)
{
    (void)player;
    if (m_Played)
    {
        return;
    }
    m_Played = true;
    m_PlayRequested = true;
    Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, m_Position, 0.62f, 0.9f);
    Input::SetVibration(3, 0.06f);
}

void VoiceRecorder::Draw(Camera* cam)
{
    cam->SetCamera();
    Matrix world = Matrix::CreateFromYawPitchRoll(m_Rotation.y, 0.0f, 0.0f) *
        Matrix::CreateTranslation(m_Position);
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();

    m_BodyMaterial->SetGPU();
    context->DrawIndexed(m_PartCount[Body], m_PartStart[Body], 0);
    m_GrilleMaterial->SetGPU();
    context->DrawIndexed(m_PartCount[Grille], m_PartStart[Grille], 0);
    m_ButtonMaterial->SetGPU();
    context->DrawIndexed(m_PartCount[Button], m_PartStart[Button], 0);

    // ランプは点滅の明るさを光る色に入れて描いている（暗い部屋で、光る点として目に入るようにしている）
    const float level = GetLampLevel();
    const Color lampColor = m_Played ? PlayedLampColor : UnplayedLampColor;
    MATERIAL lamp{};
    lamp.Diffuse = Color(lampColor.x * 0.3f, lampColor.y * 0.3f, lampColor.z * 0.3f, 1.0f);
    lamp.Ambient = Color(0.02f, 0.02f, 0.02f, 1.0f);
    lamp.Specular = Color(0.5f, 0.5f, 0.5f, 1.0f);
    lamp.Emission = Color(lampColor.x * (0.08f + 1.6f * level), lampColor.y * (0.08f + 1.6f * level),
        lampColor.z * (0.08f + 1.6f * level), 1.0f);
    lamp.Shininess = 60.0f;
    lamp.TextureEnable = FALSE;
    lamp.Roughness = 0.25f;
    m_LampMaterial->SetMaterial(lamp);
    context->DrawIndexed(m_PartCount[Lamp], m_PartStart[Lamp], 0);
}

// ランプが光っている間だけ、置かれた机や木箱の面をかすかに照らす小さな点光源を出している
void VoiceRecorder::CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
{
    const float level = GetLampLevel();
    if (level <= 0.01f)
    {
        return;
    }
    const Color lampColor = m_Played ? PlayedLampColor : UnplayedLampColor;
    ENVIRONMENT_POINT_LIGHT light{};
    light.PositionRange = Vector4(m_Position.x, m_Position.y + 2.0f, m_Position.z, 18.0f);
    light.ColorIntensity = Vector4(lampColor.x, lampColor.y, lampColor.z, 1.1f * level);
    lights.push_back(light);
}

void VoiceRecorder::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_BodyMaterial.reset();
    m_GrilleMaterial.reset();
    m_ButtonMaterial.reset();
    m_LampMaterial.reset();
}
