#include "LightShafts.h"
#include "Renderer.h"
#include "utility.h"
#include "shader/computeDispatchConfig.hlsli"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector4;

namespace
{
    // lightShaftsCS.hlsl の t0（深度）と t1（ライト一覧）。
    constexpr UINT ShaftInputCount = 2;
    // 全解像度に切り替える環境変数の値を、末尾の終端文字込みで読む。
    constexpr std::size_t FullResolutionModeBufferSize = 8;
    constexpr UINT MaxShaftLights = 256;
    constexpr UINT ShaftResultSlot = SIGNAL_LOST_LIGHT_SHAFT_TEXTURE_SLOT;
    constexpr UINT ThreadGroupWidth = SIGNAL_LOST_SHAFT_GROUP_SIZE;
    constexpr UINT ResolutionDivisor = 2;

    UINT DivideRoundUp(UINT value, UINT divisor)
    {
        return (value + divisor - 1) / divisor;
    }

    // ShaftParameters (b0) と同じ並び。人影の位置は半解像度でも本描画と共有する。
    struct ShaftParameters
    {
        Matrix InverseViewProjection;
        Vector4 EyeTime;
        Vector4 Viewport;
        UINT LightCount;
        float Padding[3];
        Vector4 UnseenFigure;
    };

    static_assert(sizeof(ShaftParameters) == 128);
    static_assert(offsetof(ShaftParameters, EyeTime) == 64);
    static_assert(offsetof(ShaftParameters, Viewport) == 80);
    static_assert(offsetof(ShaftParameters, LightCount) == 96);
    static_assert(offsetof(ShaftParameters, UnseenFigure) == 112);
}

namespace Effect
{
    void LightShafts::Init(int width, int height)
    {
        // 比較時は全解像度のピクセルシェーダーへ戻す。
        char mode[FullResolutionModeBufferSize]{};
        size_t length = 0;
        getenv_s(&length, mode, sizeof(mode), "SIGNAL_LOST_FULLRES_SHAFTS");
        m_Enabled = mode[0] != '1';

        auto* device = Renderer::GetDevice();
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = DivideRoundUp(static_cast<UINT>(width), ResolutionDivisor);
        texture.Height = DivideRoundUp(static_cast<UINT>(height), ResolutionDivisor);
        texture.MipLevels = 1;
        texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texture.SampleDesc.Count = 1;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        D3D11_BUFFER_DESC lights{};
        lights.ByteWidth = sizeof(ENVIRONMENT_POINT_LIGHT) * MaxShaftLights;
        lights.Usage = D3D11_USAGE_DEFAULT;
        lights.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        lights.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        lights.StructureByteStride = sizeof(ENVIRONMENT_POINT_LIGHT);

        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        view.Buffer.NumElements = MaxShaftLights;

        if (FAILED(device->CreateTexture2D(&texture, nullptr, m_Texture.ReleaseAndGetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(m_Texture.Get(), nullptr, m_Result.ReleaseAndGetAddressOf())) ||
            FAILED(device->CreateUnorderedAccessView(m_Texture.Get(), nullptr, m_Output.ReleaseAndGetAddressOf())) ||
            FAILED(device->CreateBuffer(&lights, nullptr, m_Lights.ReleaseAndGetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(m_Lights.Get(), &view, m_LightView.ReleaseAndGetAddressOf())) ||
            !Renderer::CreateConstantBuffer(sizeof(ShaftParameters), m_Parameters.ReleaseAndGetAddressOf()) ||
            !m_Shader.Create("shader/lightShaftsCS.hlsl"))
        {
            utility::ReportFatalError("光の筋の初期化に失敗しました");
        }
    }

    void LightShafts::Unbind()
    {
        ID3D11ShaderResourceView* empty = nullptr;
        Renderer::GetDeviceContext()->PSSetShaderResources(ShaftResultSlot, 1, &empty);
    }

    void LightShafts::ResetTime()
    {
        // 次のシーンの霧の揺らぎが、前のシーンの長さに依存しないようにする。
        m_Time = 0.0f;
    }

    bool LightShafts::Compute(const Matrix& view, const Matrix& projection,
        const D3D11_VIEWPORT& viewport, const std::vector<ENVIRONMENT_POINT_LIGHT>& source, float deltaTime)
    {
        Unbind();
        if (!m_Enabled)
        {
            return false;
        }

        auto* context = Renderer::GetDeviceContext();
        std::array<ENVIRONMENT_POINT_LIGHT, MaxShaftLights> lights{};
        const auto count = (std::min)(source.size(), lights.size());
        std::copy_n(source.begin(), count, lights.begin());
        context->UpdateSubresource(m_Lights.Get(), 0, nullptr, lights.data(), 0, 0);

        m_Time += deltaTime;
        const auto eye = view.Invert().Translation();
        const ShaftParameters parameters{
            (view * projection).Invert().Transpose(),
            Vector4(eye.x, eye.y, eye.z, m_Time),
            Vector4(viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY),
            static_cast<UINT>(count), {}, Renderer::GetUnseenFigure()
        };
        context->UpdateSubresource(m_Parameters.Get(), 0, nullptr, &parameters, 0, 0);

        // 深度をSRVで読む間だけDSVを外す。同じ深度を読み取りと書き込みへ同時に設定できない。
        auto* target = Renderer::GetBackBufferRTV();
        context->OMSetRenderTargets(1, &target, nullptr);
        auto* buffer = m_Parameters.Get();
        context->CSSetConstantBuffers(0, 1, &buffer);
        ID3D11ShaderResourceView* inputs[ShaftInputCount] = { Renderer::GetDepthShaderResourceView(), m_LightView.Get() };
        context->CSSetShaderResources(0, ShaftInputCount, inputs);
        auto* output = m_Output.Get();
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);

        const auto width = DivideRoundUp(static_cast<UINT>(viewport.Width), ResolutionDivisor);
        const auto height = DivideRoundUp(static_cast<UINT>(viewport.Height), ResolutionDivisor);
        m_Shader.SetGPU();
        context->Dispatch(DivideRoundUp(width, ThreadGroupWidth), DivideRoundUp(height, ThreadGroupWidth), 1);

        ID3D11UnorderedAccessView* noOutput = nullptr;
        ID3D11ShaderResourceView* noInputs[ShaftInputCount]{};
        context->CSSetUnorderedAccessViews(0, 1, &noOutput, nullptr);
        context->CSSetShaderResources(0, ShaftInputCount, noInputs);
        context->CSSetShader(nullptr, nullptr, 0);
        context->OMSetRenderTargets(1, &target, Renderer::GetDepthStencilView());
        auto* result = m_Result.Get();
        context->PSSetShaderResources(ShaftResultSlot, 1, &result);
        return true;
    }
}
