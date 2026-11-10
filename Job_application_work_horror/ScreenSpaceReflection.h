// ============================================================================
// ファイルの役割: 水面の反射を比べるための「SSR（スクリーンスペース反射）」に使う、画面の深度と画像を用意している。
// 主な技術: 深度プリパスの深度の写し取り（描画中は深度バッファを読めないため）、前のフレームの画面の再利用
//
// ・反射の光線を画面の深度に沿ってたどり、ぶつかった所の色を画面の画像から読む（計算は wetFloorPS）。
// ・景色をもう一度描かないので、反射の描画は要らない。その代わり、画面に映っていない物（後ろや画面の外）は映らない。
// ・色は前のフレームの画面（画面効果を掛ける前）を使うので、速く動くと少し遅れて見える。
// ・深度は深度プリパスで描いた物（壁・床・扉）だけ。小物は映らない。
// ・深度を t8、前のフレームの画面を t4 に設定している。
// ・前のフレームの画面は、縮小画像（ミップ）を持つ自分のテクスチャへ写してから t4 に設定している。
//   水たまりのレイ反射は、映る物が水面から離れているほど小さくぼやけた段を読み、本物の水面のように遠くの映り込みをぼかしている。
// ============================================================================

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace Effect
{
    class ScreenSpaceReflection
    {
    public:
        void Init(int width, int height);
        void Uninit();
        // 深度プリパスの後に呼ぶ：今の深度を写し取って t8 に、前のフレームの画面 previousScene を t4 に設定している
        void Prepare(ID3D11ShaderResourceView* previousScene);
        // 本描画の後に呼ぶ：t4・t8 を外している（画面の取り込みで、前のフレームの画面が書き換わるため）
        void Unbind();

    private:
        // 前のフレームの画面を写し、縮小画像を作るテクスチャ（取り込んだ画面と同じ大きさ・形式。必要になったときに作る）
        void EnsureSceneMipTexture(ID3D11Texture2D* source);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthCopy;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DepthView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_SceneMips;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SceneMipView;
    };
}
