// ============================================================================
// ファイルの役割: 画面効果のうち、明るい部分のにじみ（ブルーム：抽出と2方向のぼかし）と、目の慣れ（自動露出）の Compute Shader を実行している。
// （PostProcess.cpp から分けた。説明は元のファイルとヘッダーを参照）
// ============================================================================

#include "PostProcess.h"
#include "MathUtility.h"
#include "GpuTimer.h"
#include "Renderer.h"
#include "Application.h"
#include "Utility.h"
#include "CaptureMode.h"
#include "shader/computeDispatchConfig.hlsli"
#include <algorithm>
#include <cmath>

namespace
{
    // 1面でライトを点けた廊下の測光値を基準とし、順応の強さを抑えて暗さを残す。
    constexpr float AutoExposureTargetLuminance = 0.075f;
    constexpr float AutoExposureMinGain = 0.95f;
    constexpr float AutoExposureMaxGain = 1.08f;
    constexpr float DarkAdaptationRatePerSecond = 0.35f;
    constexpr float LightAdaptationRatePerSecond = 2.5f;
    // 自動露出の1フレームの時間の上限（長い停止の後に一気に慣れないように）
    constexpr float MaxAutoExposureDeltaTime = 0.1f;

    // --- ブルームのコンピュートシェーダーの1グループの大きさ（シェーダーの numthreads と同じ）。
    // 明るい部分の抽出と縮小は8x8画素、横と縦のぼかしは1行（1列）の128画素
    constexpr UINT BloomTileGroupSize = SIGNAL_LOST_BLOOM_TILE_GROUP_SIZE;
    constexpr UINT BloomBlurGroupSize = SIGNAL_LOST_BLOOM_BLUR_GROUP_SIZE;

    // 画素の数を、1グループの大きさで割って切り上げた、グループの数
    constexpr UINT GroupCount(UINT pixels, UINT groupSize)
    {
        return (pixels + groupSize - 1) / groupSize;
    }
}

namespace Effect
{
    // UAVとSRVを同時に設定しないようにしながら、明るい部分の抽出と、2方向のぼかしを順番に実行している。
    void PostProcess::RunBloom()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11RenderTargetView* nullRenderTarget = nullptr;
        context->OMSetRenderTargets(1, &nullRenderTarget, nullptr);

        const UINT bloomWidth = static_cast<UINT>(m_BloomExtractTexture.GetWidth());
        const UINT bloomHeight = static_cast<UINT>(m_BloomExtractTexture.GetHeight());

        // 1つの段階を実行している：入力をSRV、出力をUAVに設定してDispatchし、終わったら両方を外している
        const auto dispatchPass = [context](
            const ComputeShader& shader,
            ID3D11ShaderResourceView* input,
            ID3D11UnorderedAccessView* output,
            UINT groupX,
            UINT groupY)
        {
            shader.SetGPU();
            context->CSSetShaderResources(SIGNAL_LOST_BLOOM_COMPUTE_INPUT_SLOT, 1, &input);
            context->CSSetUnorderedAccessViews(SIGNAL_LOST_BLOOM_COMPUTE_OUTPUT_SLOT, 1, &output, nullptr);
            context->Dispatch(groupX, groupY, 1);

            ID3D11ShaderResourceView* nullSRV = nullptr;
            ID3D11UnorderedAccessView* nullUAV = nullptr;
            context->CSSetShaderResources(SIGNAL_LOST_BLOOM_COMPUTE_INPUT_SLOT, 1, &nullSRV);
            context->CSSetUnorderedAccessViews(SIGNAL_LOST_BLOOM_COMPUTE_OUTPUT_SLOT, 1, &nullUAV, nullptr);
        };

        // 明るい部分の抽出（8x8の画素を1グループとして処理している）
        dispatchPass(
            m_BloomExtractShader,
            m_RenderTexture.GetSRV(),
            m_BloomExtractTexture.GetUAV(),
            GroupCount(bloomWidth, BloomTileGroupSize),
            GroupCount(bloomHeight, BloomTileGroupSize));

        // 横のぼかし（1行を128画素ずつのグループで処理している）
        dispatchPass(
            m_BloomHorizontalShader,
            m_BloomExtractTexture.GetSRV(),
            m_BloomHorizontalTexture.GetUAV(),
            GroupCount(bloomWidth, BloomBlurGroupSize),
            bloomHeight);

        // 縦のぼかし（1列を128画素ずつのグループで処理している）
        dispatchPass(
            m_BloomVerticalShader,
            m_BloomHorizontalTexture.GetSRV(),
            m_BloomVerticalTexture.GetUAV(),
            bloomWidth,
            GroupCount(bloomHeight, BloomBlurGroupSize));

        // 広くにじむ段：1つ上の段のぼかした画像を縦横半分に縮め（8x8画素を1グループ）、横と縦にぼかしている。
        // 同じ9画素のぼかしでも、段が小さいほど画面の上では広くにじむ
        ID3D11ShaderResourceView* previousLevel = m_BloomVerticalTexture.GetSRV();
        for (unsigned int level = 0; level < WideBloomLevelCount; ++level)
        {
            const UINT levelWidth = static_cast<UINT>(m_WideBloomDownsampled[level].GetWidth());
            const UINT levelHeight = static_cast<UINT>(m_WideBloomDownsampled[level].GetHeight());
            dispatchPass(
                m_BloomDownsampleShader,
                previousLevel,
                m_WideBloomDownsampled[level].GetUAV(),
                GroupCount(levelWidth, BloomTileGroupSize),
                GroupCount(levelHeight, BloomTileGroupSize));
            dispatchPass(
                m_BloomHorizontalShader,
                m_WideBloomDownsampled[level].GetSRV(),
                m_WideBloomHorizontal[level].GetUAV(),
                GroupCount(levelWidth, BloomBlurGroupSize),
                levelHeight);
            dispatchPass(
                m_BloomVerticalShader,
                m_WideBloomHorizontal[level].GetSRV(),
                m_WideBloomBlurred[level].GetUAV(),
                levelWidth,
                GroupCount(levelHeight, BloomBlurGroupSize));
            previousLevel = m_WideBloomBlurred[level].GetSRV();
        }

        context->CSSetShader(nullptr, nullptr, 0);
        Renderer::SetBackBufferRenderTarget();
    }

    // 描いた画面（m_RenderTexture）の明るさを1グループ256スレッドで測り、目の慣れの倍率をバッファへ書いている。
    // 結果はGPUに置いたまま、続く露出のシェーダーが読むため、CPUがGPUを待つことはない。
    void PostProcess::RunAutoExposure()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        AutoExposureParams params{};
        // ポーズ中なども時間は進めている。ただし長い停止の後に一気に慣れないよう、1フレームの時間に上限を付けている
        params.DeltaTime = (std::min)(Application::GetDeltaTime(), MaxAutoExposureDeltaTime);
        // 目標の明るさ。1面の懐中電灯を点けた廊下（画面の明るさの平均が0.06〜0.12）で、倍率がほぼ1になる値にしている
        params.TargetLuminance = AutoExposureTargetLuminance;
        // 倍率の範囲：暗い所では最大8%明るくし、懐中電灯で近くの壁を照らしているときは、シーン側の持ち上げを少し弱める
        params.MinGain = AutoExposureMinGain;
        params.MaxGain = AutoExposureMaxGain;
        // 暗い所へは約3秒かけてゆっくり、明るい所へは約0.4秒で素早く慣れる
        params.DarkAdaptSpeed = DarkAdaptationRatePerSecond;
        params.LightAdaptSpeed = LightAdaptationRatePerSecond;
        params.Enabled = m_EnableAutoExposure ? 1.0f : 0.0f;
        params.ResetAdaptation = m_ResetAutoExposure ? 1.0f : 0.0f;
        m_ResetAutoExposure = false;
        context->UpdateSubresource(m_AutoExposureParams.Get(), 0, nullptr, &params, 0, 0);

        // 画面のテクスチャを描画先から外したうえで、読み取りに使っている
        ID3D11RenderTargetView* nullRenderTarget = nullptr;
        context->OMSetRenderTargets(1, &nullRenderTarget, nullptr);

        m_AutoExposureShader.SetGPU();
        ID3D11Buffer* paramsBuffer = m_AutoExposureParams.Get();
        context->CSSetConstantBuffers(SIGNAL_LOST_AUTO_EXPOSURE_PARAMS_SLOT, 1, &paramsBuffer);
        ID3D11ShaderResourceView* sceneSRV = m_RenderTexture.GetSRV();
        context->CSSetShaderResources(SIGNAL_LOST_AUTO_EXPOSURE_SCENE_SLOT, 1, &sceneSRV);
        ID3D11UnorderedAccessView* stateUAV = m_AutoExposureUAV.Get();
        context->CSSetUnorderedAccessViews(SIGNAL_LOST_AUTO_EXPOSURE_STATE_SLOT, 1, &stateUAV, nullptr);

        // 1グループだけで画面全体を測っている（64x64点）
        context->Dispatch(1, 1, 1);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        context->CSSetShaderResources(SIGNAL_LOST_AUTO_EXPOSURE_SCENE_SLOT, 1, &nullSRV);
        context->CSSetUnorderedAccessViews(SIGNAL_LOST_AUTO_EXPOSURE_STATE_SLOT, 1, &nullUAV, nullptr);
        context->CSSetShader(nullptr, nullptr, 0);
        Renderer::SetBackBufferRenderTarget();
    }
}
