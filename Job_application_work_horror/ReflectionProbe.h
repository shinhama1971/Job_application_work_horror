// ============================================================================
// ファイルの役割: 水面の反射を比べるための「キューブマップ」。カメラの位置から6方向を撮った画像を、反射の向きで読んでいる。
// 主な技術: キューブマップ（6面のテクスチャ）への描画、1フレームに1面ずつ描き直す負荷の分散、右手系と左手系の向きの変換
//
// ・景色をもう一度まるごと描かず、6面のうち1面だけを毎フレーム描き直すので軽い。
// ・その代わり、反射は「撮った位置から見た景色」なので、撮った位置から離れた所や近くの物では、映る位置がずれる。
//   （SSR・レイ反射との見比べ用。ゲームの普段の方式はレイ反射）
// ・撮った画像は t7 に設定し、濡れた床のシェーダー（wetFloorPS）が反射の向きで読んでいる。
// ・同じ仕組みを、PBRの金属や濡れた面の映り込み（環境マップ。t14）にも使っている（Game::Draw）。こちらは縮小画像（ミップ）を
//   作り、粗い面ほど小さくぼやけた段を読むことで、粗さに応じてぼやけた映り込みを近似している（pbrLighting.hlsli）。
// ============================================================================

#pragma once

#include <SimpleMath.h>
#include <array>
#include <d3d11.h>
#include <wrl/client.h>

class Camera;

namespace Effect
{
    class ReflectionProbe
    {
    public:
        // 1面の大きさ（画素）
        static constexpr int FaceSize = 128;

        // slot はキューブマップを設定するテクスチャの番号。withMipChain なら、描くたびに縮小画像（ミップ）も作っている
        void Init(UINT slot = 7, bool withMipChain = false);
        void Uninit();
        // 6面がそろっているか（そろっていなければ、そろうまで1フレームに6面とも描いている）
        bool HasAllFaces() const { return m_FacesDrawn >= 6; }
        // 次の1面を描き始めている：描画先をその面にし、position から面の向きを見るカメラの行列に差し替えている
        void BeginFace(Camera& camera, const DirectX::SimpleMath::Vector3& position);
        // 1面を描き終えている：カメラと描画先を戻し、キューブマップを 決めた番号 に設定している
        void EndFace(Camera& camera);
        // 描き直さないフレームに、キューブマップを 決めた番号 に設定している
        void Bind();
        // 決めた番号からキューブマップを外している（使わない場面で、前の場面の景色が映らないように）
        void Unbind();
        // 今描いている面のビュー行列（BeginFaceで決めたもの）。懐中電灯の向きを、その面の視点へ回すときに使っている
        const DirectX::SimpleMath::Matrix& GetFaceView() const { return m_FaceView; }
        // 何も描かれない所（遠くて描くのを省いた所）を塗る色。環境マップでは、闇に溶ける暗い色にしている
        void SetClearColor(const DirectX::SimpleMath::Color& color) { m_ClearColor = color; }
        // 次にこの方式を選んだとき、6面とも描き直させている
        void Invalidate() { m_FacesDrawn = 0; }

    private:
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 6> m_FaceViews;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_CubeView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthView;
        // 次に描く面の番号、これまでに描いた面の数
        int m_NextFace = 0;
        int m_FacesDrawn = 0;
        // キューブマップを設定するテクスチャの番号と、縮小画像を作るか
        UINT m_Slot = 7;
        bool m_HasMipChain = false;
        DirectX::SimpleMath::Matrix m_FaceView;
        DirectX::SimpleMath::Color m_ClearColor = DirectX::SimpleMath::Color(0.1f, 0.1f, 0.13f, 1.0f);
    };
}
