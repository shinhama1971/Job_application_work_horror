// ============================================================================
// ファイルの役割: 扉の形と描画、開くときの動き、鍵がかかっているときの反応、プレイヤーとの当たり判定を管理している。
// 主な技術: 有限状態機械、拡大・回転・移動の行列、蝶番を軸にした回転、回転を打ち消した空間での当たり判定、調べる操作
// ============================================================================

#include "Door.h"
#include "Application.h"

#include "CeilingLight.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

// 扉の形（扉板・上下の飾り板・取っ手・足元の漏れ光）を箱の組み合わせで作り、マテリアルを用意している。
void Door::Init()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Vertices.reserve(96);
    m_Indices.reserve(288);

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

        for (size_t i = 0; i < positions.size(); ++i)
        {
            VERTEX_3D vertex{};
            vertex.position = positions[i];
            vertex.normal = normal;
            vertex.color = color;
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

    // 扉板（茶色）
    addBox(Vector3::Zero, Vector3(0.5f, 0.5f, 0.5f),
           Color(0.30f, 0.13f, 0.07f, 1.0f));
    // 扉板の手前側に付けた、上下2枚の飾り板（濃い茶色）
    addBox(Vector3(0.0f, 0.22f, -0.54f), Vector3(0.35f, 0.17f, 0.035f),
           Color(0.16f, 0.055f, 0.025f, 1.0f));
    addBox(Vector3(0.0f, -0.22f, -0.54f), Vector3(0.35f, 0.17f, 0.035f),
           Color(0.16f, 0.055f, 0.025f, 1.0f));
    // 蝶番をローカル座標の左端に置くため、取っ手は扉板の反対側（右端寄り）に付けている。
    addBox(Vector3(0.28f, 0.0f, -0.64f), Vector3(0.055f, 0.075f, 0.11f),
           Color(0.72f, 0.48f, 0.12f, 1.0f));

    // ここまでが扉本体。足元の漏れ光は別のマテリアルで描くので、インデックス数を区切っている
    m_DoorIndexCount = m_Indices.size();
    // 影を落とさない別のメッシュで、扉の下の隙間から漏れる奥の部屋の光を表している。
    addBox(Vector3(0.0f, -0.505f, -0.61f),
           Vector3(0.48f, 0.018f, 0.045f),
           Color(1.0f, 0.72f, 0.42f, 1.0f));

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // 扉本体のマテリアル。色は頂点の色を使い、光沢は弱くしている
    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.Specular = Color(0.08f, 0.08f, 0.06f, 1.0f);
    material.Shininess = 8.0f;
    material.TextureEnable = FALSE;
    m_Material->Create(material);

    // 漏れ光のマテリアル（光る色はDrawで毎回決め直している）
    m_LeakMaterial = std::make_unique<Material>();
    MATERIAL leakMaterial{};
    leakMaterial.Diffuse = Color(0.05f, 0.035f, 0.02f, 1.0f);
    leakMaterial.Emission = Color(0.08f, 0.035f, 0.015f, 1.0f);
    leakMaterial.TextureEnable = FALSE;
    m_LeakMaterial->Create(leakMaterial);

    // 扉の大きさ（幅30・高さ50・厚さ4）
    m_Scale = Vector3(30.0f, 50.0f, 4.0f);
}
// 1フレーム分、扉の動きを進めている：鍵がかかっていれば小刻みに揺らし、開け始めたら少しためてから回転させている
void Door::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    // 鍵がかかった扉を調べた直後：0.28秒だけ小さくガタガタ揺らしている
    if (m_LockedRattleTimer > 0.0f)
    {
        constexpr float rattleDuration = 0.28f;
        m_LockedRattleTimer = (std::max)(
            0.0f,
            m_LockedRattleTimer - deltaTime);
        const float remaining = m_LockedRattleTimer / rattleDuration;
        const float elapsed = rattleDuration - m_LockedRattleTimer;
        m_OpenAngle = std::sin(elapsed * 62.0f) * 0.018f * remaining;
        if (m_LockedRattleTimer <= 0.0f)
        {
            m_OpenAngle = 0.0f;
        }
        return;
    }

    if (!m_IsOpening)
    {
        return;
    }

    // 開く前のため：ループ廊下の周回が進むほど強く・速く揺れ、なかなか開かない感じを出している
    if (m_OpenDelayTimer > 0.0f)
    {
        m_OpenDelayTimer = (std::max)(
            0.0f,
            m_OpenDelayTimer - deltaTime);
        const float elapsed = m_OpenDelayDuration - m_OpenDelayTimer;
        const float remaining = m_OpenDelayTimer /
            (std::max)(m_OpenDelayDuration, 0.001f);
        const float rattleStrength =
            0.006f + static_cast<float>(m_LoopPhase) * 0.0045f;
        m_OpenAngle = std::sin(
            elapsed * (30.0f + static_cast<float>(m_LoopPhase) * 7.0f)) *
            rattleStrength * remaining;

        if (m_OpenDelayTimer <= 0.0f)
        {
            m_OpenAngle = 0.0f;
        }
        return;
    }

    // 1.5ラジアン（約86度）まで、周回ごとの速さで開いている
    constexpr float targetAngle = 1.50f;
    m_OpenAngle = (std::min)(
        m_OpenAngle + m_OpenSpeedPerSecond * deltaTime,
        targetAngle);

    if (m_OpenAngle >= targetAngle)
    {
        m_IsOpen = true;
        m_IsOpening = false;
    }
}

// 調べるときに表示する文章（鍵がかかっていれば「開かない」）
const char* Door::GetInteractionPrompt() const
{
    return m_IsLocked
        ? "ドアは開かない"
        : "ドアを開ける";
}

// 扉を調べたときの処理
void Door::Interact(Player& player)
{
    (void)player;

    // 鍵がかかっている：ガタッという音と小さな揺れ・画面の脈動・振動で、開かないことを伝えている
    if (m_IsLocked)
    {
        Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_DOOR, m_Position);
        m_LockedRattleTimer = 0.28f;
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            0.10f,
            0.16f);
        Input::SetVibration(2, 0.06f);
        return;
    }

    // この扉はループ廊下へ続くため、電力が戻る前でも開けられるようにしている。
    // 最後の出口だけは、別の条件で電力が戻るまで開かないようにしている。
    if (!m_IsOpen && !m_IsOpening)
    {
        // 開ける音を鳴らし、ためる時間を始めている。周回が進むほど画面の演出と振動を強くしている
        Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_DOOR, m_Position);
        m_IsOpening = true;
        m_OpenDelayTimer = m_OpenDelayDuration;

        const float pulseStrength =
            0.08f + static_cast<float>(m_LoopPhase) * 0.055f;
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            pulseStrength,
            0.18f + static_cast<float>(m_LoopPhase) * 0.06f);
        Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(
            0.82f + static_cast<float>(m_LoopPhase) * 0.10f,
            0.20f + static_cast<float>(m_LoopPhase) * 0.10f);

        // 扉の近くの天井照明を一瞬明滅させている
        CeilingLight* doorLight =
            Core::Game::GetInstance()->GetObj<CeilingLight>("CeilingLight4");
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(
                0.24f + static_cast<float>(m_LoopPhase) * 0.14f,
                0.46f + static_cast<float>(m_LoopPhase) * 0.16f);
        }

        Input::SetVibration(
            3 + m_LoopPhase * 2,
            0.07f + static_cast<float>(m_LoopPhase) * 0.025f);
    }
}

// 扉を閉じた状態に戻している（鍵は外れた状態になる）。loopPhaseはループ廊下の周回（0〜3）
void Door::ResetClosed(int loopPhase)
{
    m_Position = m_StartPosition;
    m_OpenAngle = 0.0f;
    m_IsOpen = false;
    m_IsOpening = false;
    m_IsLocked = false;
    m_LockedRattleTimer = 0.0f;
    m_OpenDelayTimer = 0.0f;
    m_LoopPhase = (std::clamp)(loopPhase, 0, 3);

    // 同じ扉でも周回ごとに開き方を変えている。2周目は重く、最終周は一度ためてから
    // 不自然な速さで開き、見慣れた空間の違和感を強めている。
    if (m_LoopPhase == 0)
    {
        m_OpenSpeedPerSecond = 1.92f;
        m_OpenDelayDuration = 0.06f;
    }
    else if (m_LoopPhase == 1)
    {
        m_OpenSpeedPerSecond = 1.74f;
        m_OpenDelayDuration = 0.20f;
    }
    else if (m_LoopPhase == 2)
    {
        m_OpenSpeedPerSecond = 1.38f;
        m_OpenDelayDuration = 0.38f;
    }
    else
    {
        m_OpenSpeedPerSecond = 2.76f;
        m_OpenDelayDuration = 0.58f;
    }
}

// 2点を結ぶ線分を、閉じた扉板がさえぎるかを判定している（開いている・開き始めていれば何もさえぎらない）
bool Door::BlocksSoundSegment(const Vector3& start, const Vector3& end) const
{
    if (m_IsOpen || m_IsOpening)
    {
        return false;
    }

    // 扉の回転を打ち消した空間で、扉板の長方形（床に投影したもの）と線分を判定している。
    // 高さは当たり判定と同じく考えず、扉の前後で鳴る音をさえぎるものとして扱っている。
    const Matrix inverseRotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z).Invert();
    const Vector3 localStart = Vector3::Transform(start - m_Position, inverseRotation);
    const Vector3 localEnd = Vector3::Transform(end - m_Position, inverseRotation);
    const Vector3 delta = localEnd - localStart;

    float minimumTime = 0.0f;
    float maximumTime = 1.0f;
    // 1つの軸について、線分が扉板の幅の中にある区間（始点からの割合）を絞り込んでいる（スラブ法）。
    // x軸とz軸の両方で区間が残れば、線分は扉板を通っている
    const auto clipAxis = [&](float origin, float direction, float halfExtent)
    {
        constexpr float epsilon = 0.000001f;
        if (std::abs(direction) <= epsilon)
        {
            return std::abs(origin) <= halfExtent;
        }
        float enter = (-halfExtent - origin) / direction;
        float exit = (halfExtent - origin) / direction;
        if (enter > exit)
        {
            std::swap(enter, exit);
        }
        minimumTime = (std::max)(minimumTime, enter);
        maximumTime = (std::min)(maximumTime, exit);
        return minimumTime <= maximumTime;
    };
    return clipAxis(localStart.x, delta.x, std::abs(m_Scale.x) * 0.5f) &&
        clipAxis(localStart.z, delta.z, std::abs(m_Scale.z) * 0.5f);
}

// プレイヤー（半径radiusの円）が閉じた扉板にめり込んでいたら、外へ押し出している
void Door::ResolveCollision(Vector3& position, float radius) const
{
    // 取っ手を操作した後は、扉板が回転している途中でも通れるようにしている。
    // 回転するメッシュがプレイヤーを壁へ押し込むのを防いでいる。
    if (m_IsOpen || m_IsOpening)
    {
        return;
    }

    // 扉の回転を打ち消したローカル空間で、扉板を長方形として扱っている
    const float halfX = std::abs(m_Scale.x) * 0.5f;
    const float halfZ = std::abs(m_Scale.z) * 0.5f;
    const Matrix baseRotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    const Matrix inverseRotation = baseRotation.Invert();
    Vector3 localPosition = Vector3::Transform(
        position - m_Position,
        inverseRotation);
    const float minX = -halfX;
    const float maxX = halfX;
    const float minZ = -halfZ;
    const float maxZ = halfZ;

    // 長方形の中で、プレイヤーに最も近い点を求めている
    const float closestX = std::clamp(localPosition.x, minX, maxX);
    const float closestZ = std::clamp(localPosition.z, minZ, maxZ);
    const float deltaX = localPosition.x - closestX;
    const float deltaZ = localPosition.z - closestZ;
    const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;

    if (distanceSquared >= radius * radius)
    {
        return;
    }

    // 中心が長方形の外にあるなら、最も近い点から半径の分だけ離している
    constexpr float epsilon = 0.000001f;
    if (distanceSquared > epsilon)
    {
        const float distance = std::sqrt(distanceSquared);
        const float pushDistance = radius - distance;
        localPosition.x += deltaX / distance * pushDistance;
        localPosition.z += deltaZ / distance * pushDistance;
        position = m_Position + Vector3::Transform(
            localPosition,
            baseRotation);
        return;
    }

    // 中心が長方形の中にまで入っているなら、一番近い辺の外側へ出している
    const float distanceToLeft = localPosition.x - minX;
    const float distanceToRight = maxX - localPosition.x;
    const float distanceToNear = localPosition.z - minZ;
    const float distanceToFar = maxZ - localPosition.z;
    const float nearestFace = (std::min)(
        (std::min)(distanceToLeft, distanceToRight),
        (std::min)(distanceToNear, distanceToFar)
    );

    if (nearestFace == distanceToLeft)
    {
        localPosition.x = minX - radius;
    }
    else if (nearestFace == distanceToRight)
    {
        localPosition.x = maxX + radius;
    }
    else if (nearestFace == distanceToNear)
    {
        localPosition.z = minZ - radius;
    }
    else
    {
        localPosition.z = maxZ + radius;
    }
    // ローカル空間からワールド座標へ戻している
    position = m_Position + Vector3::Transform(
        localPosition,
        baseRotation);
}

Matrix Door::GetDoorWorldMatrix() const
{
    // 原点を中心にして作ったメッシュを、左端が原点になるよう移動し、蝶番を軸に回転してから
    // ワールド座標へ戻すことで、端を軸に開く扉の行列を作っている。
    const float halfWidth = std::abs(m_Scale.x) * 0.5f;
    const Matrix baseRotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z);
    const Vector3 hingeOffset = Vector3::Transform(
        Vector3(-halfWidth, 0.0f, 0.0f),
        baseRotation);
    const Vector3 hingePosition = m_StartPosition + hingeOffset;
    const Matrix scale = Matrix::CreateScale(m_Scale);
    const Matrix centerFromHinge = Matrix::CreateTranslation(
        halfWidth,
        0.0f,
        0.0f);
    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y + m_OpenAngle,
        m_Rotation.x,
        m_Rotation.z);
    return scale * centerFromHinge * rotation *
        Matrix::CreateTranslation(hingePosition);
}

// 扉本体を描き、続けて足元の漏れ光を開き具合に合わせた明るさで描いている
void Door::Draw(Camera* camera)
{
    camera->SetCamera();

    Matrix world = GetDoorWorldMatrix();
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_DoorIndexCount), 0, 0);

    // 開き具合（0〜1）をなめらかな曲線（smoothstep）に変えている
    const float normalizedOpen = (std::clamp)(m_OpenAngle / 1.20f, 0.0f, 1.0f);
    const float openAmount = normalizedOpen * normalizedOpen *
        (3.0f - 2.0f * normalizedOpen);
    // 開く前のためで揺れている間は、漏れ光もちらつかせている
    float rattleGlow = 0.0f;
    if (m_OpenDelayTimer > 0.0f)
    {
        const float elapsed = m_OpenDelayDuration - m_OpenDelayTimer;
        const float remaining = m_OpenDelayTimer /
            (std::max)(m_OpenDelayDuration, 0.001f);
        rattleGlow =
            (std::sin(elapsed * 45.0f) * 0.5f + 0.5f) * remaining;
    }

    // 漏れ光の明るさ：周回が進むほど、開くほど、明るくしている
    const bool powerRestored =
        Core::Game::GetInstance()->IsPowerRestored();
    const float leakIntensity =
        0.045f + static_cast<float>(m_LoopPhase) * 0.025f +
        openAmount * (powerRestored ? 0.62f : 0.30f) +
        rattleGlow * 0.22f;

    // 漏れ光の色：電力が戻っていれば青白、停電中は赤
    MATERIAL leakMaterial{};
    leakMaterial.Diffuse = Color(0.04f, 0.03f, 0.02f, 1.0f);
    leakMaterial.Emission = powerRestored
        ? Color(
            0.72f * leakIntensity,
            0.82f * leakIntensity,
            1.00f * leakIntensity,
            1.0f)
        : Color(
            1.00f * leakIntensity,
            0.24f * leakIntensity,
            0.10f * leakIntensity,
            1.0f);
    leakMaterial.TextureEnable = FALSE;
    m_LeakMaterial->SetMaterial(leakMaterial);
    m_LeakMaterial->SetGPU();

    // 漏れ光は戸口の側の表現なので、扉板が蝶番で回転しても元の位置に残している。
    const Matrix baseRotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    Matrix leakWorld = Matrix::CreateScale(m_Scale) * baseRotation *
        Matrix::CreateTranslation(m_StartPosition);
    Renderer::SetWorldMatrix(&leakWorld);
    context->DrawIndexed(
        static_cast<UINT>(m_Indices.size() - m_DoorIndexCount),
        static_cast<UINT>(m_DoorIndexCount),
        0);
}

// 影を作るための描画（漏れ光は影を落とさないので、扉本体だけを描いている）
void Door::DrawShadow()
{
    Matrix world = GetDoorWorldMatrix();
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Core::Game::GetInstance()->GetShadowMap()->SetShader();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_DoorIndexCount), 0, 0);
}

// 深度プリパス：本描画と同じ頂点シェーダー・同じ行列で、扉の本体の深度だけを描いている
void Door::DrawDepthPrepass(Camera* camera)
{
    camera->SetCamera();

    Matrix world = GetDoorWorldMatrix();
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    context->PSSetShader(nullptr, nullptr, 0);
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_DoorIndexCount), 0, 0);
}

// 頂点データを解放している
void Door::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}
