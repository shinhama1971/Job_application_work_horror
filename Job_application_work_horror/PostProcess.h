// ============================================================================
// ファイルの役割: 露出、ブルーム、CRT、霧など画面全体のシェーダー演出を統括します。
// 主な技術: Render To Texture、Compute Shader、Ping-Pong Blur、トーン調整
// ============================================================================

#pragma once

#include <algorithm>

#include "RenderTexture.h"
#include "FullScreenQuad.h"
#include "ComputeShader.h"

class GpuTimer;

namespace Effect
{
    class PostProcess
    {
    private:
        // enable系は機能そのもののON/OFF、target系は急変を防ぐ補間先です。
        bool m_EnableNoise = true;
        bool m_EnableBloom = true;
        bool m_EnableVolumetricLight = false;

        float m_Time = 0.0f;
        // 通常時は控えめにし、停電復旧などのイベント時だけ強く発光させます。
        float m_BloomBaseIntensity = 0.48f;
        float m_BloomIntensity = 0.48f;
        float m_BloomPulseStrength = 0.0f;
        float m_BloomPulseTimer = 0.0f;
        float m_BloomPulseDuration = 0.0f;
        float m_NoiseAmount = 0.18f;
        float m_TargetNoiseAmount = 0.18f;
        float m_VignetteStrength = 0.55f;
        float m_TargetVignetteStrength = 0.55f;
        float m_LensDistortionStrength = 0.32f;
        float m_HorrorPulseStrength = 0.0f;
        float m_HorrorPulsePeak = 0.0f;
        float m_HorrorPulseTimer = 0.0f;
        float m_HorrorPulseDuration = 0.0f;
        float m_Exposure = 1.0f;
        float m_TargetExposure = 1.0f;
        float m_UserBrightnessOffset = 0.0f;
        float m_UserEffectScale = 1.0f;
        float m_LensMoisture = 0.0f;
        float m_LensMoisturePeak = 0.0f;
        float m_LensMoistureTimer = 0.0f;
        float m_LensMoistureDuration = 0.0f;
        float m_CorridorTension = 0.0f;
        float m_TargetCorridorTension = 0.0f;
        float m_VolumetricIntensity = 0.58f;
        float m_TargetVolumetricIntensity = 0.58f;
        float m_FilmGradeStrength = 0.55f;
        float m_LensDirtStrength = 0.35f;
        float m_SignalInterference = 0.0f;
        float m_TargetSignalInterference = 0.0f;

        // シーン描画を一度RenderTextureへ保存し、後段で複数の画面効果を合成します。
        Graphics::RenderTexture m_RenderTexture;
        Graphics::RenderTexture m_BloomExtractTexture;
        Graphics::RenderTexture m_BloomHorizontalTexture;
        Graphics::RenderTexture m_BloomVerticalTexture;
        Graphics::FullScreenQuad m_FullScreenQuad;

        ComputeShader m_BloomExtractShader;
        ComputeShader m_BloomHorizontalShader;
        ComputeShader m_BloomVerticalShader;

        // 高輝度抽出 → 横ぼかし → 縦ぼかしの順でブルーム画像を生成します。
        void RunBloom();

    public:
        void Init();
        void Uninit();
        void Update();

        // 本描画が終わったバックバッファをコピーし、画面効果の入力にします。
        void CaptureBackBuffer();
        void Draw(GpuTimer* gpuTimer); // 保存したシーンへ画面効果を合成します。

        // 一時的な演出。指定した強さから始まり、durationかけて元に戻ります。
        // BloomPulse: 停電復旧などで一瞬強く光らせます。
        // HorrorPulse: 驚かせる瞬間に、ノイズ・周辺減光・色ずれ・画面の裂けを強めます。
        // LensMoisture: 水しぶきなどでレンズが濡れたように見せます（0〜1）。
        void TriggerBloomPulse(float peakIntensity, float duration);
        void TriggerHorrorPulse(float strength, float duration);
        void TriggerLensMoisture(float strength, float duration);

        // 緊張度・ノイズ・周辺減光・露出・ボリューム光・電波障害は目標値を設定し、
        // Updateで少しずつ近づけます（急に切り替わりません）。それ以外のSet系はその場で反映します。
        // 廊下の緊張度（0〜1）。高いほどノイズと画面の暗さが増します。
        void SetCorridorTension(float tension)
        {
            m_TargetCorridorTension = (std::clamp)(tension, 0.0f, 1.0f);
        }

        // 常時かかるノイズと周辺減光の強さです。
        void SetAtmosphere(float noiseAmount, float vignetteStrength)
        {
            m_TargetNoiseAmount = noiseAmount;
            m_TargetVignetteStrength = vignetteStrength;
        }

        void SetVolumetricLight(bool enable)
        {
            m_EnableVolumetricLight = enable;
        }

        void SetVolumetricIntensity(float intensity)
        {
            m_TargetVolumetricIntensity =
                (std::clamp)(intensity, 0.0f, 1.0f);
        }

        void SetBloomEnabled(bool enable)
        {
            m_EnableBloom = enable;
        }

        void SetNoise(bool enable)
        {
            m_EnableNoise = enable;
        }

        void SetExposure(float exposure)
        {
            m_TargetExposure = (std::clamp)(exposure, 0.85f, 1.20f);
        }

        // ポーズメニューの「明るさ」「演出強度」の設定値です。
        void SetUserBrightnessOffset(float offset)
        {
            m_UserBrightnessOffset =
                (std::clamp)(offset, -0.12f, 0.14f);
        }

        void SetUserEffectScale(float scale)
        {
            m_UserEffectScale =
                (std::clamp)(scale, 0.65f, 1.25f);
        }

        void SetLensDistortionStrength(float strength)
        {
            m_LensDistortionStrength = (std::clamp)(strength, 0.0f, 0.80f);
        }

        void SetFilmGradeStrength(float strength)
        {
            m_FilmGradeStrength = (std::clamp)(strength, 0.0f, 1.0f);
        }

        void SetLensDirtStrength(float strength)
        {
            m_LensDirtStrength = (std::clamp)(strength, 0.0f, 1.0f);
        }

        // 監視映像の電波障害のような横線と色ずれ（0〜1）です。
        void SetSignalInterference(float strength)
        {
            m_TargetSignalInterference =
                (std::clamp)(strength, 0.0f, 1.0f);
        }

        bool IsNoiseEnable() const
        {
            return m_EnableNoise;
        }

        // デバッグUIからの調整用。目標値ではなく、その場で値を変えます。
        void SetBloomIntensity(float intensity)
        {
            m_BloomBaseIntensity = intensity;
            m_BloomIntensity = intensity;
        }
    };
}
