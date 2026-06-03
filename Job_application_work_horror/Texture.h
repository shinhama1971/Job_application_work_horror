#pragma once

#include	<d3d11.h>
#include	<string>
#include	<wrl/client.h> // ComPtrの定義を含むヘッダファイル
#include	<filesystem>

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
//Textureクラス
//-----------------------------------------------------------------------------
class Texture
{
	std::string m_texname{}; // ファイル名
	ComPtr<ID3D11ShaderResourceView> m_srv{}; // シェーダーリソースビュー

	int m_width=0; // 幅
	int m_height=0; // 高さ
	int m_bpp=0; // BPP
public:
	Texture();
	~Texture();
	bool Load(const std::string& filename);
	bool LoadFromMemory(const unsigned char* data,int len);

	void SetGPU();
};
