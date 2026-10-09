// ============================================================================
// ファイルの役割: 画面全体を覆う四角形で、画面効果（露出・ブルーム・光の筋・ブラウン管風の効果）を重ねて描いている。
// 主な技術: フルスクリーンクアッド、加算合成、ブルーム、露出補正、CRT風の効果、ボリュームライト（光の筋）
// ============================================================================

#pragma once

#include <wrl/client.h>
#include "Renderer.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

namespace Graphics
{
    // 描画済みの画面の上に、画面効果を重ねて描くための四角形
    class FullScreenQuad
    {
    private:
        // 画面効果のシェーダーへ渡す値（定数バッファb0）。16バイト単位にそろえるため、最後に詰め物を入れている
        struct TimeBuffer
        {
            // 経過時間（ノイズなどを動かすのに使っている）
            float time;
            // ブルームの強さ、フィルムノイズの量、周辺減光の強さ
            float bloomIntensity;
            float noiseAmount;
            float vignetteStrength;
            // 画面の縦横比、光の筋の強さ、レンズの歪みの強さ、驚かせる演出の強さ
            float screenAspect;
            float volumeIntensity;
            float lensDistortionStrength;
            float horrorPulseStrength;
            // 露出、レンズの曇り、ループ廊下の緊張感、映画風の色調の強さ、レンズの汚れのにじみ、信号の乱れ
            float exposure;
            float lensMoisture;
            float corridorTension;
            float filmGradeStrength;
            float lensDirtStrength;
            float signalInterference;
            // 詰め物
            float postProcessPadding1;
            float postProcessPadding2;
        };

        // 四角形の頂点・インデックスとGPUのバッファ
        std::vector<VERTEX_3D> m_Vertices;
        std::vector<unsigned int> m_Indices;

        VertexBuffer<VERTEX_3D> m_VertexBuffer;
        IndexBuffer m_IndexBuffer;

        // ブルーム・光の筋・CRT風の効果・露出の補正のシェーダーと、テクスチャを使うマテリアル
        Shader m_BloomShader;
        Shader m_VolumeShader;
        Shader m_OverlayShader;
        Shader m_ExposureShader;
        std::unique_ptr<Material> m_Material;

        // 画面効果の値を入れる定数バッファ
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_TimeBuffer;

    public:
        // 四角形・シェーダー・定数バッファを作る／解放する
        void Init();
        void Uninit();

        // sceneSRVは描画済みの画面、bloomSRVはぼかした明るい部分、autoExposureSRVは自動露出の倍率（露出のシェーダーのt3）。
        // 値を定数バッファへ入れて、効果を順に重ねて描いている
        void Draw(
            ID3D11ShaderResourceView* sceneSRV,
            ID3D11ShaderResourceView* bloomSRV,
            ID3D11ShaderResourceView* autoExposureSRV,
            float time,
            float bloomIntensity,
            float noiseAmount,
            float vignetteStrength,
            float volumeIntensity,
            float lensDistortionStrength,
            float horrorPulseStrength,
            float exposure,
            float lensMoisture,
            float corridorTension,
            float filmGradeStrength,
            float lensDirtStrength,
            float signalInterference);
    };
}
