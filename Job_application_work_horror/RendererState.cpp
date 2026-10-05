// ============================================================================
// ファイルの役割: ライト、マテリアル、深度、行列など描画状態を管理します。
// 主な技術: OM/RS/IAステート管理、状態キャッシュ、パイプライン復元
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

void Renderer::SetDebugViewMode(int mode, float wallDampStrength)
{
	m_DebugView.Mode = mode;
	m_DebugView.WallDampStrength = wallDampStrength;
	UploadDebugViewBuffer();
}

void Renderer::SetWallWeathering(float weathering)
{
	m_DebugView.WallWeathering = weathering < 0.0f ? 0.0f : (weathering > 1.0f ? 1.0f : weathering);
	UploadDebugViewBuffer();
}

void Renderer::SetRoomOcclusion(
	const Vector4* boxes, unsigned int boxCount,
	float floorY, float ceilingY, float strength)
{
	if (!m_pRoomOcclusionBuffer)
	{
		return;
	}
	ROOM_OCCLUSION_BUFFER buffer{};
	buffer.BoxCount = boxes == nullptr ? 0u
		: (boxCount < ROOM_OCCLUSION_BUFFER::MaxBoxes ? boxCount : ROOM_OCCLUSION_BUFFER::MaxBoxes);
	for (unsigned int i = 0; i < buffer.BoxCount; ++i)
	{
		buffer.Boxes[i] = boxes[i];
	}
	buffer.FloorY = floorY;
	buffer.CeilingY = ceilingY;
	buffer.Strength = strength;
	m_pDeviceContext->UpdateSubresource(
		m_pRoomOcclusionBuffer.Get(), 0, NULL, &buffer, 0, 0);
	m_pDeviceContext->PSSetConstantBuffers(
		11, 1, m_pRoomOcclusionBuffer.GetAddressOf());
}

void Renderer::UploadDebugViewBuffer()
{
	if (!m_pDebugViewBuffer)
	{
		return;
	}
	m_pDeviceContext->UpdateSubresource(
		m_pDebugViewBuffer.Get(), 0, NULL, &m_DebugView, 0, 0);
	m_pDeviceContext->PSSetConstantBuffers(
		7, 1, m_pDebugViewBuffer.GetAddressOf());
}

//--------------------------------------------------------------------------------------
// ライトの有効・無効を切り替えます（2D描画など、照明を使わない描画で無効にします）
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
// 深度バッファへの書き込みの有効・無効を設定します。
// falseでも深度テスト（手前の物に隠れる判定）は続け、書き込みだけを止めます。
// 半透明・加算合成の物が、後から描く奥の物を隠してしまわないようにするために使います。
//--------------------------------------------------------------------------------------
void Renderer::SetDepthEnable(bool Enable)
{
	if (Enable) 
	{
		// 深度テストと書き込みの両方を行う
		m_pDeviceContext->OMSetDepthStencilState(
			m_pDepthStateEnable.Get(), NULL);
	}
	else
	{
		// 深度テストは行い、書き込みだけを止める
		m_pDeviceContext->OMSetDepthStencilState(
			m_pDepthStateDisable.Get(), NULL);
	}
}

//--------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------
void Renderer::SetWorldViewProjection2D()
{
	SetWorldViewProjection2D(
		static_cast<float>(Application::GetWidth()),
		static_cast<float>(Application::GetHeight()));
}

//--------------------------------------------------------------------------------------
// 2D描画用の行列を、指定した大きさの座標系（左上原点）で設定します
//--------------------------------------------------------------------------------------
void Renderer::SetWorldViewProjection2D(float width, float height)
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
		width,											// ビューボリュームの最大Ｘ
		height,											// ビューボリュームの最小Ｙ
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
