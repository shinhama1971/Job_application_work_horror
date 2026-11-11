// ============================================================================
// ファイルの役割: 水面の反射を比べるための「SSR（スクリーンスペース反射）」に使う、画面の深度と画像を用意している。
// 主な技術: 深度プリパスの深度の写し取り（描画中は深度バッファを読めないため）、前のフレームの画面の再利用
//
// ・反射の光線を画面の深度に沿ってたどり、ぶつかった所の色を画面の画像から読む（計算は wetFloorPS）。
// ・景色をもう一度描かないので、反射の描画は要らない。その代わり、画面に映っていない物（後ろや画面の外）は映らない。
// ・前のフレームの色を同じワールド位置へ対応づけ、前の深度と合わない当たりは使わない。
// ・深度は深度プリパスで描いた物（壁・床・扉）だけ。小物は映らない。
// ・深度を t8、前のフレームの画面を t4 に設定している。
// ・前のフレームの画面は、縮小画像（ミップ）を持つ自分のテクスチャへ写してから t4 に設定している。
//   水たまりのレイ反射は、映る物が水面から離れているほど小さくぼやけた段を読み、本物の水面のように遠くの映り込みをぼかしている。
// ============================================================================

#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>

namespace Effect
{
    class ScreenSpaceReflection
    {
    public:
        void Init(int width, int height);
        void Uninit();
        // 深度プリパスの後に呼ぶ。今の深度をt8、前の色と深度をt4・t17に設定する。
        void Prepare(ID3D11ShaderResourceView* previousScene);
        // 本描画の後に呼ぶ。色と深度を読み取る設定から外す。
        void Unbind();
        // 色を取り込めたフレームだけ、同じフレームの深度とカメラを次の描画に残す。
        void FinishFrame(const DirectX::SimpleMath::Matrix& view,
            const DirectX::SimpleMath::Matrix& projection, bool captured);
        void Invalidate() { m_HistoryValid = false; }

    private:
        // 前のフレームの画面を写し、縮小画像を作るテクスチャ（取り込んだ画面と同じ大きさ・形式。必要になったときに作る）
        void EnsureSceneMipTexture(ID3D11Texture2D* source);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthCopy;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DepthView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_PreviousDepthCopy;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_PreviousDepthView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_SceneMips;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SceneMipView;
        DirectX::SimpleMath::Matrix m_PreviousViewProjection;
        DirectX::SimpleMath::Vector4 m_PreviousDepth;
        bool m_HistoryValid = false;
    };
}
