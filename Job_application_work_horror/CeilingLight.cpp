// ============================================================================
// ファイルの役割: 天井照明の形・点灯の状態・故障したときのちらつきを管理している。
// 主な技術: 点光源の登録、自己発光（エミッシブ）の表現、器具ごとにずらした周期、時間で変わる蛍光灯の演出
// ============================================================================

#include "Game.h"
#include "Application.h"
#include "CeilingLight.h"

#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

// 照明器具の形（本体の箱と、下に付いた発光パネルの箱）を頂点で組み立て、マテリアルを作っている。
void CeilingLight::Init()
{
    m_Vertices.reserve(48);
    m_Indices.reserve(144);

    // 四角形の面を1枚追加している。表と裏の両方から見えるよう、三角形を両面分（12個のインデックス）入れている
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

    // 中心と半分の大きさを指定して、6面の箱を追加している
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

    // 本体の箱。描くときにマテリアルを変えるため、ここまでのインデックス数を覚えている
    addBox(Vector3::Zero, Vector3(0.50f, 0.14f, 0.50f),
           Color(0.16f, 0.17f, 0.16f, 1.0f));
    m_BodyIndexCount = m_Indices.size();

    // 本体の下に付いた発光パネル
    addBox(Vector3(0.0f, -0.18f, 0.0f), Vector3(0.40f, 0.055f, 0.34f),
           Color(0.72f, 0.70f, 0.62f, 1.0f));

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // 本体のマテリアル：くすんだ灰色の金属
    MATERIAL body{};
    body.Diffuse = Color(0.42f, 0.44f, 0.41f, 1.0f);
    body.Specular = Color(0.12f, 0.12f, 0.10f, 1.0f);
    body.Shininess = 12.0f;
    body.TextureEnable = FALSE;
    m_BodyMaterial = std::make_unique<Material>();
    m_BodyMaterial->Create(body);

    // 発光パネルのマテリアル。光る色は明るさに合わせてDrawで毎回変えている
    MATERIAL panel{};
    panel.Diffuse = Color(0.25f, 0.24f, 0.20f, 1.0f);
    panel.TextureEnable = FALSE;
    m_LightMaterial = std::make_unique<Material>();
    m_LightMaterial->Create(panel);
}

// 電力の状態と演出から、このフレームの照明の明るさを決めている。
// 目標の明るさを決めてから、実際の明るさを少しずつ近づけている（急に変わらないように）。
void CeilingLight::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    m_Time += deltaTime;

    // 電力が復旧した瞬間から、点灯までの経過時間を数えている
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
        // 照明を順番に点け、各蛍光灯が完全に点く直前に短く明滅させている。
        // 一斉に点くのを避け、古い設備が復旧するときの不安定さを出している。
        const float startupDelay =
            std::fmod(std::fabs(m_FlickerOffset), 5.0f) * 0.10f;
        const float startupTime = m_PowerOnTimer - startupDelay;

        if (startupTime >= 0.0f && startupTime < 0.58f)
        {
            // 1/24秒ごとに、点いているか消えているかを決まった並びで切り替えている（点き始めの蛍光灯の明滅）
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
            // 器具ごとに安定器の個体差を持たせ、まれに起きる電圧の低下を再現している。
            // 一定すぎる光を避け、古い施設らしさを出している。
            const float fixtureWear = std::fmod(
                std::fabs(m_FlickerOffset) * 0.371f + 0.17f,
                1.0f);
            // 電源のうなりによる小さな明るさの揺れ（器具ごとに周期を変えている）
            const float electricalHum =
                std::sin((m_Time + m_FlickerOffset) *
                    (15.0f + fixtureWear * 4.0f)) * 0.012f;
            const float highFrequencyBuzz =
                std::sin((m_Time * 43.0f) + m_FlickerOffset * 7.0f) * 0.004f;
            // 器具ごとの周期で、まれに短く暗くなる（電圧の低下）
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
    // 停電中の非常灯：赤く暗い光が不安定に揺れ、4.3秒ごとに一瞬消えている
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

    // 故障した蛍光灯も時々点いて進む方向を示すが、不規則に消えるようにしている。
    // この自然な揺らぎの後からイベント用の明滅を重ね、怖い演出を確実に見せている。
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

    // イベント用の明滅（影が出たときなど）。決まった並びで点滅させ、点いている間は普段より明るくしている
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

    // 演出で強制的に消しているときは、何があっても真っ暗にしている
    if (m_IsForcedOff)
    {
        targetBrightness = 0.0f;
    }

    // 明るさの追いつく速さ：点き始めと電圧低下は速く、普段はゆっくり変えている
    const bool startingUp = powerRestored && m_PowerOnTimer < 1.1f;
    const bool voltageDip =
        powerRestored && !startingUp && targetBrightness < 0.75f;
    const float response = powerRestored
        ? (startingUp ? 0.48f : (voltageDip ? 0.24f : 0.08f))
        : 0.32f;
    // フレームレートが変わっても同じ速さで近づくよう、60fpsを基準にした指数で補正している
    const float deltaResponse = 1.0f - std::pow(
        1.0f - response, deltaTime * 60.0f);
    m_Brightness +=
        (targetBrightness - m_Brightness) * deltaResponse;
}

// 本体を描き、続けて発光パネルを明るさに応じた色で光らせて描いている
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

    // 発光パネルの光る色：通常は青白、故障した灯は黄緑がかった色、停電中は赤（非常灯）
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

    // 本体の後ろに並んだ、発光パネルのインデックスだけを描いている
    const UINT panelIndexCount =
        static_cast<UINT>(m_Indices.size() - m_BodyIndexCount);
    context->DrawIndexed(
        panelIndexCount,
        static_cast<UINT>(m_BodyIndexCount),
        0);
}

// 頂点データを解放している
void CeilingLight::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}

// 明るさが残っている天井照明を点光源として登録し、部屋の壁や床へ光を当てている。
// 以前はPlayerがまとめて作っていた処理で、照明自身が自分の光を申告する形にしている。
void CeilingLight::CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
{
    const float brightness = GetBrightness();
    // ほとんど消えている照明は登録しない（タイルベースのライティングの計算を減らすため）
    if (brightness <= 0.01f)
    {
        return;
    }

    const bool powerRestored = Core::Game::GetInstance()->IsPowerRestored();
    ENVIRONMENT_POINT_LIGHT pointLight{};

    // 発光パネルより少し下へ光源を置き、天井に埋もれず室内を照らすようにしている。
    pointLight.PositionRange = Vector4(
        m_Position.x, m_Position.y - 3.0f, m_Position.z,
        powerRestored ? 165.0f : 115.0f);

    // 光の色：通常は青白、故障した灯は黄緑、停電中は赤
    if (powerRestored)
    {
        pointLight.ColorIntensity = IsFaulted()
            ? Vector4(0.70f, 0.78f, 0.56f, brightness * 1.08f)
            : Vector4(0.84f, 0.91f, 1.0f, brightness * 1.32f);
    }
    else
    {
        pointLight.ColorIntensity = Vector4(
            1.0f, 0.055f, 0.025f, brightness * 1.05f);
    }
    lights.push_back(pointLight);
}
