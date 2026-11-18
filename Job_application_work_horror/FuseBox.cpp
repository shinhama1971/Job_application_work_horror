// ============================================================================
// ファイルの役割: 配電盤（ヒューズを入れて電力を戻す）と、同じ形を使うスイッチ類の、調べたときの処理と見た目を管理している。
// 主な技術: Interactableインターフェース、有限状態機械、進行の条件、照明・効果音・画面効果との連携
// ============================================================================

#include "EventFeedback.h"
#include "Game.h"
#include "PostProcess.h"
#include "FuseBox.h"
#include "Input.h"
#include "Player.h"
#include "ScreenDustOverlay.h"
#include "PropModel.h"

#include <array>

using namespace DirectX::SimpleMath;

namespace
{
    // --- 見た目：ヒューズ（電力が戻ると明るい緑がかった色）、ランプ（停電中は赤、電力が戻ると緑）、本体・ふた・ホルダー・真鍮のレバー
    const Color FusePoweredColor(0.68f, 0.82f, 0.64f, 1.0f);
    const Color FuseOffColor(0.11f, 0.13f, 0.12f, 1.0f);
    const Color LampPoweredColor(0.12f, 1.0f, 0.24f, 1.0f);
    const Color LampOffColor(0.85f, 0.04f, 0.025f, 1.0f);
    const Color BodyColor(0.16f, 0.18f, 0.17f, 1.0f);
    const Color CoverColor(0.035f, 0.045f, 0.04f, 1.0f);
    const Color HolderColor(0.30f, 0.29f, 0.26f, 1.0f);
    const Color LeverColor(0.52f, 0.39f, 0.12f, 1.0f);
    // モデルの実際の大きさ（幅12・高さ18。奥行きは標準の拡大率の4で割る）。置いたときの大きさも同じ
    const Vector3 FuseBoxSize(12.0f, 18.0f, 4.0f);
    // 頂点とインデックスの数（モデルを読み込む前に、入れ物の大きさを決めておく）
    constexpr std::size_t ReservedVertexCount = 168;
    constexpr std::size_t ReservedIndexCount = 504;
    // マテリアル：白（頂点の色をそのまま使う）、鈍い映り込み、光沢。
    // PBR：塗装が擦れて地の鋼が少し出た配電盤（塗装の光沢に、わずかに金属の映り込みを混ぜている）
    const Color MaterialDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
    const Color MaterialSpecular(0.14f, 0.14f, 0.12f, 1.0f);
    constexpr float MaterialShininess = 18.0f;
    constexpr float MaterialMetallic = 0.30f;
    constexpr float MaterialRoughness = 0.48f;
    // 主電源を戻すのに要るヒューズの数
    constexpr int RequiredFuseCount = 3;

    // --- 操作したときの演出の強さと長さ
    using EventFeedback::PulseSpec;
    using EventFeedback::RumbleSpec;
    // スイッチ（非常用充電器・端末・暗証番号の入力など）を押したとき
    constexpr PulseSpec SwitchBloom{ 0.72f, 0.28f };
    constexpr PulseSpec SwitchPulse{ 0.16f, 0.22f };
    constexpr RumbleSpec SwitchRumble{ 6, 0.13f };
    // 出口の送電盤を入れたとき（画面のノイズの強さと秒数も）
    constexpr PulseSpec ExitPowerBloom{ 1.25f, 0.72f };
    constexpr PulseSpec ExitPowerPulse{ 0.32f, 0.36f };
    constexpr RumbleSpec ExitPowerRumble{ 10, 0.22f };
    constexpr float ExitPowerNoisePower = 0.48f;
    constexpr float ExitPowerNoiseSeconds = 0.32f;
    // 主電源を戻したとき（画面にブラウン管のようなノイズを0.7秒出す）
    constexpr PulseSpec MainPowerBloom{ 1.65f, 1.35f };
    constexpr PulseSpec MainPowerPulse{ 0.55f, 0.75f };
    constexpr RumbleSpec MainPowerRumble{ 18, 0.28f };
    constexpr float MainPowerNoisePower = 0.9f;
    constexpr float MainPowerNoiseSeconds = 0.7f;
}

// 本体・前面のふた・ヒューズ3本・状態を示すランプ・レバーを、Mayaで作ったモデルから読み込んでいる。
// 電力が戻ったかどうかでヒューズとランプの色が変わるので、状態が変わるたびに作り直している。
void FuseBox::BuildGeometry()
{
    m_Vertices.clear();
    m_Indices.clear();

    // ヒューズ：電力が戻ると明るい緑がかった色になる
    const Color fuseColor = m_IsPowered
        ? FusePoweredColor
        : FuseOffColor;
    // ランプ：停電中は赤、電力が戻ると緑
    const Color indicatorColor = m_IsPowered
        ? LampPoweredColor
        : LampOffColor;
    // モデルは幅12・高さ18・奥行き1.6の実際の大きさなので、標準の拡大率で割って読み込んでいる（tools/maya/build_props.py）。
    // 色が変わっても頂点の数は同じなので、作り直した後は頂点バッファの中身だけを入れ替えている
    PropModel::Append("assets/model/props/fuse_box.fbx",
        {
            { "Body", BodyColor },
            { "Cover", CoverColor },
            { "Holder", HolderColor },
            { "Fuse", fuseColor },
            { "Lamp", indicatorColor },
            // 横のレバーとヒューズの金具（真鍮色）
            { "Lever", LeverColor },
        },
        FuseBoxSize, m_Vertices, m_Indices);
}

// 形とバッファ、シェーダー、マテリアルを作っている。色は頂点の色を使っている
void FuseBox::Init()
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
    material.Shininess = MaterialShininess;
    material.TextureEnable = FALSE;
    material.Metallic = MaterialMetallic;
    material.Roughness = MaterialRoughness;
    m_Material->CreateRequired(material);

    m_Scale = FuseBoxSize;
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
        EventFeedback::BloomPulse(*game->GetPostProcess(), SwitchBloom);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), SwitchPulse);
        EventFeedback::Rumble(SwitchRumble);
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
        EventFeedback::BloomPulse(*game->GetPostProcess(), ExitPowerBloom);
        EventFeedback::HorrorPulse(*game->GetPostProcess(), ExitPowerPulse);

        BuildGeometry();
        m_VertexBuffer.Modify(m_Vertices);
        EventFeedback::Rumble(ExitPowerRumble);

        ScreenDustOverlay* crt = game->GetObj<ScreenDustOverlay>("CRTNoise");
        if (crt != nullptr)
        {
            crt->SetPower(ExitPowerNoisePower);
            crt->SetActive(true);
            crt->SetTimer(ExitPowerNoiseSeconds);
        }
        return;
    }

    // 主電源の配電盤：ヒューズを3本持っていれば、施設の電力を戻している（照明が点き、出口の扉などが使えるようになる）
    if (m_IsPowered || game->GetItemCount() < RequiredFuseCount)
    {
        return;
    }

    m_IsPowered = true;
    game->PlayAudioCueAt(SOUND_CUE_POWER, m_Position);
    game->SetPowerRestored(true);
    EventFeedback::BloomPulse(*game->GetPostProcess(), MainPowerBloom);
    EventFeedback::HorrorPulse(*game->GetPostProcess(), MainPowerPulse);
    BuildGeometry();
    m_VertexBuffer.Modify(m_Vertices);
    EventFeedback::Rumble(MainPowerRumble);

    // 画面にブラウン管のようなノイズを0.7秒出している
    ScreenDustOverlay* crt =
        game->GetObj<ScreenDustOverlay>("CRTNoise");
    if (crt != nullptr)
    {
        crt->SetPower(MainPowerNoisePower);
        crt->SetActive(true);
        crt->SetTimer(MainPowerNoiseSeconds);
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
