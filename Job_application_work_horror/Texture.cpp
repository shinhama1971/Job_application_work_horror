// ============================================================================
// ファイルの役割: 画像ファイル（またはメモリ上の画像）からDirect3D 11のテクスチャを作って持っている。
// 主な技術: Shader Resource View、stb_imageによる画像の読み込み、ComPtrによるGPU資源の管理
// ============================================================================

#include	<iostream>
#include	"Texture.h"
#include	"stb_image.h"
#include	"Renderer.h"

// コンストラクタ（何もしない）
Texture::Texture()
{
}

// デストラクタ
Texture::~Texture()
{
	m_srv.Reset();// SRVをはっきり解放している
}
// 画像ファイルを読み込み、テクスチャを作っている（読めなければfalse）
bool Texture::Load(const std::string& filename)
{
	unsigned char* pixels=nullptr;

	// 画像を読み込んでいる（どの形式でもRGBAの4チャンネルにそろえている）
	pixels = stbi_load(filename.c_str(), &m_width, &m_height, &m_bpp, 4);
	if (pixels == nullptr) {
		std::cout << filename.c_str() << " Load error " << std::endl;
		return false;
	}

	// テクスチャ（2D）の資源を作っている。ミップマップは作っていない
	ComPtr<ID3D11Texture2D> pTexture;

	D3D11_TEXTURE2D_DESC desc{};

	desc.Width = m_width;
	desc.Height = m_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;			// RGBA
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA subResource{};
	subResource.pSysMem = pixels;
	subResource.SysMemPitch = desc.Width * 4;			// RGBAは1画素4バイト
	subResource.SysMemSlicePitch = 0;

	ID3D11Device* device = Renderer::GetDevice();

	HRESULT hr = device->CreateTexture2D(&desc, &subResource, pTexture.GetAddressOf());
	if (FAILED(hr)) {
		stbi_image_free(pixels);
		return false;
	}

	// シェーダーから読むためのSRVを作っている
	hr = device->CreateShaderResourceView(pTexture.Get(), nullptr, m_srv.ReleaseAndGetAddressOf());
	if (FAILED(hr)) {
		stbi_image_free(pixels);
		return false;
	}

	// 読み込んだ画素のメモリを解放している
	stbi_image_free(pixels);

	return true;
}

// メモリ上の画像データを読み込み、テクスチャを作っている（読めなかった場合の確認はしていない）
bool Texture::LoadFromMemory(const unsigned char* Data,int len) {

	unsigned char* pixels=nullptr;

	// 画像を読み込んでいる（RGBAの4チャンネルにそろえている）
	pixels = stbi_load_from_memory(Data, 
		len, 
		&m_width, 
		&m_height, 
		&m_bpp, 
		STBI_rgb_alpha);

	// テクスチャ（2D）の資源を作っている
	ComPtr<ID3D11Texture2D> pTexture;

	D3D11_TEXTURE2D_DESC desc{};

	desc.Width = m_width;
	desc.Height = m_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;			// RGBA
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA subResource{};
	subResource.pSysMem = pixels;
	subResource.SysMemPitch = desc.Width * 4;			// RGBAは1画素4バイト
	subResource.SysMemSlicePitch = 0;

	ID3D11Device* device = Renderer::GetDevice();

	HRESULT hr = device->CreateTexture2D(&desc, &subResource, pTexture.GetAddressOf());
	if (FAILED(hr)) {
		stbi_image_free(pixels);
		return false;
	}

	// シェーダーから読むためのSRVを作っている
	hr = device->CreateShaderResourceView(pTexture.Get(), nullptr, m_srv.ReleaseAndGetAddressOf());
	if (FAILED(hr)) {
		stbi_image_free(pixels);
		return false;
	}

	// 読み込んだ画素のメモリを解放している
	stbi_image_free(pixels);

	return true;
}

// テクスチャをピクセルシェーダーのt0に設定している
void Texture::SetGPU()
{
	ID3D11DeviceContext* devicecontext = Renderer::GetDeviceContext();
	devicecontext->PSSetShaderResources(0, 1, m_srv.GetAddressOf());
}
