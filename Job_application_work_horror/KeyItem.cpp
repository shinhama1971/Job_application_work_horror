// ============================================================================
// ファイルの役割: 1面に落ちている鍵（隠し部屋の扉の鍵・西棟の扉の鍵）の見た目と、拾ったときの処理を管理している。
// 主な技術: Interactableインターフェース、Mayaで作ったモデル（PropModel）、時間で回る・浮く動き
// ============================================================================

#include "KeyItem.h"
#include "Application.h"
#include "Game.h"
#include "PostProcess.h"
#include "Input.h"
#include "PropModel.h"

#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 頂点の色（白。色はマテリアルで付ける）と、モデルの大きさ（長さ約9.4の実際の大きさ）を割って読み込み、置いたときに戻す拡大率
    const Color ModelVertexColor(1.0f, 1.0f, 1.0f, 1.0f);
    const Vector3 ModelScale(4.0f, 4.0f, 4.0f);
    // くすんだ真鍮。暗い部屋でもライトを当てれば光るよう、弱い自己発光と強い鏡面反射を持たせている。
    // PBR：真鍮の鍵（金属なので、色は真鍮の映り込みの色。手で磨かれた細かい傷の粗さ）
    const Color BrassDiffuse(0.78f, 0.60f, 0.28f, 1.0f);
    const Color BrassAmbient(0.15f, 0.11f, 0.04f, 1.0f);
    const Color BrassSpecular(0.95f, 0.80f, 0.45f, 1.0f);
    const Color BrassEmission(0.030f, 0.020f, 0.006f, 1.0f);
    constexpr float BrassShininess = 64.0f;
    constexpr float BrassRoughness = 0.32f;
    // 拾われるまでの動き：回る速さ（1秒あたりのラジアン）、上下に浮く速さと幅
    constexpr float SpinSpeed = 1.1f;
    constexpr float BobSpeed = 2.2f;
    constexpr float BobHeight = 0.30f;
    // 拾ったときの音（少し高く）、画面の光（一番明るいときの強さと秒数）と振動
    constexpr float PickupPitch = 1.25f;
    constexpr float PickupBloomPeak = 0.56f;
    constexpr float PickupBloomSeconds = 0.28f;
    constexpr int PickupRumbleFrames = 5;
    constexpr float PickupRumbleStrength = 0.12f;
}

// 古い真鍮の鍵の形（持ち手の輪・軸の飾りの輪・軸・歯）を、Mayaで作ったモデルから読み込んでいる。
// 色はマテリアルで付けるため、頂点の色は白（× 環境遮蔽）にしている。モデルは長さ約9.4の実際の大きさなので、拡大率4で割っている
void KeyItem::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();
    PropModel::Append("assets/model/props/key.fbx",
        { { "Key", ModelVertexColor } },
        ModelScale, m_Vertices, m_Indices);
}

void KeyItem::Init()
{
    BuildGeometry();
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // くすんだ真鍮。暗い部屋でもライトを当てれば光るよう、弱い自己発光と強い鏡面反射を持たせている。
    MATERIAL material{};
    // PBR：真鍮の鍵（金属なので、色は真鍮の映り込みの色。手で磨かれた細かい傷の粗さ）
    material.Diffuse = BrassDiffuse;
    material.Ambient = BrassAmbient;
    material.Specular = BrassSpecular;
    material.Emission = BrassEmission;
    material.Shininess = BrassShininess;
    material.TextureEnable = FALSE;
    material.Metallic = 1.0f;
    material.Roughness = BrassRoughness;
    m_Material = std::make_unique<Material>();
    m_Material->CreateRequired(material);

    // 置き場所の高さを、上下に浮く動きの中心として覚えている
    m_Scale = ModelScale;
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
    m_Rotation.y += SpinSpeed * deltaTime;
    m_Position.y = m_BaseY + std::sin(m_AnimationTime * BobSpeed) * BobHeight;
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
    Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_PICKUP, PickupPitch);
    Core::Game::GetInstance()->GetPostProcess()->TriggerBloomPulse(PickupBloomPeak, PickupBloomSeconds);
    Input::SetVibration(PickupRumbleFrames, PickupRumbleStrength);
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
