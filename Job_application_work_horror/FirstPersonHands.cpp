// ============================================================================
// ファイルの役割: 一人称の「懐中電灯を持った右手」のモデルを読み込み、カメラに付けて揺らし、壁より手前に描いている。
// 主な技術: ModelCache（FBX）、マテリアルの名前ごとのシェーダーと材質、ばねによる視点の揺れの遅れ、
//           ビューポートの深度の範囲を手前に寄せる描画、手元だけの射影行列（b9）
// ============================================================================

#include "FirstPersonHands.h"
#include "MathUtility.h"

#include "Application.h"
#include "Camera.h"
#include "FlashlightRig.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace DirectX::SimpleMath;

namespace
{
    // 手を描く深度の範囲（0〜この値）。本描画の深度は near=1 なので、壁がこれより手前に来ることはない
    constexpr float HandDepthRange = 0.02f;
    // 手元の射影：縦の画角と、描く距離
    constexpr float HandFieldOfView = DirectX::XMConvertToRadians(52.0f);
    constexpr float HandNearPlane = 0.1f;
    constexpr float HandFarPlane = 60.0f;

    // 握る所の中ほど（ビュー空間。1単位は約5cm）と、懐中電灯を向ける先（画面の中央の遠く）
    const Vector3 GripPosition(2.15f, -1.95f, 5.0f);
    const Vector3 AimPoint(0.0f, 0.0f, FlashlightRig::AimDistance);
    // 手首を少しひねり、親指を内側へ傾けている
    constexpr float GripRoll = 0.16f;

    // ばねを進める細かさ（1秒を240に割る）と、1フレームで進める時間の上限（止まった後の大きな飛びを防ぐ）
    constexpr float SpringStepSeconds = 1.0f / 240.0f;
    constexpr float MaxFrameSeconds = 0.1f;
    // 視点を回したときの遅れ：回す速さに掛ける割合、ずれの上限（横・縦）、ばねの硬さと減衰、位置と向きへの効き方
    constexpr float LookSwayYawRate = 0.045f;
    constexpr float LookSwayPitchRate = 0.040f;
    constexpr float LookSwayYawLimit = 0.14f;
    constexpr float LookSwayPitchLimit = 0.12f;
    constexpr float LookSwayStiffness = 140.0f;
    constexpr float LookSwayDamping = 17.0f;
    constexpr float LookSwayYawOffset = 1.6f;
    constexpr float LookSwayPitchOffset = 1.4f;
    // 歩くときの揺れ：頭の揺れに掛ける割合（横・縦）と、ばねの硬さと減衰
    constexpr float BobSideRate = 0.9f;
    constexpr float BobVerticalRate = 0.55f;
    constexpr float BobStiffness = 90.0f;
    constexpr float BobDamping = 14.0f;
    // 走る・手を下げる・壁が近い・スイッチ・光が一瞬暗くなった、の状態へ近づく速さ（1秒あたり）
    constexpr float SprintApproachRate = 6.0f;
    constexpr float LowerApproachRate = 5.0f;
    constexpr float RaiseApproachRate = 3.5f;
    constexpr float NearSurfaceApproachRate = 8.0f;
    constexpr float ClickDecayRate = 9.0f;
    constexpr float JoltDecayRate = 5.0f;
    // 走るとき・壁が近いとき・手を下げたときの位置のずれ（ビュー空間）、スイッチを押したときに手前へ引く量
    const Vector3 SprintOffset(0.35f, -0.75f, -0.55f);
    const Vector3 NearSurfaceOffset(-0.15f, 0.25f, -0.9f);
    const Vector3 LoweredOffset(0.4f, -3.2f, -0.8f);
    constexpr float ClickPull = 0.10f;
    // 息で手がゆっくり上下する速さと大きさ
    constexpr float BreathFrequency = 1.15f;
    constexpr float BreathLift = 0.02f;
    // 息で手首がわずかに起きる角度と、上下の揺れからの遅れ（ラジアン）
    constexpr float BreathTilt = 0.006f;
    constexpr float BreathTiltPhase = 0.6f;
    // 電池が少ないときの手の震え（速さと大きさ）。ひねりには、この倍率で多めに効かせる
    constexpr float TremorFrequency = 13.7f;
    constexpr float TremorAmount = 0.004f;
    constexpr float TremorRollScale = 1.5f;
    // 光が一瞬暗くなったときに懐中電灯を振る動き（速さと大きさ）
    constexpr float JoltFrequency = 38.0f;
    constexpr float JoltAmount = 0.035f;
    // 横の向き（ヨー）：視点を回したときの遅れ・走るとき・振る動きの効き方
    constexpr float YawLookSwayScale = 0.6f;
    constexpr float YawSprint = 0.25f;
    constexpr float YawJoltScale = 0.6f;
    // 縦の向き（ピッチ）：視点を回したときの遅れ・走る・壁が近い・スイッチ・手を下げたときの効き方
    constexpr float PitchLookSwayScale = 0.5f;
    constexpr float PitchSprint = 0.45f;
    constexpr float PitchNearSurface = 0.12f;
    constexpr float PitchClick = 0.05f;
    constexpr float PitchLowered = 0.9f;
    // ひねり（ロール）：歩く揺れと走るときの効き方
    constexpr float RollBobScale = 0.25f;
    constexpr float RollSprint = 0.2f;
    // ここまで下げたら描かない（手が画面の外へ出きった）
    constexpr float HiddenLowerAmount = 0.98f;
    // 肌に描く汚れの量（viewModelSkinPS の ViewModelState.z）
    constexpr float SkinGrimeAmount = 0.6f;

    // --- 手と懐中電灯のマテリアル
    // 少し血の気の引いた肌、色の褪せた濃い灰色の作業着
    const Color SkinDiffuse(0.50f, 0.37f, 0.31f, 1.0f);
    const Color SleeveDiffuse(0.115f, 0.12f, 0.11f, 1.0f);
    // 先の金属の輪（メッキのはげかけた鋼）
    const Color BezelDiffuse(0.72f, 0.70f, 0.66f, 1.0f);
    constexpr float BezelRoughness = 0.24f;
    // レンズ：点けている間だけ、この色で光る（viewModelPS が点灯の度合いを掛けている）
    const Color LensDiffuse(0.02f, 0.02f, 0.02f, 1.0f);
    constexpr float LensRoughness = 0.06f;
    const Color LensEmission(2.6f, 2.5f, 2.25f, 1.0f);
    // ゴムのスイッチ
    const Color SwitchDiffuse(0.03f, 0.03f, 0.03f, 1.0f);
    constexpr float SwitchRoughness = 0.78f;
    // 本体：黒い塗装の金属（塗装は金属ではない。はげた所だけ地のアルミが出る）
    const Color BodyDiffuse(0.055f, 0.057f, 0.06f, 1.0f);
    constexpr float BodyRoughness = 0.42f;

    // 経過時間でなめらかに目標へ近づける割合
    float Approach(float current, float target, float ratePerSecond, float deltaTime)
    {
        return target + (current - target) * std::exp(-ratePerSecond * deltaTime);
    }

}

// ばねの式（目標との差に比例する力と、速さに比例する減衰）を、時間を細かく割って進めている
void FirstPersonHands::Spring::Update(float target, float stiffness, float damping, float deltaTime)
{
    const int steps = (std::max)(1, static_cast<int>(std::ceil(deltaTime / SpringStepSeconds)));
    const float step = deltaTime / static_cast<float>(steps);
    for (int index = 0; index < steps; ++index)
    {
        const float acceleration = (target - Value) * stiffness - Velocity * damping;
        Velocity += acceleration * step;
        Value += Velocity * step;
    }
}

// モデルを読み込み、マテリアルの名前ごとに、使うシェーダーと材質を決めている
void FirstPersonHands::Init()
{
    m_ModelData = ModelCache::Load("assets/model/flashlight_hand/flashlight_hand.fbx", "assets/model/flashlight_hand");
    m_FlashlightShader.CreateRequired("shader/viewModelVS.hlsl", "shader/viewModelPS.hlsl");
    m_SkinShader.CreateRequired("shader/viewModelVS.hlsl", "shader/viewModelSkinPS.hlsl");
    m_ClothShader.CreateRequired("shader/viewModelVS.hlsl", "shader/viewModelClothPS.hlsl");
    Renderer::CreateRequiredConstantBuffer(sizeof(VIEW_MODEL_BUFFER), m_ViewModelBuffer.ReleaseAndGetAddressOf(), "一人称の手と懐中電灯");

    // サブセットからマテリアルの番号と名前の対応を作っている（FBXのマテリアルの名前は build_flashlight_hand.py で付けている）
    std::vector<std::string> names(m_ModelData->Materials.size());
    for (const SUBSET& subset : m_ModelData->Subsets)
    {
        if (subset.MaterialIdx < names.size())
        {
            names[subset.MaterialIdx] = subset.MtrlName;
        }
    }

    m_Materials.clear();
    for (const std::string& name : names)
    {
        MATERIAL material{};
        material.Ambient = Color(0.0f, 0.0f, 0.0f, 1.0f);
        material.Specular = Color(0.0f, 0.0f, 0.0f, 1.0f);
        material.Emission = Color(0.0f, 0.0f, 0.0f, 1.0f);
        material.Shininess = 1.0f;
        material.TextureEnable = FALSE;
        Shader* shader = &m_FlashlightShader;
        if (name == "Skin")
        {
            // 少し血の気の引いた肌
            material.Diffuse = SkinDiffuse;
            shader = &m_SkinShader;
        }
        else if (name == "Sleeve")
        {
            // 色の褪せた濃い灰色の作業着
            material.Diffuse = SleeveDiffuse;
            shader = &m_ClothShader;
        }
        else if (name == "Bezel")
        {
            // 先の金属の輪（メッキのはげかけた鋼）
            material.Diffuse = BezelDiffuse;
            material.Metallic = 1.0f;
            material.Roughness = BezelRoughness;
        }
        else if (name == "Lens")
        {
            // レンズ：点けている間だけ、この色で光る（viewModelPS が点灯の度合いを掛けている）
            material.Diffuse = LensDiffuse;
            material.Roughness = LensRoughness;
            material.Emission = LensEmission;
        }
        else if (name == "Switch")
        {
            // ゴムのスイッチ
            material.Diffuse = SwitchDiffuse;
            material.Roughness = SwitchRoughness;
        }
        else
        {
            // 本体：黒い塗装の金属（塗装は金属ではない。はげた所だけ地のアルミが出る）
            material.Diffuse = BodyDiffuse;
            material.Roughness = BodyRoughness;
        }
        auto created = std::make_unique<Material>();
        created->CreateRequired(material);
        created->SetShader(shader);
        m_Materials.emplace_back(std::move(created));
    }
    ResetMotion();
}

void FirstPersonHands::ResetMotion()
{
    m_LookSwayYaw = {};
    m_LookSwayPitch = {};
    m_BobX = {};
    m_BobY = {};
    m_HasPreviousLook = false;
    m_Click = 0.0f;
    m_Jolt = 0.0f;
}

// 視点の回転・歩き・走り・息・スイッチ・電池の揺れから、手元の空間 → ビュー空間の行列を作っている
void FirstPersonHands::Update(const FrameInput& input)
{
    const float deltaTime = (std::min)(input.DeltaTime, MaxFrameSeconds);
    m_Time += deltaTime;

    // 視点を回したとき、手は少し遅れて付いてくる（回した向きと逆へずれ、ばねで戻る）
    float yawVelocity = 0.0f;
    float pitchVelocity = 0.0f;
    if (m_HasPreviousLook && deltaTime > 0.0f)
    {
        yawVelocity = MathUtility::WrapAngle(input.Yaw - m_PreviousYaw) / deltaTime;
        pitchVelocity = (input.Pitch - m_PreviousPitch) / deltaTime;
    }
    m_PreviousYaw = input.Yaw;
    m_PreviousPitch = input.Pitch;
    m_HasPreviousLook = true;
    m_LookSwayYaw.Update((std::clamp)(-yawVelocity * LookSwayYawRate, -LookSwayYawLimit, LookSwayYawLimit),
        LookSwayStiffness, LookSwayDamping, deltaTime);
    m_LookSwayPitch.Update((std::clamp)(pitchVelocity * LookSwayPitchRate, -LookSwayPitchLimit, LookSwayPitchLimit),
        LookSwayStiffness, LookSwayDamping, deltaTime);

    // 歩くと、頭の揺れより少し大きく、遅れて揺れる
    m_BobX.Update(input.HeadBobSide * BobSideRate, BobStiffness, BobDamping, deltaTime);
    m_BobY.Update(-input.HeadBobVertical * BobVerticalRate, BobStiffness, BobDamping, deltaTime);

    m_Sprint = Approach(m_Sprint, input.Sprinting ? 1.0f : 0.0f, SprintApproachRate, deltaTime);
    m_Lower = Approach(m_Lower, input.Lowered ? 1.0f : 0.0f, input.Lowered ? LowerApproachRate : RaiseApproachRate, deltaTime);
    m_NearSurface = Approach(m_NearSurface, input.NearSurface, NearSurfaceApproachRate, deltaTime);
    m_Click = input.Toggled ? 1.0f : m_Click * std::exp(-deltaTime * ClickDecayRate);
    m_Jolt = input.VoltageDrop ? 1.0f : m_Jolt * std::exp(-deltaTime * JoltDecayRate);
    m_LensGlow = input.LensGlow;
    m_BatteryWeakness = input.BatteryWeakness;
    m_Bounce = input.Bounce;

    // 位置のずれ（ビュー空間）
    Vector3 position = GripPosition;
    position.x += m_LookSwayYaw.Value * LookSwayYawOffset + m_BobX.Value;
    position.y += m_LookSwayPitch.Value * LookSwayPitchOffset + m_BobY.Value + std::sin(m_Time * BreathFrequency) * BreathLift;
    // 走ると手を下げて体に引き寄せ、前の壁が近いと手を引いて少し上げる
    position += SprintOffset * m_Sprint;
    position += NearSurfaceOffset * m_NearSurface;
    position.z += m_Click * ClickPull;
    // 手を下げて画面の外へ
    position += LoweredOffset * m_Lower;

    // 向き：握る位置から画面の中央の遠くへ向け、そこへ揺れの回転を足している
    Vector3 forward = AimPoint - GripPosition;
    forward.Normalize();
    Vector3 right = Vector3::Up.Cross(forward);
    right.Normalize();
    const Vector3 up = forward.Cross(right);
    const Matrix aim(
        right.x, right.y, right.z, 0.0f,
        up.x, up.y, up.z, 0.0f,
        forward.x, forward.y, forward.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);

    // 電池が少ないと光が揺れ、光が一瞬暗くなると懐中電灯を振る（叩いて直そうとする動き）
    const float tremor = std::sin(m_Time * TremorFrequency) * TremorAmount * input.BatteryStress;
    const float jolt = std::sin(m_Time * JoltFrequency) * JoltAmount * m_Jolt;
    const float yaw = m_LookSwayYaw.Value * YawLookSwayScale + YawSprint * m_Sprint + jolt * YawJoltScale + tremor;
    const float pitch = -m_LookSwayPitch.Value * PitchLookSwayScale + PitchSprint * m_Sprint - PitchNearSurface * m_NearSurface -
        PitchClick * m_Click + PitchLowered * m_Lower + jolt + std::sin(m_Time * BreathFrequency + BreathTiltPhase) * BreathTilt;
    const float roll = GripRoll + m_BobX.Value * RollBobScale + RollSprint * m_Sprint + tremor * TremorRollScale;
    m_ViewPose = Matrix::CreateFromYawPitchRoll(yaw, pitch, roll) * aim * Matrix::CreateTranslation(position);
}

// 手元だけの射影行列と、前で跳ね返った光を b9 に入れ、深度の範囲を手前に寄せて描いている
void FirstPersonHands::Draw(Camera& camera)
{
    if (!m_ModelData || m_Lower > HiddenLowerAmount)
    {
        return;
    }

    Matrix view;
    Matrix projection;
    camera.GetMainMatrices(view, projection);
    const Matrix inverseView = view.Invert();
    Matrix world = m_ViewPose * inverseView;
    Renderer::SetWorldMatrix(&world);

    const float aspectRatio = static_cast<float>(Application::GetWidth()) /
        static_cast<float>(Application::GetHeight());
    VIEW_MODEL_BUFFER buffer{};
    buffer.Projection = Matrix(DirectX::XMMatrixPerspectiveFovLH(
        HandFieldOfView, aspectRatio, HandNearPlane, HandFarPlane)).Transpose();
    const Vector3 eye = camera.GetPosition();
    buffer.Eye = Vector4(eye.x, eye.y, eye.z, m_Time);
    buffer.Bounce = Vector4(m_Bounce.x, m_Bounce.y, m_Bounce.z, 0.0f);
    Vector3 front = Vector3::TransformNormal(Vector3(0.0f, 0.0f, 1.0f), inverseView);
    front.Normalize();
    buffer.BounceDirection = Vector4(front.x, front.y, front.z, 0.0f);
    buffer.State = Vector4(m_LensGlow, m_BatteryWeakness, SkinGrimeAmount, 0.0f);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->UpdateSubresource(m_ViewModelBuffer.Get(), 0, nullptr, &buffer, 0, 0);
    ID3D11Buffer* constantBuffer = m_ViewModelBuffer.Get();
    context->VSSetConstantBuffers(9, 1, &constantBuffer);
    context->PSSetConstantBuffers(9, 1, &constantBuffer);

    UINT viewportCount = 1;
    D3D11_VIEWPORT viewport{};
    context->RSGetViewports(&viewportCount, &viewport);
    D3D11_VIEWPORT handViewport = viewport;
    handViewport.MinDepth = 0.0f;
    handViewport.MaxDepth = HandDepthRange;
    context->RSSetViewports(1, &handViewport);

    m_ModelData->Renderer.BeforeDraw();
    for (const SUBSET& subset : m_ModelData->Subsets)
    {
        const size_t materialIndex = static_cast<size_t>(subset.MaterialIdx);
        if (materialIndex >= m_Materials.size())
        {
            continue;
        }
        m_Materials[materialIndex]->SetGPU();
        m_ModelData->Renderer.DrawSubset(subset.IndexNum, subset.IndexBase, subset.VertexBase);
    }

    context->RSSetViewports(1, &viewport);
}

void FirstPersonHands::Uninit()
{
    m_Materials.clear();
    m_ModelData.reset();
    m_ViewModelBuffer.Reset();
}
