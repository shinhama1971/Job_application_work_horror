// ============================================================================
// ファイルの役割: 水面の反射を比べるための「SSR（スクリーンスペース反射）」に使う、画面の深度と画像を用意している。
// 主な技術: 深度プリパスの深度の写し取り、前のフレームの画面の再利用（説明はScreenSpaceReflection.hを参照）
// ============================================================================

#include "ScreenSpaceReflection.h"

#include "Renderer.h"
#include "utility.h"

namespace
{
    // 前のフレームの画面と、写し取った深度を読むテクスチャの番号
    constexpr UINT SceneSlot = 4;
    constexpr UINT DepthSlot = 8;
}

namespace Effect
{
    void ScreenSpaceReflection::Init(int width, int height)
    {
        // 深度バッファと同じ型なしの32ビット。書き込みは写し取り、読み取りは浮動小数点（R32_FLOAT）として使う
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = static_cast<UINT>(width);
        depthDesc.Height = static_cast<UINT>(height);
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        viewDesc.Format = DXGI_FORMAT_R32_FLOAT;
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipLevels = 1;
        ID3D11Device* device = Renderer::GetDevice();
        if (FAILED(device->CreateTexture2D(&depthDesc, nullptr, m_DepthCopy.ReleaseAndGetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(m_DepthCopy.Get(), &viewDesc, m_DepthView.ReleaseAndGetAddressOf())))
        {
            utility::ReportFatalError("SSRの深度のテクスチャの作成に失敗しました。");
        }
    }

    void ScreenSpaceReflection::Uninit()
    {
        m_DepthView.Reset();
        m_DepthCopy.Reset();
    }

    void ScreenSpaceReflection::Prepare(ID3D11ShaderResourceView* previousScene)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        // 深度バッファを描画先から外してから写し取り、描画先を戻している
        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(DepthSlot, 1, &nullResource);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        Microsoft::WRL::ComPtr<ID3D11Resource> depthResource;
        Renderer::GetDepthStencilView()->GetResource(depthResource.GetAddressOf());
        context->CopyResource(m_DepthCopy.Get(), depthResource.Get());
        Renderer::SetBackBufferRenderTarget();

        ID3D11ShaderResourceView* depthView = m_DepthView.Get();
        context->PSSetShaderResources(DepthSlot, 1, &depthView);
        context->PSSetShaderResources(SceneSlot, 1, &previousScene);
    }

    void ScreenSpaceReflection::Unbind()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(DepthSlot, 1, &nullResource);
        context->PSSetShaderResources(SceneSlot, 1, &nullResource);
    }
}
