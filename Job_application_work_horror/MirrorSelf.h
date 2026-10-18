// ============================================================================
// ファイルの役割: 2面の壁の鏡にだけ映る「自分の姿」（プレイヤーの体）を描いている。
// 主な技術: 円柱と楕円体を組み合わせた体のメッシュ（頂点の色で服・肌・髪を塗り分け）、部位ごとの回転で歩くときに手足を振る動き、
//           鏡を描く間だけの描画、過去の位置と向きを覚えておく輪のバッファ（鏡の中の自分だけ動きが遅れる）
//
// ・一人称のゲームなのでプレイヤーの体はないが、鏡には自分が映らないと不自然なため、鏡を描く間だけ体を描いている。
// ・懐中電灯を前に構えた姿にしている。歩くと、脚と左腕が歩く速さに合わせて前後に振れる。
// ・鏡の異変の周回では、鏡の中の自分だけが少し遅れて動く（SetDelaySeconds）。
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

#include <array>
#include <memory>
#include <vector>

class Player;

class MirrorSelf : public Object
{
private:
    // 頂点・インデックスとGPUのバッファ、マテリアル（色は頂点の色で塗り分け、マテリアルは白）
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;

    // 体の部位（インデックスの範囲、振るときの回転の中心、振り方）。振り方は 0 = 振らない、1 = 左脚、2 = 右脚、3 = 左腕
    struct BodyPart
    {
        unsigned int IndexStart = 0;
        unsigned int IndexCount = 0;
        DirectX::SimpleMath::Vector3 Pivot;
        int Swing = 0;
    };
    std::vector<BodyPart> m_Parts;

    // 位置と向きの記録（1秒あたり60個、最大2秒分）。遅れて動くとき、この中から昔の姿勢を選んでいる
    struct PoseSample
    {
        DirectX::SimpleMath::Vector3 Position;
        float Yaw = 0.0f;
    };
    static constexpr int HistoryCount = 120;
    static constexpr float SampleInterval = 1.0f / 60.0f;
    std::array<PoseSample, HistoryCount> m_History{};
    int m_HistoryHead = 0;
    int m_HistoryFilled = 0;
    float m_SampleTimer = 0.0f;
    // 遅らせる秒数（0なら今の姿勢）
    float m_DelaySeconds = 0.0f;
    // 歩く動きの位相と、振る大きさ（歩く速さから決める）
    float m_WalkPhase = 0.0f;
    float m_WalkAmount = 0.0f;
    // 映すか（隠れている間は映さない）
    bool m_Hidden = false;
    // 最初のUpdateで探して覚えておくプレイヤー
    Player* m_Player = nullptr;

    // 何個前の記録かを指定して、記録の姿勢を返している
    const PoseSample& GetSample(int samplesAgo) const;
    // 部位を1つ作り始める・作り終える（その間に足した三角形が、その部位になる）
    void BeginPart(const DirectX::SimpleMath::Vector3& pivot, int swing);
    void EndPart();
    // 点aから点bへの円柱（太さは両端で変えられ、両端はふさいでいる）、楕円体、箱を、色を付けて足している
    void AddCylinder(
        const DirectX::SimpleMath::Vector3& a, const DirectX::SimpleMath::Vector3& b,
        float radiusA, float radiusB, float depthScale, const DirectX::SimpleMath::Color& color);
    void AddEllipsoid(
        const DirectX::SimpleMath::Vector3& center, const DirectX::SimpleMath::Vector3& radius,
        const DirectX::SimpleMath::Color& color);
    void AddTriangle(
        const DirectX::SimpleMath::Vector3& a, const DirectX::SimpleMath::Vector3& b, const DirectX::SimpleMath::Vector3& c,
        const DirectX::SimpleMath::Vector3& normalA, const DirectX::SimpleMath::Vector3& normalB,
        const DirectX::SimpleMath::Vector3& normalC, const DirectX::SimpleMath::Color& color);

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    void Uninit() override;
    // 鏡にだけ映り、水たまりの反射・影・本描画には出さない
    bool ContributesToWallMirror() const override { return true; }
    bool DrawsInAuxiliaryView() const override { return false; }

    // 鏡の中の自分を、何秒遅らせて動かすか（最大2秒）
    void SetDelaySeconds(float seconds) { m_DelaySeconds = seconds; }
};
