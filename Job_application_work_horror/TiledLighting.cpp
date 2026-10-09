// ============================================================================
// ファイルの役割: 画面をタイルに分け、タイルごとに影響する点光源だけを
//                 Compute Shaderで絞り込む「タイルベースライティング」を管理している。
// 主な技術: Compute Shader、StructuredBuffer(SRV/UAV)、グループ共有メモリ、
//           InterlockedAdd、タイルの視錐台と球の交差判定
// ============================================================================

#include "TiledLighting.h"

#include "Application.h"
#include "Camera.h"
#include "utility.h"

#include <algorithm>
#include <cstring>

using namespace DirectX::SimpleMath;

namespace
{
    // ピクセルシェーダーが参照するスロット。今あるテクスチャ(t0〜t6)と重ならない番号にしている。
    constexpr UINT PointLightSlot = 10;
    constexpr UINT TileIndexSlot = 11;
    constexpr UINT TileCountSlot = 12;
    constexpr UINT ShadingParamsSlot = 6;

    // StructuredBuffer（構造体の配列のバッファ）を作っている
    bool CreateStructuredBuffer(
        UINT stride,
        UINT count,
        bool writableByGpu,
        ID3D11Buffer** buffer)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = stride * count;
        desc.StructureByteStride = stride;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        if (writableByGpu)
        {
            // Compute Shaderが書き込み(UAV)、ピクセルシェーダーが読み取る(SRV)バッファ。
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        }
        else
        {
            // 毎フレームCPUから書き換えるライトの一覧は、読み取り専用のSRVにしている。
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
        return SUCCEEDED(Renderer::GetDevice()->CreateBuffer(&desc, nullptr, buffer));
    }

    // バッファの読み取り口（SRV）を作っている
    bool CreateBufferSRV(ID3D11Buffer* buffer, UINT count,
        ID3D11ShaderResourceView** srv)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = count;
        return SUCCEEDED(
            Renderer::GetDevice()->CreateShaderResourceView(buffer, &desc, srv));
    }

    // バッファの書き込み口（UAV）を作っている
    bool CreateBufferUAV(ID3D11Buffer* buffer, UINT count,
        ID3D11UnorderedAccessView** uav)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = count;
        return SUCCEEDED(
            Renderer::GetDevice()->CreateUnorderedAccessView(buffer, &desc, uav));
    }

    // 定数バッファを作っている
    bool CreateConstantBuffer(UINT size, ID3D11Buffer** buffer)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = size;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(Renderer::GetDevice()->CreateBuffer(&desc, nullptr, buffer));
    }
}

namespace Effect
{
    void TiledLighting::Init()
    {
        // タイルの数は、描画先（全画面のバックバッファ）の大きさで確保している。
        const uint32_t width = (std::max)(Application::GetWidth(), 1u);
        const uint32_t height = (std::max)(Application::GetHeight(), 1u);
        m_MaxTiles = ((width + TileSize - 1) / TileSize) *
            ((height + TileSize - 1) / TileSize);
        const UINT indexCount = m_MaxTiles * MaxLightsPerTile;

        // シェーダーとバッファをまとめて作り、1つでも失敗したら理由を表示して終了している
        const bool created =
            m_CullingShader.Create("shader/tiledLightCullingCS.hlsl") &&
            CreateStructuredBuffer(sizeof(ENVIRONMENT_POINT_LIGHT), MaxLights, false,
                m_LightBuffer.ReleaseAndGetAddressOf()) &&
            CreateBufferSRV(m_LightBuffer.Get(), MaxLights,
                m_LightSRV.ReleaseAndGetAddressOf()) &&
            CreateStructuredBuffer(sizeof(uint32_t), indexCount, true,
                m_TileIndexBuffer.ReleaseAndGetAddressOf()) &&
            CreateBufferSRV(m_TileIndexBuffer.Get(), indexCount,
                m_TileIndexSRV.ReleaseAndGetAddressOf()) &&
            CreateBufferUAV(m_TileIndexBuffer.Get(), indexCount,
                m_TileIndexUAV.ReleaseAndGetAddressOf()) &&
            CreateStructuredBuffer(sizeof(uint32_t), m_MaxTiles, true,
                m_TileCountBuffer.ReleaseAndGetAddressOf()) &&
            CreateBufferSRV(m_TileCountBuffer.Get(), m_MaxTiles,
                m_TileCountSRV.ReleaseAndGetAddressOf()) &&
            CreateBufferUAV(m_TileCountBuffer.Get(), m_MaxTiles,
                m_TileCountUAV.ReleaseAndGetAddressOf()) &&
            CreateConstantBuffer(sizeof(CullingParams),
                m_CullingParamsBuffer.ReleaseAndGetAddressOf()) &&
            CreateConstantBuffer(sizeof(ShadingParams),
                m_ShadingParamsBuffer.ReleaseAndGetAddressOf());
        if (!created)
        {
            utility::ReportFatalError("タイルベースライティングの初期化に失敗しました。");
        }

        SetLights({});
    }

    // ピクセルシェーダーから外してから、全部のバッファを解放している
    void TiledLighting::Uninit()
    {
        ID3D11ShaderResourceView* nullViews[3] = {};
        Renderer::GetDeviceContext()->PSSetShaderResources(
            PointLightSlot, 3, nullViews);

        m_CullingShader.Uninit();
        m_ShadingParamsBuffer.Reset();
        m_CullingParamsBuffer.Reset();
        m_TileCountUAV.Reset();
        m_TileCountSRV.Reset();
        m_TileCountBuffer.Reset();
        m_TileIndexUAV.Reset();
        m_TileIndexSRV.Reset();
        m_TileIndexBuffer.Reset();
        m_LightSRV.Reset();
        m_LightBuffer.Reset();
        m_Lights.clear();
        m_UploadedLightCount = 0;
    }

    // ライトの一覧を覚え、上限までをGPUのバッファへ書き込んでいる。最初は全ライトを使う設定にしている
    void TiledLighting::SetLights(const std::vector<ENVIRONMENT_POINT_LIGHT>& lights)
    {
        m_Lights = lights;
        m_UploadedLightCount = static_cast<uint32_t>(
            (std::min)(m_Lights.size(), static_cast<size_t>(MaxLights)));

        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(
            m_LightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            if (m_UploadedLightCount > 0)
            {
                std::memcpy(mapped.pData, m_Lights.data(),
                    sizeof(ENVIRONMENT_POINT_LIGHT) * m_UploadedLightCount);
            }
            context->Unmap(m_LightBuffer.Get(), 0);
        }
        BindAllLights();
    }

    // 全ライトを順に計算する設定にしている
    void TiledLighting::BindAllLights()
    {
        UpdateShadingParams(false);
        BindForShading();
    }

    // 本描画の直前に、プレイヤー視点でタイルごとのライトリストを作っている
    void TiledLighting::BuildTiles(const Camera& camera)
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();

        // 本描画のビューポートに合わせてタイルを敷いている（左右の黒帯は含めない）。
        UINT viewportCount = 1;
        D3D11_VIEWPORT viewport{};
        context->RSGetViewports(&viewportCount, &viewport);
        const float viewportWidth = (std::max)(viewport.Width, 1.0f);
        const float viewportHeight = (std::max)(viewport.Height, 1.0f);
        m_ViewportOffset = Vector2(viewport.TopLeftX, viewport.TopLeftY);
        m_TilesX = (static_cast<uint32_t>(viewportWidth) + TileSize - 1) / TileSize;
        m_TilesY = (static_cast<uint32_t>(viewportHeight) + TileSize - 1) / TileSize;
        while (m_TilesX * m_TilesY > m_MaxTiles && m_TilesY > 1)
        {
            --m_TilesY;
        }

        // プレイヤー視点の行列の逆行列を渡し、Compute Shaderがタイルの四隅の向きを求められるようにしている
        Matrix view;
        Matrix projection;
        camera.GetMainMatrices(view, projection);

        CullingParams params{};
        params.InverseViewProjection = (view * projection).Invert().Transpose();
        const Vector3 eye = camera.GetPosition();
        params.CameraPosition = Vector4(eye.x, eye.y, eye.z, 1.0f);
        params.LightCount = m_UploadedLightCount;
        params.TilesX = m_TilesX;
        params.TilesY = m_TilesY;
        params.ViewportSize = Vector2(viewportWidth, viewportHeight);
        params.ViewportOffset = m_ViewportOffset;
        context->UpdateSubresource(m_CullingParamsBuffer.Get(), 0, nullptr, &params, 0, 0);

        // 同じバッファをSRVとUAVに同時に設定できないため、先に描画側から外している。
        ID3D11ShaderResourceView* nullViews[3] = {};
        context->PSSetShaderResources(PointLightSlot, 3, nullViews);

        m_CullingShader.SetGPU();
        ID3D11Buffer* cullingParams = m_CullingParamsBuffer.Get();
        context->CSSetConstantBuffers(0, 1, &cullingParams);
        ID3D11ShaderResourceView* lightSRV = m_LightSRV.Get();
        context->CSSetShaderResources(0, 1, &lightSRV);
        ID3D11UnorderedAccessView* uavs[2] = {
            m_TileIndexUAV.Get(), m_TileCountUAV.Get() };
        context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

        // 1タイル = 1スレッドグループ。グループ内の64スレッドで、ライトを分担して判定している。
        context->Dispatch(m_TilesX, m_TilesY, 1);

        // Compute Shaderからバッファを外し、ピクセルシェーダーがタイルのリストを使う設定にしている
        ID3D11UnorderedAccessView* nullUAVs[2] = {};
        context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->CSSetShaderResources(0, 1, &nullSRV);
        context->CSSetShader(nullptr, nullptr, 0);

        UpdateShadingParams(true);
        BindForShading();
    }

    // ピクセルシェーダーへ渡す値（ライトの数・タイルのリストを使うか・タイルの数・描く範囲）を更新している
    void TiledLighting::UpdateShadingParams(bool tiled)
    {
        ShadingParams params{};
        params.LightCount = m_UploadedLightCount;
        params.TiledMode = tiled ? 1u : 0u;
        params.TilesX = m_TilesX;
        params.TilesY = m_TilesY;
        params.ViewportOffset = m_ViewportOffset;
        Renderer::GetDeviceContext()->UpdateSubresource(
            m_ShadingParamsBuffer.Get(), 0, nullptr, &params, 0, 0);
    }

    // b6に定数バッファを、t10〜t12にライトの一覧・タイルのリスト・タイルごとの数を設定している
    void TiledLighting::BindForShading()
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11Buffer* shadingParams = m_ShadingParamsBuffer.Get();
        context->PSSetConstantBuffers(ShadingParamsSlot, 1, &shadingParams);
        ID3D11ShaderResourceView* views[3] = {
            m_LightSRV.Get(), m_TileIndexSRV.Get(), m_TileCountSRV.Get() };
        context->PSSetShaderResources(PointLightSlot, 3, views);
    }
}
