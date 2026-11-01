// ============================================================================
// ファイルの役割: 配電盤（ヒューズを入れて電力を戻す）と、同じ形を使うスイッチ類の、調べたときの処理と見た目を管理している。
// 主な技術: Interactableインターフェース、有限状態機械、進行の条件、照明・効果音・画面効果との連携
// ============================================================================

#include "Game.h"
#include "FuseBox.h"
#include "Input.h"
#include "Player.h"
#include "ScreenDustOverlay.h"
#include "PropModel.h"

#include <array>

using namespace DirectX::SimpleMath;

// 本体・前面のふた・ヒューズ3本・状態を示すランプ・レバーを、Mayaで作ったモデルから読み込んでいる。
// 電力が戻ったかどうかでヒューズとランプの色が変わるので、状態が変わるたびに作り直している。
void FuseBox::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();

    // ヒューズ：電力が戻ると明るい緑がかった色になる
    const Color fuseColor = m_IsPowered
        ? Color(0.68f, 0.82f, 0.64f, 1.0f)
        : Color(0.11f, 0.13f, 0.12f, 1.0f);
    // ランプ：停電中は赤、電力が戻ると緑
    const Color indicatorColor = m_IsPowered
        ? Color(0.12f, 1.0f, 0.24f, 1.0f)
        : Color(0.85f, 0.04f, 0.025f, 1.0f);
    // モデルは幅12・高さ18・奥行き1.6の実際の大きさなので、標準の拡大率で割って読み込んでいる（tools/maya/build_props.py）。
    // 色が変わっても頂点の数は同じなので、作り直した後は頂点バッファの中身だけを入れ替えている
    PropModel::Append("assets/model/props/fuse_box.fbx",
        {
            { "Body", Color(0.16f, 0.18f, 0.17f, 1.0f) },
            { "Cover", Color(0.035f, 0.045f, 0.04f, 1.0f) },
            { "Holder", Color(0.30f, 0.29f, 0.26f, 1.0f) },
            { "Fuse", fuseColor },
            { "Lamp", indicatorColor },
            // 横のレバーとヒューズの金具（真鍮色）
            { "Lever", Color(0.52f, 0.39f, 0.12f, 1.0f) },
        },
        Vector3(12.0f, 18.0f, 4.0f), m_Vertices, m_Indices);
}

// 形とバッファ、シェーダー、マテリアルを作っている。色は頂点の色を使っている
void FuseBox::Init()
{
    m_Vertices.reserve(168);
    m_Indices.reserve(504);
    BuildGeometry();

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.Specular = Color(0.14f, 0.14f, 0.12f, 1.0f);
    material.Shininess = 18.0f;
    material.TextureEnable = FALSE;
    // PBR：塗装が擦れて地の鋼が少し出た配電盤（塗装の光沢に、わずかに金属の映り込みを混ぜている）
    material.Metallic = 0.30f;
    material.Roughness = 0.48f;
    m_Material->Create(material);

    m_Scale = Vector3(12.0f, 18.0f, 4.0f);
}

// 毎フレームの処理はない（調べられたときだけ状態が変わる）
void FuseBox::Update()
{
}

// 調べるときに表示する文章。役割（スイッチ・出口の送電盤・主電源の配電盤）によって変えている
const char* FuseBox::GetInteractionPrompt() const
{
    if (m_IsManualControl)
    {
        return m_ManualPrompt;
    }

    if (m_IsExitControl)
    {
        return Core::Game::GetInstance()->IsPowerRestored()
            ? "出口へ非常電源を送る"
            : "先に主電源を復旧する";
    }

    return Core::Game::GetInstance()->GetItemCount() < 3
        ? "ヒューズが3本必要"
        : "電力を復旧する";
}

// 調べたときの処理。役割ごとに、電力を入れて見た目を作り直し、音・画面の光・振動で知らせている
void FuseBox::Interact(Player& player)
{
    (void)player;

    Core::Game* game = Core::Game::GetInstance();
    // スイッチとして使う場合（非常用充電器・監視カメラの端末・暗証番号の入力など）：押せる状態なら一度だけ反応している
    if (m_IsManualControl)
    {
        if (m_IsPowered || !m_ManualInteractionAllowed)
        {
            return;
        }

        m_IsPowered = true;
        game->PlayAudioCueAt(SOUND_CUE_POWER, m_Position);
        BuildGeometry();
        m_VertexBuffer.Modify(m_Vertices);
        game->GetPostProcess()->TriggerBloomPulse(0.72f, 0.28f);
        game->GetPostProcess()->TriggerHorrorPulse(0.16f, 0.22f);
        Input::SetVibration(6, 0.13f);
        return;
    }

    // 出口の送電盤：主電源が戻った後にだけ、出口へ電力を送っている
    if (m_IsExitControl)
    {
        if (m_IsPowered || !game->IsPowerRestored())
        {
            return;
        }

        m_IsPowered = true;
        game->PlayAudioCueAt(SOUND_CUE_POWER, m_Position);
        game->GetPostProcess()->TriggerBloomPulse(1.25f, 0.72f);
        game->GetPostProcess()->TriggerHorrorPulse(0.32f, 0.36f);

        BuildGeometry();
        m_VertexBuffer.Modify(m_Vertices);
        Input::SetVibration(10, 0.22f);

        ScreenDustOverlay* crt = game->GetObj<ScreenDustOverlay>("CRTNoise");
        if (crt != nullptr)
        {
            crt->SetPower(0.48f);
            crt->SetActive(true);
            crt->SetTimer(0.32f);
        }
        return;
    }

    // 主電源の配電盤：ヒューズを3本持っていれば、施設の電力を戻している（照明が点き、出口の扉などが使えるようになる）
    if (m_IsPowered || game->GetItemCount() < 3)
    {
        return;
    }

    m_IsPowered = true;
    game->PlayAudioCueAt(SOUND_CUE_POWER, m_Position);
    game->SetPowerRestored(true);
    game->GetPostProcess()->TriggerBloomPulse(1.65f, 1.35f);
    game->GetPostProcess()->TriggerHorrorPulse(0.55f, 0.75f);
    BuildGeometry();
    m_VertexBuffer.Modify(m_Vertices);
    Input::SetVibration(18, 0.28f);

    // 画面にブラウン管のようなノイズを0.7秒出している
    ScreenDustOverlay* crt =
        game->GetObj<ScreenDustOverlay>("CRTNoise");
    if (crt != nullptr)
    {
        crt->SetPower(0.9f);
        crt->SetActive(true);
        crt->SetTimer(0.7f);
    }
}

// 電力を入れていない状態に戻し、見た目も作り直している（やり直しや周回の切り替えで使っている）
void FuseBox::ResetActivation()
{
    m_IsPowered = false;
    BuildGeometry();
    m_VertexBuffer.Modify(m_Vertices);
}

// 箱を描いている
void FuseBox::Draw(Camera* camera)
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
    m_Material->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
}

// 頂点データを解放している
void FuseBox::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}
