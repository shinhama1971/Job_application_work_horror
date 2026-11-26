// ============================================================================
// ファイルの役割: 画面全体を覆う四角形で、画面効果（露出・ブルーム・光の筋・ブラウン管風の効果）を重ねて描いている。
// 主な技術: フルスクリーンクアッド、加算合成、ブルーム、露出補正、CRT風の効果、ボリュームライト（光の筋）
// ============================================================================

#include "FullScreenQuad.h"
#include "shader/renderingConfig.hlsli"
#include "Application.h"

using namespace DirectX::SimpleMath;

namespace
{
    // 画面効果がこれより弱ければ、その効果の描画を省く
    constexpr float EffectEpsilon = 0.001f;
    // 画面を覆う四角形の頂点の数（三角形の帯で描く）
    constexpr UINT QuadVertexCount = 4;
    // 残像の少し前の画面を読むテクスチャの番号（crtOverlayPS.hlsl の t1）
    constexpr UINT EchoTextureSlot = SIGNAL_LOST_CRT_ECHO_TEXTURE_SLOT;
    constexpr UINT DepthTextureSlot = SIGNAL_LOST_CRT_DEPTH_TEXTURE_SLOT;
    // 三角形ストリップの順に、位置とUVで同じ四隅を参照する。
    constexpr UINT TopLeftVertex = 0;
    constexpr UINT TopRightVertex = 1;
    constexpr UINT BottomLeftVertex = 2;
    constexpr UINT BottomRightVertex = 3;
    // コマ落ちしているかを見るしきい値（0 か 1。crtOverlayPS.hlsl の CRT_FRAME_HOLD_THRESHOLD と同じ）
    constexpr float FrameHoldThreshold = 0.5f;
    // 自動露出の倍率を読むシェーダーの番号（exposurePS.hlsl の t3）
    constexpr UINT AutoExposureTextureSlot = SIGNAL_LOST_AUTO_EXPOSURE_TEXTURE_SLOT;
}

namespace Graphics
{
    // 画面と同じ大きさの四角形（左上が原点の画素単位）と、5種類の画面効果のシェーダー、定数バッファを作っている
    bool FullScreenQuad::Init()
    {
        m_Vertices.resize(QuadVertexCount);

        float w = (float)Application::GetWidth();
        float h = (float)Application::GetHeight();

        m_Vertices[TopLeftVertex].position = Vector3(0.0f, 0.0f, 0.0f);
        m_Vertices[TopRightVertex].position = Vector3(w, 0.0f, 0.0f);
        m_Vertices[BottomLeftVertex].position = Vector3(0.0f, h, 0.0f);
        m_Vertices[BottomRightVertex].position = Vector3(w, h, 0.0f);

        m_Vertices[TopLeftVertex].color = Color(1, 1, 1, 1);
        m_Vertices[TopRightVertex].color = Color(1, 1, 1, 1);
        m_Vertices[BottomLeftVertex].color = Color(1, 1, 1, 1);
        m_Vertices[BottomRightVertex].color = Color(1, 1, 1, 1);

        m_Vertices[TopLeftVertex].uv = Vector2(0, 0);
        m_Vertices[TopRightVertex].uv = Vector2(1, 0);
        m_Vertices[BottomLeftVertex].uv = Vector2(0, 1);
        m_Vertices[BottomRightVertex].uv = Vector2(1, 1);

        m_VertexBuffer.Create(m_Vertices);

        // 三角形ストリップで描くので、インデックスは0〜3の4つだけ
        m_Indices.clear();
        m_Indices.push_back(0);
        m_Indices.push_back(1);
        m_Indices.push_back(2);
        m_Indices.push_back(3);

        m_IndexBuffer.Create(m_Indices);

        // どの段も後続の描画で使うため、読み込めなければ描画開始前に終了する。
        constexpr auto fullScreenVertexShader = "shader/unlitTextureVS.hlsl";
        if (!m_BloomShader.Create(fullScreenVertexShader, "shader/bloomCompositePS.hlsl") ||
            !m_VolumeShader.Create(fullScreenVertexShader, "shader/volumetricFlashlightPS.hlsl") ||
            !m_OverlayShader.Create(fullScreenVertexShader, "shader/crtOverlayPS.hlsl") ||
            !m_ExposureShader.Create(fullScreenVertexShader, "shader/exposurePS.hlsl") ||
            !m_ToneMapShader.Create(fullScreenVertexShader, "shader/toneMapPS.hlsl"))
        {
            return false;
        }

        m_Material = std::make_unique<Material>();

        MATERIAL mtrl{};
        mtrl.Diffuse = Color(1, 1, 1, 1);
        mtrl.TextureEnable = true;

        // 描画前に、使用する定数バッファがすべて作れたことを確認する。
        return m_Material->Create(mtrl) &&
            Renderer::CreateConstantBuffer(sizeof(TimeBuffer), m_TimeBuffer.ReleaseAndGetAddressOf()) &&
            Renderer::CreateConstantBuffer(sizeof(ToneMapBuffer), m_ToneMapBuffer.ReleaseAndGetAddressOf());
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
        context->PSSetConstantBuffers(SIGNAL_LOST_TONE_MAP_BUFFER_SLOT, 1, &toneMapBuffer);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        Renderer::SetBlendState(BS_NONE);
        context->PSSetShaderResources(0, 1, &hdrSceneSRV);
        context->DrawIndexed(QuadVertexCount, 0, 0);

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
        float lensDropAge,
        const DirectX::SimpleMath::Vector4* presenceWarps,
        ID3D11ShaderResourceView* echoSRV,
        const DirectX::SimpleMath::Vector4& echo,
        float submergeLevel,
        float otherworld,
        float otherworldSeed,
        const DirectX::SimpleMath::Vector4& depthOfField,
        ID3D11ShaderResourceView* depthSRV)
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
        for (unsigned int index = 0; index < PresenceWarpCount; ++index)
        {
            tb.presenceWarps[index] = presenceWarps != nullptr ? presenceWarps[index] : DirectX::SimpleMath::Vector4::Zero;
        }
        // 少し前の画面がないときは、残像を出さない
        tb.echo = echoSRV != nullptr ? echo : DirectX::SimpleMath::Vector4::Zero;
        tb.submerge = DirectX::SimpleMath::Vector4(submergeLevel, otherworld, otherworldSeed, 0.0f);
        // 深度が読めないときは、ピントの効果を出さない
        tb.depthOfField = depthSRV != nullptr ? depthOfField : DirectX::SimpleMath::Vector4::Zero;
        bool presenceWarpActive = false;
        for (const DirectX::SimpleMath::Vector4& warp : tb.presenceWarps)
        {
            presenceWarpActive = presenceWarpActive || warp.w > EffectEpsilon;
        }

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
        context->PSSetConstantBuffers(SIGNAL_LOST_POST_EFFECT_BUFFER_SLOT, 1, &timeBuffer);

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
        context->PSSetShaderResources(AutoExposureTextureSlot, 1, &autoExposureSRV);
        context->DrawIndexed(QuadVertexCount, 0, 0);
        context->PSSetShaderResources(0, 1, &nullResource);
        // 次のフレームでCompute Shaderが書き込めるよう、読み取りを外している
        context->PSSetShaderResources(AutoExposureTextureSlot, 1, &nullResource);

        // 露出の補正の後にブルームを加算し、明るい照明のにじみを残している。
        // 大きさの違う段（t0〜t3。1/4・1/8・1/16・1/32）を合成のシェーダーで重ねている
        if (bloomSRVs != nullptr && bloomIntensity > EffectEpsilon)
        {
            m_BloomShader.SetGPU();
            Renderer::SetBlendState(BS_ADDITIVE);
            context->PSSetShaderResources(0, BloomLevelCount, bloomSRVs);
            context->DrawIndexed(QuadVertexCount, 0, 0);
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
            context->DrawIndexed(QuadVertexCount, 0, 0);
        }

        // CRT風の効果は半透明の重ね描きとして合成し、画面全体を黒で塗りつぶさないようにしている。
        if (noiseAmount > 0.0f || horrorPulseStrength > EffectEpsilon ||
            lensMoisture > EffectEpsilon || filmGradeStrength > EffectEpsilon ||
            signalInterference > EffectEpsilon || frameHold > FrameHoldThreshold || fearVision > EffectEpsilon ||
            presenceWarpActive || tb.echo.x > EffectEpsilon || submergeLevel > EffectEpsilon ||
            otherworld > EffectEpsilon || tb.depthOfField.x > EffectEpsilon)
        {
            m_OverlayShader.SetGPU();
            Renderer::SetBlendState(BS_ALPHABLEND);
            context->PSSetShaderResources(0, 1, &sceneSRV);
            context->PSSetShaderResources(EchoTextureSlot, 1, &echoSRV);
            // ピントの効果を出すときは、深度を読むため、描画先から深度バッファを外している（読み取りと書き込みに同時に使えない）
            const bool useDepth = tb.depthOfField.x > EffectEpsilon;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> currentTarget;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> currentDepth;
            if (useDepth)
            {
                context->OMGetRenderTargets(1, currentTarget.GetAddressOf(), currentDepth.GetAddressOf());
                ID3D11RenderTargetView* target = currentTarget.Get();
                context->OMSetRenderTargets(1, &target, nullptr);
                context->PSSetShaderResources(DepthTextureSlot, 1, &depthSRV);
                context->PSSetShaderResources(AutoExposureTextureSlot, 1, &autoExposureSRV);
            }
            context->DrawIndexed(QuadVertexCount, 0, 0);
            context->PSSetShaderResources(0, 1, &nullResource);
            context->PSSetShaderResources(EchoTextureSlot, 1, &nullResource);
            if (useDepth)
            {
                context->PSSetShaderResources(DepthTextureSlot, 1, &nullResource);
                context->PSSetShaderResources(AutoExposureTextureSlot, 1, &nullResource);
                ID3D11RenderTargetView* target = currentTarget.Get();
                context->OMSetRenderTargets(1, &target, currentDepth.Get());
            }
        }

        // ブレンドと深度の設定を元に戻している
        Renderer::SetBlendState(BS_NONE);
        Renderer::SetDepthEnable(true);
    }
}
