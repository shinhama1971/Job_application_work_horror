// ============================================================================
// ファイルの役割: 画面以外に描くためのテクスチャと、その描画先（RTV）・読み取り口（SRV）・書き込み口（UAV）・深度を管理している。
// 主な技術: Render Target View、Shader Resource View、Unordered Access View、Depth Stencil
// ============================================================================

#pragma once
#include <wrl/client.h>
#include "Renderer.h"

namespace Graphics
{
    // 描画先にも、シェーダーで読むテクスチャにもなる1枚の画像（反射・画面効果・ブルーム・監視映像で使っている）
    class RenderTexture
    {
    private:
        // テクスチャ本体、描画先として使う口、シェーダーから読む口、コンピュートシェーダーから書く口（必要なときだけ）
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_UAV;
        // 深度バッファ（ブルーム用には作らない）、大きさ
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthView;
        int m_Width = 0;
        int m_Height = 0;

    public:
        // 指定の大きさと形式で作っている。enableUnorderedAccessをtrueにすると、深度の代わりにUAVを作っている
        void Init(
            int width,
            int height,
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
            bool enableUnorderedAccess = false);
        void Uninit();

        // このテクスチャを描画先にしている
        void SetRenderTarget();
        // 指定した色で塗りつぶしている
        void Clear(float r, float g, float b, float a);

        // シェーダーから読む口・コンピュートシェーダーから書く口・テクスチャ本体・大きさを返している
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
