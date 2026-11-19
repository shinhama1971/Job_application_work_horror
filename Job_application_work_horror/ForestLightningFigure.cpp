// ============================================================================
// ファイルの役割: 外の森の、稲光の一瞬だけ浮かぶ黒い人影の読み込みと描画。
// ForestLightningFigure.hで宣言した処理の中身をここで定義している。
// ============================================================================

#include "ForestLightningFigure.h"
#include "Application.h"
#include "Camera.h"
#include "PropModel.h"
#include "Renderer.h"

using namespace DirectX::SimpleMath;

namespace
{
    // 人影の大きさ（ShadowMan と同じ）：モデルを読み込むときに割る大きさ（モデルの実際の大きさのまま読むための基準）と、
    // 実際に描く大きさの倍率（背が約31 → 約54、幅1.45倍）
    const Vector3 FigureModelScale(8.0f, 16.0f, 8.0f);
    const Vector3 FigureEnlarge(1.45f, 1.75f, 1.45f);
    const Vector3 FigureSize = FigureModelScale * FigureEnlarge;
    // 体の色（ほとんど黒）。光の当たり方はシェーダーで決めるので、マテリアルの色は使わない
    const Color FigureBodyColor(0.025f, 0.03f, 0.027f, 1.0f);
    const Color FigureDiffuse(0.70f, 0.74f, 0.70f, 1.0f);
    // 描く間だけ借りる定数バッファの番号（shader/lightningFigurePS.hlsl の b7。デバッグ表示の定数と同じ番号なので、描いた後に戻す）
    constexpr UINT FigureBufferSlot = 7;
    // これより見え方が小さければ描かない
    constexpr float MinVisibleAmount = 0.001f;
}

// Mayaで作った人の形のモデル（ShadowMan と同じ）と、稲光の人影のシェーダーを読み込んでいる
void ForestLightningFigure::Init()
{
    PropModel::Append("assets/model/props/shadow_man.fbx",
        { { "Body", FigureBodyColor } },
        FigureModelScale, m_Vertices, m_Indices);
    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.CreateRequired(
        "shader/litTextureVS.hlsl",
        "shader/lightningFigurePS.hlsl");
    Renderer::CreateRequiredConstantBuffer(
        sizeof(LightningFigureBuffer),
        m_FigureBuffer.ReleaseAndGetAddressOf(), "稲光の人影");

    MATERIAL material{};
    material.Diffuse = FigureDiffuse;
    material.Specular = Color(0.0f, 0.0f, 0.0f, 1.0f);
    material.Shininess = 1.0f;
    material.TextureEnable = FALSE;
    m_Material = std::make_unique<Material>();
    m_Material->CreateRequired(material);
    m_Scale = FigureSize;
}

// 出している間だけ、崩れる模様を動かす時間を進めている
void ForestLightningFigure::Update()
{
    if (m_Visible)
    {
        m_Time += Application::GetDeltaTime();
    }
}

void ForestLightningFigure::Show(const Vector3& footPosition, float facing)
{
    m_Position = footPosition;
    m_Rotation = Vector3(0.0f, facing, 0.0f);
    m_Visible = true;
    m_Time = 0.0f;
}

void ForestLightningFigure::SetLightning(float visibility, const Vector3& lightningPosition,
    const Vector3& lightningColor, float backlight)
{
    m_Visibility = visibility;
    m_LightningPosition = lightningPosition;
    m_LightningColor = lightningColor;
    m_Backlight = backlight;
}

void ForestLightningFigure::Draw(Camera* camera)
{
    if (!m_Visible || m_Visibility <= MinVisibleAmount)
    {
        return;
    }
    camera->SetCamera();
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

    LightningFigureBuffer buffer{};
    buffer.Time = m_Time;
    buffer.Visibility = m_Visibility;
    buffer.Backlight = m_Backlight;
    buffer.LightningPosition = Vector4(m_LightningPosition.x, m_LightningPosition.y, m_LightningPosition.z, 1.0f);
    buffer.LightningColor = Vector4(m_LightningColor.x, m_LightningColor.y, m_LightningColor.z, 1.0f);
    context->UpdateSubresource(m_FigureBuffer.Get(), 0, nullptr, &buffer, 0, 0);

    // b7 は普通のシェーダーではデバッグ表示の設定にも使っているので、描いた後に元へ戻している（ShadowMan と同じ）
    Microsoft::WRL::ComPtr<ID3D11Buffer> previousBuffer;
    context->PSGetConstantBuffers(FigureBufferSlot, 1, previousBuffer.GetAddressOf());
    ID3D11Buffer* figureBuffer = m_FigureBuffer.Get();
    context->PSSetConstantBuffers(FigureBufferSlot, 1, &figureBuffer);
    context->DrawIndexed(static_cast<UINT>(m_Indices.size()), 0, 0);
    ID3D11Buffer* restoredBuffer = previousBuffer.Get();
    context->PSSetConstantBuffers(FigureBufferSlot, 1, &restoredBuffer);
}

void ForestLightningFigure::Uninit()
{
    m_FigureBuffer.Reset();
    m_Material.reset();
    m_Vertices.clear();
    m_Indices.clear();
}
