// ============================================================================
// ファイルの役割: 1面の書類保管室に出る「照らすと止まる影」の進行と動きのルールを管理している。
// 主な技術: 有限状態機械（待機 → 出現 → 触れた後の間）、照らされている間だけ止まる追跡、
//           格子の幅優先探索による経路探索（棚の迷路を回り込む）、行き詰まったときの移動
//
// ・書類保管室に入ってしばらくすると、プレイヤーから一番遠い場所に影が現れる。
// ・懐中電灯で照らしている間はその場で止まり、光が外れると歩くより速く近づいてくる。
//   照らし続けると電池が減り、棚の迷路を進むには目を離す必要がある、という駆け引きを作っている。
// ・触れられると懐中電灯の電池を奪われ、影は消える。しばらくすると部屋の奥にまた現れる。
// ・部屋を出ると消え、戻るとまた現れる。電力が戻った後は出ない。
// 描画・入力・音に依存せず、「照らされているか」「部屋の中にいるか」などの判定はシーンから受け取っている。
//
// 経路探索：部屋を4単位の格子に分け、各マスが通れるかを最初に一度だけ調べている（壁・棚の押し戻しで判定）。
// プレイヤーのいるマスから幅優先探索で「各マスからプレイヤーまでの歩数」を求め、影は歩数が減る隣のマスへ進む。
// 探索はプレイヤーが別のマスへ移ったときだけやり直している（格子は約1500マスなので、1回の探索は軽い）。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>

class Stage1LightStalker final
{
public:
    // 書類保管室の範囲（壁の内側の面。南はWall5、北はWall14、東は入口のあるx=-45の壁、西は西棟との境の壁）
    static constexpr float RoomMinX = -218.0f;
    static constexpr float RoomMaxX = -47.0f;
    static constexpr float RoomMinZ = 42.0f;
    static constexpr float RoomMaxZ = 178.0f;

    // 1フレーム分の判定結果（シーンが求めて渡している）
    struct FrameInput
    {
        DirectX::SimpleMath::Vector3 PlayerPosition;
        bool Lit = false;       // 懐中電灯の光の円の中にいて、間に壁や扉がないか
        bool Enabled = false;   // 出てよいか（電力が戻る前で、他の大きな出来事の最中でない）
        float DeltaTime = 0.0f;
    };

    // 1フレーム分の結果。シーンはこれを見て、影の表示・音・画面効果・電池を変えている
    struct FrameResult
    {
        bool Visible = false;   // 影を表示するか
        DirectX::SimpleMath::Vector3 Position;
        bool Appeared = false;  // このフレームで現れた
        bool FirstAppearance = false;  // 初めて現れた（遊び方の知らせを出す）
        bool Caught = false;    // このフレームで触れられた
        bool Step = false;      // このフレームで足音を1歩鳴らす
    };

    // 位置を、壁や棚にめり込まないよう押し戻す処理（半径を受け取る）。シーンが壁の一覧を使って行っている
    using ResolveCollision = std::function<void(DirectX::SimpleMath::Vector3&, float)>;

    // 点が書類保管室の中にあるか
    static bool IsInsideRoom(const DirectX::SimpleMath::Vector3& position)
    {
        return position.x > RoomMinX && position.x < RoomMaxX &&
            position.z > RoomMinZ && position.z < RoomMaxZ;
    }

    // 1フレーム進めている
    FrameResult Update(const FrameInput& input, const ResolveCollision& resolveCollision)
    {
        FrameResult result;
        const bool playerInRoom = IsInsideRoom(input.PlayerPosition);
        const bool canAppear = input.Enabled && playerInRoom;

        // 通れるマスの表は、壁や棚が配置された後の最初の更新で一度だけ作っている
        if (!m_GridBuilt && resolveCollision)
        {
            BuildWalkableGrid(resolveCollision);
        }

        switch (m_State)
        {
        case State::Waiting:
            // 部屋の中にいる間だけ数え、出てしまったら最初から数え直している
            if (!canAppear)
            {
                m_Timer = m_HasAppeared ? ReappearDelay : FirstAppearDelay;
                break;
            }
            m_Timer -= input.DeltaTime;
            if (m_Timer <= 0.0f)
            {
                Appear(input.PlayerPosition, result);
            }
            break;

        case State::Active:
            // 部屋を出た・出てはいけない状況になったら、消して待つ状態に戻している
            if (!canAppear)
            {
                m_State = State::Waiting;
                m_Timer = ReappearDelay;
                break;
            }
            UpdateActive(input, resolveCollision, result);
            break;

        case State::Cooldown:
            // 触れた後は、しばらく間を空けてから部屋の奥に出し直している
            m_Timer -= input.DeltaTime;
            if (m_Timer <= 0.0f)
            {
                m_State = State::Waiting;
                m_Timer = canAppear ? 0.0f : ReappearDelay;
            }
            break;
        }

        result.Visible = m_State == State::Active;
        result.Position = m_Position;
        return result;
    }

    // 影が出ているか
    bool IsActive() const { return m_State == State::Active; }

private:
    enum class State
    {
        Waiting,    // 部屋に入るのを待っている（入ってから現れるまでの間も含む）
        Active,     // 部屋の中にいて、照らされていなければ近づいてくる
        Cooldown    // 触れた後、次に現れるまでの間
    };

    // 初めて現れるまで・部屋に入り直してから現れるまで・触れた後に次が現れるまでの秒数
    static constexpr float FirstAppearDelay = 4.0f;
    static constexpr float ReappearDelay = 2.5f;
    static constexpr float CaughtCooldown = 9.0f;
    // 近づく速さ（歩く速さは30、走ると約50）。歩くより速いので、照らさずに逃げ切るのは難しい
    static constexpr float Speed = 38.0f;
    // この距離まで近づかれたら触れられたとしている。体の半径（棚との押し戻しと、通れるマスの判定に使う）。足音1歩の長さ
    static constexpr float CatchDistance = 15.0f;
    static constexpr float BodyRadius = 5.0f;
    static constexpr float StrideLength = 20.0f;
    // 行き詰まりの判定：この秒数の間に、進めたはずの距離の3割も進めなかったら、別の場所へ移る
    static constexpr float StuckWindow = 2.0f;
    // 行き詰まって移る先は、プレイヤーからこの距離以上離れた候補にしている（目の前に急に出ないように）
    static constexpr float RelocateMinimumDistance = 55.0f;

    // 経路探索の格子：1マスの大きさと、横・縦のマスの数（部屋の範囲を覆う数）
    static constexpr float CellSize = 4.0f;
    static constexpr int GridWidth =
        static_cast<int>((RoomMaxX - RoomMinX) / CellSize) + 1;
    static constexpr int GridHeight =
        static_cast<int>((RoomMaxZ - RoomMinZ) / CellSize) + 1;
    static constexpr int CellCount = GridWidth * GridHeight;
    // 歩数が求まっていない（たどり着けない）マス
    static constexpr std::uint16_t Unreachable = (std::numeric_limits<std::uint16_t>::max)();

    // 現れる場所の候補（部屋の四隅と棚の間の通路。棚にめり込まない位置を選んでいる）
    static constexpr std::array<DirectX::SimpleMath::Vector3, 6> SpawnPoints =
    {
        DirectX::SimpleMath::Vector3(-205.0f, -99.0f, 52.0f),
        DirectX::SimpleMath::Vector3(-60.0f, -99.0f, 52.0f),
        DirectX::SimpleMath::Vector3(-205.0f, -99.0f, 85.0f),
        DirectX::SimpleMath::Vector3(-130.0f, -99.0f, 115.0f),
        DirectX::SimpleMath::Vector3(-170.0f, -99.0f, 145.0f),
        DirectX::SimpleMath::Vector3(-90.0f, -99.0f, 170.0f),
    };

    // プレイヤーから一番遠い候補に出している
    void Appear(const DirectX::SimpleMath::Vector3& playerPosition, FrameResult& result)
    {
        float farthest = -1.0f;
        for (const DirectX::SimpleMath::Vector3& point : SpawnPoints)
        {
            const float distance = HorizontalDistance(point, playerPosition);
            if (distance > farthest)
            {
                farthest = distance;
                m_Position = point;
            }
        }
        result.Appeared = true;
        result.FirstAppearance = !m_HasAppeared;
        m_HasAppeared = true;
        m_State = State::Active;
        m_StrideDistance = 0.0f;
        ResetStuckWindow();
    }

    // 出ている間：照らされていれば止まり、照らされていなければ経路に沿ってプレイヤーへ近づいている
    void UpdateActive(
        const FrameInput& input, const ResolveCollision& resolveCollision, FrameResult& result)
    {
        const float distance = HorizontalDistance(m_Position, input.PlayerPosition);
        if (distance < CatchDistance)
        {
            result.Caught = true;
            m_State = State::Cooldown;
            m_Timer = CaughtCooldown;
            return;
        }

        // 照らされている間は1歩も動かない（行き詰まりの判定も止めている）
        if (input.Lit)
        {
            ResetStuckWindow();
            return;
        }

        // 向かう先：経路探索で次に進むマスの中心（すぐ近くまで来たら、プレイヤーそのもの）
        const DirectX::SimpleMath::Vector3 target = GetSteeringTarget(input.PlayerPosition);
        DirectX::SimpleMath::Vector3 direction = target - m_Position;
        direction.y = 0.0f;
        const float targetDistance = direction.Length();
        if (targetDistance < 0.001f)
        {
            return;
        }
        direction /= targetDistance;
        const float travel = (std::min)(Speed * input.DeltaTime, distance);
        const DirectX::SimpleMath::Vector3 before = m_Position;
        DirectX::SimpleMath::Vector3 next = m_Position + direction * travel;
        // 棚や壁にめり込まないよう押し戻し、部屋の外へは出ないようにしている
        if (resolveCollision)
        {
            resolveCollision(next, BodyRadius);
        }
        next.x = (std::clamp)(next.x, RoomMinX + BodyRadius, RoomMaxX - BodyRadius);
        next.z = (std::clamp)(next.z, RoomMinZ + BodyRadius, RoomMaxZ - BodyRadius);
        next.y = before.y;
        m_Position = next;

        // 実際に進んだ距離で足音を鳴らしている（押し戻されて止まっている間は鳴らない）
        const float moved = HorizontalDistance(before, m_Position);
        m_StrideDistance += moved;
        if (m_StrideDistance >= StrideLength)
        {
            m_StrideDistance -= StrideLength;
            result.Step = true;
        }

        // 経路探索で回り込めない場合の保険：一定時間に進めたはずの距離と、実際に進んだ距離を比べている
        m_StuckExpected += travel;
        m_StuckMoved += moved;
        m_StuckTimer += input.DeltaTime;
        if (m_StuckTimer >= StuckWindow)
        {
            if (m_StuckMoved < m_StuckExpected * 0.3f)
            {
                Relocate(input.PlayerPosition);
            }
            ResetStuckWindow();
        }
    }

    // 経路探索で次に向かう点を返している
    DirectX::SimpleMath::Vector3 GetSteeringTarget(const DirectX::SimpleMath::Vector3& playerPosition)
    {
        if (!m_GridBuilt)
        {
            return playerPosition;
        }

        // プレイヤーが別のマスへ移ったら、各マスからプレイヤーまでの歩数を求め直している
        const int playerCell = FindNearestWalkableCell(playerPosition);
        if (playerCell < 0)
        {
            return playerPosition;
        }
        if (playerCell != m_PlayerCell)
        {
            ComputeDistanceField(playerCell);
            m_PlayerCell = playerCell;
        }

        const int myCell = FindNearestWalkableCell(m_Position);
        if (myCell < 0 || m_Distance[myCell] == Unreachable || m_Distance[myCell] <= 1)
        {
            // たどり着けない・すぐ隣にいるときは、まっすぐプレイヤーへ向かっている
            return playerPosition;
        }

        // 今のマスより歩数が少ない隣のマスのうち、一番少ないマスへ向かっている。
        // 斜めに進むのは、角の両側のマスも通れるときだけにしている（棚の角をすり抜けないように）。
        const int myX = myCell % GridWidth;
        const int myZ = myCell / GridWidth;
        int bestCell = -1;
        std::uint16_t bestDistance = m_Distance[myCell];
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dz == 0)
                {
                    continue;
                }
                const int x = myX + dx;
                const int z = myZ + dz;
                if (!IsWalkableCell(x, z))
                {
                    continue;
                }
                if (dx != 0 && dz != 0 &&
                    (!IsWalkableCell(myX + dx, myZ) || !IsWalkableCell(myX, myZ + dz)))
                {
                    continue;
                }
                const int cell = z * GridWidth + x;
                if (m_Distance[cell] < bestDistance)
                {
                    bestDistance = m_Distance[cell];
                    bestCell = cell;
                }
            }
        }
        if (bestCell < 0)
        {
            return playerPosition;
        }
        return CellCenter(bestCell);
    }

    // 各マスが通れるかを、マスの中心を壁・棚から押し戻したときに動かないか（＝めり込んでいないか）で調べている
    void BuildWalkableGrid(const ResolveCollision& resolveCollision)
    {
        for (int cell = 0; cell < CellCount; ++cell)
        {
            const DirectX::SimpleMath::Vector3 center = CellCenter(cell);
            DirectX::SimpleMath::Vector3 resolved = center;
            resolveCollision(resolved, BodyRadius);
            const bool insideRoom =
                center.x > RoomMinX + BodyRadius && center.x < RoomMaxX - BodyRadius &&
                center.z > RoomMinZ + BodyRadius && center.z < RoomMaxZ - BodyRadius;
            m_Walkable[cell] = insideRoom &&
                HorizontalDistance(center, resolved) < 0.01f;
        }
        m_GridBuilt = true;
        m_PlayerCell = -1;
    }

    // プレイヤーのマスから幅優先探索で広げ、各マスからプレイヤーまでの歩数（上下左右と斜め）を求めている
    void ComputeDistanceField(int startCell)
    {
        m_Distance.fill(Unreachable);
        int head = 0;
        int tail = 0;
        m_Queue[tail++] = static_cast<std::uint16_t>(startCell);
        m_Distance[startCell] = 0;
        while (head < tail)
        {
            const int cell = m_Queue[head++];
            const int cellX = cell % GridWidth;
            const int cellZ = cell / GridWidth;
            const std::uint16_t nextDistance = static_cast<std::uint16_t>(m_Distance[cell] + 1);
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (dx == 0 && dz == 0)
                    {
                        continue;
                    }
                    const int x = cellX + dx;
                    const int z = cellZ + dz;
                    if (!IsWalkableCell(x, z))
                    {
                        continue;
                    }
                    // 斜めは、角の両側も通れるときだけつないでいる
                    if (dx != 0 && dz != 0 &&
                        (!IsWalkableCell(cellX + dx, cellZ) || !IsWalkableCell(cellX, cellZ + dz)))
                    {
                        continue;
                    }
                    const int neighbor = z * GridWidth + x;
                    if (m_Distance[neighbor] != Unreachable)
                    {
                        continue;
                    }
                    m_Distance[neighbor] = nextDistance;
                    m_Queue[tail++] = static_cast<std::uint16_t>(neighbor);
                }
            }
        }
    }

    // 点に一番近い通れるマスを返している（点のマスが通れなければ、周り2マスまで探す。無ければ-1）
    int FindNearestWalkableCell(const DirectX::SimpleMath::Vector3& position) const
    {
        const int baseX = static_cast<int>((position.x - RoomMinX) / CellSize);
        const int baseZ = static_cast<int>((position.z - RoomMinZ) / CellSize);
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
                const int cell = (baseZ + dz) * GridWidth + (baseX + dx);
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
        return x >= 0 && x < GridWidth && z >= 0 && z < GridHeight &&
            m_Walkable[z * GridWidth + x];
    }

    // マスの中心の位置（高さは影の足元と同じ）
    static DirectX::SimpleMath::Vector3 CellCenter(int cell)
    {
        return DirectX::SimpleMath::Vector3(
            RoomMinX + (static_cast<float>(cell % GridWidth) + 0.5f) * CellSize,
            -99.0f,
            RoomMinZ + (static_cast<float>(cell / GridWidth) + 0.5f) * CellSize);
    }

    // 見ていない間に、プレイヤーに一番近い（ただし近すぎない）候補へ移っている
    void Relocate(const DirectX::SimpleMath::Vector3& playerPosition)
    {
        float nearest = (std::numeric_limits<float>::max)();
        for (const DirectX::SimpleMath::Vector3& point : SpawnPoints)
        {
            const float distance = HorizontalDistance(point, playerPosition);
            if (distance >= RelocateMinimumDistance && distance < nearest)
            {
                nearest = distance;
                m_Position = point;
            }
        }
        m_StrideDistance = 0.0f;
    }

    // 行き詰まりの判定を最初からにしている
    void ResetStuckWindow()
    {
        m_StuckTimer = 0.0f;
        m_StuckExpected = 0.0f;
        m_StuckMoved = 0.0f;
    }

    // 上から見た2点の距離
    static float HorizontalDistance(
        const DirectX::SimpleMath::Vector3& a, const DirectX::SimpleMath::Vector3& b)
    {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    }

    // 今の状態、状態ごとの残り秒数、一度でも現れたか、影の位置、足音までの距離
    State m_State = State::Waiting;
    float m_Timer = FirstAppearDelay;
    bool m_HasAppeared = false;
    DirectX::SimpleMath::Vector3 m_Position;
    float m_StrideDistance = 0.0f;
    // 行き詰まりの判定に使う経過秒数、進めたはずの距離、実際に進んだ距離
    float m_StuckTimer = 0.0f;
    float m_StuckExpected = 0.0f;
    float m_StuckMoved = 0.0f;
    // 経路探索：通れるマスの表と、それを作ったか、各マスからプレイヤーまでの歩数、探索の順番待ちの列、
    // 歩数を求めたときのプレイヤーのマス
    std::array<bool, CellCount> m_Walkable{};
    bool m_GridBuilt = false;
    std::array<std::uint16_t, CellCount> m_Distance{};
    std::array<std::uint16_t, CellCount> m_Queue{};
    int m_PlayerCell = -1;
};
