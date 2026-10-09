// ============================================================================
// ファイルの役割: 2面の照明の区画を通り過ぎたかどうかを管理している。
// 主な技術: ビットで記録する軽い状態、区画ごとに一度だけ起こす判定
// 照明・画面効果・振動への演出の命令は Stage2Scene が担当している。
// ============================================================================

#pragma once

// 区画ごとに1ビットを使い、一度通った区画で同じ演出が二度起きないようにしている。
class LightZoneProgress final
{
private:
    // 通った区画のビット
    unsigned int m_VisitedMask = 0u;

public:
    // どこも通っていない状態に戻している
    void Reset() noexcept { m_VisitedMask = 0u; }

    // プレイヤーがtriggerZを越えたとき、まだ通っていない区画なら記録してtrueを返している（その区画の演出を起こす合図）
    bool TryEnter(int zoneIndex, float playerZ, float triggerZ) noexcept
    {
        const unsigned int zoneBit = 1u << zoneIndex;
        if ((m_VisitedMask & zoneBit) != 0u || playerZ <= triggerZ)
        {
            return false;
        }

        m_VisitedMask |= zoneBit;
        return true;
    }

    // その区画を通ったかを返している
    bool HasVisited(int zoneIndex) const noexcept
    {
        return (m_VisitedMask & (1u << zoneIndex)) != 0u;
    }
};
