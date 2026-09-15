// ============================================================================
// ファイルの役割: 視線レイと距離を使って、現在操作できる対象を選択します。
// 主な技術: レイ判定、最近傍選択、遮蔽判定、インターフェース分離
// 読み方: 公開関数は外部から使う操作、メンバー変数は保持する状態を表します。
// ============================================================================

#pragma once

#include <string_view>
#include <SimpleMath.h>

class Interactable;
class Player;

class InteractionSystem
{
public:
    // 毎フレーム、カメラ正面に最も近く遮蔽物のない対象を選びます。
    void Update(Player& player);
    Interactable* GetFocusedInteractable() const { return m_FocusedInteractable; }
    std::string_view GetPrompt() const;

private:
    // 距離・正面度・壁面許容値を一か所にまとめ、操作感を調整しやすくします。
    static constexpr float MaxInteractionDistance = 55.0f;
    static constexpr float MinimumFacingDot = 0.72f;
    static constexpr float SurfaceInteractionTolerance = 4.5f;

    Interactable* m_FocusedInteractable = nullptr;
    // originからtargetまでに壁があればfalse。壁越し操作を防止します。
    bool HasClearLineOfSight(
        const DirectX::SimpleMath::Vector3& origin,
        const DirectX::SimpleMath::Vector3& target,
        float targetDistance) const;
};
