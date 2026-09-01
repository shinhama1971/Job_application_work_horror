// ============================================================================
// ファイルの役割: オフスクリーン描画用テクスチャ、RTV、SRV、深度を管理します。
// ============================================================================

#pragma once
#include <wrl/client.h>
#include "Renderer.h"

namespace Graphics
{
    class RenderTexture
    {
    private:
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_UAV;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthView;
        int m_Width = 0;
        int m_Height = 0;

    public:
        void Init(
            int width,
            int height,
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
            bool enableUnorderedAccess = false);
        void Uninit();

        void SetRenderTarget();
        void Clear(float r, float g, float b, float a);

        ID3D11ShaderResourceView* GetSRV()
        {
            return m_SRV.Get();
        }

        ID3D11UnorderedAccessView* GetUAV() { return m_UAV.Get(); }
        ID3D11Texture2D* GetTexture() { return m_Texture.Get(); }
        int GetWidth() const { return m_Width; }
        int GetHeight() const { return m_Height; }
    };
}
