// ============================================================================
// ファイルの役割: シェーダーの中で光線を進めて描く人の形の「距離場」（3Dテクスチャ）を読み込み、ピクセルシェーダーへ設定している。
// 主な技術: Direct3D 11 の3Dテクスチャ（float16）、端で折り返さない線形のサンプラー、焼いた距離場のファイル（tools/maya/build_figure_volumes.py）
// ・人影（figure.sdfv）は鏡と水たまりにだけ映る黒い人影、自分の体（self_body.sdfv）は鏡に映る自分。
//   どちらも物として置かず、鏡と床のシェーダーが光線と距離場で交差を求めている（shader/figureVolume.hlsli）。
// ・テクスチャ t6・t13 と、サンプラー s2 を使っている（ほかのシェーダーでは使っていない番号）。
// ============================================================================

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <string>

namespace Effect
{
    class FigureVolumes
    {
    public:
        // 2つの距離場を読み込んでいる（読めなかったときは、理由を出して終了している）
        void Init();
        // ピクセルシェーダーの t6（自分の体）・t13（人影）・s2 へ設定している（本描画の前に毎フレーム呼ぶ）
        void Bind() const;

    private:
        struct Volume
        {
            Microsoft::WRL::ComPtr<ID3D11Texture3D> Texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> View;
        };
        static Volume Load(const std::string& path);

        Volume m_Self;
        Volume m_Figure;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_Sampler;
    };
}
