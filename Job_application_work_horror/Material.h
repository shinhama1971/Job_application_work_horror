// ============================================================================
// ファイルの役割: シェーダーへ渡すマテリアル（色・光沢・テクスチャを使うか）の定数バッファと、その設定を管理している。
// 主な技術: 定数バッファ（b4）、拡散・環境・鏡面・自己発光の色と光沢の強さ、GPUへの設定
// ============================================================================

#pragma once
#include	<Windows.h>
#include	<directxmath.h>
#include	<d3d11.h>
#include	<wrl/client.h>
#include	"Renderer.h"
#include	"Shader.h"
#include	"Texture.h"

class Shader;
class Texture;

// マテリアル1つ分。値を定数バッファに入れ、描く直前に頂点シェーダーとピクセルシェーダーのb4へ設定している。
class Material {

	// 定数バッファに入れる形（MATERIALはRenderer.hで定義している）
	struct ConstantBufferMaterial {
		MATERIAL	Material;
	};

	// 今のマテリアルの値と、定数バッファ
	MATERIAL	m_Material{};
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pConstantBufferMaterial;

	// 一緒に設定するシェーダーとテクスチャ（どちらも持ち主は別で、ここでは参照するだけ）
	Shader* m_pShader;
	Texture* m_pTexture;	

public:
	// 空のマテリアル（後でCreateを呼ぶ）
	Material() :
		m_pShader(nullptr),
		m_pTexture(nullptr)
	{}

	// 値を指定して、すぐに定数バッファを作っている
	Material(MATERIAL mtrl) :
		m_pShader(nullptr),
		m_pTexture(nullptr)
	{
		Create(mtrl);
	}
	// 定数バッファはComPtrなので、ここで解放する必要はない
	~Material() {
		Uninit();
	}

	// 定数バッファを作り、値を入れてGPUへ送っている
	bool Create(MATERIAL mtrl) {

		ID3D11Device* dev;
		dev = Renderer::GetDevice();

		// 定数バッファを作っている
		bool sts = Renderer::CreateConstantBuffer(
			sizeof(ConstantBufferMaterial),		// 大きさ
			m_pConstantBufferMaterial.ReleaseAndGetAddressOf());		// 作ったバッファを受け取る場所
		if (!sts) {
			MessageBox(NULL, "CreateBuffer(constant buffer Material) error", "Error", MB_OK);
			return false;
		}

		m_Material.Ambient = mtrl.Ambient;
		m_Material.Diffuse = mtrl.Diffuse;
		m_Material.Specular = mtrl.Specular;
		m_Material.Emission = mtrl.Emission;
		m_Material.Shininess = mtrl.Shininess;
		m_Material.TextureEnable = mtrl.TextureEnable;

		Update();

		return true;
	}

	// 今の値を定数バッファへ送り、b4に設定している
	void Update() {
		ConstantBufferMaterial		cb{};

		cb.Material = m_Material;

		ID3D11DeviceContext* devcontext;
		devcontext = Renderer::GetDeviceContext();

		devcontext->UpdateSubresource(
			m_pConstantBufferMaterial.Get(),
			0,
			nullptr,
			&cb,
			0, 0);

		// 定数バッファをb4レジスタへ設定している（頂点シェーダー用）
		devcontext->VSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

		// 定数バッファをb4レジスタへ設定している（ピクセルシェーダー用）
		devcontext->PSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

	}

	// 描く直前に呼び、シェーダー・テクスチャ（あれば）・定数バッファを設定している
	void SetGPU() {
		if (m_pShader)
		{
			m_pShader->SetGPU();
		}

		if (m_pTexture)
		{
			m_pTexture->SetGPU();
		}


		ID3D11DeviceContext* devcontext;
		devcontext = Renderer::GetDeviceContext();

		// 定数バッファをb4レジスタへ設定している（頂点シェーダー用）
		devcontext->VSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

		// 定数バッファをb4レジスタへ設定している（ピクセルシェーダー用）
		devcontext->PSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());
	}

	// 一時的な値を定数バッファへ送って設定している（m_Materialは変えない。明るさが毎フレーム変わる物で使っている）
	void SetMaterial(const MATERIAL& mtrl) {
		ConstantBufferMaterial		cb{};

		cb.Material = mtrl;

		ID3D11DeviceContext* devcontext;
		devcontext = Renderer::GetDeviceContext();

		devcontext->UpdateSubresource(
			m_pConstantBufferMaterial.Get(),
			0,
			nullptr,
			&cb,
			0, 0);

		// 定数バッファをb4レジスタへ設定している（頂点シェーダー用）
		devcontext->VSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

		// 定数バッファをb4レジスタへ設定している（ピクセルシェーダー用）
		devcontext->PSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

	}

	// 解放するものはない
	void Uninit() {
	}

	// 各値を書き換えている（GPUへ送るのは次のUpdateを呼んだとき）
	void SetDiffuse(DirectX::XMFLOAT4 diffuse) {
		m_Material.Diffuse = diffuse;
	}

	void SetAmbient(DirectX::XMFLOAT4 ambient) {
		m_Material.Ambient = ambient;
	}

	void SetSpecular(DirectX::XMFLOAT4 specular) {
		m_Material.Specular = specular;
	}

	void SetEmission(DirectX::XMFLOAT4 emission) {
		m_Material.Emission = emission;
	}

	void SetShininess(float shininess) {
		m_Material.Shininess = shininess;
	}

	// 一緒に設定するシェーダーとテクスチャを決めている
	void SetShader(Shader* shader) {
		m_pShader = shader;
	}

	void SetTexture(Texture* texture) {
		m_pTexture = texture;
	}

	// テクスチャを使う設定か
	bool isTextureEnable() {
		return m_Material.TextureEnable == TRUE;
	}
};