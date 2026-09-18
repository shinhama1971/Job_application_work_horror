// ============================================================================
// ファイルの役割: 画像ファイルからDirect3D 11テクスチャを生成・保持します。
// 主な技術: Shader Resource View、画像デコード、COM、GPUリソース管理
// 読み方: 公開関数は外部から使う操作、メンバー変数は保持する状態を表します。
// ============================================================================

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
