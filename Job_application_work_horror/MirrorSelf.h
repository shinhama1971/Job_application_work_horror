// ============================================================================
// ファイルの役割: 2面の壁の鏡にだけ映る「自分の姿」（プレイヤーの体）を描いている。
// 主な技術: 箱を組み合わせた体のメッシュ（頂点の色で服・肌・髪を塗り分け）、鏡を描く間だけの描画、
//           過去の位置と向きを覚えておく輪のバッファ（鏡の中の自分だけ動きが遅れる）
//
// ・一人称のゲームなのでプレイヤーの体はないが、鏡には自分が映らないと不自然なため、鏡を描く間だけ体を描いている。
// ・懐中電灯を前に構えた姿にしている。
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
    // 映すか（隠れている間は映さない）
    bool m_Hidden = false;
    // 最初のUpdateで探して覚えておくプレイヤー
    Player* m_Player = nullptr;

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
