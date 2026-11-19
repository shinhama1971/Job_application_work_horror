// ============================================================================
// ファイルの役割: 露出・ブルーム・ブラウン管風の効果・光の筋など、画面全体に掛けるシェーダーの演出をまとめて管理している。
// 主な技術: Render To Texture、Compute Shader、横と縦に分けたぼかし（2枚のテクスチャを交互に使う）、明るさと色の調整
// ============================================================================

#pragma once

#include <algorithm>
#include <array>

#include "RenderTexture.h"
#include "FullScreenQuad.h"
#include "ComputeShader.h"

class GpuTimer;

namespace Effect
{
    // HDRの画面を、画面に出せる0〜1の明るさへ移す曲線（トーンマッピング）。toneMapPS.hlsl の ToneMapMode と同じ番号
    enum class ToneMapMode : unsigned int
    {
        // ACES（映画の色の規格の曲線の近似。Narkowicz 2015）。明るい所を白く飛ばさず、なだらかに丸める
        Aces = 0,
        // HDRにする前に近い曲線（ACESを28%だけ混ぜて、1で切る）。見比べる用
        Legacy = 1,
        // 曲線を掛けず1で切るだけ。デバッグ表示（法線・光源のタイル・SSAOなど）の色を、そのまま見せるときに使う
        Passthrough = 2,
        Reinhard = 3,
    };

    // 描き終えた画面を取り込み、ブルームを作り、露出やノイズなどの画面効果を重ねている。演出からは一時的な効果を起こせる。
    class PostProcess
    {
    public:
        // --- 画面効果の普段の値。シーンが終わるときは、この値へ戻している
        static constexpr float DefaultNoiseAmount = 0.18f;
        static constexpr float DefaultVignetteStrength = 0.55f;
        static constexpr float DefaultLensDistortionStrength = 0.20f;
        static constexpr float DefaultFilmGradeStrength = 0.55f;
        static constexpr float DefaultLensDirtStrength = 0.10f;
        static constexpr float DefaultVolumetricIntensity = 0.58f;
        static constexpr float DefaultBloomIntensity = 0.48f;
        static constexpr float DefaultExposure = 1.0f;
        // Init の前（作った直後）のレンズの歪みと汚れ。どのシーンも始めに自分の値を入れ直す
        static constexpr float StartupLensDistortionStrength = 0.32f;
        static constexpr float StartupLensDirtStrength = 0.35f;
        // ポーズメニューの「明るさ」で足せる範囲と、「演出の強さ」の範囲
        static constexpr float MinUserBrightnessOffset = -0.12f;
        static constexpr float MaxUserBrightnessOffset = 0.14f;
        static constexpr float MinUserEffectScale = 0.65f;
        static constexpr float MaxUserEffectScale = 1.25f;
        // レンズの歪みの上限
        static constexpr float MaxLensDistortionStrength = 0.80f;
        // 恐怖で視界が狭まる強さを、渡されなくなってから保つ秒数
        static constexpr float FearVisionHoldSeconds = 0.25f;

    private:
        // enable系は機能そのもののON/OFF、target系は急に変わらないよう、少しずつ近づける先の値。
        bool m_EnableNoise = true;
        bool m_EnableBloom = true;
        bool m_EnableVolumetricLight = false;

        // 経過時間（ノイズなどを動かす）
        float m_Time = 0.0f;
        // 普段は控えめにし、停電からの復旧などのイベントのときだけ強く光らせている。
        float m_BloomBaseIntensity = DefaultBloomIntensity;
        float m_BloomIntensity = DefaultBloomIntensity;
        // ブルームを一時的に強める量・残り秒数・長さ
        float m_BloomPulseStrength = 0.0f;
        float m_BloomPulseTimer = 0.0f;
        float m_BloomPulseDuration = 0.0f;
        // フィルムノイズの量、周辺減光の強さ（今の値と目標）、レンズの歪みの強さ
        float m_NoiseAmount = DefaultNoiseAmount;
        float m_TargetNoiseAmount = DefaultNoiseAmount;
        float m_VignetteStrength = DefaultVignetteStrength;
        float m_TargetVignetteStrength = DefaultVignetteStrength;
        float m_LensDistortionStrength = StartupLensDistortionStrength;
        // 驚かせる演出の強さ（今・最大）、残り秒数、長さ
        float m_HorrorPulseStrength = 0.0f;
        float m_HorrorPulsePeak = 0.0f;
        float m_HorrorPulseTimer = 0.0f;
        float m_HorrorPulseDuration = 0.0f;
        // 露出（今の値と目標）、ポーズメニューの明るさと演出の強さ
        float m_Exposure = DefaultExposure;
        float m_TargetExposure = DefaultExposure;
        float m_UserBrightnessOffset = 0.0f;
        float m_UserEffectScale = 1.0f;
        // レンズの曇り（今・最大）、残り秒数、長さ
        float m_LensMoisture = 0.0f;
        float m_LensMoisturePeak = 0.0f;
        float m_LensMoistureTimer = 0.0f;
        float m_LensMoistureDuration = 0.0f;
        // レンズのしぶきの乱数の種（付くたびに変え、毎回違う位置に付ける）と、付いてからの秒数
        float m_LensDropSeed = 0.0f;
        float m_LensDropAge = 0.0f;
        // ループ廊下の緊張感、光の筋の強さ（今の値と目標）
        float m_CorridorTension = 0.0f;
        float m_TargetCorridorTension = 0.0f;
        float m_VolumetricIntensity = DefaultVolumetricIntensity;
        float m_TargetVolumetricIntensity = DefaultVolumetricIntensity;
        // 映画風の色調の強さ、レンズの汚れのにじみ、監視映像のような信号の乱れ（今の値と目標）
        float m_FilmGradeStrength = DefaultFilmGradeStrength;
        float m_LensDirtStrength = StartupLensDirtStrength;
        float m_SignalInterference = 0.0f;
        float m_TargetSignalInterference = 0.0f;
        // 恐怖で視界が狭まる強さ（0〜1）と、心拍に合わせた脈（鳴った瞬間に1になり、約0.3秒で消える）。
        // Scene が毎フレーム SetFearVision で渡し、渡されなくなったら（タイトルへ戻ったときなど）0へ戻している
        float m_FearVision = 0.0f;
        float m_TargetFearVision = 0.0f;
        float m_FearBeat = 0.0f;
        float m_FearVisionHold = 0.0f;
        // 影のまわりの空間のゆがみ（Scene が毎フレーム SetPresenceWarps で渡し、渡されなくなったら消す）と、渡されてからの残り秒数
        std::array<DirectX::SimpleMath::Vector4, Graphics::FullScreenQuad::PresenceWarpCount> m_PresenceWarps{};
        float m_PresenceWarpHold = 0.0f;
        // 残像：少し前の画面（ときどき写し取って止めておく）、強さ（今の値と目標）、渡されてからの残り秒数、
        // 次に写し取るまでの秒数、写し取った画面があるか、次の写し取りを頼んだか、ずらす量、乱数の種と乱数の状態
        Graphics::RenderTexture m_EchoTexture;
        float m_EchoStrength = 0.0f;
        float m_TargetEchoStrength = 0.0f;
        float m_EchoHold = 0.0f;
        float m_EchoCaptureTimer = 0.0f;
        bool m_EchoValid = false;
        bool m_EchoCopyRequested = false;
        DirectX::SimpleMath::Vector2 m_EchoOffset = DirectX::SimpleMath::Vector2::Zero;
        float m_EchoSeed = 0.0f;
        unsigned int m_EchoRandom = FrameHoldInitialSeed;
        // 携帯ラジオの雑音の強さ（影が近いほど強い）。信号の乱れと同じ画面の乱れを、弱めに重ねている
        float m_PresenceStatic = 0.0f;
        // コマ落ち：雑音が強いとき、ときどき映像が一瞬止まり、縦に少しずれる。止めている残り秒数、次に止めるまでの秒数、
        // 縦のずれ（画面の高さに対する割合）、間隔とずれを決める乱数の状態
        float m_FrameHoldTimer = 0.0f;
        float m_FrameHoldCooldown = 1.0f;
        float m_FrameHoldOffset = 0.0f;
        // xorshiftをゼロ以外の固定値から始め、コマ落ち間隔の系列を再現する。
        static constexpr unsigned int FrameHoldInitialSeed = 0x9E3779B9u;
        unsigned int m_FrameHoldRandom = FrameHoldInitialSeed;
        // 0〜1の乱数（xorshift）
        float NextFrameHoldRandom();

        // 本描画を一度RenderTextureへ写し、その後で複数の画面効果を合成している。
        Graphics::RenderTexture m_RenderTexture;
        // ブルームの作業用テクスチャ（明るい部分を取り出した物、横にぼかした物、縦にぼかした物）
        Graphics::RenderTexture m_BloomExtractTexture;
        Graphics::RenderTexture m_BloomHorizontalTexture;
        Graphics::RenderTexture m_BloomVerticalTexture;
        Graphics::FullScreenQuad m_FullScreenQuad;

        // 広くにじむブルームの段（縦横1/8・1/16・1/32）。段ごとに、縮めた物・横にぼかした物・縦にぼかした物を持つ
        static constexpr unsigned int WideBloomLevelCount = Graphics::FullScreenQuad::BloomLevelCount - 1;
        std::array<Graphics::RenderTexture, WideBloomLevelCount> m_WideBloomDownsampled;
        std::array<Graphics::RenderTexture, WideBloomLevelCount> m_WideBloomHorizontal;
        std::array<Graphics::RenderTexture, WideBloomLevelCount> m_WideBloomBlurred;

        // 明るい部分の抽出・横のぼかし・縦のぼかし・縦横半分への縮小のコンピュートシェーダー
        ComputeShader m_BloomExtractShader;
        ComputeShader m_BloomHorizontalShader;
        ComputeShader m_BloomVerticalShader;
        ComputeShader m_BloomDownsampleShader;

        // 自動露出のCompute Shaderへ渡す値（b0）。autoExposureCS.hlslのAutoExposureBufferと同じ並び
        struct AutoExposureParams
        {
            float DeltaTime;
            float TargetLuminance;
            float MinGain;
            float MaxGain;
            float DarkAdaptSpeed;
            float LightAdaptSpeed;
            float Enabled;
            float ResetAdaptation;
        };
        static_assert(sizeof(AutoExposureParams) == 32,
            "AutoExposureParams must match autoExposureCS.hlsl");

        // 自動露出：画面の明るさを測るコンピュートシェーダーと、慣れた明るさ・倍率を置くバッファ（書き込みはUAV、読み取りはSRV）
        ComputeShader m_AutoExposureShader;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_AutoExposureState;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AutoExposureUAV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_AutoExposureSRV;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_AutoExposureParams;
        // 自動露出を使うか、次のフレームで今の明るさにすぐ慣れさせるか（始めのフレーム）
        bool m_EnableAutoExposure = true;
        bool m_ResetAutoExposure = true;

        // トーンマッピングの曲線
        ToneMapMode m_ToneMapMode = ToneMapMode::Aces;

        // 明るい部分の抽出 → 横のぼかし → 縦のぼかしの順で、ブルームの画像を作っている。
        // 続けて、ぼかした画像を縦横半分に縮めては横と縦にぼかし、広くにじむ段を作っている。
        void RunBloom();
        // 描いた画面の明るさを測り、目の慣れの倍率をGPUのバッファへ書いている。
        void RunAutoExposure();

    public:
        // 作業用テクスチャとシェーダーを作る／解放する／値を目標へ近づける
        bool Init();
        void Uninit();
        void Update();

        // 本描画が終わったバックバッファを写し、画面効果の入力にしている。
        bool CaptureBackBuffer();
        // 取り込んだ画面（画面効果を掛ける前）。次のフレームの本描画では「前のフレームの画面」として、SSRの色に使っている
        ID3D11ShaderResourceView* GetCapturedSceneSRV() { return m_RenderTexture.GetSRV(); }
        // 写した画面へ画面効果を合成し、最後にトーンマッピングでHDRの画面をバックバッファへ移している。
        // この後の描画（HUD・デバッグ画面）は、バックバッファへそのまま描かれる
        void Draw(GpuTimer* gpuTimer);

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

        // 自動露出のON/OFF（OFFのときは倍率を1にし、シーンが決めた露出だけを使う）
        void SetAutoExposureEnabled(bool enable)
        {
            m_EnableAutoExposure = enable;
        }

        // 次のフレームで、目の慣れを今の画面の明るさにすぐ合わせている（シーンを切り替えたとき）
        void ResetAutoExposure()
        {
            m_ResetAutoExposure = true;
        }

        // トーンマッピングの曲線を選んでいる（デバッグ画面で見比べる用）
        void SetToneMapMode(ToneMapMode mode)
        {
            m_ToneMapMode = mode;
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
        static constexpr float MinimumSceneExposure = 0.76f;
        static constexpr float MaximumSceneExposure = 1.20f;

        void SetExposure(float exposure)
        {
            m_TargetExposure = (std::clamp)(exposure, MinimumSceneExposure, MaximumSceneExposure);
        }

        // ポーズメニューの「明るさ」「演出の強さ」の設定値。
        void SetUserBrightnessOffset(float offset)
        {
            m_UserBrightnessOffset =
                (std::clamp)(offset, MinUserBrightnessOffset, MaxUserBrightnessOffset);
        }

        void SetUserEffectScale(float scale)
        {
            m_UserEffectScale =
                (std::clamp)(scale, MinUserEffectScale, MaxUserEffectScale);
        }

        // レンズの歪み・映画風の色調・レンズの汚れのにじみの強さ
        void SetLensDistortionStrength(float strength)
        {
            m_LensDistortionStrength = (std::clamp)(strength, 0.0f, MaxLensDistortionStrength);
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
        // 影が近いときの、ラジオの雑音に合わせた画面の乱れの強さ（0〜1）を設定している
        void SetPresenceStatic(float strength)
        {
            m_PresenceStatic = (std::clamp)(strength, 0.0f, 1.0f);
        }
        void SetSignalInterference(float strength)
        {
            m_TargetSignalInterference =
                (std::clamp)(strength, 0.0f, 1.0f);
        }

        // 恐怖で視界が狭まる強さ（0〜1）を設定している。beat は心拍が鳴ったフレームで true（その瞬間、端の闇が一瞬強まる）
        void SetFearVision(float strength, bool beat)
        {
            m_TargetFearVision = (std::clamp)(strength, 0.0f, 1.0f);
            m_FearVisionHold = FearVisionHoldSeconds;
            if (beat)
            {
                m_FearBeat = 1.0f;
            }
        }

        // 残像の強さ（0〜1）を設定している。Scene が毎フレーム渡し、渡されなくなったら0へ戻す
        void SetEcho(float strength)
        {
            m_TargetEchoStrength = (std::clamp)(strength, 0.0f, 1.0f);
            m_EchoHold = FearVisionHoldSeconds;
        }

        // 影のまわりの空間のゆがみを設定している（xy = 画面の位置 0〜1、z = 半径（縦の高さを1とした長さ）、w = 強さ 0〜1）
        void SetPresenceWarps(const std::array<DirectX::SimpleMath::Vector4, Graphics::FullScreenQuad::PresenceWarpCount>& warps)
        {
            m_PresenceWarps = warps;
            m_PresenceWarpHold = FearVisionHoldSeconds;
        }

        // デバッグ画面からの調整用。目標値ではなく、その場で値を変えている。
        void SetBloomIntensity(float intensity)
        {
            m_BloomBaseIntensity = intensity;
            m_BloomIntensity = intensity;
        }
    };
}
