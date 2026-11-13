// ============================================================================
// ファイルの役割: 1つの部屋の中で、棚や壁を回り込んでプレイヤーへ近づくための経路探索。
// 主な技術: 格子の幅優先探索（各マスからプレイヤーまでの歩数の表）、押し戻しを使った通れるマスの判定
//
// ・部屋を決まった大きさのマスに分け、各マスが通れるかを最初に一度だけ調べている（マスの中心を壁・棚から押し戻して、動かなければ通れる）。
// ・プレイヤーのいるマスから幅優先探索で「各マスからプレイヤーまでの歩数」を求め、追う側は歩数が減る隣のマスへ進む。
// ・探索はプレイヤーが別のマスへ移ったときだけやり直している。
// ・斜めに進むのは、角の両側のマスも通れるときだけにしている（棚の角をすり抜けないように）。
// 書類保管室の影（Stage1LightStalker）と同じ考え方を、部屋の広さを渡して使えるようにしたもの。
// 描画・入力・音には依存していない。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

class RoomPathGrid final
{
public:
    // 位置を、壁や棚にめり込まないよう押し戻す処理（半径を受け取る）
    using ResolveCollision = std::function<void(DirectX::SimpleMath::Vector3&, float)>;

    // 部屋の範囲（壁の内側の面）と、マスの大きさ、追う側の体の半径、足元の高さを決めている
    RoomPathGrid(float minX, float maxX, float minZ, float maxZ,
        float cellSize, float bodyRadius, float floorY)
        : m_MinX(minX), m_MaxX(maxX), m_MinZ(minZ), m_MaxZ(maxZ),
        m_CellSize(cellSize), m_BodyRadius(bodyRadius), m_FloorY(floorY),
        m_Width(static_cast<int>((maxX - minX) / cellSize) + 1),
        m_Height(static_cast<int>((maxZ - minZ) / cellSize) + 1)
    {
        const std::size_t count = static_cast<std::size_t>(m_Width * m_Height);
        m_Walkable.assign(count, false);
        m_Distance.assign(count, Unreachable);
        m_Queue.assign(count, 0);
    }

    // 通れるマスの表を作っている（壁や棚が置かれた後に一度だけ呼ぶ）
    void Build(const ResolveCollision& resolveCollision)
    {
        for (int cell = 0; cell < m_Width * m_Height; ++cell)
        {
            const DirectX::SimpleMath::Vector3 center = CellCenter(cell);
            DirectX::SimpleMath::Vector3 resolved = center;
            if (resolveCollision)
            {
                resolveCollision(resolved, m_BodyRadius);
            }
            const bool insideRoom =
                center.x > m_MinX + m_BodyRadius && center.x < m_MaxX - m_BodyRadius &&
                center.z > m_MinZ + m_BodyRadius && center.z < m_MaxZ - m_BodyRadius;
            m_Walkable[static_cast<std::size_t>(cell)] = insideRoom &&
                HorizontalDistance(center, resolved) < 0.01f;
        }
        m_Built = true;
        m_PlayerCell = -1;
    }

    bool IsBuilt() const { return m_Built; }

    // その点のまわりに通れるマスがあるか（現れる場所の候補が棚にめり込んでいないかを調べるのに使う）
    bool IsWalkable(const DirectX::SimpleMath::Vector3& position) const
    {
        return FindNearestWalkableCell(position) >= 0;
    }

    // from から target へ向かうとき、次に向かう点を返している（すぐ近く・たどり着けないときは target そのもの）
    DirectX::SimpleMath::Vector3 GetSteeringTarget(
        const DirectX::SimpleMath::Vector3& from, const DirectX::SimpleMath::Vector3& target)
    {
        if (!m_Built)
        {
            return target;
        }

        // 目標が別のマスへ移ったら、各マスから目標までの歩数を求め直している
        const int targetCell = FindNearestWalkableCell(target);
        if (targetCell < 0)
        {
            return target;
        }
        if (targetCell != m_PlayerCell)
        {
            ComputeDistanceField(targetCell);
            m_PlayerCell = targetCell;
        }

        const int myCell = FindNearestWalkableCell(from);
        if (myCell < 0 || m_Distance[static_cast<std::size_t>(myCell)] == Unreachable ||
            m_Distance[static_cast<std::size_t>(myCell)] <= 1)
        {
            return target;
        }

        // 今のマスより歩数が少ない隣のマスのうち、一番少ないマスへ向かっている
        const int myX = myCell % m_Width;
        const int myZ = myCell / m_Width;
        int bestCell = -1;
        std::uint16_t bestDistance = m_Distance[static_cast<std::size_t>(myCell)];
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if ((dx == 0 && dz == 0) || !CanStep(myX, myZ, dx, dz))
                {
                    continue;
                }
                const int cell = (myZ + dz) * m_Width + (myX + dx);
                if (m_Distance[static_cast<std::size_t>(cell)] < bestDistance)
                {
                    bestDistance = m_Distance[static_cast<std::size_t>(cell)];
                    bestCell = cell;
                }
            }
        }
        return bestCell < 0 ? target : CellCenter(bestCell);
    }

private:
    // 歩数が求まっていない（たどり着けない）マス
    static constexpr std::uint16_t Unreachable = (std::numeric_limits<std::uint16_t>::max)();

    // 目標のマスから幅優先探索で広げ、各マスから目標までの歩数（上下左右と斜め）を求めている
    void ComputeDistanceField(int startCell)
    {
        std::fill(m_Distance.begin(), m_Distance.end(), Unreachable);
        std::size_t head = 0;
        std::size_t tail = 0;
        m_Queue[tail++] = startCell;
        m_Distance[static_cast<std::size_t>(startCell)] = 0;
        while (head < tail)
        {
            const int cell = m_Queue[head++];
            const int cellX = cell % m_Width;
            const int cellZ = cell / m_Width;
            const std::uint16_t nextDistance =
                static_cast<std::uint16_t>(m_Distance[static_cast<std::size_t>(cell)] + 1);
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if ((dx == 0 && dz == 0) || !CanStep(cellX, cellZ, dx, dz))
                    {
                        continue;
                    }
                    const std::size_t neighbor = static_cast<std::size_t>((cellZ + dz) * m_Width + (cellX + dx));
                    if (m_Distance[neighbor] != Unreachable)
                    {
                        continue;
                    }
                    m_Distance[neighbor] = nextDistance;
                    m_Queue[tail++] = static_cast<int>(neighbor);
                }
            }
        }
    }

    // (x, z) のマスから (dx, dz) だけ隣へ進めるか。斜めは、角の両側も通れるときだけ
    bool CanStep(int x, int z, int dx, int dz) const
    {
        if (!IsWalkableCell(x + dx, z + dz))
        {
            return false;
        }
        return dx == 0 || dz == 0 || (IsWalkableCell(x + dx, z) && IsWalkableCell(x, z + dz));
    }

    // 点に一番近い通れるマスを返している（点のマスが通れなければ、周り2マスまで探す。無ければ-1）
    int FindNearestWalkableCell(const DirectX::SimpleMath::Vector3& position) const
    {
        const int baseX = static_cast<int>(std::floor((position.x - m_MinX) / m_CellSize));
        const int baseZ = static_cast<int>(std::floor((position.z - m_MinZ) / m_CellSize));
        int bestCell = -1;
        float bestDistance = (std::numeric_limits<float>::max)();
        for (int dz = -2; dz <= 2; ++dz)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                if (!IsWalkableCell(baseX + dx, baseZ + dz))
                {
                    continue;
                }
                const int cell = (baseZ + dz) * m_Width + (baseX + dx);
                const float distance = HorizontalDistance(CellCenter(cell), position);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestCell = cell;
                }
            }
        }
        return bestCell;
    }

    // マスが格子の中にあり、通れるか
    bool IsWalkableCell(int x, int z) const
    {
        return m_Built && x >= 0 && x < m_Width && z >= 0 && z < m_Height &&
            m_Walkable[static_cast<std::size_t>(z * m_Width + x)];
    }

    // マスの中心の位置（高さは足元）
    DirectX::SimpleMath::Vector3 CellCenter(int cell) const
    {
        return DirectX::SimpleMath::Vector3(
            m_MinX + (static_cast<float>(cell % m_Width) + 0.5f) * m_CellSize,
            m_FloorY,
            m_MinZ + (static_cast<float>(cell / m_Width) + 0.5f) * m_CellSize);
    }

    // 上から見た2点の距離
    static float HorizontalDistance(
        const DirectX::SimpleMath::Vector3& a, const DirectX::SimpleMath::Vector3& b)
    {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    }

    // 部屋の範囲、マスの大きさ、体の半径、足元の高さ、横・縦のマスの数
    float m_MinX;
    float m_MaxX;
    float m_MinZ;
    float m_MaxZ;
    float m_CellSize;
    float m_BodyRadius;
    float m_FloorY;
    int m_Width;
    int m_Height;
    // 通れるマスの表、それを作ったか、各マスから目標までの歩数、探索の順番待ちの列、歩数を求めたときの目標のマス
    std::vector<bool> m_Walkable;
    bool m_Built = false;
    std::vector<std::uint16_t> m_Distance;
    std::vector<int> m_Queue;
    int m_PlayerCell = -1;
};
