// ============================================================================
// ファイルの役割: 画面全体を覆う四角形で、画面効果（露出・ブルーム・光の筋・ブラウン管風の効果）を重ねて描いている。
// 主な技術: フルスクリーンクアッド、加算合成、ブルーム、露出補正、CRT風の効果、ボリュームライト（光の筋）
// ============================================================================

#include "FullScreenQuad.h"
#include "Application.h"

using namespace DirectX::SimpleMath;

namespace Graphics
{
    // 画面と同じ大きさの四角形（左上が原点の画素単位）と、5種類の画面効果のシェーダー、定数バッファを作っている
    void FullScreenQuad::Init()
    {
        m_Vertices.resize(4);

        float w = (float)Application::GetWidth();
        float h = (float)Application::GetHeight();

        m_Vertices[0].position = Vector3(0.0f, 0.0f, 0.0f);
        m_Vertices[1].position = Vector3(w, 0.0f, 0.0f);
        m_Vertices[2].position = Vector3(0.0f, h, 0.0f);
        m_Vertices[3].position = Vector3(w, h, 0.0f);

        m_Vertices[0].color = Color(1, 1, 1, 1);
        m_Vertices[1].color = Color(1, 1, 1, 1);
        m_Vertices[2].color = Color(1, 1, 1, 1);
        m_Vertices[3].color = Color(1, 1, 1, 1);

        m_Vertices[0].uv = Vector2(0, 0);
        m_Vertices[1].uv = Vector2(1, 0);
        m_Vertices[2].uv = Vector2(0, 1);
        m_Vertices[3].uv = Vector2(1, 1);

        m_VertexBuffer.Create(m_Vertices);

        // 三角形ストリップで描くので、インデックスは0〜3の4つだけ
        m_Indices.clear();
        m_Indices.push_back(0);
        m_Indices.push_back(1);
        m_Indices.push_back(2);
        m_Indices.push_back(3);

        m_IndexBuffer.Create(m_Indices);

        // ブルーム（明るい部分のにじみ）を足すシェーダー
        m_BloomShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/bloomCompositePS.hlsl"
        );

        // 懐中電灯の光の筋（空気中のちりで光が見える効果）を足すシェーダー
        m_VolumeShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/volumetricFlashlightPS.hlsl"
        );

        // ノイズ・周辺減光・画面の乱れなどを重ねるシェーダー
        m_OverlayShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/crtOverlayPS.hlsl"
        );

        // 目が暗さ・明るさに慣れる効果（露出の補正）を足すシェーダー
        m_ExposureShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/exposurePS.hlsl"
        );

        // HDRの画面を0〜1へ移すシェーダー（画面効果の最後）
        m_ToneMapShader.Create(
            "shader/unlitTextureVS.hlsl",
            "shader/toneMapPS.hlsl"
        );

        m_Material = std::make_unique<Material>();

        MATERIAL mtrl{};
        mtrl.Diffuse = Color(1, 1, 1, 1);
        mtrl.TextureEnable = true;

        m_Material->Create(mtrl);

        // 画面効果の値を渡す定数バッファ
        Renderer::CreateConstantBuffer(
            sizeof(TimeBuffer),
            m_TimeBuffer.ReleaseAndGetAddressOf()
        );
        Renderer::CreateConstantBuffer(
            sizeof(ToneMapBuffer),
            m_ToneMapBuffer.ReleaseAndGetAddressOf()
        );
    }

    // 定数バッファを解放している
    void FullScreenQuad::Uninit()
    {
        m_ToneMapBuffer.Reset();
        m_TimeBuffer.Reset();
    }

    // HDRの画面を、選んだ曲線で0〜1へ移して描いている。合成せずに上書きするので、描画先の前の中身は残らない
    void FullScreenQuad::DrawToneMap(ID3D11ShaderResourceView* hdrSceneSRV, unsigned int mode)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        Renderer::SetWorldViewProjection2D();
        Renderer::SetDepthEnable(false);
        Renderer::SetUV(0.0f, 0.0f, 1.0f, 1.0f);

        ToneMapBuffer buffer{};
        buffer.mode = mode;
        context->UpdateSubresource(m_ToneMapBuffer.Get(), 0, nullptr, &buffer, 0, 0);

        m_VertexBuffer.SetGPU();
        m_IndexBuffer.SetGPU();
        m_Material->SetGPU();
        m_ToneMapShader.SetGPU();
        ID3D11Buffer* toneMapBuffer = m_ToneMapBuffer.Get();
        context->PSSetConstantBuffers(0, 1, &toneMapBuffer);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        Renderer::SetBlendState(BS_NONE);
        context->PSSetShaderResources(0, 1, &hdrSceneSRV);
        context->DrawIndexed(4, 0, 0);

        // 次のフレームでHDRの描き先へ描けるよう、読み取りを外している
        ID3D11ShaderResourceView* nullResource = nullptr;
        context->PSSetShaderResources(0, 1, &nullResource);
        Renderer::SetDepthEnable(true);
    }

    // 画面効果の値を定数バッファへ入れ、描画済みの画面の上に効果を順に重ねて描いている
    void FullScreenQuad::Draw(
        ID3D11ShaderResourceView* sceneSRV,
        ID3D11ShaderResourceView* const* bloomSRVs,
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
        float signalInterference,
        float frameHold,
        float frameHoldOffset,
        float fearVision,
        float fearBeat,
        float lensDropSeed,
        float lensDropAge)
    {
        ID3D11DeviceContext* context =
            Renderer::GetDeviceContext();

        // 2D用の行列にし、深度は使わずに描いている
        Renderer::SetWorldViewProjection2D();
        Renderer::SetDepthEnable(false);
        Renderer::SetUV(0.0f, 0.0f, 1.0f, 1.0f);

        // シェーダーへ渡す画面効果の値（画面の縦横比も一緒に渡している）
        TimeBuffer tb{};
        tb.time = time;
        tb.bloomIntensity = bloomIntensity;
        tb.noiseAmount = noiseAmount;
        tb.vignetteStrength = vignetteStrength;
        tb.screenAspect = static_cast<float>(Application::GetWidth()) /
            static_cast<float>(Application::GetHeight());
        tb.volumeIntensity = volumeIntensity;
        tb.lensDistortionStrength = lensDistortionStrength;
        tb.horrorPulseStrength = horrorPulseStrength;
        tb.exposure = exposure;
        tb.lensMoisture = lensMoisture;
        tb.corridorTension = corridorTension;
        tb.filmGradeStrength = filmGradeStrength;
        tb.lensDirtStrength = lensDirtStrength;
        tb.signalInterference = signalInterference;
        tb.frameHold = frameHold;
        tb.frameHoldOffset = frameHoldOffset;
        tb.fearVision = fearVision;
        tb.fearBeat = fearBeat;
        tb.lensDropSeed = lensDropSeed;
        tb.lensDropAge = lensDropAge;

        context->UpdateSubresource(
            m_TimeBuffer.Get(),
            0,
            nullptr,
            &tb,
            0,
            0
        );

        m_VertexBuffer.SetGPU();
        m_IndexBuffer.SetGPU();
        m_Material->SetGPU();

        ID3D11Buffer* timeBuffer = m_TimeBuffer.Get();
        context->PSSetConstantBuffers(0, 1, &timeBuffer);

        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
        );

        ID3D11ShaderResourceView* nullResource = nullptr;

        // 目の順応による明るさの差だけを加算している。
        // 読み取り用のSRVが使えない場合も、元のバックバッファがそのまま見える状態を保っている。
        m_ExposureShader.SetGPU();
        Renderer::SetBlendState(BS_ADDITIVE);
        context->PSSetShaderResources(0, 1, &sceneSRV);
        // 自動露出の倍率は、Compute Shaderが書いたバッファをそのまま読んでいる（CPUへは読み戻さない）
        context->PSSetShaderResources(3, 1, &autoExposureSRV);
        context->DrawIndexed(4, 0, 0);
        context->PSSetShaderResources(0, 1, &nullResource);
        // 次のフレームでCompute Shaderが書き込めるよう、読み取りを外している
        context->PSSetShaderResources(3, 1, &nullResource);

        // 露出の補正の後にブルームを加算し、明るい照明のにじみを残している。
        // 大きさの違う段（t0〜t3。1/4・1/8・1/16・1/32）を合成のシェーダーで重ねている
        if (bloomSRVs != nullptr && bloomIntensity > 0.001f)
        {
            m_BloomShader.SetGPU();
            Renderer::SetBlendState(BS_ADDITIVE);
            context->PSSetShaderResources(0, BloomLevelCount, bloomSRVs);
            context->DrawIndexed(4, 0, 0);
        }

        // 次の描画で同じテクスチャへ書き込めるよう、読み取りの設定を外している
        ID3D11ShaderResourceView* nullBloomResources[BloomLevelCount] = {};
        context->PSSetShaderResources(0, BloomLevelCount, nullBloomResources);

        // 懐中電灯の光線の上で、短い区間の大気の散乱を積み重ねている。
        // シャドウマップの深度を参照し、壁の位置で光の筋を止めている。
        if (volumeIntensity > 0.0f)
        {
            m_VolumeShader.SetGPU();
            Renderer::SetBlendState(BS_ADDITIVE);
            context->DrawIndexed(4, 0, 0);
        }

        // CRT風の効果は半透明の重ね描きとして合成し、画面全体を黒で塗りつぶさないようにしている。
        if (noiseAmount > 0.0f || horrorPulseStrength > 0.001f ||
            lensMoisture > 0.001f || filmGradeStrength > 0.001f ||
            signalInterference > 0.001f || frameHold > 0.5f || fearVision > 0.001f)
        {
            m_OverlayShader.SetGPU();
            Renderer::SetBlendState(BS_ALPHABLEND);
            context->PSSetShaderResources(0, 1, &sceneSRV);
            context->DrawIndexed(4, 0, 0);
            context->PSSetShaderResources(0, 1, &nullResource);
        }

        // ブレンドと深度の設定を元に戻している
        Renderer::SetBlendState(BS_NONE);
        Renderer::SetDepthEnable(true);
    }
}
