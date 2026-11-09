// ============================================================================
// ファイルの役割: 外の森の研究所の玄関（調べると、研究所の中＝1面へ入る）。
// 主な技術: 調べる操作（Interactable）、見た目を持たない調べる対象
//
// 玄関の扉の見た目と当たり判定は、配置ファイルの LabDoor（Wall）が受け持っている。
// ここでは、扉の手前に「研究所に入る」と調べられる点だけを置き、調べられたことを ForestScene に知らせている。
// 入る演出（扉の音・暗転）とシーンの切り替えは ForestScene がしている。
// ============================================================================

#pragma once

#include "Interactable.h"
#include "Object.h"

class LabEntrance final : public Object, public Interactable
{
public:
    void Init() override { m_Entered = false; }
    void Update() override {}
    // 見た目は持たない（扉は配置ファイルの LabDoor）
    void Draw(Camera* camera) override { (void)camera; }
    void Uninit() override {}

    bool IsInteractionEnabled() const override { return !m_Entered; }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override { return m_Entered ? "" : "研究所に入る"; }
    void Interact(Player& player) override;

    // 調べられたか（ForestScene が毎フレーム見て、入る演出を始めている）
    bool WasEntered() const { return m_Entered; }

private:
    bool m_Entered = false;
};
