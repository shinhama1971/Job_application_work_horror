// ============================================================================
// ファイルの役割: 外の森の研究所の玄関（説明は LabEntrance.h を参照）。
// ============================================================================

#include "LabEntrance.h"

#include "Player.h"

// 調べたら、プレイヤーを止め、入ったことを覚えている（演出とシーンの切り替えは ForestScene）
void LabEntrance::Interact(Player& player)
{
    if (m_Entered)
    {
        return;
    }
    m_Entered = true;
    player.SetCanControl(false);
}
