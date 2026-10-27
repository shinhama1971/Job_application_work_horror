// ============================================================================
// ファイルの役割: 天井から吊るした裸電球（P.T. の廊下の電球を参考）。コードの先の電球が振り子のように揺れ、照らす光も一緒に動く。
// 主な技術: 減衰する2方向の振り子（ばねの式）、すきま風の小さな揺れ、外から加える揺さぶり、電球の位置に置く点光源、
//           円柱と球を組み合わせたメッシュ（コード・ソケット・電球）
//
// ・普段はすきま風で、わずかにゆっくり揺れている。
// ・異変や物音のとき（Kick）と、影が近いとき（SetDisturbance）に大きく揺れ、光だまりが床と壁の上で行き来する。
// ・揺れの大きさで、電球の明るさも少し揺れる（接触の悪いソケットのように）。
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

#include <memory>
#include <vector>

class HangingBulb : public Object
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    // コードとソケットのマテリアル（光らない）と、電球のマテリアル（光る）。電球のインデックスは後ろにまとめている
    std::unique_ptr<Material> m_CordMaterial;
    std::unique_ptr<Material> m_BulbMaterial;
    unsigned int m_CordIndexCount = 0;

    // コードの長さ（電球が目より少し上に来る長さ）、振り子の角度（x・z方向）と角速度、揺れの強さを足す度合い（影が近いとき）、時間、明るさ
    float m_CordLength = 4.0f;
    float m_AngleX = 0.0f;
    float m_AngleZ = 0.0f;
    float m_VelocityX = 0.0f;
    float m_VelocityZ = 0.0f;
    float m_Disturbance = 0.0f;
    float m_Time = 0.0f;
    float m_Brightness = 1.0f;
    float m_Seed = 0.0f;

    // 電球の中心のワールド座標（揺れを含む）
    DirectX::SimpleMath::Vector3 GetBulbPosition() const;
    // 揺れを含めたワールド行列
    DirectX::SimpleMath::Matrix MakeWorldMatrix() const;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    void Uninit() override;
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const override;

    // 天井の吊るす位置（コードの付け根）と、揺れの向きと風をずらす値
    void Place(const DirectX::SimpleMath::Vector3& anchor, float seed);
    // 揺さぶっている（strength は角速度。0.6 で大きく揺れる）。向きは乱数で決める
    void Kick(float strength);
    // 影が近いときなど、揺れを強める度合い（0〜1）を設定している
    void SetDisturbance(float disturbance) { m_Disturbance = disturbance; }
};
