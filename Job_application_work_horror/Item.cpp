// ============================================================================
// ファイルの役割: ヒューズ（1面で集めるアイテム）の形、浮かぶ動き、拾ったときの処理を管理している。
// 主な技術: Mayaで作ったモデル（PropModel）、ゆっくり脈打つ大きさ、調べる操作、時間で回る・浮く動き
// ============================================================================

#include "Item.h"
#include "Application.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"
#include "Player.h"
#include "ShadowMan.h"
#include "PropModel.h"

#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 頂点の色：緑がかったガラスの筒・上下の金具・手前に透けて見えるオレンジのフィラメント
    const Color GlassColor(0.34f, 0.46f, 0.38f, 1.0f);
    const Color MetalColor(0.62f, 0.58f, 0.42f, 1.0f);
    const Color FilamentColor(1.0f, 0.48f, 0.10f, 1.0f);
    // モデルの大きさ（高さ約7の実際の大きさ）を、この拡大率で割って読み込み、置いたときにこの拡大率で戻す
    const Vector3 ModelScale(5.0f, 5.0f, 5.0f);
    // 頂点とインデックスの数の見込み（先に入れ物の大きさを決めておく）
    constexpr std::size_t ReservedVertexCount = 96;
    constexpr std::size_t ReservedIndexCount = 288;
    // マテリアル：色は頂点の色で付けるため白。わずかに自ら光らせ、暗い部屋でも見つけやすくしている。
    // PBR：ヒューズの金属の口金とガラス（半分ほど金属。懐中電灯で照らすと芯が光る）
    const Color MaterialDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
    const Color MaterialSpecular(0.55f, 0.48f, 0.30f, 1.0f);
    const Color MaterialEmission(0.065f, 0.035f, 0.008f, 1.0f);
    constexpr float MaterialShininess = 32.0f;
    constexpr float MaterialMetallic = 0.50f;
    constexpr float MaterialRoughness = 0.30f;
    // 拾われるまでの動き：回る速さ（1秒あたりのラジアン）、上下に浮く速さと幅、大きさが脈打つ速さと幅
    constexpr float SpinSpeed = 1.5f;
    constexpr float BobSpeed = 2.6f;
    constexpr float BobHeight = 0.75f;
    constexpr float PulseSpeed = 3.2f;
    constexpr float PulseAmount = 0.08f;
    // 拾ったときの画面の光（一番明るいときの強さと秒数）と振動
    constexpr float PickupBloomPeak = 0.72f;
    constexpr float PickupBloomSeconds = 0.30f;
    constexpr int PickupRumbleFrames = 6;
    constexpr float PickupRumbleStrength = 0.14f;
    // 1本目を拾った瞬間に影を出す、プレイヤーの後ろ（-Z 方向）の距離
    constexpr float FirstFuseShadowDistance = 80.0f;
}

// ヒューズの形（緑がかったガラスの筒・上下の金具・手前に透けて見えるオレンジのフィラメント）を、Mayaで作ったモデルから読み込んでいる。
// モデルは高さ約7の実際の大きさなので、拡大率5で割っている
void Item::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();
    PropModel::Append("assets/model/props/fuse.fbx",
        {
            { "Glass", GlassColor },
            { "Metal", MetalColor },
            { "Filament", FilamentColor },
        },
        ModelScale, m_Vertices, m_Indices);
}

// 形とバッファ、シェーダー、マテリアルを作っている。わずかに自ら光らせ、暗い部屋でも見つけやすくしている
void Item::Init()
{
    m_Vertices.reserve(ReservedVertexCount);
    m_Indices.reserve(ReservedIndexCount);
    BuildGeometry();

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = MaterialDiffuse;
    material.Specular = MaterialSpecular;
    material.Emission = MaterialEmission;
    material.Shininess = MaterialShininess;
    material.TextureEnable = FALSE;
    material.Metallic = MaterialMetallic;
    material.Roughness = MaterialRoughness;
    m_Material->CreateRequired(material);

    m_Scale = ModelScale;
    m_BaseY = m_Position.y;
    m_AnimationTime = 0.0f;
}

void Item::Update()
{
    if (!m_IsActive || m_IsCollected) return;

    // ゆっくり回りながら浮き沈みさせ、暗い部屋でも小さなヒューズを見つけやすくしている。
    const float deltaTime = Application::GetDeltaTime();
    m_AnimationTime += deltaTime;
    m_Rotation.y += SpinSpeed * deltaTime;
    m_Position.y = m_BaseY + std::sin(m_AnimationTime * BobSpeed) * BobHeight;
}

// 拾ったときの処理：数を増やし、音・画面の光・振動で知らせている
void Item::Interact(Player& player)
{
    if (!m_IsActive || m_IsCollected)
    {
        return;
    }

    m_IsCollected = true;
    Core::Game::GetInstance()->AddItemCount();
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_PICKUP);
    Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(
        PickupBloomPeak, PickupBloomSeconds);
    Input::SetVibration(PickupRumbleFrames, PickupRumbleStrength);

    // 1本目を拾った瞬間、プレイヤーから-Z方向に80離れた位置に影を出している（最初の驚かせる演出）
    if (Core::Game::GetInstance()->GetItemCount() == 1)
    {
        const Vector3 shadowPosition(
            player.GetPosition().x,
            player.GetPosition().y,
            player.GetPosition().z - FirstFuseShadowDistance
        );

        Core::Game::GetInstance()->RequestAddObject<ShadowMan>(
            [shadowPosition](ShadowMan& shadow)
            {
                shadow.SetPosition(
                    shadowPosition.x,
                    shadowPosition.y,
                    shadowPosition.z
                );
            }
        );
    }
}

// 回転と浮き沈みに加えて、大きさを少し脈打たせて描いている
void Item::Draw(Camera* cam)
{
    if (!m_IsActive || m_IsCollected) return;

    cam->SetCamera();

    Matrix r = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y,
        m_Rotation.x,
        m_Rotation.z
    );

    Matrix t = Matrix::CreateTranslation(m_Position);

    const float pulse =
        1.0f + (std::sin(m_AnimationTime * PulseSpeed) * 0.5f + 0.5f) * PulseAmount;
    Matrix s = Matrix::CreateScale(
        m_Scale.x * pulse,
        m_Scale.y * pulse,
        m_Scale.z * pulse
    );

    Matrix worldmtx = s * r * t;

    Renderer::SetWorldMatrix(&worldmtx);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

// 頂点データとマテリアルを解放している
void Item::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Material.reset();
}
