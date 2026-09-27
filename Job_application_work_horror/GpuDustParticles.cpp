// ============================================================================
// ファイルの役割: 空気中を漂う埃を、生成・更新・削除から描画までGPUだけで行う
//                 「GPUパーティクル」を管理します。
// 主な技術: Append/Consume StructuredBuffer（内部カウンター）、CopyStructureCount、
//           DispatchIndirect、DrawIndexedInstancedIndirect、SV_VertexIDによる
//           頂点バッファなしのビルボード生成
// ============================================================================

#include "GpuDustParticles.h"

#include "Camera.h"
#include "utility.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace DirectX::SimpleMath;

namespace
{
    // 埃を置く範囲。カメラ（目の高さ）を中心に、床から天井までを覆う箱です。
    const Vector3 VolumeExtent(130.0f, 26.0f, 130.0f);
    const Vector3 VolumeCenterOffset(0.0f, -14.0f, 0.0f);
    constexpr float MinLifetime = 6.0f;
    constexpr float MaxLifetime = 12.0f;
    constexpr float MinSize = 0.06f;
    constexpr float MaxSize = 0.22f;
    // 生存数の目安。寿命の平均で割った数を毎秒生成すると、この数前後で釣り合います。
    constexpr float TargetFillRatio = 0.9f;

    // 間接実行の引数の位置（バイト）。dustArgsCS.hlsl と一致させます。
    constexpr UINT DispatchArgsOffset = 0;
    constexpr UINT DrawArgsOffset = 12;

    constexpr uint32_t ThreadGroupSize = 64;   // dustParticleCommon.hlsli の DUST_THREAD_GROUP_SIZE
    constexpr UINT DrawParamsSlot = 11;
    constexpr UINT KeepCounter = static_cast<UINT>(-1);

    bool CreateConstantBuffer(UINT size, ID3D11Buffer** buffer)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = size;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(Renderer::GetDevice()->CreateBuffer(&desc, nullptr, buffer));
    }

    void UnbindComputeResources(ID3D11DeviceContext* context)
    {
        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ID3D11UnorderedAccessView* nullUAVs[2] = {};
        context->CSSetShaderResources(0, 2, nullSRVs);
        context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    }
}

namespace Effect
{
    void GpuDustParticles::Init()
    {
        ID3D11Device* device = Renderer::GetDevice();

        bool created =
            m_EmitShader.Create("shader/dustEmitCS.hlsl") &&
            m_UpdateShader.Create("shader/dustUpdateCS.hlsl") &&
            m_ArgsShader.Create("shader/dustArgsCS.hlsl") &&
            CreateParticleList(m_Lists[0]) &&
            CreateParticleList(m_Lists[1]) &&
            CreateConstantBuffer(sizeof(SimulationParams),
                m_SimulationParamsBuffer.ReleaseAndGetAddressOf()) &&
            CreateConstantBuffer(sizeof(DrawParams),
                m_DrawParamsBuffer.ReleaseAndGetAddressOf());

        // 頂点入力を持たないシェーダーなので、Input Layoutは作りません。
        if (created)
        {
            std::vector<unsigned char> vertexShaderObject;
            created =
                SUCCEEDED(Renderer::CompileShader(
                    "shader/dustVS.hlsl", "main", "vs_5_0", vertexShaderObject)) &&
                SUCCEEDED(device->CreateVertexShader(
                    vertexShaderObject.data(), vertexShaderObject.size(), nullptr,
                    m_VertexShader.ReleaseAndGetAddressOf())) &&
                SUCCEEDED(Renderer::CreatePixelShader(
                    m_PixelShader.ReleaseAndGetAddressOf(), "shader/dustPS.hlsl"));
        }

        // Emitのスレッドごとの乱数状態。0だとXorshiftが止まるため奇数にします。
        if (created)
        {
            std::vector<uint32_t> seeds(MaxEmitPerFrame);
            std::mt19937 random(0x5eed1234u);
            for (uint32_t& seed : seeds)
            {
                seed = random() | 1u;
            }

            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(uint32_t) * MaxEmitPerFrame;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = sizeof(uint32_t);
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = seeds.data();

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = MaxEmitPerFrame;
            created =
                SUCCEEDED(device->CreateBuffer(
                    &desc, &initial, m_SeedBuffer.ReleaseAndGetAddressOf())) &&
                SUCCEEDED(device->CreateUnorderedAccessView(
                    m_SeedBuffer.Get(), &uavDesc, m_SeedUAV.ReleaseAndGetAddressOf()));
        }

        // 内部カウンターの写し先。HLSLからByteAddressBufferとして読むためRAWビューにします。
        if (created)
        {
            const uint32_t zeros[4] = {};
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(zeros);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = zeros;

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            srvDesc.BufferEx.NumElements = 4;
            srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            created =
                SUCCEEDED(device->CreateBuffer(
                    &desc, &initial, m_CountBuffer.ReleaseAndGetAddressOf())) &&
                SUCCEEDED(device->CreateShaderResourceView(
                    m_CountBuffer.Get(), &srvDesc, m_CountSRV.ReleaseAndGetAddressOf()));

            D3D11_BUFFER_DESC readbackDesc{};
            readbackDesc.ByteWidth = sizeof(zeros);
            readbackDesc.Usage = D3D11_USAGE_STAGING;
            readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (auto& readback : m_ReadbackBuffers)
            {
                created = created && SUCCEEDED(device->CreateBuffer(
                    &readbackDesc, nullptr, readback.ReleaseAndGetAddressOf()));
            }
        }

        // 間接実行の引数バッファ。Compute Shaderが書き込むためUAV（RAW）も作ります。
        if (created)
        {
            const uint32_t initialArgs[8] = { 0, 1, 1, 0, 1, 0, 0, 0 };
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(initialArgs);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS |
                D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = initialArgs;

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = 8;
            uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            created =
                SUCCEEDED(device->CreateBuffer(
                    &desc, &initial, m_IndirectArgsBuffer.ReleaseAndGetAddressOf())) &&
                SUCCEEDED(device->CreateUnorderedAccessView(
                    m_IndirectArgsBuffer.Get(), &uavDesc,
                    m_IndirectArgsUAV.ReleaseAndGetAddressOf()));
        }

        // 最大数分の「フラットな」インデックス配列を先に作っておきます。
        // 粒子iは頂点4i〜4i+3を使い、頂点の中身はSV_VertexIDから頂点シェーダーが作ります。
        if (created)
        {
            std::vector<uint32_t> indices(static_cast<size_t>(MaxParticles) * 6);
            for (uint32_t i = 0; i < MaxParticles; ++i)
            {
                const uint32_t vertexBase = i * 4;
                uint32_t* index = &indices[static_cast<size_t>(i) * 6];
                index[0] = vertexBase + 0;
                index[1] = vertexBase + 1;
                index[2] = vertexBase + 2;
                index[3] = vertexBase + 2;
                index[4] = vertexBase + 1;
                index[5] = vertexBase + 3;
            }

            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * indices.size());
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = indices.data();
            created = SUCCEEDED(device->CreateBuffer(
                &desc, &initial, m_IndexBuffer.ReleaseAndGetAddressOf()));
        }

        if (!created)
        {
            utility::ReportFatalError("GPUパーティクル（埃）の初期化に失敗しました。");
        }

        Reset();
    }

    bool GpuDustParticles::CreateParticleList(ParticleList& list)
    {
        ID3D11Device* device = Renderer::GetDevice();

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(Particle) * MaxParticles;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(Particle);

        // Append/Consumeで使うUAVは、配列とは別に隠れた個数カウンターを持ちます。
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = MaxParticles;
        uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;

        // 描画の頂点シェーダーと、Updateの入力はSRVで読みます。
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = MaxParticles;

        return SUCCEEDED(device->CreateBuffer(
                &desc, nullptr, list.Buffer.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(device->CreateUnorderedAccessView(
                list.Buffer.Get(), &uavDesc, list.UAV.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(device->CreateShaderResourceView(
                list.Buffer.Get(), &srvDesc, list.SRV.ReleaseAndGetAddressOf()));
    }

    void GpuDustParticles::Uninit()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        UnbindComputeResources(context);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->VSSetShaderResources(0, 1, &nullSRV);

        m_EmitShader.Uninit();
        m_UpdateShader.Uninit();
        m_ArgsShader.Uninit();
        m_VertexShader.Reset();
        m_PixelShader.Reset();
        for (ParticleList& list : m_Lists)
        {
            list.UAV.Reset();
            list.SRV.Reset();
            list.Buffer.Reset();
        }
        for (auto& readback : m_ReadbackBuffers)
        {
            readback.Reset();
        }
        m_SeedUAV.Reset();
        m_SeedBuffer.Reset();
        m_CountSRV.Reset();
        m_CountBuffer.Reset();
        m_IndirectArgsUAV.Reset();
        m_IndirectArgsBuffer.Reset();
        m_IndexBuffer.Reset();
        m_SimulationParamsBuffer.Reset();
        m_DrawParamsBuffer.Reset();
    }

    void GpuDustParticles::Reset()
    {
        if (!m_CountBuffer || !m_IndirectArgsBuffer)
        {
            return;
        }

        // 個数の写しと引数を0に戻し、次のEmitで内部カウンターも0から始めます。
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        const uint32_t zeros[4] = {};
        context->UpdateSubresource(m_CountBuffer.Get(), 0, nullptr, zeros, 0, 0);
        const uint32_t initialArgs[8] = { 0, 1, 1, 0, 1, 0, 0, 0 };
        context->UpdateSubresource(
            m_IndirectArgsBuffer.Get(), 0, nullptr, initialArgs, 0, 0);

        m_CounterResetPending = true;
        m_HasPreviousCameraPosition = false;
        m_EmitRemainder = 0.0f;
        m_DebugAliveCount = 0;
    }

    void GpuDustParticles::Simulate(const Camera& camera, float deltaTime, float density)
    {
        if (!m_EmitShader.IsCreated() || deltaTime <= 0.0f)
        {
            return;
        }
        deltaTime = (std::min)(deltaTime, 0.1f);
        density = std::clamp(density, 0.0f, 1.0f);
        m_SimulationTime += deltaTime;

        // カメラの速さ。ワープ（リスタートなど）のような大きな移動は空気の流れにしません。
        const Vector3 cameraPosition = camera.GetPosition();
        Vector3 cameraVelocity = Vector3::Zero;
        if (m_HasPreviousCameraPosition)
        {
            cameraVelocity = (cameraPosition - m_PreviousCameraPosition) / deltaTime;
            if (cameraVelocity.LengthSquared() > 400.0f * 400.0f)
            {
                cameraVelocity = Vector3::Zero;
            }
        }
        m_PreviousCameraPosition = cameraPosition;
        m_HasPreviousCameraPosition = true;

        // 平均寿命で割った数を毎秒作ると、生存数が目標の前後で釣り合います。
        // リセット直後は空気が空にならないよう、1フレームの上限までまとめて作ります。
        const float targetAlive = static_cast<float>(MaxParticles) * TargetFillRatio * density;
        const float emitPerSecond = targetAlive / ((MinLifetime + MaxLifetime) * 0.5f);
        m_EmitRemainder += emitPerSecond * deltaTime;
        uint32_t emitCount = static_cast<uint32_t>(m_EmitRemainder);
        m_EmitRemainder -= static_cast<float>(emitCount);
        if (m_CounterResetPending && density > 0.0f)
        {
            emitCount = MaxEmitPerFrame;
        }
        emitCount = (std::min)(emitCount, MaxEmitPerFrame);
        m_LastEmitCount = emitCount;

        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        SimulationParams params{};
        params.CameraPosition = cameraPosition;
        params.DeltaTime = deltaTime;
        params.VolumeExtent = VolumeExtent;
        params.SimulationTime = m_SimulationTime;
        params.CameraVelocity = cameraVelocity;
        params.EmitCount = emitCount;
        params.VolumeCenterOffset = VolumeCenterOffset;
        params.MaxParticles = MaxParticles;
        params.MinLifetime = MinLifetime;
        params.MaxLifetime = MaxLifetime;
        params.MinSize = MinSize;
        params.MaxSize = MaxSize;
        context->UpdateSubresource(m_SimulationParamsBuffer.Get(), 0, nullptr, &params, 0, 0);
        ID3D11Buffer* simulationParams = m_SimulationParamsBuffer.Get();
        context->CSSetConstantBuffers(0, 1, &simulationParams);

        // 描画側に残っている粒子のSRVを外し、UAVとして結び付けられるようにします。
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->VSSetShaderResources(0, 1, &nullSRV);

        ParticleList& current = m_Lists[m_CurrentList];
        ParticleList& next = m_Lists[1 - m_CurrentList];

        // 1. Emit: 生きている一覧の末尾へ新しい粒子をAppendします。
        //    UAVを結び付けるときの初期値で内部カウンターを設定でき、
        //    -1を渡すと前回の個数（=生きている粒子数）をそのまま引き継ぎます。
        {
            m_EmitShader.SetGPU();
            ID3D11ShaderResourceView* countSRV = m_CountSRV.Get();
            context->CSSetShaderResources(0, 1, &countSRV);
            ID3D11UnorderedAccessView* uavs[2] = { m_SeedUAV.Get(), current.UAV.Get() };
            const UINT initialCounts[2] = {
                KeepCounter, m_CounterResetPending ? 0u : KeepCounter };
            context->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);
            if (emitCount > 0)
            {
                context->Dispatch(
                    (emitCount + ThreadGroupSize - 1) / ThreadGroupSize, 1, 1);
            }
            UnbindComputeResources(context);
        }

        // 2. Emit後の個数を普通のバッファへ写し、Updateの起動グループ数へ変換します。
        context->CopyStructureCount(m_CountBuffer.Get(), 0, current.UAV.Get());
        RunArgsShader();

        // 3. Update: 生き残った粒子だけをもう1本の一覧へAppendします。
        //    起動するグループ数はGPUが決めた値（DispatchIndirect）なので、CPUは個数を知りません。
        {
            m_UpdateShader.SetGPU();
            ID3D11ShaderResourceView* srvs[2] = { current.SRV.Get(), m_CountSRV.Get() };
            context->CSSetShaderResources(0, 2, srvs);
            ID3D11UnorderedAccessView* outputUAV = next.UAV.Get();
            const UINT resetCounter = 0;
            context->CSSetUnorderedAccessViews(0, 1, &outputUAV, &resetCounter);
            context->DispatchIndirect(m_IndirectArgsBuffer.Get(), DispatchArgsOffset);
            UnbindComputeResources(context);
        }

        // 4. 更新後の個数を写し、描画のインデックス数（個数 x 6）へ変換します。
        context->CopyStructureCount(m_CountBuffer.Get(), 0, next.UAV.Get());
        RunArgsShader();
        context->CSSetShader(nullptr, nullptr, 0);

        m_CurrentList = 1 - m_CurrentList;
        m_CounterResetPending = false;

        ReadBackAliveCount();
    }

    void GpuDustParticles::RunArgsShader()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        m_ArgsShader.SetGPU();
        ID3D11ShaderResourceView* countSRV = m_CountSRV.Get();
        context->CSSetShaderResources(0, 1, &countSRV);
        ID3D11UnorderedAccessView* argsUAV = m_IndirectArgsUAV.Get();
        context->CSSetUnorderedAccessViews(0, 1, &argsUAV, nullptr);
        context->Dispatch(1, 1, 1);
        UnbindComputeResources(context);
    }

    void GpuDustParticles::ReadBackAliveCount()
    {
        // 今フレームの個数を写し、3フレーム前に写した分をGPUを待たずに読みます。
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        context->CopyResource(
            m_ReadbackBuffers[m_ReadbackFrame % ReadbackLatency].Get(),
            m_CountBuffer.Get());
        ++m_ReadbackFrame;
        if (m_ReadbackFrame < ReadbackLatency)
        {
            return;
        }

        ID3D11Buffer* oldest = m_ReadbackBuffers[m_ReadbackFrame % ReadbackLatency].Get();
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (context->Map(oldest, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped) == S_OK)
        {
            m_DebugAliveCount = *static_cast<const uint32_t*>(mapped.pData);
            context->Unmap(oldest, 0);
        }
    }

    void GpuDustParticles::Draw(const Camera& camera)
    {
        if (!m_VertexShader || !m_PixelShader)
        {
            return;
        }

        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        UINT viewportCount = 1;
        D3D11_VIEWPORT viewport{};
        context->RSGetViewports(&viewportCount, &viewport);

        Matrix view;
        Matrix projection;
        camera.GetMainMatrices(view, projection);

        DrawParams params{};
        params.View = view.Transpose();
        params.Projection = projection.Transpose();
        params.VolumeCenter = camera.GetPosition() + VolumeCenterOffset;
        params.Time = m_SimulationTime;
        params.VolumeExtent = VolumeExtent;
        // 距離1あたりのピクセルの大きさ = 2 * tan(画角/2) / 画面の高さ
        params.PixelWorldScale =
            2.0f / ((std::max)(projection._22, 0.001f) * (std::max)(viewport.Height, 1.0f));
        context->UpdateSubresource(m_DrawParamsBuffer.Get(), 0, nullptr, &params, 0, 0);

        // 頂点バッファもInput Layoutも使わず、頂点はSV_VertexIDから作ります。
        context->IASetInputLayout(nullptr);
        ID3D11Buffer* nullVertexBuffer = nullptr;
        const UINT zero = 0;
        context->IASetVertexBuffers(0, 1, &nullVertexBuffer, &zero, &zero);
        context->IASetIndexBuffer(m_IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        context->VSSetShader(m_VertexShader.Get(), nullptr, 0);
        context->PSSetShader(m_PixelShader.Get(), nullptr, 0);
        ID3D11Buffer* drawParams = m_DrawParamsBuffer.Get();
        context->VSSetConstantBuffers(DrawParamsSlot, 1, &drawParams);
        ID3D11ShaderResourceView* particleSRV = m_Lists[m_CurrentList].SRV.Get();
        context->VSSetShaderResources(0, 1, &particleSRV);

        // 光を足すだけの加算合成なので、奥から順に並べ替えなくても正しく重なります。
        Renderer::SetBlendState(BS_ADDITIVE);
        Renderer::SetDepthEnable(false);

        // 描く個数（インデックス数）はUpdateの結果からGPUが書いた値を使います。
        context->DrawIndexedInstancedIndirect(m_IndirectArgsBuffer.Get(), DrawArgsOffset);

        Renderer::SetDepthEnable(true);
        Renderer::SetBlendState(BS_ALPHABLEND);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->VSSetShaderResources(0, 1, &nullSRV);
    }
}
