// ============================================================================
// ファイルの役割: 露出・ブルーム・ブラウン管風の効果・光の筋など、画面全体に掛けるシェーダーの演出をまとめて管理している。
// 主な技術: Render To Texture、Compute Shader、横と縦に分けたぼかし（2枚のテクスチャを交互に使う）、明るさと色の調整
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
    constexpr float LightShaftsDisabledEffectScale = 0.75f;
    constexpr float LightShaftsFullEffectScale = 1.0f;
    constexpr float AtmosphereResponseSeconds = 0.075f;
    // 演出強度が低くても、照明のにじみを一部残す。
    // ブルームの中間画像は縦横ともにこの比率で縮小する。端の画素も含めるため切り上げる。
    constexpr int BloomResolutionDivisor = 4;
    // コマ落ちの周期を選ぶ乱数には、下位24ビットを使う。
    constexpr unsigned int FrameHoldRandomMask = 0xFFFFFFu;
    constexpr float BloomBaseScale = 0.72f;
    constexpr float BloomEffectScaleWeight = 0.28f;
    constexpr float MaxAdjustedBloom = 1.25f;
    // シーン露出にプレイヤーの明るさ設定を足した後に適用する範囲。
    constexpr float MinCompositeExposure = 0.75f;
    constexpr float MaxCompositeExposure = 1.34f;

    // コマ落ちの最初の待ち秒数
    constexpr float InitialFrameHoldCooldown = 1.0f;

    // --- 目標へ近づく割合（60fpsで1フレームあたり）。ResponseReferenceFps を基準に、フレームレートが違っても同じ速さにする
    constexpr float ResponseReferenceFps = 60.0f;
    // 暗い所で目を慣らす（ゆっくり）・明るい所へ戻る（速く）
    constexpr float DarkExposureResponse = 0.012f;
    constexpr float BrightExposureResponse = 0.065f;
    // 廊下の緊張・光の筋・電波の乱れ・恐怖で視界が狭まる・普段のブルームへ戻る・驚かせた後に戻る・レンズの曇りが晴れる
    constexpr float CorridorTensionResponse = 0.035f;
    constexpr float VolumetricResponse = 0.055f;
    constexpr float SignalInterferenceResponse = 0.085f;
    constexpr float FearVisionResponse = 0.05f;
    constexpr float BloomRecoverResponse = 0.12f;
    constexpr float HorrorRecoverResponse = 0.18f;
    constexpr float LensMoistureClearResponse = 0.035f;
    // 心臓の鼓動に合わせた視界の脈が、1秒で弱まる量
    constexpr float FearBeatDecayPerSecond = 3.2f;
    // 残像：強さが目標へ近づく割合（60fpsで1フレームあたり）、少し前の画面を写し取る間隔（秒。この間は止まった画面が残る）、
    // ずらす量の最大（画面の幅・高さに対する割合）、強さがこれより弱ければ写し取らない
    constexpr float EchoResponse = 0.08f;
    constexpr float EchoCaptureInterval = 0.16f;
    constexpr float EchoMaxOffsetX = 0.018f;
    constexpr float EchoMaxOffsetY = 0.006f;
    constexpr float EchoMinStrength = 0.001f;

    // --- コマ落ち：起き始める強さと、そこから最大になるまでの幅、止まる秒数（最短＋強さの幅）、
    // 映像のずれ（強さに掛ける幅）、次までの間（最長−強さの幅、乱数で 最低＋幅 倍）、弱いときに残す最短の間
    constexpr float FrameHoldThreshold = 0.22f;
    constexpr float FrameHoldRange = 0.23f;
    constexpr float FrameHoldMinSeconds = 0.05f;
    constexpr float FrameHoldExtraSeconds = 0.05f;
    constexpr float FrameHoldOffsetRange = 0.10f;
    constexpr float FrameHoldMaxCooldown = 3.2f;
    constexpr float FrameHoldCooldownShrink = 2.2f;
    constexpr float FrameHoldCooldownMinScale = 0.6f;
    constexpr float FrameHoldCooldownRandomScale = 0.8f;
    constexpr float FrameHoldIdleCooldown = 0.8f;

    // --- 演出の長さの下限（0で割らないため）：ブルームと驚かせる演出、レンズの曇り
    constexpr float MinimumPulseSeconds = 0.01f;
    constexpr float MinimumMoistureSeconds = 0.05f;
    // レンズのしぶきの経過秒数の上限（数え続けて大きくなりすぎないように）と、乱数の種を進める量（黄金比 0.618…）
    constexpr float MaxLensDropAge = 60.0f;
    constexpr float GoldenRatioFraction = 0.6180339f;
    // 自動露出の1フレームの時間の上限（長い停止の後に一気に慣れないように）
    constexpr float MaxAutoExposureDeltaTime = 0.1f;
    // 「演出の強さ」を下げても残す周辺減光の強さ
    constexpr float BaseVignetteStrength = 0.45f;

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
    // 作業用のRenderTextureと各シェーダーを一度だけ作っている。
    bool PostProcess::Init()
    {
        m_EnableNoise = true;
        m_EnableBloom = true;
        m_Time = 0.0f;
        m_NoiseAmount = DefaultNoiseAmount;
        m_TargetNoiseAmount = DefaultNoiseAmount;
        m_VignetteStrength = DefaultVignetteStrength;
        m_TargetVignetteStrength = DefaultVignetteStrength;
        m_HorrorPulseStrength = 0.0f;
        m_HorrorPulseTimer = 0.0f;
        m_Exposure = DefaultExposure;
        m_TargetExposure = DefaultExposure;
        m_LensMoisture = 0.0f;
        m_LensMoisturePeak = 0.0f;
        m_LensMoistureTimer = 0.0f;
        m_LensMoistureDuration = 0.0f;
        m_CorridorTension = 0.0f;
        m_TargetCorridorTension = 0.0f;
        m_VolumetricIntensity = DefaultVolumetricIntensity;
        m_TargetVolumetricIntensity = DefaultVolumetricIntensity;
        m_FilmGradeStrength = DefaultFilmGradeStrength;
        m_LensDirtStrength = DefaultLensDirtStrength;
        m_SignalInterference = 0.0f;
        m_TargetSignalInterference = 0.0f;
        m_PresenceStatic = 0.0f;
        m_FrameHoldTimer = 0.0f;
        m_FrameHoldCooldown = InitialFrameHoldCooldown;
        m_FearVision = 0.0f;
        m_TargetFearVision = 0.0f;
        m_FearBeat = 0.0f;
        m_FearVisionHold = 0.0f;
        m_PresenceWarps.fill(DirectX::SimpleMath::Vector4::Zero);
        m_PresenceWarpHold = 0.0f;
        m_EchoStrength = 0.0f;
        m_TargetEchoStrength = 0.0f;
        m_EchoHold = 0.0f;
        m_SubmergeLevel = 0.0f;
        m_SubmergeHold = 0.0f;
        m_EchoCaptureTimer = 0.0f;
        m_EchoValid = false;
        m_EchoCopyRequested = false;

        // 本描画のHDRの描き先を、そのまま写せるよう同じ形式にしている
        if (!m_RenderTexture.Init(
            Application::GetWidth(),
            Application::GetHeight(),
            Renderer::SceneHdrFormat
        ))
        {
            MessageBoxW(nullptr, L"画面効果の描画テクスチャを作成できません。", L"描画エラー", MB_OK | MB_ICONERROR);
            return false;
        }
        // 残像用のHDR画像は通常プレイでだけ使う。撮影と計測のGPUメモリを消費しないよう、ここで分ける。
        if (!Tools::CaptureMode::IsActive() && !m_EchoTexture.Init(
            Application::GetWidth(),
            Application::GetHeight(),
            Renderer::SceneHdrFormat
        ))
        {
            MessageBoxW(nullptr, L"残像の描画テクスチャを作成できません。", L"描画エラー", MB_OK | MB_ICONERROR);
            return false;
        }

        // ブルームは縦横1/4の解像度で処理している。元の画面は拡大しないため、
        // 物の輪郭そのものははっきりしたまま、光っている部分だけが外側へにじむ。
        const int bloomWidth = (Application::GetWidth() + (BloomResolutionDivisor - 1)) / BloomResolutionDivisor;
        const int bloomHeight = (Application::GetHeight() + (BloomResolutionDivisor - 1)) / BloomResolutionDivisor;
        // 明るさが1を超える値も残せるよう、16ビットの浮動小数点のテクスチャにしている（コンピュートシェーダーから書けるようUAV付き）
        constexpr DXGI_FORMAT bloomFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        if (!m_BloomExtractTexture.Init(bloomWidth, bloomHeight, bloomFormat, true) ||
            !m_BloomHorizontalTexture.Init(bloomWidth, bloomHeight, bloomFormat, true) ||
            !m_BloomVerticalTexture.Init(bloomWidth, bloomHeight, bloomFormat, true))
        {
            MessageBoxW(nullptr, L"画面効果の描画テクスチャを作成できません。", L"描画エラー", MB_OK | MB_ICONERROR);
            return false;
        }

        // 広くにじむ段は、1つ上の段の縦横半分（1/8・1/16・1/32）。小さくなりすぎないよう1画素で止める
        constexpr int downsampleFactor = SIGNAL_LOST_BLOOM_WIDE_DOWNSAMPLE_FACTOR;
        int levelWidth = bloomWidth;
        int levelHeight = bloomHeight;
        for (unsigned int level = 0; level < WideBloomLevelCount; ++level)
        {
            levelWidth = (std::max)(1, (levelWidth + downsampleFactor - 1) / downsampleFactor);
            levelHeight = (std::max)(1, (levelHeight + downsampleFactor - 1) / downsampleFactor);
            if (!m_WideBloomDownsampled[level].Init(levelWidth, levelHeight, bloomFormat, true) ||
                !m_WideBloomHorizontal[level].Init(levelWidth, levelHeight, bloomFormat, true) ||
                !m_WideBloomBlurred[level].Init(levelWidth, levelHeight, bloomFormat, true))
            {
                MessageBoxW(nullptr, L"画面効果の描画テクスチャを作成できません。", L"描画エラー", MB_OK | MB_ICONERROR);
                return false;
            }
        }

        if (!m_BloomExtractShader.Create("shader/bloomExtractCS.hlsl") ||
            !m_BloomHorizontalShader.Create("shader/bloomBlurHorizontalCS.hlsl") ||
            !m_BloomVerticalShader.Create("shader/bloomBlurVerticalCS.hlsl") ||
            !m_BloomDownsampleShader.Create("shader/bloomDownsampleCS.hlsl") ||
            !m_AutoExposureShader.Create("shader/autoExposureCS.hlsl"))
        {
            return false;
        }

        // 自動露出の順応状態と倍率は GPU 内で更新し、露出の PS と共有する。
        {
            ID3D11Device* device = Renderer::GetDevice();
            D3D11_BUFFER_DESC stateDesc{};
            constexpr unsigned int stateCount = SIGNAL_LOST_EXPOSURE_GAIN_INDEX + 1;
            stateDesc.ByteWidth = sizeof(float) * stateCount;
            stateDesc.StructureByteStride = sizeof(float);
            stateDesc.Usage = D3D11_USAGE_DEFAULT;
            stateDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            stateDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            // 最初の値は「倍率1」にしている（最初のフレームは、すぐ今の明るさに慣れさせる）
            float initialState[stateCount] = {};
            initialState[SIGNAL_LOST_EXPOSURE_GAIN_INDEX] = 1.0f;
            D3D11_SUBRESOURCE_DATA initialData{};
            initialData.pSysMem = initialState;

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = 2;
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.NumElements = 2;

            D3D11_BUFFER_DESC paramsDesc{};
            paramsDesc.ByteWidth = sizeof(AutoExposureParams);
            paramsDesc.Usage = D3D11_USAGE_DEFAULT;
            paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

            const bool created =
                SUCCEEDED(device->CreateBuffer(&stateDesc, &initialData,
                    m_AutoExposureState.ReleaseAndGetAddressOf())) &&
                SUCCEEDED(device->CreateUnorderedAccessView(m_AutoExposureState.Get(), &uavDesc,
                    m_AutoExposureUAV.ReleaseAndGetAddressOf())) &&
                SUCCEEDED(device->CreateShaderResourceView(m_AutoExposureState.Get(), &srvDesc,
                    m_AutoExposureSRV.ReleaseAndGetAddressOf())) &&
                SUCCEEDED(device->CreateBuffer(&paramsDesc, nullptr,
                    m_AutoExposureParams.ReleaseAndGetAddressOf()));
            if (!created)
            {
                Utility::ReportFatalError("自動露出のバッファの作成に失敗しました。");
            }
        }
        m_ResetAutoExposure = true;

        return m_FullScreenQuad.Init();
    }

    // 作った物を解放している
    void PostProcess::Uninit()
    {
        m_AutoExposureParams.Reset();
        m_AutoExposureSRV.Reset();
        m_AutoExposureUAV.Reset();
        m_AutoExposureState.Reset();
        m_AutoExposureShader.Uninit();
        m_BloomDownsampleShader.Uninit();
        m_BloomVerticalShader.Uninit();
        m_BloomHorizontalShader.Uninit();
        m_BloomExtractShader.Uninit();
        m_FullScreenQuad.Uninit();
        for (unsigned int level = 0; level < WideBloomLevelCount; ++level)
        {
            m_WideBloomBlurred[level].Uninit();
            m_WideBloomHorizontal[level].Uninit();
            m_WideBloomDownsampled[level].Uninit();
        }
        m_BloomVerticalTexture.Uninit();
        m_BloomHorizontalTexture.Uninit();
        m_BloomExtractTexture.Uninit();
        m_RenderTexture.Uninit();
        m_EchoTexture.Uninit();
    }

    // 目標値へゆっくり近づけ、場面が変わったときの露出やノイズの急な変化を防いでいる。
    void PostProcess::Update()
    {
        const float deltaTime = Application::GetDeltaTime();
        // 60fpsで1フレームあたりの近づく割合を、フレームレートが違っても同じ速さになるよう補正している
        const auto response = [deltaTime](float responseAt60Fps)
        {
            return 1.0f - std::pow(
                1.0f - responseAt60Fps, deltaTime * ResponseReferenceFps);
        };
        // 画面の粒・光の中の埃・霧の流れが使う時間は、世界の時計で進めている（時間が止まった部屋では、粒も埃も止まる）。
        // 強さを目標へ近づける処理は、普段の経過秒のまま
        m_Time += Application::GetWorldDeltaTime();

        // 走ったときや電池の警告の強さをなめらかにし、画面効果が一瞬で切り替わらないようにしている。
        m_NoiseAmount +=
            (m_TargetNoiseAmount - m_NoiseAmount) * response(AtmosphereResponseSeconds);
        m_VignetteStrength +=
            (m_TargetVignetteStrength - m_VignetteStrength) * response(AtmosphereResponseSeconds);

        // 暗い所へ入った後はゆっくり目を慣らし、懐中電灯や照明が戻ったときは
        // 素早く普段の露出へ戻すことで、人の目の慣れ方に近づけている。
        const float exposureResponse = response(
            m_TargetExposure > m_Exposure ? DarkExposureResponse : BrightExposureResponse);
        m_Exposure +=
            (m_TargetExposure - m_Exposure) * exposureResponse;
        m_CorridorTension +=
            (m_TargetCorridorTension - m_CorridorTension) * response(CorridorTensionResponse);
        m_VolumetricIntensity +=
            (m_TargetVolumetricIntensity - m_VolumetricIntensity) * response(VolumetricResponse);
        m_SignalInterference +=
            (m_TargetSignalInterference - m_SignalInterference) * response(SignalInterferenceResponse);
        // 恐怖で視界が狭まる：Scene から渡されなくなったら0へ戻す。狭まるのも戻るのも、1秒ほどかけてなめらかにしている
        m_FearVisionHold -= deltaTime;
        if (m_FearVisionHold <= 0.0f)
        {
            m_TargetFearVision = 0.0f;
        }
        m_FearVision += (m_TargetFearVision - m_FearVision) * response(FearVisionResponse);
        m_FearBeat = (std::max)(0.0f, m_FearBeat - deltaTime * FearBeatDecayPerSecond);
        // 水に呑まれる視界：Scene から渡されなくなったら、すぐに消す
        m_SubmergeHold -= deltaTime;
        if (m_SubmergeHold <= 0.0f)
        {
            m_SubmergeLevel = 0.0f;
        }
        // 残像：Scene から渡されなくなったら0へ戻す。出ている間は、少し前の画面をときどき写し取り、そのたびにずらす向きを変える
        m_EchoHold -= deltaTime;
        if (m_EchoHold <= 0.0f)
        {
            m_TargetEchoStrength = 0.0f;
        }
        m_EchoStrength += (m_TargetEchoStrength - m_EchoStrength) * response(EchoResponse);
        if (m_EchoTexture.GetTexture() != nullptr && m_EchoStrength > EchoMinStrength)
        {
            m_EchoCaptureTimer -= deltaTime;
            if (m_EchoCaptureTimer <= 0.0f || !m_EchoValid)
            {
                m_EchoCaptureTimer = EchoCaptureInterval;
                m_EchoCopyRequested = true;
                const auto nextSigned = [this]()
                {
                    MathUtility::NextXorShift32(m_EchoRandom);
                    return static_cast<float>(m_EchoRandom & FrameHoldRandomMask) / static_cast<float>(FrameHoldRandomMask) * 2.0f - 1.0f;
                };
                m_EchoOffset = DirectX::SimpleMath::Vector2(nextSigned() * EchoMaxOffsetX, nextSigned() * EchoMaxOffsetY);
                m_EchoSeed = nextSigned() * 0.5f + 0.5f;
            }
        }
        else
        {
            // 再開時は現在の画面から写す。コマ落ち中などに残った前のコピー要求も取り消す。
            m_EchoValid = false;
            m_EchoCopyRequested = false;
        }
        // 影のまわりのゆがみ：Scene から渡されなくなったら（場面が変わったときなど）消している
        m_PresenceWarpHold -= deltaTime;
        if (m_PresenceWarpHold <= 0.0f)
        {
            m_PresenceWarps.fill(DirectX::SimpleMath::Vector4::Zero);
        }

        // コマ落ち：影が近く、ラジオの雑音が強いほど、短い間隔で映像が一瞬（0.05〜0.1秒）止まる。
        // ポーズメニューの「演出の強さ」を下げると起きにくくなり、0なら起きない。自動撮影と計測では、画像と数字がぶれないよう起こさない
        const float frameHoldStrength = Tools::CaptureMode::IsActive() ? 0.0f : m_PresenceStatic * m_UserEffectScale;
        if (m_FrameHoldTimer > 0.0f)
        {
            m_FrameHoldTimer = (std::max)(0.0f, m_FrameHoldTimer - deltaTime);
        }
        else if (frameHoldStrength > FrameHoldThreshold)
        {
            m_FrameHoldCooldown -= deltaTime;
            if (m_FrameHoldCooldown <= 0.0f)
            {
                const float rate = (std::min)(1.0f, (frameHoldStrength - FrameHoldThreshold) / FrameHoldRange);
                m_FrameHoldTimer = FrameHoldMinSeconds + FrameHoldExtraSeconds * rate;
                m_FrameHoldOffset = (NextFrameHoldRandom() - 0.5f) * FrameHoldOffsetRange * rate;
                m_FrameHoldCooldown = (FrameHoldMaxCooldown - FrameHoldCooldownShrink * rate) *
                    (FrameHoldCooldownMinScale + NextFrameHoldRandom() * FrameHoldCooldownRandomScale);
            }
        }
        else
        {
            m_FrameHoldCooldown = (std::max)(m_FrameHoldCooldown, FrameHoldIdleCooldown);
        }

        // ブルームの一時的な強まり：残り時間の2乗で弱めている（始めは急に、終わりはゆっくり戻る）
        if (m_BloomPulseTimer > 0.0f && m_BloomPulseDuration > 0.0f)
        {
            m_BloomPulseTimer =
                (std::max)(0.0f, m_BloomPulseTimer - deltaTime);
            const float remaining =
                m_BloomPulseTimer / m_BloomPulseDuration;
            m_BloomIntensity = m_BloomBaseIntensity +
                m_BloomPulseStrength * remaining * remaining;
        }
        else
        {
            m_BloomIntensity +=
                (m_BloomBaseIntensity - m_BloomIntensity) * response(BloomRecoverResponse);
        }

        // 驚かせる演出の強さ：同じく残り時間の2乗で弱めている
        if (m_HorrorPulseTimer > 0.0f && m_HorrorPulseDuration > 0.0f)
        {
            m_HorrorPulseTimer =
                (std::max)(0.0f, m_HorrorPulseTimer - deltaTime);
            const float remaining =
                m_HorrorPulseTimer / m_HorrorPulseDuration;
            m_HorrorPulseStrength =
                m_HorrorPulsePeak * remaining * remaining;
        }
        else
        {
            m_HorrorPulseStrength +=
                (0.0f - m_HorrorPulseStrength) * response(HorrorRecoverResponse);
        }

        // レンズのしぶきが付いてからの秒数を進めている（crtOverlayPS.hlsl が、粒の流れ落ちた量と縦の伸びに使う）。
        // しぶきがない間も数え続けるので、大きくなりすぎないよう60秒で止めている
        m_LensDropAge = (std::min)(m_LensDropAge + deltaTime, MaxLensDropAge);
        // レンズの曇り：なめらかな曲線（smoothstep）で消している
        if (m_LensMoistureTimer > 0.0f && m_LensMoistureDuration > 0.0f)
        {
            m_LensMoistureTimer = (std::max)(
                0.0f, m_LensMoistureTimer - deltaTime);
            const float remaining =
                m_LensMoistureTimer / m_LensMoistureDuration;
            const float easedRemaining = MathUtility::SmoothCurve(remaining);
            m_LensMoisture = m_LensMoisturePeak * easedRemaining;
        }
        else
        {
            m_LensMoisture += (0.0f - m_LensMoisture) * response(LensMoistureClearResponse);
        }
    }

    // ブルームを一時的に強めている（peakIntensityは普段の強さを含めた最大値）
    void PostProcess::TriggerBloomPulse(float peakIntensity, float duration)
    {
        m_BloomPulseDuration = (std::max)(duration, MinimumPulseSeconds);
        m_BloomPulseTimer = m_BloomPulseDuration;
        m_BloomPulseStrength =
            (std::max)(0.0f, peakIntensity - m_BloomBaseIntensity);
        m_BloomIntensity = m_BloomBaseIntensity + m_BloomPulseStrength;
    }

    // 驚かせる演出を始めている
    void PostProcess::TriggerHorrorPulse(float strength, float duration)
    {
        m_HorrorPulseDuration = (std::max)(duration, MinimumPulseSeconds);
        m_HorrorPulseTimer = m_HorrorPulseDuration;
        m_HorrorPulsePeak = (std::max)(strength, 0.0f);
        m_HorrorPulseStrength = m_HorrorPulsePeak;
    }

    // レンズの曇りを始めている（すでに曇っていれば、強い方を残している）
    void PostProcess::TriggerLensMoisture(float strength, float duration)
    {
        const float clampedStrength = (std::clamp)(strength, 0.0f, 1.0f);
        m_LensMoistureDuration = (std::max)(duration, MinimumMoistureSeconds);
        m_LensMoistureTimer = m_LensMoistureDuration;
        m_LensMoisturePeak = (std::max)(m_LensMoisture, clampedStrength);
        m_LensMoisture = m_LensMoisturePeak;
        // 新しいしぶき：粒の位置・大きさ・速さを決める乱数の種を進め、流れ落ちる時間を0から数え直している。
        // 種は黄金比（0.618…）ずつずらしているので、続けてしぶきが付いても、前と似た位置に並ばない
        m_LensDropSeed = std::fmod(m_LensDropSeed + GoldenRatioFraction, 1.0f);
        m_LensDropAge = 0.0f;
    }

    // 3Dのシーンは、本描画の間の描画先（HDRの描き先。Renderer::BeginHdrScene）へ描いているため、画面効果の入力として
    // 同じ大きさ・形式の作業用テクスチャへ写している。
    bool PostProcess::CaptureBackBuffer()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // 描画先に設定している資源は安全に写せないため、描画先から外している。
        context->OMSetRenderTargets(0, nullptr, nullptr);

        // コマ落ちの間は写し取らず、前のフレームの画面を残している（Draw がそれを画面全体に出す）
        const bool captured = m_FrameHoldTimer <= 0.0f;
        if (captured)
        {
            Microsoft::WRL::ComPtr<ID3D11Resource> backBufferResource;
            Renderer::GetBackBufferRTV()->GetResource(backBufferResource.GetAddressOf());
            context->CopyResource(m_RenderTexture.GetTexture(), backBufferResource.Get());
            // 残像：頼まれていれば、今の画面を少し前の画面として止めておく（次に頼まれるまで、この画面が残る）
            if (m_EchoCopyRequested)
            {
                context->CopyResource(m_EchoTexture.GetTexture(), m_RenderTexture.GetTexture());
                m_EchoCopyRequested = false;
                m_EchoValid = true;
            }
        }

        Renderer::SetBackBufferRenderTarget();
        return captured;
    }

    float PostProcess::NextFrameHoldRandom()
    {
        MathUtility::NextXorShift32(m_FrameHoldRandom);
        return static_cast<float>(m_FrameHoldRandom & FrameHoldRandomMask) / static_cast<float>(FrameHoldRandomMask);
    }

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

    // ブルームの結果と元の画像を合成し、最後にブラウン管風の効果・色調・光の筋・レンズの汚れを重ねている。
    void PostProcess::Draw(GpuTimer* gpuTimer)
    {
        if (m_EnableBloom)
        {
            if (gpuTimer != nullptr)
            {
                gpuTimer->BeginPass(
                    GpuPass::Bloom, Renderer::GetDeviceContext());
            }
            RunBloom();
            if (gpuTimer != nullptr)
            {
                gpuTimer->EndPass(
                    GpuPass::Bloom, Renderer::GetDeviceContext());
            }
        }
        else if (gpuTimer != nullptr)
        {
            gpuTimer->SkipPass(GpuPass::Bloom);
        }
        // 自動露出の倍率を、露出のシェーダーより先に更新している
        RunAutoExposure();

        // ポーズメニューの「演出の強さ」を、各効果の強さに掛けている
        const float effectScale = m_UserEffectScale;
        const float adjustedBloom = (std::clamp)(
            m_BloomIntensity * (BloomBaseScale + effectScale * BloomEffectScaleWeight),
            0.0f, MaxAdjustedBloom);
        const float adjustedVignette = (std::clamp)(
            BaseVignetteStrength + (m_VignetteStrength - BaseVignetteStrength) * effectScale,
            0.0f, 1.0f);
        // 軽い設定では、全画面の光の筋の描画を止めている。ブルームと懐中電灯そのものは
        // 残るため見やすさは変わらず、古いGPUで最も重い追加の描画だけを省ける。
        const float volumeQuality = (std::clamp)(
            (effectScale - LightShaftsDisabledEffectScale) /
                (LightShaftsFullEffectScale - LightShaftsDisabledEffectScale), 0.0f, 1.0f);
        // ブルームの段（1/4・1/8・1/16・1/32）を、合成のシェーダーへまとめて渡している
        std::array<ID3D11ShaderResourceView*, Graphics::FullScreenQuad::BloomLevelCount> bloomLevels{};
        bloomLevels[0] = m_BloomVerticalTexture.GetSRV();
        for (unsigned int level = 0; level < WideBloomLevelCount; ++level)
        {
            bloomLevels[level + 1] = m_WideBloomBlurred[level].GetSRV();
        }
        // 影のまわりのゆがみも、ポーズメニューの「演出の強さ」で弱める（自動撮影と計測では、画像と数字がぶれないよう出さない）
        std::array<DirectX::SimpleMath::Vector4, Graphics::FullScreenQuad::PresenceWarpCount> presenceWarps = m_PresenceWarps;
        for (DirectX::SimpleMath::Vector4& warp : presenceWarps)
        {
            warp.w = Tools::CaptureMode::IsActive() ? 0.0f : (std::clamp)(warp.w * effectScale, 0.0f, 1.0f);
        }
        m_FullScreenQuad.Draw(
            m_RenderTexture.GetSRV(),
            m_EnableBloom ? bloomLevels.data() : nullptr,
            m_AutoExposureSRV.Get(),
            m_Time,
            m_EnableBloom ? adjustedBloom : 0.0f,
            m_EnableNoise ? m_NoiseAmount * effectScale : 0.0f,
            adjustedVignette,
            m_EnableVolumetricLight
                ? m_VolumetricIntensity * volumeQuality
                : 0.0f,
            m_LensDistortionStrength * effectScale,
            m_HorrorPulseStrength * effectScale,
            (std::clamp)(
                m_Exposure + m_UserBrightnessOffset, MinCompositeExposure, MaxCompositeExposure),
            m_LensMoisture,
            (std::clamp)(m_CorridorTension * effectScale, 0.0f, 1.0f),
            (std::clamp)(m_FilmGradeStrength * effectScale, 0.0f, 1.0f),
            (std::clamp)(m_LensDirtStrength * effectScale, 0.0f, 1.0f),
            (std::clamp)((std::max)(m_SignalInterference, m_PresenceStatic) * effectScale, 0.0f, 1.0f),
            m_FrameHoldTimer > 0.0f ? 1.0f : 0.0f,
            m_FrameHoldOffset,
            (std::clamp)(m_FearVision * effectScale, 0.0f, 1.0f),
            (std::clamp)(m_FearBeat * effectScale, 0.0f, 1.0f),
            m_LensDropSeed,
            m_LensDropAge,
            presenceWarps.data(),
            m_EchoValid ? m_EchoTexture.GetSRV() : nullptr,
            DirectX::SimpleMath::Vector4(
                Tools::CaptureMode::IsActive() ? 0.0f : (std::clamp)(m_EchoStrength * effectScale, 0.0f, 1.0f),
                m_EchoOffset.x, m_EchoOffset.y, m_EchoSeed),
            m_SubmergeLevel);

        // 最後に、HDRの画面を曲線で0〜1へ移して、バックバッファへ描いている（自動撮影・計測では --tonemap で曲線を選べる）
        const auto toneMapOverride = Tools::CaptureMode::GetToneMapOverride();
        // デバッグ表示の間は、色分けがずれないよう曲線を掛けない
        const ToneMapMode toneMapMode = Renderer::GetDebugViewMode() != ShaderDebugView::Final
            ? ToneMapMode::Passthrough
            : toneMapOverride.value_or(m_ToneMapMode);
        Renderer::EndHdrScene();
        m_FullScreenQuad.DrawToneMap(Renderer::GetSceneHdrSRV(), static_cast<unsigned int>(toneMapMode));
    }
}
