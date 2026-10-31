// ============================================================================
// ファイルの役割: 懐中電灯（ライト）から見た深度を描き、影の判定に使うテクスチャを作っている。
// 主な技術: 深度テクスチャ、比較サンプラー、ライトの行列、深度だけを書く描画
// ============================================================================

#pragma once

#include <wrl/client.h>
#include "Renderer.h"
#include "Shader.h"

class Camera;

namespace Effect
{
    // 懐中電灯はカメラと同じ位置・向きから出ているので、カメラの位置から見た深度を描いて影に使っている。
    class ShadowMap
    {
    private:
        // シェーダーへ渡す値（b8）：ライトのビュー×射影の行列と、x=1テクセルの大きさ、y=深度の比較のずれ（バイアス）、z=near、w=far、
        // 影の方式（x=0なら影なし、1なら硬い影、2ならPCSS。技術デモで切り替えている）、
        // 揺れる裸電球の影（PointShadowMap。xyz=電球の位置、w=光の届く範囲）と、その設定（x=1なら使う、y=1ならPBRを止める（技術デモ））。
        // 定数バッファの番号（b0〜b13）がすべて使われているため、懐中電灯の影と同じ定数バッファにまとめている。
        struct ShadowBuffer
        {
            DirectX::SimpleMath::Matrix ViewProjection;
            DirectX::SimpleMath::Vector4 Parameters;
            DirectX::SimpleMath::Vector4 Options;
            DirectX::SimpleMath::Vector4 PointShadowLight;
            DirectX::SimpleMath::Vector4 PointShadowOptions;
        };

        // 影のテクスチャの大きさ（1024x1024）
        static constexpr UINT ShadowResolution = 1024;

        // 深度テクスチャ、深度を書く口、シェーダーから読む口、深度を比べて読むサンプラー
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthView;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_ComparisonSampler;
        // 影を描くときのラスタライザー（深度を少しずらす設定）、元のラスタライザー、定数バッファ
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_ShadowRasterizer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_PreviousRasterizer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_ShadowBuffer;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_TwoSidedRasterizer;// （今は使っていない）

        // 影を描く前のビューポート（終わったら戻す）
        D3D11_VIEWPORT m_PreviousViewport{};
        UINT m_PreviousViewportCount = 1;
        // 深度だけを書くシェーダー
        Shader m_DepthShader;
        // 最後にb8へ送った値（電球の影の値だけを変えて送り直すときに使う）と、影を描くシェーダーの差し替え先
        ShadowBuffer m_BufferData{};
        Shader* m_ShaderOverride = nullptr;

        // m_BufferData をb8へ送り、頂点・ピクセルシェーダーに設定している
        void UploadBuffer();

    public:
        // テクスチャ・サンプラー・ラスタライザー・定数バッファ・シェーダーを作る／解放する
        void Init();
        void Uninit();
        // 影を描き始めている：描画先を深度テクスチャにし、カメラの位置から見た行列を設定している
        void Begin(const Camera& camera);
        // 影を描き終えている：描画先と状態を戻し、影のテクスチャをt5、比較サンプラーをs1に設定している
        void End();
        // 描き直さないフレームに、前に描いた影のテクスチャを設定している
        void Bind();
        // 深度だけを書くシェーダーを設定している
        void SetShader();
        // 別の影（揺れる裸電球の影）を描く間だけ、DrawShadowが使うシェーダーを差し替えている（nullptrで元に戻る）
        void SetShaderOverride(Shader* shader) { m_ShaderOverride = shader; }
        // 揺れる裸電球の影の位置と範囲を、シェーダーへ渡している（enabledがfalseなら影を使わない）
        void SetPointShadow(const DirectX::SimpleMath::Vector3& lightPosition, float range, bool enabled);
        // 壁と小物を物理ベースの材質（PBR）で描くかを、シェーダーへ渡している（技術デモの比較用。falseなら拡散反射だけ）
        void SetPbrEnabled(bool enabled);
    };
}
