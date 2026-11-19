// ============================================================================
// ファイルの役割: DDGI（壁や床で跳ね返った間接光）のプローブの格子・画像・Compute Shader の実行（説明は DDGIVolume.h）
// ============================================================================

#include "DDGIVolume.h"
#include "MathUtility.h"
#include "GIUpdateSettings.h"
#include "shader/computeDispatchConfig.hlsli"

#include "CaptureMode.h"
#include "Renderer.h"
#include "utility.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 間接光の画像をピクセルシェーダーへ渡す番号（screenAmbientOcclusion.hlsli の g_ScreenIndirectLight）
    constexpr UINT IndirectLightSlot = SIGNAL_LOST_SCREEN_INDIRECT_LIGHT_TEXTURE_SLOT;
    // Compute Shader の入力の番号（ddgiCommon.hlsli・ddgiTraceCS・ddgiUpdate.hlsli・ddgiApplyCS と同じ）
    constexpr UINT PointLightsSlot = 0;
    constexpr UINT RayResultsSlot = 1;
    constexpr UINT IrradianceAtlasSlot = 2;
    constexpr UINT DistanceAtlasSlot = 3;
    static_assert(DistanceAtlasSlot == IrradianceAtlasSlot + 1);
    constexpr UINT PreviousAtlasSlot = 4;
    constexpr UINT DepthSlot = 5;
    // 光線の追跡は t0〜t3（t1 は空）。解除は適用パスの深度 t5 まで含める。
    constexpr UINT TraceInputCount = DistanceAtlasSlot - PointLightsSlot + 1;
    constexpr UINT AtlasInputCount = DistanceAtlasSlot - IrradianceAtlasSlot + 1;
    constexpr UINT ComputeInputSlotCount = DepthSlot + 1;
    // 部屋の形の定数バッファ（roomOcclusion.hlsli の b11）
    constexpr UINT RoomShapeSlot = SIGNAL_LOST_ROOM_OCCLUSION_BUFFER_SLOT;

    // プローブを並べる間隔の目安（1単位は約5cm。約1.5m）と、1方向に並べる数の上限、高さの段の数
    constexpr float TargetProbeSpacing = 30.0f;
    constexpr uint32_t MaxProbesPerAxis = 32;
    constexpr uint32_t VerticalProbeCount = 4;
    // 一番下と一番上のプローブを、床と天井から離す距離（面の上にプローブを置くと、光線がすぐ面に当たって暗くなる）
    constexpr float ProbeSurfaceMargin = 4.0f;
    // 前の値を残す割合。大きいほどちらつかないが、懐中電灯を動かしたときの跳ね返りが遅れる（60fpsで約0.2秒で追いつく）
    constexpr float ProbeHysteresis = 0.92f;
    // 読む位置を面と視線の向きへずらす距離（プローブの間隔に対する割合。Majercik 2019 の 0.3 に近い値）
    constexpr float NormalBiasRatio = 0.06f;
    constexpr float ViewBiasRatio = 0.18f;
    // 光線の当たった面に足す、前のフレームの間接光の割合（2回目以降の跳ね返り。1で全部）
    constexpr float BounceStrength = 0.8f;
    // 光線を追う一番遠い距離
    constexpr float MaxRayDistance = 600.0f;
    // 円周率（光線の向きの乱数に使う）
    constexpr float Pi = MathUtility::Pi;
    // 間接光の画像を縦横何分の1の解像度で作るか、Compute Shader の1グループの縦横の数
    constexpr int OutputDownscale = SIGNAL_LOST_DDGI_APPLY_DOWNSAMPLE_FACTOR;
    constexpr UINT ApplyGroupSize = SIGNAL_LOST_DDGI_APPLY_GROUP_SIZE;
}

namespace Effect
{
    bool DDGIVolume::GpuTexture::Create(uint32_t width, uint32_t height, DXGI_FORMAT format)
    {
        ID3D11Device* device = Renderer::GetDevice();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, Texture.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(device->CreateUnorderedAccessView(Texture.Get(), nullptr, UAV.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(device->CreateShaderResourceView(Texture.Get(), nullptr, SRV.ReleaseAndGetAddressOf()));
    }

    void DDGIVolume::GpuTexture::Reset()
    {
        SRV.Reset();
        UAV.Reset();
        Texture.Reset();
    }

    void DDGIVolume::Init(int width, int height)
    {
        m_Width = (std::max)(1, (width + OutputDownscale - 1) / OutputDownscale);
        m_Height = (std::max)(1, (height + OutputDownscale - 1) / OutputDownscale);
        m_Random.seed(Tools::CaptureMode::MakeRandomSeed(Tools::CaptureMode::RandomSeed::DdgiProbeSampling));

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        const bool created =
            m_IndirectLight.Create(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height),
                DXGI_FORMAT_R16G16B16A16_FLOAT) &&
            Renderer::CreateConstantBuffer(sizeof(DDGIParameters), m_ParameterBuffer.ReleaseAndGetAddressOf()) &&
            SUCCEEDED(Renderer::GetDevice()->CreateSamplerState(&samplerDesc, m_LinearClamp.ReleaseAndGetAddressOf())) &&
            m_TraceShader.Create("shader/ddgiTraceCS.hlsl") &&
            m_IrradianceUpdateShader.Create("shader/ddgiIrradianceUpdateCS.hlsl") &&
            m_DistanceUpdateShader.Create("shader/ddgiDistanceUpdateCS.hlsl") &&
            m_ApplyShader.Create("shader/ddgiApplyCS.hlsl");
        if (!created)
        {
            utility::ReportFatalError("間接光（DDGI）の準備に失敗しました。");
        }
        m_HasGrid = false;
        m_RoomVersion = 0;
    }

    void DDGIVolume::Uninit()
    {
        m_TraceShader.Uninit();
        m_IrradianceUpdateShader.Uninit();
        m_DistanceUpdateShader.Uninit();
        m_ApplyShader.Uninit();
        m_LinearClamp.Reset();
        m_ParameterBuffer.Reset();
        m_IndirectLight.Reset();
        m_RayResults.Reset();
        for (std::size_t index = 0; index < AtlasBufferCount; ++index)
        {
            m_IrradianceAtlas[index].Reset();
            m_DistanceAtlas[index].Reset();
        }
        m_HasGrid = false;
    }

    // 壁の長方形の範囲に、プローブを目安の間隔で並べている（1方向に MaxProbesPerAxis 個まで）
    bool DDGIVolume::RebuildGrid()
    {
        const ROOM_OCCLUSION_BUFFER& room = Renderer::GetRoomOcclusion();
        m_HasGrid = false;
        if (room.BoxCount == 0 || room.CeilingY <= room.FloorY)
        {
            return false;
        }
        Vector2 minimum(room.Boxes[0].x - room.Boxes[0].z, room.Boxes[0].y - room.Boxes[0].w);
        Vector2 maximum(room.Boxes[0].x + room.Boxes[0].z, room.Boxes[0].y + room.Boxes[0].w);
        for (unsigned int index = 1; index < room.BoxCount; ++index)
        {
            const Vector4& box = room.Boxes[index];
            minimum.x = (std::min)(minimum.x, box.x - box.z);
            minimum.y = (std::min)(minimum.y, box.y - box.w);
            maximum.x = (std::max)(maximum.x, box.x + box.z);
            maximum.y = (std::max)(maximum.y, box.y + box.w);
        }
        const auto countFor = [](float extent)
        {
            const uint32_t count = static_cast<uint32_t>(std::ceil(extent / TargetProbeSpacing)) + 1u;
            // 補間先があるよう、各軸に最低2つ置く。
            constexpr uint32_t minProbesPerAxis = 2;
            return (std::clamp)(count, minProbesPerAxis, MaxProbesPerAxis);
        };
        const Vector2 extent = maximum - minimum;
        const float bottom = room.FloorY + ProbeSurfaceMargin;
        const float top = room.CeilingY - ProbeSurfaceMargin;
        m_ProbeCounts = { countFor(extent.x), VerticalProbeCount, countFor(extent.y) };
        m_ProbeOrigin = Vector3(minimum.x, bottom, minimum.y);
        m_ProbeSpacing = Vector3(
            extent.x / static_cast<float>(m_ProbeCounts[0] - 1),
            (top - bottom) / static_cast<float>(m_ProbeCounts[1] - 1),
            extent.y / static_cast<float>(m_ProbeCounts[2] - 1));

        // 画像：光線の結果は「光線の番号 x プローブの番号」、八面体マップは横にプローブの x と y の段、縦に z を並べる
        const uint32_t probeCount = GetProbeCount();
        const uint32_t tilesAcross = m_ProbeCounts[0] * m_ProbeCounts[1];
        const uint32_t irradianceTile = IrradianceTexels + 2 * SIGNAL_LOST_DDGI_BORDER;
        const uint32_t distanceTile = DistanceTexels + 2 * SIGNAL_LOST_DDGI_BORDER;
        bool created = m_RayResults.Create(RaysPerProbe, probeCount, DXGI_FORMAT_R16G16B16A16_FLOAT);
        for (std::size_t index = 0; index < AtlasBufferCount && created; ++index)
        {
            created = m_IrradianceAtlas[index].Create(tilesAcross * irradianceTile, m_ProbeCounts[2] * irradianceTile,
                    DXGI_FORMAT_R16G16B16A16_FLOAT) &&
                m_DistanceAtlas[index].Create(tilesAcross * distanceTile, m_ProbeCounts[2] * distanceTile,
                    DXGI_FORMAT_R16G16B16A16_FLOAT);
        }
        if (!created)
        {
            return false;
        }
        m_Current = 0;
        m_ResetHistory = true;
        m_UpdatePhase = 0;
        m_HasGrid = true;
        return true;
    }

    void DDGIVolume::UploadParameters()
    {
        Renderer::GetDeviceContext()->UpdateSubresource(m_ParameterBuffer.Get(), 0, nullptr, &m_Parameters, 0, 0);
    }

    bool DDGIVolume::Update(ID3D11ShaderResourceView* pointLights, uint32_t lightCount,
        const LIGHT& light, const Vector3& cameraPosition, const Matrix& view, uint32_t updateFrames)
    {
        if (m_RoomVersion != Renderer::GetRoomOcclusionVersion())
        {
            m_RoomVersion = Renderer::GetRoomOcclusionVersion();
            RebuildGrid();
        }
        m_Active = m_HasGrid && pointLights != nullptr;
        if (!m_Active)
        {
            return false;
        }
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // 光線の向きを、毎フレーム乱数で回している（同じ向きだけを調べて、決まった模様が出ないように）
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        Vector3 axis(unit(m_Random) * 2.0f - 1.0f, unit(m_Random) * 2.0f - 1.0f, unit(m_Random) * 2.0f - 1.0f);
        // ほぼゼロの軸を正規化すると回転が不安定になるため、既定の軸に置き換える。
        constexpr float MinRotationAxisLengthSquared = 1e-6f;
        if (axis.LengthSquared() < MinRotationAxisLengthSquared)
        {
            axis = Vector3::UnitY;
        }
        axis.Normalize();
        const Matrix rotation = Matrix::CreateFromAxisAngle(axis, unit(m_Random) * 2.0f * Pi);

        // 懐中電灯：向きはビュー空間で渡されているので、ワールドへ戻している
        Vector3 flashlightDirection = Vector3::TransformNormal(
            Vector3(light.Direction.x, light.Direction.y, light.Direction.z), view.Invert());
        flashlightDirection.Normalize();

        DDGIParameters& p = m_Parameters;
        p.ProbeOrigin = m_ProbeOrigin;
        p.RaysPerProbe = RaysPerProbe;
        p.ProbeSpacing = m_ProbeSpacing;
        // 初回は全プローブを作る。以降は通し番号の余りで均等に分ける。
        p.ProbeUpdateStride = m_ResetHistory ? 1u : (std::clamp)(updateFrames,
            static_cast<uint32_t>(GIUpdateSettings::MinFrames), static_cast<uint32_t>(GIUpdateSettings::MaxFrames));
        p.ProbeUpdatePhase = m_UpdatePhase % p.ProbeUpdateStride;
        m_UpdatePhase = (p.ProbeUpdatePhase + 1u) % p.ProbeUpdateStride;
        // 同じ時間で古い値の重みが同程度に減るよう、更新しないフレーム数も含める。
        p.Hysteresis = std::pow(ProbeHysteresis, static_cast<float>(p.ProbeUpdateStride));
        p.ProbeCounts[0] = m_ProbeCounts[0];
        p.ProbeCounts[1] = m_ProbeCounts[1];
        p.ProbeCounts[2] = m_ProbeCounts[2];
        p.LightCount = lightCount;
        p.RayRotation = rotation.Transpose();
        p.FlashlightPosition = cameraPosition;
        p.FlashlightRange = light.Range;
        p.FlashlightDirection = flashlightDirection;
        p.FlashlightIntensity = light.Intensity;
        p.FlashlightColor = Vector3(light.Diffuse.x, light.Diffuse.y, light.Diffuse.z);
        p.FlashlightEnabled = (light.Enable && light.FlashlightEnabled) ? 1.0f : 0.0f;
        p.FlashlightCone = light.SpotParams;
        const float smallestSpacing = (std::min)(m_ProbeSpacing.x, (std::min)(m_ProbeSpacing.y, m_ProbeSpacing.z));
        p.NormalBias = smallestSpacing * NormalBiasRatio;
        p.ViewBias = smallestSpacing * ViewBiasRatio;
        p.BounceStrength = BounceStrength;
        p.ResetHistory = m_ResetHistory ? 1.0f : 0.0f;
        p.MaxRayDistance = MaxRayDistance;
        UploadParameters();
        m_ResetHistory = false;

        ID3D11Buffer* parameterBuffer = m_ParameterBuffer.Get();
        ID3D11Buffer* roomBuffer = Renderer::GetRoomOcclusionBuffer();
        context->CSSetConstantBuffers(0, 1, &parameterBuffer);
        context->CSSetConstantBuffers(RoomShapeSlot, 1, &roomBuffer);
        ID3D11SamplerState* sampler = m_LinearClamp.Get();
        context->CSSetSamplers(0, 1, &sampler);

        const int previous = m_Current;
        const int next = 1 - m_Current;
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        ID3D11ShaderResourceView* nullSRVs[ComputeInputSlotCount] = {};

        // 1. 光線を飛ばす（t0 = 点光源、t2・t3 = 前の八面体マップ、u0 = 光線の結果）。1グループ = 1プローブ
        m_TraceShader.SetGPU();
        ID3D11ShaderResourceView* traceInputs[TraceInputCount] = {
            pointLights, nullptr, m_IrradianceAtlas[previous].SRV.Get(), m_DistanceAtlas[previous].SRV.Get() };
        context->CSSetShaderResources(PointLightsSlot, TraceInputCount, traceInputs);
        ID3D11UnorderedAccessView* rayUAV = m_RayResults.UAV.Get();
        context->CSSetUnorderedAccessViews(0, 1, &rayUAV, nullptr);
        const UINT probeGroups =
            (GetProbeCount() - p.ProbeUpdatePhase + p.ProbeUpdateStride - 1u) / p.ProbeUpdateStride;
        context->Dispatch(probeGroups, 1, 1);
        context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        context->CSSetShaderResources(0, ComputeInputSlotCount, nullSRVs);

        // 2. 八面体マップを更新する（t1 = 光線の結果、t4 = 前の画像、u0 = 新しい画像）。1グループ = 1プローブの区画
        const auto runUpdate = [&](const ComputeShader& shader, const GpuTexture& previousAtlas, const GpuTexture& nextAtlas)
        {
            // 更新しない区画を引き継ぐ。読み取り・書き込みにバインドする前にコピーする。
            if (p.ProbeUpdateStride > 1u)
            {
                context->CopyResource(nextAtlas.Texture.Get(), previousAtlas.Texture.Get());
            }
            shader.SetGPU();
            ID3D11ShaderResourceView* rays = m_RayResults.SRV.Get();
            ID3D11ShaderResourceView* previousSRV = previousAtlas.SRV.Get();
            context->CSSetShaderResources(RayResultsSlot, 1, &rays);
            context->CSSetShaderResources(PreviousAtlasSlot, 1, &previousSRV);
            ID3D11UnorderedAccessView* output = nextAtlas.UAV.Get();
            context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            context->Dispatch(probeGroups, 1, 1);
            context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
            context->CSSetShaderResources(0, ComputeInputSlotCount, nullSRVs);
        };
        runUpdate(m_IrradianceUpdateShader, m_IrradianceAtlas[previous], m_IrradianceAtlas[next]);
        runUpdate(m_DistanceUpdateShader, m_DistanceAtlas[previous], m_DistanceAtlas[next]);
        m_Current = next;

        context->CSSetShader(nullptr, nullptr, 0);
        return true;
    }

    void DDGIVolume::Apply(const Matrix& view, const Matrix& projection, const D3D11_VIEWPORT& viewport)
    {
        if (!m_Active)
        {
            Unbind();
            return;
        }
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // 描く範囲が作った画像より大きいときは、画像の大きさまでにしている
        m_Parameters.InverseProjection = projection.Invert().Transpose();
        m_Parameters.InverseView = view.Invert().Transpose();
        m_Parameters.ViewportSize = Vector2(
            (std::min)(viewport.Width, static_cast<float>(m_Width * OutputDownscale)),
            (std::min)(viewport.Height, static_cast<float>(m_Height * OutputDownscale)));
        m_Parameters.OutputSize = Vector2(
            std::ceil(m_Parameters.ViewportSize.x / OutputDownscale), std::ceil(m_Parameters.ViewportSize.y / OutputDownscale));
        m_Parameters.ViewportOffset = Vector2(viewport.TopLeftX, viewport.TopLeftY);
        UploadParameters();

        // 書き込む画像をピクセルシェーダーの読み取りから外し、深度バッファも描画先から外してから読んでいる
        Unbind();
        ID3D11RenderTargetView* sceneTarget = Renderer::GetBackBufferRTV();
        context->OMSetRenderTargets(1, &sceneTarget, nullptr);

        // 壁の向こうのプローブを使わない判定に、部屋の形（b11）も使っている
        ID3D11Buffer* parameterBuffer = m_ParameterBuffer.Get();
        ID3D11Buffer* roomBuffer = Renderer::GetRoomOcclusionBuffer();
        context->CSSetConstantBuffers(0, 1, &parameterBuffer);
        context->CSSetConstantBuffers(RoomShapeSlot, 1, &roomBuffer);
        ID3D11SamplerState* sampler = m_LinearClamp.Get();
        context->CSSetSamplers(0, 1, &sampler);
        m_ApplyShader.SetGPU();
        ID3D11ShaderResourceView* atlases[AtlasInputCount] = {
            m_IrradianceAtlas[m_Current].SRV.Get(), m_DistanceAtlas[m_Current].SRV.Get() };
        context->CSSetShaderResources(IrradianceAtlasSlot, AtlasInputCount, atlases);
        ID3D11ShaderResourceView* depth = Renderer::GetDepthShaderResourceView();
        context->CSSetShaderResources(DepthSlot, 1, &depth);
        ID3D11UnorderedAccessView* output = m_IndirectLight.UAV.Get();
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        context->Dispatch(
            (static_cast<UINT>(m_Parameters.OutputSize.x) + ApplyGroupSize - 1) / ApplyGroupSize,
            (static_cast<UINT>(m_Parameters.OutputSize.y) + ApplyGroupSize - 1) / ApplyGroupSize, 1);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        ID3D11ShaderResourceView* nullSRVs[ComputeInputSlotCount] = {};
        context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        context->CSSetShaderResources(0, ComputeInputSlotCount, nullSRVs);
        context->CSSetShader(nullptr, nullptr, 0);

        // 本描画の描画先（色と深度）に戻して、結果を t18 に設定している
        context->OMSetRenderTargets(1, &sceneTarget, Renderer::GetDepthStencilView());
        ID3D11ShaderResourceView* indirect = m_IndirectLight.SRV.Get();
        context->PSSetShaderResources(IndirectLightSlot, 1, &indirect);
    }

    void DDGIVolume::Unbind()
    {
        ID3D11ShaderResourceView* nullResource = nullptr;
        Renderer::GetDeviceContext()->PSSetShaderResources(IndirectLightSlot, 1, &nullResource);
    }
}
