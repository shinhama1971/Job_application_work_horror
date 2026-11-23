// ============================================================================
// ファイルの役割: DirectX 11の画面を読み戻し、WICでPNGとして保存する。
// ============================================================================

#include "CaptureImage.h"
#include "CapturePixelFormat.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace Tools::CaptureImage
{
    using CapturePixelFormat::BytesPerPixel;

    // RGBA（上の行から並ぶ）の画像をPNGで保存している。
    bool SavePng(const std::wstring& path, const BYTE* rgba, UINT rowPitch, UINT width, UINT height)
    {
        // WICのファクトリーを作り、ファイルへ書き出すPNGのエンコーダーと1枚分のフレームを準備している
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))))
        {
            return false;
        }
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
            FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
            FAILED(frame->Initialize(nullptr)) ||
            FAILED(frame->SetSize(width, height)))
        {
            return false;
        }
        // PNGの書き出しはBGRAの並びを使っている（RGBAのまま渡すと赤と青が入れ替わるため、並べ替えてから渡している）。
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
        {
            return false;
        }
        std::vector<BYTE> bgra(static_cast<size_t>(width) * height * BytesPerPixel);
        for (UINT y = 0; y < height; ++y)
        {
            const BYTE* source = rgba + static_cast<size_t>(y) * rowPitch;
            BYTE* destination = bgra.data() + static_cast<size_t>(y) * width * BytesPerPixel;
            CapturePixelFormat::CopyRgbaToOpaqueBgra(source, destination, width);
        }
        if (FAILED(frame->WritePixels(height, width * BytesPerPixel, static_cast<UINT>(bgra.size()), bgra.data())) ||
            FAILED(frame->Commit()) ||
            FAILED(encoder->Commit()))
        {
            return false;
        }
        return true;
    }

    bool SaveBackBufferPng(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer, const std::wstring& path)
    {
        bool saved = false;
        // CPUから読めるステージングテクスチャを作り、バックバッファをコピーして読み出している
        D3D11_TEXTURE2D_DESC description{};
        backBuffer->GetDesc(&description);
        D3D11_TEXTURE2D_DESC stagingDescription = description;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDescription.MiscFlags = 0;
        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        ComPtr<ID3D11Texture2D> staging;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(device->CreateTexture2D(&stagingDescription, nullptr, &staging)))
        {
            context->CopyResource(staging.Get(), backBuffer);
            if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                saved = SavePng(path,
                    static_cast<const BYTE*>(mapped.pData), mapped.RowPitch,
                    description.Width, description.Height);
                context->Unmap(staging.Get(), 0);
            }
        }
        return saved;
    }
}
