// ============================================================================
// ファイルの役割: HLSLの読み込み、頂点の入力レイアウト、GPUへのシェーダーの設定を管理している。
// 主な技術: D3DCompileFromFile（またはコンパイル済みの.cso）、頂点・ピクセルシェーダー、入力レイアウト、エラーの表示
// ============================================================================

#pragma once
#include	<wrl/client.h>
#include	<string>
#include	<d3d11.h>

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
// Shaderクラス：頂点シェーダーとピクセルシェーダーの組。同じ組み合わせは一度だけ作って使い回している
//-----------------------------------------------------------------------------
class Shader{
public:
	Shader() {}
	~Shader() {}
	// 頂点シェーダーとピクセルシェーダーのファイルから作っている（作成済みの組み合わせならそれを使う）
	void Create(std::string vs, std::string ps);
	// このシェーダーを次の描画で使うよう設定している
	void SetGPU();
	// 使い回し用に覚えているシェーダーを全部手放している（D3Dデバイスを壊す前に呼んでいる）
	static void ClearCache();
private:
	ComPtr<ID3D11VertexShader> m_pVertexShader;		// 頂点シェーダー
	ComPtr<ID3D11PixelShader>  m_pPixelShader;		// ピクセルシェーダー
	ComPtr<ID3D11InputLayout>  m_pVertexLayout;		// 頂点レイアウト
};

