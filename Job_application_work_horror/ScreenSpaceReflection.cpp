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
    constexpr UINT PreviousDepthSlot = 17;
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
            FAILED(device->CreateShaderResourceView(m_DepthCopy.Get(), &viewDesc, m_DepthView.ReleaseAndGetAddressOf())) ||
            FAILED(device->CreateTexture2D(&depthDesc, nullptr, m_PreviousDepthCopy.ReleaseAndGetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(m_PreviousDepthCopy.Get(), &viewDesc,
                m_PreviousDepthView.ReleaseAndGetAddressOf())))
        {
            utility::ReportFatalError("SSRの深度のテクスチャの作成に失敗しました。");
        }
    }

    void ScreenSpaceReflection::Uninit()
    {
        m_DepthView.Reset();
        m_DepthCopy.Reset();
        m_PreviousDepthView.Reset();
        m_PreviousDepthCopy.Reset();
        m_SceneMipView.Reset();
        m_SceneMips.Reset();
        Invalidate();
    }

    // 前のフレームの画面を写すテクスチャ（縮小画像の段つき）を用意している。
    // 取り込んだ画面と大きさ・形式が同じなら作り直さず、画面の大きさが変わったときだけ作り直している
    void ScreenSpaceReflection::EnsureSceneMipTexture(ID3D11Texture2D* source)
    {
        D3D11_TEXTURE2D_DESC sourceDesc{};
        source->GetDesc(&sourceDesc);
        if (m_SceneMips)
        {
            D3D11_TEXTURE2D_DESC currentDesc{};
            m_SceneMips->GetDesc(&currentDesc);
            if (currentDesc.Width == sourceDesc.Width && currentDesc.Height == sourceDesc.Height &&
                currentDesc.Format == sourceDesc.Format)
            {
                return;
            }
        }
        // 縮小画像は、GenerateMips で作るため、描画先にもできるテクスチャにしている（0 = 1画素まで全部の段）
        D3D11_TEXTURE2D_DESC desc = sourceDesc;
        desc.MipLevels = 0;
        desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ID3D11Device* device = Renderer::GetDevice();
        if (FAILED(device->CreateTexture2D(&desc, nullptr, m_SceneMips.ReleaseAndGetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(m_SceneMips.Get(), nullptr, m_SceneMipView.ReleaseAndGetAddressOf())))
        {
            // 作れない環境では、縮小画像を使わず、取り込んだ画面をそのまま使う（ぼかしが無くなるだけ）
            m_SceneMips.Reset();
            m_SceneMipView.Reset();
        }
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
        if (!m_HistoryValid || previousScene == nullptr)
        {
            return;
        }

        // 前のフレームの画面を、縮小画像を持つテクスチャの一番大きい段へ写し、GenerateMips で半分ずつ縮めた段（1/2・1/4・…）を作っている。
        // 水たまりのレイ反射は、映る物が水面から離れているほど小さい段を読み、遠くの映り込みをぼかす（rayReflection.hlsli）。
        // 段を作れなかったときは、取り込んだ画面をそのまま渡す（ぼかしが無くなるだけで、反射は映る）
        ID3D11ShaderResourceView* sceneView = previousScene;
        {
            Microsoft::WRL::ComPtr<ID3D11Resource> sceneResource;
            previousScene->GetResource(sceneResource.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneTexture;
            if (SUCCEEDED(sceneResource.As(&sceneTexture)))
            {
                EnsureSceneMipTexture(sceneTexture.Get());
                if (m_SceneMipView)
                {
                    context->CopySubresourceRegion(m_SceneMips.Get(), 0, 0, 0, 0, sceneTexture.Get(), 0, nullptr);
                    context->GenerateMips(m_SceneMipView.Get());
                    sceneView = m_SceneMipView.Get();
                }
            }
        }

        ID3D11ShaderResourceView* depthView = m_DepthView.Get();
        ID3D11ShaderResourceView* previousDepthView = m_PreviousDepthView.Get();
        context->PSSetShaderResources(DepthSlot, 1, &depthView);
        context->PSSetShaderResources(PreviousDepthSlot, 1, &previousDepthView);
        context->PSSetShaderResources(SceneSlot, 1, &sceneView);
        Renderer::SetReflectionHistory(m_PreviousViewProjection, m_PreviousDepth);
    }

    void ScreenSpaceReflection::Unbind()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(DepthSlot, 1, &nullResource);
        context->PSSetShaderResources(PreviousDepthSlot, 1, &nullResource);
        context->PSSetShaderResources(SceneSlot, 1, &nullResource);
    }

    void ScreenSpaceReflection::FinishFrame(const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& projection, bool captured)
    {
        m_HistoryValid = captured;
        if (captured)
        {
            m_PreviousDepthCopy.Swap(m_DepthCopy);
            m_PreviousDepthView.Swap(m_DepthView);
            m_PreviousViewProjection = view * projection;
            m_PreviousDepth = DirectX::SimpleMath::Vector4(projection._33, projection._43, 0.0f, 0.0f);
        }
    }
}
