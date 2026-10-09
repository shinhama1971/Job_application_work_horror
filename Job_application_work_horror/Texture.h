// ============================================================================
// ファイルの役割: 画像ファイル（またはメモリ上の画像）からDirect3D 11のテクスチャを作って持っている。
// 主な技術: Shader Resource View、stb_imageによる画像の読み込み、ComPtrによるGPU資源の管理
// ============================================================================

#pragma once

#include	<d3d11.h>
#include	<string>
#include	<wrl/client.h> // ComPtrの定義を含むヘッダー
#include	<filesystem>

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
// Textureクラス：画像1枚分のテクスチャと、シェーダーから読むためのSRV
//-----------------------------------------------------------------------------
class Texture
{
	std::string m_texname{}; // ファイル名（今は記録していない）
	ComPtr<ID3D11ShaderResourceView> m_srv{}; // シェーダーリソースビュー（シェーダーから読む口）

	int m_width=0; // 幅
	int m_height=0; // 高さ
	int m_bpp=0; // 元の画像の1画素あたりのチャンネル数
public:
	// 作る・壊す（SRVはComPtrなので自動で解放される）
	Texture();
	~Texture();
	// 画像ファイルを読み込んでいる／メモリ上の画像データ（モデルに埋め込まれた画像など）を読み込んでいる
	bool Load(const std::string& filename);
	bool LoadFromMemory(const unsigned char* data,int len);

	// ピクセルシェーダーのt0に設定している
	void SetGPU();
};
