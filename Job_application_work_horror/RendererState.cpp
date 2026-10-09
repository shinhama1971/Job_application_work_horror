// ============================================================================
// ファイルの役割: ライト・マテリアル・深度・行列など、描画の状態をシェーダーへ設定している。
// 主な技術: 定数バッファの更新、深度ステンシルの状態の切り替え、行列の転置
// ============================================================================

#include "Renderer.h"
#include "Application.h"

using namespace DirectX::SimpleMath;

//--------------------------------------------------------------------------------------
// 懐中電灯と環境光を定数バッファ（b3）へ送っている。Enableは SetLightEnable の状態を優先している
void Renderer::SetLight(LIGHT Light)
{
	m_Light = Light;
	m_Light.Enable = m_LightEnable;
	m_pDeviceContext->UpdateSubresource(
		m_pLightBuffer.Get(), 0, NULL, &m_Light, 0, 0);

}

// デバッグ表示の番号と、2面の壁の湿り気の強さを設定している
void Renderer::SetDebugViewMode(int mode, float wallDampStrength)
{
	m_DebugView.Mode = mode;
	m_DebugView.WallDampStrength = wallDampStrength;
	UploadDebugViewBuffer();
}

// 壁の古さを0〜1に収めて設定している
void Renderer::SetWallWeathering(float weathering)
{
	m_DebugView.WallWeathering = weathering < 0.0f ? 0.0f : (weathering > 1.0f ? 1.0f : weathering);
	UploadDebugViewBuffer();
}

// 部屋の角の暗がりに使う壁の形（最大32個）と、床・天井の高さ・強さを定数バッファ（b11）へ送っている
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

// デバッグ表示と壁の古さの定数バッファ（b7）を、覚えている中身で更新している
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
// ライトの計算をするかを切り替えている（2Dの描画など、照明を使わない描画で切っている）
//--------------------------------------------------------------------------------------
void Renderer::SetLightEnable(bool Enable)
{
	m_LightEnable = Enable;
	m_Light.Enable = Enable;
	m_pDeviceContext->UpdateSubresource(
		m_pLightBuffer.Get(), 0, NULL, &m_Light, 0, 0);
}

//--------------------------------------------------------------------------------------
// ライトの計算をするかを返している
//--------------------------------------------------------------------------------------
bool Renderer::GetLightEnable()
{
	return m_LightEnable;
}

//--------------------------------------------------------------------------------------
// マテリアルを定数バッファ（b4）へ送っている
//--------------------------------------------------------------------------------------
void Renderer::SetMaterial(MATERIAL Material)
{
	m_pDeviceContext->UpdateSubresource(
		m_pMaterialBuffer.Get(), 0, NULL, &Material, 0, 0);
}

//--------------------------------------------------------------------------------------
// UVの行列を設定している（テクスチャのどの範囲を使うか）
//--------------------------------------------------------------------------------------
void Renderer::SetUV(float u, float v, float uw, float vh)
{
	// UVを(uw, vh)倍に拡大し、(u, v)だけずらす行列を作っている
	Matrix mat = Matrix::CreateScale(uw, vh, 1.0f);
	mat *= Matrix::CreateTranslation(u, v, 0.0f).Transpose();
	m_pDeviceContext->UpdateSubresource(
		m_pTextureBuffer.Get(), 0, NULL, &mat, 0, 0);
}

//--------------------------------------------------------------------------------------
// 深度バッファへの書き込みをするかを設定している。
// falseでも深度テスト（手前の物に隠れる判定）は続け、書き込みだけを止めている。
// 半透明・加算合成の物が、後から描く奥の物を隠してしまわないようにするために使っている。
//--------------------------------------------------------------------------------------
void Renderer::SetDepthEnable(bool Enable)
{
	if (Enable) 
	{
		// 深度テストと書き込みの両方を行っている
		m_pDeviceContext->OMSetDepthStencilState(
			m_pDepthStateEnable.Get(), NULL);
	}
	else
	{
		// 深度テストは行い、書き込みだけを止めている
		m_pDeviceContext->OMSetDepthStencilState(
			m_pDepthStateDisable.Get(), NULL);
	}
}

//--------------------------------------------------------------------------------------
// 2D描画用の行列を、描画解像度の大きさの座標系（左上が原点、1単位＝1画素）で設定している
//--------------------------------------------------------------------------------------
void Renderer::SetWorldViewProjection2D()
{
	SetWorldViewProjection2D(
		static_cast<float>(Application::GetWidth()),
		static_cast<float>(Application::GetHeight()));
}

//--------------------------------------------------------------------------------------
// 2D描画用の行列を、指定した大きさの座標系（左上が原点）で設定している
//--------------------------------------------------------------------------------------
void Renderer::SetWorldViewProjection2D(float width, float height)
{
	Matrix world = Matrix::Identity;			// ワールド行列は単位行列にしている
	world = world.Transpose();			// シェーダーに合わせて転置している
	m_pDeviceContext->UpdateSubresource(
		m_pWorldBuffer.Get(), 0, NULL, &world, 0, 0);

	Matrix view = Matrix::Identity;			// ビュー行列も単位行列にしている
	view = view.Transpose();			// 転置している
	m_pDeviceContext->UpdateSubresource(
		m_pViewBuffer.Get(), 0, NULL, &view, 0, 0);

	// 正射影で、左上を(0, 0)、右下を(width, height)にしている
	Matrix projection = DirectX::XMMatrixOrthographicOffCenterLH(
		0.0f,
		width,											// 右端のX
		height,											// 下端のY
		0.0f,											// 上端のY
		0.0f,
		1.0f);

	projection = projection.Transpose();

	m_pDeviceContext->UpdateSubresource(
		m_pProjectionBuffer.Get(), 0, NULL, &projection, 0, 0);
}

//--------------------------------------------------------------------------------------
// ワールド行列を設定している
//--------------------------------------------------------------------------------------
void Renderer::SetWorldMatrix(Matrix* WorldMatrix)
{
	Matrix world;
	world = WorldMatrix->Transpose(); // シェーダーの mul(v, M) に合わせて転置している

	// ワールド行列をGPUへ送っている
	m_pDeviceContext->UpdateSubresource(
		m_pWorldBuffer.Get(), 0, NULL, &world, 0, 0);
}

//--------------------------------------------------------------------------------------
// ビュー行列を設定している
//--------------------------------------------------------------------------------------
void Renderer::SetViewMatrix(Matrix* ViewMatrix)
{
	Matrix view;
	view = ViewMatrix->Transpose(); // 転置している

	// ビュー行列をGPUへ送っている
	m_pDeviceContext->UpdateSubresource(
		m_pViewBuffer.Get(), 0, NULL, &view, 0, 0);
}

//--------------------------------------------------------------------------------------
// 射影行列を設定している
//--------------------------------------------------------------------------------------
void Renderer::SetProjectionMatrix(Matrix* ProjectionMatrix)
{
	Matrix projection;
	projection = ProjectionMatrix->Transpose(); // 転置している

	// 射影行列をGPUへ送っている
	m_pDeviceContext->UpdateSubresource(
		m_pProjectionBuffer.Get(), 0, NULL, &projection, 0, 0);
}

