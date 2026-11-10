// ============================================================================
// ファイルの役割: 外の森に降る雨（雨粒の筋と、地面で跳ねるしぶき）を GPU で描いている。
// 主な技術: 頂点バッファを使わない描画（SV_VertexID・SV_InstanceID から雨粒の位置を作る）、
//           カメラを囲む箱の中での折り返し（雨粒はワールドに対して動き、カメラと一緒には動かない）、
//           落ちる向きを軸にしてカメラへ向けた細い四角形（筋）、1画素より細い筋を広げて薄くするちらつき対策、加算合成
//
// ・雨粒の位置は、番号から作った乱数の位置を「落ちる速さ × 時間」だけずらすだけで決まるので、CPU からは毎フレーム
//   時間とカメラの位置を送るだけでよい（バッファも Compute Shader も使わない）。
// ・雨粒は自分では光らず、懐中電灯（影つき）・街灯・窓の明かりを受けたときだけ光る（shader/rainPS.hlsl）。
//   夜の雨は、ライトの光の中だけで白く浮かぶ。
// ・研究所の屋根や玄関のひさしの下には降らない（当たり判定があり、地面から浮いている大きな板を屋根とみなしている）。
// ・水たまりに落ちる雨は、Ground の波のシミュレーション（WaterRippleSimulation）へ「おもり」として渡し、
//   水面に輪を広げている（Ground::SetRainIntensity）。
// ・深度の判定はするが書き込まないので、木や建物の奥の雨は隠れ、雨どうしは重なって足される。
// ============================================================================

#pragma once

#include "Object.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>

class ForestRain final : public Object
{
public:
    // 雨粒の数と、地面のしぶきの数
    static constexpr std::uint32_t DropCount = 24000;
    static constexpr std::uint32_t SplashCount = 900;

    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    void Uninit() override;

    // 監視カメラ・水面の反射などの別の視点には描かない
    bool DrawsInAuxiliaryView() const override { return false; }

    // 雨の強さ（0 = 降らない、1 = 強い雨）。弱い雨ほど、雨粒としぶきの数が減り、少しゆっくり落ちて筋が短くなる
    void SetIntensity(float intensity) { m_Intensity = (std::clamp)(intensity, 0.0f, 1.0f); }
    float GetIntensity() const { return m_Intensity; }
    // 地面の高さ（これより下の雨粒は描かない。しぶきはこの高さに出す）
    void SetGroundHeight(float groundY) { m_GroundY = groundY; }

private:
    // 屋根やひさし（雨が当たらない所）を、置かれている物から集め直している
    void CollectShelters();

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DrawParams;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_TwoSidedRasterizer;
    float m_Time = 0.0f;
    float m_Intensity = 1.0f;
    // 負荷を比べるために、起動時の環境変数で雨を止めたか（SetIntensity では戻さない）
    bool m_DisabledByEnvironment = false;
    float m_GroundY = -100.0f;
    // 屋根を集め直すまでの秒数（配置を読み直したときにも合うよう、ときどき集め直している）
    float m_ShelterTimer = 0.0f;
    static constexpr int MaxShelters = 8;
    DirectX::SimpleMath::Vector4 m_ShelterRects[MaxShelters];
    float m_ShelterBottoms[MaxShelters] = {};
    int m_ShelterCount = 0;
};
