// ============================================================================
// ファイルの役割: 2面の壁の鏡にだけ映る「自分の姿」（プレイヤーの体）の位置・向き・手足の振りを決め、鏡のシェーダーへ渡している。
// 主な技術: 過去の位置と向きを覚えておく輪のバッファ（鏡の中の自分だけ動きが遅れる）、歩く速さから決める手足の振り
//
// ・一人称のゲームなのでプレイヤーの体はないが、鏡には自分が映らないと不自然なため、鏡にだけ体を映している。
// ・体はメッシュとして描かず、鏡のシェーダー（shader/mirrorPS.hlsl の TraceMirrorSelf）が、鏡で反射した光線と
//   懐中電灯を前に持った立ち姿の距離場（shader/figureVolume.hlsli）に光線を通して描いている。ここでは足もとの位置・向き・手足の振りだけを渡している（b12）。
// ・懐中電灯を前に構えた姿にしている。歩くと、脚と左腕が歩く速さに合わせて前後に振れる。
// ・鏡の異変の周回では、鏡の中の自分だけが少し遅れて動く（SetDelaySeconds）。
// ============================================================================

#pragma once

#include "Object.h"

#include <array>

class Player;

class MirrorSelf : public Object
{
private:
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
    // 最初のUpdateで探して覚えておくプレイヤー
    Player* m_Player = nullptr;

    // 何個前の記録かを指定して、記録の姿勢を返している
    const PoseSample& GetSample(int samplesAgo) const;

public:
    void Init() override;
    void Update() override;
    // 体は鏡のシェーダーが描くため、ここでは何も描かない
    void Draw(Camera* camera) override { (void)camera; }
    void Uninit() override;

    // 鏡の中の自分を、何秒遅らせて動かすか（最大2秒）
    void SetDelaySeconds(float seconds) { m_DelaySeconds = seconds; }
};
