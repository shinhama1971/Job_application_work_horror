// ============================================================================
// ファイルの役割: ライト、マテリアル、深度、行列など描画状態を管理します。
// ============================================================================

#include "Renderer.h"
#include "Application.h"

using namespace DirectX::SimpleMath;

//--------------------------------------------------------------------------------------
void Renderer::SetLight(LIGHT Light)
{
	m_Light = Light;
	m_Light.Enable = m_LightEnable;
	m_pDeviceContext->UpdateSubresource(
		m_pLightBuffer.Get(), 0, NULL, &m_Light, 0, 0);

}

void Renderer::SetEnvironmentLights(const ENVIRONMENT_LIGHTS& lights)
{
	m_EnvironmentLights = lights;
	m_pDeviceContext->UpdateSubresource(
		m_pEnvironmentLightBuffer.Get(),
		0, NULL, &m_EnvironmentLights, 0, 0);
}

void Renderer::SetDebugViewMode(int mode, float wallDampStrength)
{
	DEBUG_VIEW_BUFFER buffer{};
	buffer.Mode = mode;
	buffer.WallDampStrength = wallDampStrength;
	m_pDeviceContext->UpdateSubresource(
		m_pDebugViewBuffer.Get(), 0, NULL, &buffer, 0, 0);
	m_pDeviceContext->PSSetConstantBuffers(
		7, 1, m_pDebugViewBuffer.GetAddressOf());
}

//--------------------------------------------------------------------------------------
// 
//--------------------------------------------------------------------------------------
void Renderer::SetLightEnable(bool Enable)
{
	m_LightEnable = Enable;
	m_Light.Enable = Enable;
	m_pDeviceContext->UpdateSubresource(
		m_pLightBuffer.Get(), 0, NULL, &m_Light, 0, 0);
}

//--------------------------------------------------------------------------------------
// ライトの有効状態を取得
//--------------------------------------------------------------------------------------
bool Renderer::GetLightEnable()
{
	return m_LightEnable;
}

//--------------------------------------------------------------------------------------
//マテリアルを設定
//--------------------------------------------------------------------------------------
void Renderer::SetMaterial(MATERIAL Material)
{
	m_pDeviceContext->UpdateSubresource(
		m_pMaterialBuffer.Get(), 0, NULL, &Material, 0, 0);
}

//--------------------------------------------------------------------------------------
//UV情報を設定
//--------------------------------------------------------------------------------------
void Renderer::SetUV(float u, float v, float uw, float vh)
{
	//UVの行列作成
	Matrix mat = Matrix::CreateScale(uw, vh, 1.0f);
	mat *= Matrix::CreateTranslation(u, v, 0.0f).Transpose();
	m_pDeviceContext->UpdateSubresource(
		m_pTextureBuffer.Get(), 0, NULL, &mat, 0, 0);
}

//--------------------------------------------------------------------------------------
// 深度ステンシルの有効・無効を設定
//--------------------------------------------------------------------------------------
void Renderer::SetDepthEnable(bool Enable)
{
	if (Enable) 
	{
		// 深度テストを有効にするステンシルステートをセット
		m_pDeviceContext->OMSetDepthStencilState(
			m_pDepthStateEnable.Get(), NULL);
	}
	else
	{
		// 深度テストを無効にするステンシルステートをセット
		m_pDeviceContext->OMSetDepthStencilState(
			m_pDepthStateDisable.Get(), NULL);
	}
}

//--------------------------------------------------------------------------------------
// アルファテストとカバレッジ（ATC）の有効・無効を設定
//--------------------------------------------------------------------------------------
void Renderer::SetATCEnable(bool Enable)
{
	// ブレンドファクター（透明度などの調整に使用）
	float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	if (Enable)
	{
		// アルファテストとカバレッジ (ATC) を有効にするブレンドステートをセット
		m_pDeviceContext->OMSetBlendState(
			m_pBlendStateATC.Get(), blendFactor, 0xffffffff);
	}
	else 
	{
		// 通常のブレンドステートをセット
		m_pDeviceContext->OMSetBlendState(
			m_pBlendState[0].Get(), blendFactor, 0xffffffff);
	}
}

//--------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------
void Renderer::SetWorldViewProjection2D()
{
	Matrix world = Matrix::Identity;			// 単位行列にする
	world = world.Transpose();			// 転置
	m_pDeviceContext->UpdateSubresource(
		m_pWorldBuffer.Get(), 0, NULL, &world, 0, 0);

	Matrix view = Matrix::Identity;			// 単位行列にする
	view = view.Transpose();			// 転置
	m_pDeviceContext->UpdateSubresource(
		m_pViewBuffer.Get(), 0, NULL, &view, 0, 0);

	// 2D描画を左上原点にする
	Matrix projection = DirectX::XMMatrixOrthographicOffCenterLH(
		0.0f,
		static_cast<float>(Application::GetWidth()),	// ビューボリュームの最小Ｘ
		static_cast<float>(Application::GetHeight()),	// ビューボリュームの最小Ｙ
		0.0f,											// ビューボリュームの最大Ｙ
		0.0f,
		1.0f);

	projection = projection.Transpose();

	m_pDeviceContext->UpdateSubresource(
		m_pProjectionBuffer.Get(), 0, NULL, &projection, 0, 0);
}

//--------------------------------------------------------------------------------------
// ワールド行列を設定
//--------------------------------------------------------------------------------------
void Renderer::SetWorldMatrix(Matrix* WorldMatrix)
{
	Matrix world;
	world = WorldMatrix->Transpose(); // 転置

	// ワールド行列をGPU側へ送る
	m_pDeviceContext->UpdateSubresource(
		m_pWorldBuffer.Get(), 0, NULL, &world, 0, 0);
}

//--------------------------------------------------------------------------------------
// ビュー行列を設定
//--------------------------------------------------------------------------------------
void Renderer::SetViewMatrix(Matrix* ViewMatrix)
{
	Matrix view;
	view = ViewMatrix->Transpose(); // 転置

	// ビュー行列をGPU側へ送る
	m_pDeviceContext->UpdateSubresource(
		m_pViewBuffer.Get(), 0, NULL, &view, 0, 0);
}

//--------------------------------------------------------------------------------------
// プロジェクション行列を設定
//--------------------------------------------------------------------------------------
void Renderer::SetProjectionMatrix(Matrix* ProjectionMatrix)
{
	Matrix projection;
	projection = ProjectionMatrix->Transpose(); // 転置

	// プロジェクション行列をGPU側へ送る
	m_pDeviceContext->UpdateSubresource(
		m_pProjectionBuffer.Get(), 0, NULL, &projection, 0, 0);
}

//--------------------------------------------------------------------------------------
// ウィンドウをリサイズして画面の縦横比を維持する
//--------------------------------------------------------------------------------------
