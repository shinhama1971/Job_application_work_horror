#include "ComputeShader.h"

#include "Renderer.h"

bool ComputeShader::Create(const char* fileName)
{
    std::vector<unsigned char> shaderObject;

    const HRESULT compileResult = Renderer::CompileShader(
        fileName, "main", "cs_5_0", shaderObject);
    if (FAILED(compileResult))
    {
        return false;
    }

    const HRESULT createResult = Renderer::GetDevice()->CreateComputeShader(
        shaderObject.data(),
        shaderObject.size(),
        nullptr,
        m_Shader.ReleaseAndGetAddressOf());

    return SUCCEEDED(createResult);
}

void ComputeShader::SetGPU() const
{
    Renderer::GetDeviceContext()->CSSetShader(m_Shader.Get(), nullptr, 0);
}

void ComputeShader::Uninit()
{
    m_Shader.Reset();
}
