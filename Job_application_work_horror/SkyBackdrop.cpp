// ============================================================================
// ファイルの役割: 外の森の空に霧を重ねる描画（説明は SkyBackdrop.h を参照）。
// ============================================================================

#include "SkyBackdrop.h"

#include "Camera.h"
#include "Renderer.h"

void SkyBackdrop::Init()
{
    // 頂点は頂点シェーダーが番号から作るので、入力レイアウトは作らない
    m_Shader.Create("shader/skyBackdropVS.hlsl", "shader/skyBackdropPS.hlsl", true);
}

void SkyBackdrop::Draw(Camera* camera)
{
    if (camera == nullptr)
    {
        return;
    }
    // ビュー行列と射影行列（視線の向きを求めるのに使う）
    camera->SetCamera();

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    m_Shader.SetGPU();
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // 深度の判定はそのまま（手前に描かれた物は残す）、深度の書き込みだけを止めている
    Renderer::SetDepthEnable(false);
    // 画面全体を覆う、大きな三角形1枚
    context->Draw(3, 0);
    Renderer::SetDepthEnable(true);
}
