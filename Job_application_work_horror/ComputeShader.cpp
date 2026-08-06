#include "ComputeShader.h"

#include "Renderer.h"

bool ComputeShader::Create(const char* fileName)
{
    void* shaderObject = nullptr;
    int shaderObjectSize = 0;

    const HRESULT compileResult = Renderer::CompileShader(
        fileName, "main", "cs_5_0", &shaderObject, &shaderObjectSize);
    if (FAILED(compileResult))
    {
        return false;
    }

    const HRESULT createResult = Renderer::GetDevice()->CreateComputeShader(
        shaderObject,
        static_cast<SIZE_T>(shaderObjectSize),
        nullptr,
        m_Shader.ReleaseAndGetAddressOf());

    delete[] static_cast<unsigned char*>(shaderObject);
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
