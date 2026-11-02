// ============================================================================
// ファイルの役割: 焼いた距離場のファイル（"SDFV"）を読み、3Dテクスチャとサンプラーを作っている。
// 主な技術: 3Dテクスチャの初期データ（行と面の間隔）、float16 の形式、線形で端を伸ばすサンプラー
// ============================================================================

#include "FigureVolumes.h"

#include "Renderer.h"
#include "utility.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
    // ピクセルシェーダーの番号（shader/figureVolume.hlsli と同じ）
    constexpr UINT SelfSlot = 6;
    constexpr UINT FigureSlot = 13;
    constexpr UINT SamplerSlot = 2;

    // ファイルの先頭（tools/maya/build_figure_volumes.py の struct.pack と同じ並び）
    struct VolumeHeader
    {
        char Magic[4];
        std::uint32_t Version;
        std::uint32_t CountX;
        std::uint32_t CountY;
        std::uint32_t CountZ;
        std::uint32_t Channels;
        float Minimum[3];
        float Voxel;
    };
    static_assert(sizeof(VolumeHeader) == 40, "距離場のファイルの先頭は40バイト");
}

namespace Effect
{
    FigureVolumes::Volume FigureVolumes::Load(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        VolumeHeader header{};
        if (bytes.size() < sizeof(header))
        {
            utility::ReportFatalError("距離場のファイルを読めませんでした。\n" + path);
        }
        std::memcpy(&header, bytes.data(), sizeof(header));
        const std::size_t valueCount =
            static_cast<std::size_t>(header.CountX) * header.CountY * header.CountZ * header.Channels;
        if (std::memcmp(header.Magic, "SDFV", 4) != 0 || header.Version != 1 ||
            (header.Channels != 1 && header.Channels != 4) || bytes.size() != sizeof(header) + valueCount * 2)
        {
            utility::ReportFatalError("距離場のファイルの形が違います。\n" + path);
        }

        D3D11_TEXTURE3D_DESC description{};
        description.Width = header.CountX;
        description.Height = header.CountY;
        description.Depth = header.CountZ;
        description.MipLevels = 1;
        description.Format = header.Channels == 4 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R16_FLOAT;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        // 値は x が一番内側、次に y、最後に z の並び（1つの値は float16 の2バイト）
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = bytes.data() + sizeof(header);
        data.SysMemPitch = header.CountX * header.Channels * 2;
        data.SysMemSlicePitch = data.SysMemPitch * header.CountY;

        Volume volume;
        ID3D11Device* device = Renderer::GetDevice();
        if (FAILED(device->CreateTexture3D(&description, &data, volume.Texture.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(volume.Texture.Get(), nullptr, volume.View.GetAddressOf())))
        {
            utility::ReportFatalError("距離場の3Dテクスチャを作れませんでした。\n" + path);
        }
        return volume;
    }

    void FigureVolumes::Init()
    {
        m_Self = Load("assets/volume/self_body.sdfv");
        m_Figure = Load("assets/volume/figure.sdfv");

        // 線形に補間し、範囲の外は端の値を使う（折り返すと、反対側の端の距離が混ざるため）
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        Renderer::GetDevice()->CreateSamplerState(&sampler, m_Sampler.GetAddressOf());
    }

    void FigureVolumes::Bind() const
    {
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        ID3D11ShaderResourceView* self = m_Self.View.Get();
        ID3D11ShaderResourceView* figure = m_Figure.View.Get();
        context->PSSetShaderResources(SelfSlot, 1, &self);
        context->PSSetShaderResources(FigureSlot, 1, &figure);
        ID3D11SamplerState* sampler = m_Sampler.Get();
        context->PSSetSamplers(SamplerSlot, 1, &sampler);
    }
}
