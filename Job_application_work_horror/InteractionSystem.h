// ============================================================================
// ファイルの役割: 視線の向きと距離を使って、今調べられる対象を1つ選んでいる。
// 主な技術: 視線と対象の向きの比較、点数による一番よい対象の選択、壁でさえぎられていないかの判定、インターフェースによる分離
// ============================================================================

#pragma once

#include <string_view>
#include <SimpleMath.h>

class Interactable;
class Player;

// Playerが持ち、毎フレーム視線の先の調べられる対象を選び、Eキー（コントローラーはA）で調べている。
class InteractionSystem
{
public:
    // 毎フレーム、カメラの正面に近く、さえぎる物のない対象を選んでいる。
    void Update(Player& player);
    // 今選んでいる対象と、その操作の説明を返している
    Interactable* GetFocusedInteractable() const { return m_FocusedInteractable; }
    std::string_view GetPrompt() const;

private:
    // 距離・正面度・壁の表面に付いた物を許す幅を一か所にまとめ、操作感を調整しやすくしている。
    // 調べられる最大の距離、視線との内積の下限（約44度以内）、対象の手前で壁に当たってもよい距離
    static constexpr float MaxInteractionDistance = 55.0f;
    static constexpr float MinimumFacingDot = 0.72f;
    static constexpr float SurfaceInteractionTolerance = 4.5f;

    Interactable* m_FocusedInteractable = nullptr;
    // originからtargetまでに壁があればfalseを返している。壁越しに調べられないようにしている。
    bool HasClearLineOfSight(
        const DirectX::SimpleMath::Vector3& origin,
        const DirectX::SimpleMath::Vector3& target,
        float targetDistance) const;
};
