// ============================================================================
// ファイルの役割: 扉の形と描画、開くときの動き、鍵がかかっているときの反応、プレイヤーとの当たり判定を管理している。
// 主な技術: 有限状態機械、拡大・回転・移動の行列、蝶番を軸にした回転、回転を打ち消した空間での当たり判定、調べる操作
// ============================================================================

#include "Door.h"
#include "MathUtility.h"
#include "Application.h"

#include "CeilingLight.h"
#include "Game.h"
#include "PostProcess.h"
#include "ShadowMap.h"
#include "Input.h"
#include "Player.h"
#include "PropModel.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    const Color DoorWoodColor(0.30f, 0.13f, 0.07f, 1.0f);
    // 扉の大きさ（幅30・高さ50・厚さ4）。モデルはこの大きさを1にした空間で作っている
    const Vector3 DoorSize(30.0f, 50.0f, 4.0f);
    // 開ききったときの角度（1.5ラジアン、約86度）と、漏れ光が一番明るくなる開き具合の角度
    constexpr float DoorOpenAngle = Door::FullOpenAngle;
    constexpr float DoorLeakFullOpenAngle = 1.20f;

    // 鍵のかかった扉を調べたとき：この秒数だけ、この速さ・大きさで小刻みに揺らす
    constexpr float LockedRattleSeconds = 0.28f;
    constexpr float LockedRattleFrequency = 62.0f;
    constexpr float LockedRattleAngle = 0.018f;

    // 開始地点のドアの隙間（1面の4周目から）：照らされていない間だけ、この角度までゆっくり開き、照らされると速く閉じる
    constexpr float HauntedGapAngle = 0.18f;
    constexpr float HauntedGapOpenSpeed = 0.025f;
    constexpr float HauntedGapCloseSpeed = 0.65f;

    // 開く前のためで揺れる大きさと速さ（周回が進むほど強く速く）、漏れ光をちらつかせる速さ
    constexpr float OpenDelayRattleAngle = 0.006f;
    constexpr float OpenDelayRattleAnglePerLoop = 0.0045f;
    constexpr float OpenDelayRattleFrequency = 30.0f;
    constexpr float OpenDelayRattleFrequencyPerLoop = 7.0f;
    constexpr float OpenDelayGlowFrequency = 45.0f;

    // 周回ごとの開き方（1秒あたりに開く角度と、開く前のための秒数）。2周目は重く、最終周は一度ためてから不自然な速さで開く
    struct OpenStyle
    {
        float SpeedPerSecond;
        float DelaySeconds;
    };
    constexpr OpenStyle OpenStyles[] =
    {
        { Door::FirstLoopOpenSpeed, Door::FirstLoopOpenDelay },
        { 1.74f, 0.20f },
        { 1.38f, 0.38f },
        { 2.76f, 0.58f },
    };
    constexpr int LastLoopPhase = static_cast<int>(std::size(OpenStyles)) - 1;

    // --- 見た目：鏡板・取っ手・足元の漏れ光の頂点の色
    const Color PanelColor(0.16f, 0.055f, 0.025f, 1.0f);
    const Color HandleColor(0.72f, 0.48f, 0.12f, 1.0f);
    const Color LeakVertexColor(1.0f, 0.72f, 0.42f, 1.0f);
    // 扉本体：色は頂点の色を使い、ニスの剥げかけた木として、やや粗い光沢にしている（PBR）
    const Color WoodDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
    const Color WoodSpecular(0.08f, 0.08f, 0.06f, 1.0f);
    constexpr float WoodShininess = 8.0f;
    constexpr float WoodRoughness = 0.58f;
    // 取っ手：真鍮の金属で、手で触れて少し磨かれた粗さ。手の脂と汚れの膜の分だけ、金属らしさを少し下げている
    constexpr float HandleMetallic = 0.85f;
    constexpr float HandleRoughness = 0.36f;
    // 漏れ光（作るとき・描くとき）の地の色と、作るときの光る色
    const Color LeakInitDiffuse(0.05f, 0.035f, 0.02f, 1.0f);
    const Color LeakInitEmission(0.08f, 0.035f, 0.015f, 1.0f);
    const Color LeakDiffuse(0.04f, 0.03f, 0.02f, 1.0f);
    // 漏れ光の色（明るさを掛ける）：電力が戻っていれば青白、停電中は赤
    const Vector3 LeakPoweredColor(0.72f, 0.82f, 1.00f);
    const Vector3 LeakUnpoweredColor(1.00f, 0.24f, 0.10f);
    // 漏れ光の明るさ：いつもの明るさ（＋周回ごと）、開いたときに足す明るさ（電力が戻った後・停電中）、ためで揺れているときに足す明るさ
    constexpr float LeakBaseIntensity = 0.045f;
    constexpr float LeakIntensityPerLoop = 0.025f;
    constexpr float LeakOpenPowered = 0.62f;
    constexpr float LeakOpenUnpowered = 0.30f;
    constexpr float LeakRattleIntensity = 0.22f;
    // 開く前のための秒数がこれより短いときは、0で割らないようこの値で割る
    constexpr float MinimumDelayDuration = 0.001f;

    // --- 鍵のかかった扉を調べたとき：画面の脈打ち（強さと秒数）と振動
    constexpr float LockedPulseStrength = 0.10f;
    constexpr float LockedPulseSeconds = 0.16f;
    constexpr int LockedRumbleFrames = 2;
    constexpr float LockedRumbleStrength = 0.06f;
    // --- 開けるときの演出：周回が進むほど強くする（いつもの値＋周回ごとに足す値）
    constexpr float OpenPulseStrength = 0.08f;
    constexpr float OpenPulseStrengthPerLoop = 0.055f;
    constexpr float OpenPulseSeconds = 0.18f;
    constexpr float OpenPulseSecondsPerLoop = 0.06f;
    constexpr float OpenBloomPeak = 0.82f;
    constexpr float OpenBloomPeakPerLoop = 0.10f;
    constexpr float OpenBloomSeconds = 0.20f;
    constexpr float OpenBloomSecondsPerLoop = 0.10f;
    // 扉の近くの天井照明（1面と2面で同じ名前）を一瞬明滅させる長さと強さ
    constexpr const char* DoorLightName = "CeilingLight4";
    constexpr float OpenFlickerSeconds = 0.24f;
    constexpr float OpenFlickerSecondsPerLoop = 0.14f;
    constexpr float OpenFlickerStrength = 0.46f;
    constexpr float OpenFlickerStrengthPerLoop = 0.16f;
    constexpr int OpenRumbleFrames = 3;
    constexpr int OpenRumbleFramesPerLoop = 2;
    constexpr float OpenRumbleStrength = 0.07f;
    constexpr float OpenRumbleStrengthPerLoop = 0.025f;
}

// 扉の形（扉板・鏡板・縁の飾り・取っ手・足元の漏れ光）をMayaで作ったモデルから読み込み、マテリアルを用意している。
void Door::Init()
{
    m_Vertices.clear();
    m_Indices.clear();
    // モデルを読む前に確保しておく頂点とインデックスの数（読み込みで足りなければ増える）
    constexpr size_t ReservedVertexCount = 96;
    constexpr size_t ReservedIndexCount = 288;
    m_Vertices.reserve(ReservedVertexCount);
    m_Indices.reserve(ReservedIndexCount);

    // 扉の表と裏の鏡板・縁の飾り・レバーの取っ手（tools/maya/build_props.py）。
    // モデルは幅30・高さ50・厚さ4の実際の大きさなので、標準の拡大率で割って読み込んでいる。
    // 蝶番をローカル座標の左端に置くため、取っ手は扉板の反対側（右端寄り）に付いている。
    const std::vector<PropModel::IndexRange> ranges = PropModel::Append(
        "assets/model/props/door.fbx",
        {
            { "Wood", DoorWoodColor },
            { "Panel", PanelColor },
            { "Handle", HandleColor },
            { "Leak", LeakVertexColor },
        },
        DoorSize, m_Vertices, m_Indices);
    // 取っ手は金属のマテリアルで、足元の漏れ光は別のマテリアルで描くので、インデックスの位置を覚えている。
    // 漏れ光は、扉の下の隙間から漏れる奥の部屋の光で、影を落とさない
    constexpr std::size_t HandlePartIndex = 2;
    constexpr std::size_t LightLeakPartIndex = 3;
    m_HandleIndexStart = ranges[HandlePartIndex].Start;
    m_DoorIndexCount = ranges[LightLeakPartIndex].Start;

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // 扉本体のマテリアル。色は頂点の色を使い、ニスの剥げかけた木として、やや粗い光沢にしている（PBR）
    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = WoodDiffuse;
    material.Specular = WoodSpecular;
    material.Shininess = WoodShininess;
    material.TextureEnable = FALSE;
    material.Roughness = WoodRoughness;
    m_Material->CreateRequired(material);

    // 取っ手のマテリアル。真鍮の金属で、手で触れて少し磨かれた粗さにしている（頂点の色が映り込みの色になる）。
    // 手の脂と汚れの膜の分だけ、金属らしさを少し下げている
    m_HandleMaterial = std::make_unique<Material>();
    MATERIAL handleMaterial = material;
    handleMaterial.Metallic = HandleMetallic;
    handleMaterial.Roughness = HandleRoughness;
    m_HandleMaterial->CreateRequired(handleMaterial);

    // 漏れ光のマテリアル（光る色はDrawで毎回決め直している）
    m_LeakMaterial = std::make_unique<Material>();
    MATERIAL leakMaterial{};
    leakMaterial.Diffuse = LeakInitDiffuse;
    leakMaterial.Emission = LeakInitEmission;
    leakMaterial.TextureEnable = FALSE;
    m_LeakMaterial->CreateRequired(leakMaterial);

    // 扉の大きさ（幅30・高さ50・厚さ4）
    m_Scale = DoorSize;
}
// 1フレーム分、扉の動きを進めている：鍵がかかっていれば小刻みに揺らし、開け始めたら少しためてから回転させている
void Door::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    // 鍵がかかった扉を調べた直後：0.28秒だけ小さくガタガタ揺らしている
    if (m_LockedRattleTimer > 0.0f)
    {
        m_LockedRattleTimer = (std::max)(
            0.0f,
            m_LockedRattleTimer - deltaTime);
        const float remaining = m_LockedRattleTimer / LockedRattleSeconds;
        const float elapsed = LockedRattleSeconds - m_LockedRattleTimer;
        m_OpenAngle = std::sin(elapsed * LockedRattleFrequency) * LockedRattleAngle * remaining;
        if (m_LockedRattleTimer <= 0.0f)
        {
            m_OpenAngle = 0.0f;
        }
        return;
    }

    if (!m_IsOpening)
    {
        if (!m_IsOpen && m_IsLocked)
        {
            const float target = m_HauntedGap && !m_HauntedLit ? HauntedGapAngle : 0.0f;
            const float speed = target > m_OpenAngle ? HauntedGapOpenSpeed : HauntedGapCloseSpeed;
            m_OpenAngle += (std::clamp)(target - m_OpenAngle, -speed * deltaTime, speed * deltaTime);
        }
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
            (std::max)(m_OpenDelayDuration, MinimumDelayDuration);
        const float rattleStrength =
            OpenDelayRattleAngle + static_cast<float>(m_LoopPhase) * OpenDelayRattleAnglePerLoop;
        m_OpenAngle = std::sin(
            elapsed * (OpenDelayRattleFrequency + static_cast<float>(m_LoopPhase) * OpenDelayRattleFrequencyPerLoop)) *
            rattleStrength * remaining;

        if (m_OpenDelayTimer <= 0.0f)
        {
            m_OpenAngle = 0.0f;
        }
        return;
    }

    // 1.5ラジアン（約86度）まで、周回ごとの速さで開いている
    m_OpenAngle = (std::min)(
        m_OpenAngle + m_OpenSpeedPerSecond * deltaTime,
        DoorOpenAngle);

    if (m_OpenAngle >= DoorOpenAngle)
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
        Core::Game::GetInstance()->PlayAudioCueAt(SOUND_CUE_DOOR_LOCKED, m_Position);
        m_LockedRattleTimer = LockedRattleSeconds;
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            LockedPulseStrength,
            LockedPulseSeconds);
        Input::SetVibration(LockedRumbleFrames, LockedRumbleStrength);
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

        const float loop = static_cast<float>(m_LoopPhase);
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            OpenPulseStrength + loop * OpenPulseStrengthPerLoop,
            OpenPulseSeconds + loop * OpenPulseSecondsPerLoop);
        Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(
            OpenBloomPeak + loop * OpenBloomPeakPerLoop,
            OpenBloomSeconds + loop * OpenBloomSecondsPerLoop);

        // 扉の近くの天井照明を一瞬明滅させている
        CeilingLight* doorLight =
            Core::Game::GetInstance()->GetObj<CeilingLight>(DoorLightName);
        if (doorLight != nullptr)
        {
            doorLight->TriggerEventFlicker(
                OpenFlickerSeconds + loop * OpenFlickerSecondsPerLoop,
                OpenFlickerStrength + loop * OpenFlickerStrengthPerLoop);
        }

        Input::SetVibration(
            OpenRumbleFrames + m_LoopPhase * OpenRumbleFramesPerLoop,
            OpenRumbleStrength + loop * OpenRumbleStrengthPerLoop);
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
    m_LoopPhase = (std::clamp)(loopPhase, 0, LastLoopPhase);

    // 同じ扉でも周回ごとに開き方を変えている。2周目は重く、最終周は一度ためてから
    // 不自然な速さで開き、見慣れた空間の違和感を強めている。
    const OpenStyle& style = OpenStyles[static_cast<std::size_t>(m_LoopPhase)];
    m_OpenSpeedPerSecond = style.SpeedPerSecond;
    m_OpenDelayDuration = style.DelaySeconds;
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
    context->DrawIndexed(static_cast<UINT>(m_HandleIndexStart), 0, 0);
    m_HandleMaterial->SetGPU();
    context->DrawIndexed(
        static_cast<UINT>(m_DoorIndexCount - m_HandleIndexStart),
        static_cast<UINT>(m_HandleIndexStart),
        0);

    // 開き具合（0〜1）をなめらかな曲線（smoothstep）に変えている
    const float normalizedOpen = (std::clamp)(m_OpenAngle / DoorLeakFullOpenAngle, 0.0f, 1.0f);
    const float openAmount = MathUtility::SmoothCurve(normalizedOpen);
    // 開く前のためで揺れている間は、漏れ光もちらつかせている
    float rattleGlow = 0.0f;
    if (m_OpenDelayTimer > 0.0f)
    {
        const float elapsed = m_OpenDelayDuration - m_OpenDelayTimer;
        const float remaining = m_OpenDelayTimer /
            (std::max)(m_OpenDelayDuration, MinimumDelayDuration);
        rattleGlow =
            (std::sin(elapsed * OpenDelayGlowFrequency) * 0.5f + 0.5f) * remaining;
    }

    // 漏れ光の明るさ：周回が進むほど、開くほど、明るくしている
    const bool powerRestored =
        Core::Game::GetInstance()->IsPowerRestored();
    const float leakIntensity =
        LeakBaseIntensity + static_cast<float>(m_LoopPhase) * LeakIntensityPerLoop +
        openAmount * (powerRestored ? LeakOpenPowered : LeakOpenUnpowered) +
        rattleGlow * LeakRattleIntensity;

    // 漏れ光の色：電力が戻っていれば青白、停電中は赤
    MATERIAL leakMaterial{};
    leakMaterial.Diffuse = LeakDiffuse;
    const Vector3 leakColor = (powerRestored ? LeakPoweredColor : LeakUnpoweredColor) * leakIntensity;
    leakMaterial.Emission = Color(leakColor.x, leakColor.y, leakColor.z, 1.0f);
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

// 扉板の中心と向きを本描画と同じ行列から求め、開きかけの扉も実際の角度で映す。
bool Door::GetReflectionProxy(ReflectionProxy& proxy) const
{
    const Matrix world = GetDoorWorldMatrix();
    Vector2 widthAxis(world._11, world._13);
    widthAxis.Normalize();
    const Vector3 halfSize(
        std::abs(m_Scale.x) * 0.5f, std::abs(m_Scale.y) * 0.5f, std::abs(m_Scale.z) * 0.5f);
    proxy = { world.Translation(), halfSize, DoorWoodColor, widthAxis };
    return true;
}

// 頂点データを解放している
void Door::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}
