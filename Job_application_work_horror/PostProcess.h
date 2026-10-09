// ============================================================================
// ファイルの役割: 露出・ブルーム・ブラウン管風の効果・光の筋など、画面全体に掛けるシェーダーの演出をまとめて管理している。
// 主な技術: Render To Texture、Compute Shader、横と縦に分けたぼかし（2枚のテクスチャを交互に使う）、明るさと色の調整
// ============================================================================

#pragma once

#include <algorithm>

#include "RenderTexture.h"
#include "FullScreenQuad.h"
#include "ComputeShader.h"

class GpuTimer;

namespace Effect
{
    // 描き終えた画面を取り込み、ブルームを作り、露出やノイズなどの画面効果を重ねている。演出からは一時的な効果を起こせる。
    class PostProcess
    {
    private:
        // enable系は機能そのもののON/OFF、target系は急に変わらないよう、少しずつ近づける先の値。
        bool m_EnableNoise = true;
        bool m_EnableBloom = true;
        bool m_EnableVolumetricLight = false;

        // 経過時間（ノイズなどを動かす）
        float m_Time = 0.0f;
        // 普段は控えめにし、停電からの復旧などのイベントのときだけ強く光らせている。
        float m_BloomBaseIntensity = 0.48f;
        float m_BloomIntensity = 0.48f;
        // ブルームを一時的に強める量・残り秒数・長さ
        float m_BloomPulseStrength = 0.0f;
        float m_BloomPulseTimer = 0.0f;
        float m_BloomPulseDuration = 0.0f;
        // フィルムノイズの量、周辺減光の強さ（今の値と目標）、レンズの歪みの強さ
        float m_NoiseAmount = 0.18f;
        float m_TargetNoiseAmount = 0.18f;
        float m_VignetteStrength = 0.55f;
        float m_TargetVignetteStrength = 0.55f;
        float m_LensDistortionStrength = 0.32f;
        // 驚かせる演出の強さ（今・最大）、残り秒数、長さ
        float m_HorrorPulseStrength = 0.0f;
        float m_HorrorPulsePeak = 0.0f;
        float m_HorrorPulseTimer = 0.0f;
        float m_HorrorPulseDuration = 0.0f;
        // 露出（今の値と目標）、ポーズメニューの明るさと演出の強さ
        float m_Exposure = 1.0f;
        float m_TargetExposure = 1.0f;
        float m_UserBrightnessOffset = 0.0f;
        float m_UserEffectScale = 1.0f;
        // レンズの曇り（今・最大）、残り秒数、長さ
        float m_LensMoisture = 0.0f;
        float m_LensMoisturePeak = 0.0f;
        float m_LensMoistureTimer = 0.0f;
        float m_LensMoistureDuration = 0.0f;
        // ループ廊下の緊張感、光の筋の強さ（今の値と目標）
        float m_CorridorTension = 0.0f;
        float m_TargetCorridorTension = 0.0f;
        float m_VolumetricIntensity = 0.58f;
        float m_TargetVolumetricIntensity = 0.58f;
        // 映画風の色調の強さ、レンズの汚れのにじみ、監視映像のような信号の乱れ（今の値と目標）
        float m_FilmGradeStrength = 0.55f;
        float m_LensDirtStrength = 0.35f;
        float m_SignalInterference = 0.0f;
        float m_TargetSignalInterference = 0.0f;

        // 本描画を一度RenderTextureへ写し、その後で複数の画面効果を合成している。
        Graphics::RenderTexture m_RenderTexture;
        // ブルームの作業用テクスチャ（明るい部分を取り出した物、横にぼかした物、縦にぼかした物）
        Graphics::RenderTexture m_BloomExtractTexture;
        Graphics::RenderTexture m_BloomHorizontalTexture;
        Graphics::RenderTexture m_BloomVerticalTexture;
        Graphics::FullScreenQuad m_FullScreenQuad;

        // 明るい部分の抽出・横のぼかし・縦のぼかしのコンピュートシェーダー
        ComputeShader m_BloomExtractShader;
        ComputeShader m_BloomHorizontalShader;
        ComputeShader m_BloomVerticalShader;

        // 明るい部分の抽出 → 横のぼかし → 縦のぼかしの順で、ブルームの画像を作っている。
        void RunBloom();

    public:
        // 作業用テクスチャとシェーダーを作る／解放する／値を目標へ近づける
        void Init();
        void Uninit();
        void Update();

        // 本描画が終わったバックバッファを写し、画面効果の入力にしている。
        void CaptureBackBuffer();
        void Draw(GpuTimer* gpuTimer); // 写した画面へ画面効果を合成している。

        // 一時的な演出。指定した強さから始まり、durationかけて元に戻る。
        // BloomPulse: 停電からの復旧などで、一瞬強く光らせている。
        // HorrorPulse: 驚かせる瞬間に、ノイズ・周辺減光・色ずれ・画面の裂けを強めている。
        // LensMoisture: 水しぶきなどで、レンズが濡れたように見せている（0〜1）。
        void TriggerBloomPulse(float peakIntensity, float duration);
        void TriggerHorrorPulse(float strength, float duration);
        void TriggerLensMoisture(float strength, float duration);

        // 緊張感・ノイズ・周辺減光・露出・光の筋・信号の乱れは目標値を設定し、
        // Updateで少しずつ近づけている（急には切り替わらない）。それ以外のSet系はその場で反映している。
        // 廊下の緊張感（0〜1）。高いほどノイズと画面の暗さが増す。
        void SetCorridorTension(float tension)
        {
            m_TargetCorridorTension = (std::clamp)(tension, 0.0f, 1.0f);
        }

        // いつも掛かっているノイズと周辺減光の強さ。
        void SetAtmosphere(float noiseAmount, float vignetteStrength)
        {
            m_TargetNoiseAmount = noiseAmount;
            m_TargetVignetteStrength = vignetteStrength;
        }

        // 光の筋（懐中電灯の光が空気中で見える効果）のON/OFFと強さ
        void SetVolumetricLight(bool enable)
        {
            m_EnableVolumetricLight = enable;
        }

        void SetVolumetricIntensity(float intensity)
        {
            m_TargetVolumetricIntensity =
                (std::clamp)(intensity, 0.0f, 1.0f);
        }

        // ブルームとフィルムノイズのON/OFF
        void SetBloomEnabled(bool enable)
        {
            m_EnableBloom = enable;
        }

        void SetNoise(bool enable)
        {
            m_EnableNoise = enable;
        }

        // 露出の目標値（0.85〜1.20）
        void SetExposure(float exposure)
        {
            m_TargetExposure = (std::clamp)(exposure, 0.85f, 1.20f);
        }

        // ポーズメニューの「明るさ」「演出の強さ」の設定値。
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

        // レンズの歪み・映画風の色調・レンズの汚れのにじみの強さ
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

        // 監視映像の電波障害のような横線と色ずれ（0〜1）。
        void SetSignalInterference(float strength)
        {
            m_TargetSignalInterference =
                (std::clamp)(strength, 0.0f, 1.0f);
        }

        // フィルムノイズが有効か
        bool IsNoiseEnable() const
        {
            return m_EnableNoise;
        }

        // デバッグ画面からの調整用。目標値ではなく、その場で値を変えている。
        void SetBloomIntensity(float intensity)
        {
            m_BloomBaseIntensity = intensity;
            m_BloomIntensity = intensity;
        }
    };
}
