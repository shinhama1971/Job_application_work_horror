// ============================================================================
// ファイルの役割: シェーダーへ渡すマテリアル（色・光沢・テクスチャを使うか）の定数バッファと、その設定を管理している。
// 主な技術: 定数バッファ（b4）、拡散・環境・鏡面・自己発光の色と光沢の強さ、GPUへの設定
// ============================================================================

#pragma once
#include "shader/renderingConfig.hlsli"
#include	<Windows.h>
#include	<directxmath.h>
#include	<d3d11.h>
#include	<wrl/client.h>
#include	"Renderer.h"
#include	"Shader.h"
#include	"Texture.h"
#include "Utility.h"

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
	// マテリアルの定数のシェーダーの番号（common.hlsl の b4。頂点と画素の両方が読む）
	static constexpr UINT ConstantBufferSlot = SIGNAL_LOST_MATERIAL_BUFFER_SLOT;

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
		CreateRequired(mtrl);
	}
	// 定数バッファを作り、値を入れてGPUへ送っている
	[[nodiscard]] bool Create(MATERIAL mtrl) {

		// 定数バッファを作っている
		bool sts = Renderer::CreateConstantBuffer(
			sizeof(ConstantBufferMaterial),		// 大きさ
			m_pConstantBufferMaterial.ReleaseAndGetAddressOf());		// 作ったバッファを受け取る場所
		if (!sts) {
			MessageBox(NULL, "CreateBuffer(constant buffer Material) error", "Error", MB_OK);
			return false;
		}

		m_Material = mtrl;

		Update();

		return true;
	}

	void CreateRequired(MATERIAL mtrl) {
		if (!Create(mtrl))
		{
			Utility::ReportFatalError("マテリアルの定数バッファを作成できません。");
		}
	}

	// 保持する値は Update、一時的な値は SetMaterial から送る。
	void Update() {
		SetMaterial(m_Material);
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

		// 頂点と画素の両方が同じマテリアルの b4 を読む。
		devcontext->VSSetConstantBuffers(ConstantBufferSlot, 1, m_pConstantBufferMaterial.GetAddressOf());

		devcontext->PSSetConstantBuffers(ConstantBufferSlot, 1, m_pConstantBufferMaterial.GetAddressOf());
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

		// 頂点と画素の両方が同じマテリアルの b4 を読む。
		devcontext->VSSetConstantBuffers(ConstantBufferSlot, 1, m_pConstantBufferMaterial.GetAddressOf());

		devcontext->PSSetConstantBuffers(ConstantBufferSlot, 1, m_pConstantBufferMaterial.GetAddressOf());

	}

	// 各値を書き換えている（GPUへ送るのは次のUpdateを呼んだとき）
	// PBRの金属らしさ（0〜1）と粗さ（0は指定なし）
	void SetSurfaceFinish(float metallic, float roughness) {
		m_Material.Metallic = metallic;
		m_Material.Roughness = roughness;
	}

	// 一緒に設定するシェーダーとテクスチャを決めている
	void SetShader(Shader* shader) {
		m_pShader = shader;
	}

	void SetTexture(Texture* texture) {
		m_pTexture = texture;
	}

};