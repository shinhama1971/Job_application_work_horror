// ============================================================================
// ファイルの役割: 懐中電灯の電池を回復するアイテムの形・見た目・取ったときの演出を管理している。
// 主な技術: Interactableインターフェース、Mayaで作ったモデル（PropModel）、マテリアルの使い分け、時間で回る・浮く動き
// ============================================================================

#include "BatteryItem.h"
#include "Application.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"
#include "Player.h"
#include "PropModel.h"

#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // モデルの大きさ（高さ約6の実際の大きさ）を、この拡大率で割って読み込み、置いたときにこの拡大率で戻す
    const Vector3 ModelScale(4.2f, 4.2f, 4.2f);
    // 環境光の色は、拡散色にこの割合を掛ける
    constexpr float AmbientRatio = 0.32f;
    // 本体：暗い緑がかった黒（拡散色・映り込み・光る色・光沢）
    const Color BodyDiffuse(0.055f, 0.070f, 0.060f, 1.0f);
    const Color BodySpecular(0.28f, 0.32f, 0.28f, 1.0f);
    const Color BodyEmission(0.002f, 0.006f, 0.003f, 1.0f);
    constexpr float BodyShininess = 42.0f;
    // 金属部分：明るい銀色で、光沢を強くしている
    const Color MetalDiffuse(0.52f, 0.54f, 0.48f, 1.0f);
    const Color MetalSpecular(0.88f, 0.90f, 0.82f, 1.0f);
    const Color MetalEmission(0.008f, 0.009f, 0.006f, 1.0f);
    constexpr float MetalShininess = 76.0f;
    // 残量の帯：緑色でわずかに自ら光らせ、暗い部屋でも見つけやすくしている
    const Color ChargeDiffuse(0.10f, 0.82f, 0.28f, 1.0f);
    const Color ChargeSpecular(0.50f, 1.0f, 0.60f, 1.0f);
    const Color ChargeEmission(0.035f, 0.28f, 0.075f, 1.0f);
    constexpr float ChargeShininess = 54.0f;
    // 拾われるまでの動き：回る速さ（1秒あたりのラジアン）、上下に浮く速さと幅、大きさが脈打つ速さと幅
    constexpr float SpinSpeed = 1.32f;
    constexpr float BobSpeed = 2.4f;
    constexpr float BobHeight = 0.38f;
    constexpr float PulseSpeed = 3.0f;
    constexpr float PulseAmount = 0.035f;
    // 拾ったときの画面の光（一番明るいときの強さと秒数）と振動
    constexpr float PickupBloomPeak = 0.62f;
    constexpr float PickupBloomSeconds = 0.34f;
    constexpr int PickupRumbleFrames = 6;
    constexpr float PickupRumbleStrength = 0.13f;
}

// 電池の形（本体の円柱・両端の金属のふた・プラス極の突起・残量を示す緑の帯）を、Mayaで作ったモデルから読み込んでいる。
// 色はマテリアルで付けるため、頂点の色は白（× 環境遮蔽）にしている。モデルは高さ約6の実際の大きさなので、拡大率4.2で割っている
void BatteryItem::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();
    const Color white(1.0f, 1.0f, 1.0f, 1.0f);
    const std::vector<PropModel::IndexRange> ranges = PropModel::Append(
        "assets/model/props/battery.fbx",
        { { "Body", white }, { "Metal", white }, { "Charge", white } },
        ModelScale, m_Vertices, m_Indices);
    // 描くときにマテリアルを変えるため、部位ごとのインデックスの範囲を覚えている
    m_BodyIndexCount = ranges[0].Count;
    m_MetalIndexStart = ranges[1].Start;
    m_MetalIndexCount = ranges[1].Count;
    m_ChargeIndexStart = ranges[2].Start;
    m_ChargeIndexCount = ranges[2].Count;
}

// 頂点バッファ・シェーダー・3種類のマテリアルを作っている
void BatteryItem::Init()
{
    BuildGeometry();
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

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
            diffuse.x * AmbientRatio,
            diffuse.y * AmbientRatio,
            diffuse.z * AmbientRatio,
            diffuse.w);
        data.Diffuse = diffuse;
        data.Specular = specular;
        data.Emission = emission;
        data.Shininess = shininess;
        data.TextureEnable = FALSE;
        material->CreateRequired(data);
        return material;
    };

    // 本体：暗い緑がかった黒
    m_BodyMaterial = createMaterial(BodyDiffuse, BodySpecular, BodyEmission, BodyShininess);
    // 金属部分
    m_MetalMaterial = createMaterial(MetalDiffuse, MetalSpecular, MetalEmission, MetalShininess);
    // 残量の帯
    m_ChargeMaterial = createMaterial(ChargeDiffuse, ChargeSpecular, ChargeEmission, ChargeShininess);

    // 置き場所の高さを、上下に浮く動きの中心として覚えている
    m_Scale = ModelScale;
    m_BaseY = m_Position.y;
    m_AnimationTime = 0.0f;
}

// 拾われるまで、ゆっくり回りながら上下に浮かせている（アイテムだと分かりやすくするため）
void BatteryItem::Update()
{
    if (!m_IsActive || m_IsCollected) return;

    const float deltaTime = Application::GetDeltaTime();
    m_AnimationTime += deltaTime;
    m_Rotation.y += SpinSpeed * deltaTime;
    m_Position.y = m_BaseY + std::sin(m_AnimationTime * BobSpeed) * BobHeight;
}

// 調べたときの処理：懐中電灯の電池を回復し、取った音・画面の光・振動で知らせている
// 電池が満タンのときは拾わない（後で必要になったときのために残している）
void BatteryItem::Interact(Player& player)
{
    if (!m_IsActive || m_IsCollected || player.IsBatteryFull())
    {
        return;
    }

    player.AddBattery(m_RecoverValue);
    m_IsCollected = true;
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_PICKUP);
    Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(
        PickupBloomPeak, PickupBloomSeconds);
    Input::SetVibration(PickupRumbleFrames, PickupRumbleStrength);
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
        (std::sin(m_AnimationTime * PulseSpeed) * 0.5f + 0.5f) * PulseAmount;
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
