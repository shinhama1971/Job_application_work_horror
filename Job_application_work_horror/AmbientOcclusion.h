// ============================================================================
// ファイルの役割: 画面空間の環境遮蔽（SSAO）。深度プリパスの深度から、物と物の隙間・壁の根元・棚の下など、
// 周りの光が届きにくい所を求め、壁と床のシェーダーが環境光と天井の照明を暗くするのに使う画像（t15）を作っている。
// 主な技術: Compute Shaderでの画面全体の計算、深度からの位置と法線の復元、半球の標本、深さを見ながらのぼかし
//
// ・これまでの「部屋の角の暗がり」（roomOcclusion）は壁の配置から計算する大まかな暗がりで、小物の下や隙間は暗くならなかった。
//   SSAOは画面に描かれた物そのものから求めるので、棚の下・ロッカーと壁の間・柱の根元まで暗くなる。
// ・計算は、本描画の直前（深度プリパスの後）に1回だけ行っている。反射や環境マップなど、ほかの視点の描画では使わない。
// ============================================================================

#pragma once

#include <SimpleMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "ComputeShader.h"

namespace Effect
{
    class AmbientOcclusion
    {
    public:
        void Init(int width, int height);
        void Uninit();
        // 深度プリパスの深度から環境遮蔽を求め、ぼかして、ピクセルシェーダーの t15 に設定している。
        // projection はプレイヤーの視点の射影行列、viewport は本描画の範囲
        void Compute(const DirectX::SimpleMath::Matrix& projection, const D3D11_VIEWPORT& viewport);
        // 使わない場面で、t15 から外している
        void Unbind();

    private:
        // シェーダーへ渡す値（ambientOcclusionCS.hlsl の AmbientOcclusionBuffer と同じ並び）
        struct Parameters
        {
            DirectX::SimpleMath::Matrix Projection;
            DirectX::SimpleMath::Matrix InverseProjection;
            DirectX::SimpleMath::Vector2 ViewportSize;
            DirectX::SimpleMath::Vector2 ViewportOffset;
            float Radius;
            float Intensity;
            float Bias;
            float MaxDistance;
            // 環境遮蔽の画像の大きさ（描く範囲の縦横半分）
            DirectX::SimpleMath::Vector2 OcclusionSize;
            DirectX::SimpleMath::Vector2 OcclusionPadding;
        };

        // 描く範囲の一番大きい大きさ（環境遮蔽の画像は、その縦横半分で作っている。軽くするため）
        int m_Width = 0;
        int m_Height = 0;
        // ぼかす前と、ぼかした後の環境遮蔽（1 = 遮る物なし）
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_RawTexture;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_RawUAV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_RawSRV;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_BlurredTexture;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_BlurredUAV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_BlurredSRV;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ParameterBuffer;
        ComputeShader m_OcclusionShader;
        ComputeShader m_BlurShader;
    };
}
