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

class Material {

	struct ConstantBufferMaterial {
		MATERIAL	Material;
	};

	MATERIAL	m_Material{};
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pConstantBufferMaterial;

	Shader* m_pShader;
	Texture* m_pTexture;	

public:
	Material() :
		m_pShader(nullptr),
		m_pTexture(nullptr)
	{}

	Material(MATERIAL mtrl) :
		m_pShader(nullptr),
		m_pTexture(nullptr)
	{
		Create(mtrl);
	}
	~Material() {
		Uninit();
	}

	bool Create(MATERIAL mtrl) {

		ID3D11Device* dev;
		dev = Renderer::GetDevice();

		// コンスタントバッファ作成
		bool sts = Renderer::CreateConstantBuffer(
			sizeof(ConstantBufferMaterial),		// サイズ
			m_pConstantBufferMaterial.GetAddressOf());		// コンスタントバッファ４
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

		// コンスタントバッファ4をｂ4レジスタへセット（頂点シェーダー用）
		devcontext->VSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

		// コンスタントバッファ4をｂ4レジスタへセット(ピクセルシェーダー用)
		devcontext->PSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

	}

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

		// コンスタントバッファ4をｂ4レジスタへセット（頂点シェーダー用）
		devcontext->VSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

		// コンスタントバッファ4をｂ4レジスタへセット(ピクセルシェーダー用)
		devcontext->PSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());
	}

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

		// コンスタントバッファ4をｂ4レジスタへセット（頂点シェーダー用）
		devcontext->VSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

		// コンスタントバッファ4をｂ4レジスタへセット(ピクセルシェーダー用)
		devcontext->PSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

	}

	void Uninit() {
	}

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

	void SetShader(Shader* shader) {
		m_pShader = shader;
	}

	void SetTexture(Texture* texture) {
		m_pTexture = texture;
	}

	bool isTextureEnable() {
		return m_Material.TextureEnable == TRUE;
	}
};