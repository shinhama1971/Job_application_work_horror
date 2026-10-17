// ============================================================================
// ファイルの役割: 水面の反射を比べるための「キューブマップ」。カメラの位置から6方向を撮った画像を、反射の向きで読んでいる。
// 主な技術: キューブマップ（6面のテクスチャ）への描画、1フレームに1面ずつ描き直す負荷の分散、右手系と左手系の向きの変換
//
// ・平面反射のように景色を毎回もう一度描かず、6面のうち1面だけを毎フレーム描き直すので軽い。
// ・その代わり、反射は「撮った位置から見た景色」なので、撮った位置から離れた所や近くの物では、映る位置がずれる。
//   （平面反射・SSRとの見比べ用。ゲームの普段の方式は平面反射）
// ・撮った画像は t7 に設定し、濡れた床のシェーダー（wetFloorPS）が反射の向きで読んでいる。
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

        void Init();
        void Uninit();
        // 6面がそろっているか（そろっていなければ、そろうまで1フレームに6面とも描いている）
        bool HasAllFaces() const { return m_FacesDrawn >= 6; }
        // 次の1面を描き始めている：描画先をその面にし、position から面の向きを見るカメラの行列に差し替えている
        void BeginFace(Camera& camera, const DirectX::SimpleMath::Vector3& position);
        // 1面を描き終えている：カメラと描画先を戻し、キューブマップを t7 に設定している
        void EndFace(Camera& camera);
        // 描き直さないフレームに、キューブマップを t7 に設定している
        void Bind();
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
    };
}
