// ============================================================================
// ファイルの役割: 1面の「光の安全地帯」（停電しても消えない、暖かい色の電球の下）にいるかと、その効き目を管理している。
// 主な技術: 範囲の判定、なめらかに上がり下がりする落ち着きの度合い、描画・入力に依存しない状態クラス
//
// ・暗く冷たい施設の中で、ここだけは暖かい色の電球が落ち着いて灯っている（ほかの照明のちかちかとの対比）。
// ・灯りの下にいる間は「落ち着き」が上がり、心拍が静まり、姿の見えない物音が止み、画面のざらつきが和らぐ。
//   どれをどれだけ弱めるかはSceneが GetCalm を使って決めている。
// ・ライトを消して休むと、電池が少しずつ戻る（半分を少し超えるまで）。暗い所へ出る前の、緊張と緩和の「緩和」を作っている。
// ============================================================================

#pragma once

#include <SimpleMath.h>
#include <string_view>
#include <vector>

class Stage1SafeLights final
{
public:
    // 灯りの届く範囲（床の上の円）
    struct Zone
    {
        DirectX::SimpleMath::Vector3 Center;
        float Radius = 30.0f;
    };

    // ライトを消して休んでいる間に、1秒あたりに戻る電池の量と、戻る上限
    static constexpr float RechargePerSecond = 2.0f;
    static constexpr float RechargeLimit = 60.0f;

    // 灯りの場所を受け取り、最初の状態に戻している
    void Init(const std::vector<Zone>& zones);
    // 1フレーム分進め、このフレームに戻す電池の量を返している
    float Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition,
        bool flashlightOn, float battery);

    // 落ち着きの度合い（0〜1）。灯りに入ると約1.5秒で1になり、出ると約0.8秒で0に戻る
    float GetCalm() const { return m_Calm; }
    // 今、灯りの下にいるか
    bool IsInside() const { return m_Inside; }
    // 初めて灯りに入ったときの知らせ（出していなければ空）
    std::string_view GetNoticeText() const;

private:
    std::vector<Zone> m_Zones;
    float m_Calm = 0.0f;
    bool m_Inside = false;
    bool m_NoticeShown = false;
    float m_NoticeTimer = 0.0f;
};
