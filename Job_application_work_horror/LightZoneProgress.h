// ============================================================================
// ファイルの役割: 2面の照明区画を通過済みかどうか管理します。
// 照明・画面効果・振動への演出命令は Stage2Scene が担当します。
// ============================================================================

#pragma once

class LightZoneProgress final
{
private:
    unsigned int m_VisitedMask = 0u;

public:
    void Reset() noexcept { m_VisitedMask = 0u; }

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

    bool HasVisited(int zoneIndex) const noexcept
    {
        return (m_VisitedMask & (1u << zoneIndex)) != 0u;
    }
};
