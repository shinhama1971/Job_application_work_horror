// ============================================================================
// ファイルの役割: 1面の「懐中電灯で照らすと浮かぶ壁の文字」の進み具合を管理している。
// 主な技術: 視線による読んだかの判定、見ていない間に書き換える演出（P.T.のような手法）、見た目と状態の分離
// ============================================================================

#include "Stage1WallWritings.h"

#include "FlashlightWriting.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

// 文字のObjectを受け取り、どれも読んでいない状態から始めている
void Stage1WallWritings::Init(const Writings& writings)
{
    m_Writings = writings;
    m_ReadTimers.fill(0.0f);
    m_Read.fill(false);
    m_TurnChanged = false;
    m_TurnChangedRead = false;
    m_TurnChangedReadTimer = 0.0f;
    m_FadedByPower = false;
}

// 1フレーム分、読んだかの判定と書き換えを進めている
bool Stage1WallWritings::Update(
    float deltaTime,
    const Vector3& cameraPosition,
    const Vector3& cameraForward,
    bool flashlightOn,
    bool powerRestored)
{
    // 電力が戻ったら、すべての文字をゆっくり消し、その後は何もしない
    if (powerRestored && !m_FadedByPower)
    {
        m_FadedByPower = true;
        for (FlashlightWriting* writing : m_Writings)
        {
            if (writing != nullptr)
            {
                writing->SetTargetPresence(0.0f);
            }
        }
    }
    if (m_FadedByPower)
    {
        return false;
    }

    // まだ読んでいない文字を照らし続けた時間を数え、0.45秒続いたら読んだことにしている（途中で外れたら0から）
    for (int index = 0; index < Count; ++index)
    {
        FlashlightWriting* writing = m_Writings[static_cast<std::size_t>(index)];
        if (writing == nullptr || m_Read[static_cast<std::size_t>(index)])
        {
            continue;
        }
        float& timer = m_ReadTimers[static_cast<std::size_t>(index)];
        timer = writing->IsBeingRead(cameraPosition, cameraForward, flashlightOn)
            ? timer + deltaTime
            : 0.0f;
        m_Read[static_cast<std::size_t>(index)] = timer >= ReadHoldSeconds;
    }

    FlashlightWriting* turn = m_Writings[Turn];
    if (turn == nullptr)
    {
        return false;
    }

    // 読んだ後、プレイヤーが目を離した隙に書き換えている。見ている目の前では変えない。
    if (m_Read[Turn] && !m_TurnChanged)
    {
        Vector3 toWriting = turn->GetPosition() - cameraPosition;
        const float distance = toWriting.Length();
        const float facing = distance > 0.001f
            ? cameraForward.Dot(toWriting / distance)
            : 1.0f;
        if (facing <= LookAwayFacing || distance >= LookAwayDistance)
        {
            turn->ShowAlternate();
            m_TurnChanged = turn->IsShowingAlternate();
        }
        return false;
    }

    // 振り返って、書き換わった文字を読んだ瞬間を知らせている。
    if (m_TurnChanged && !m_TurnChangedRead)
    {
        m_TurnChangedReadTimer = turn->IsBeingRead(cameraPosition, cameraForward, flashlightOn)
            ? m_TurnChangedReadTimer + deltaTime
            : 0.0f;
        if (m_TurnChangedReadTimer >= ReadHoldSeconds)
        {
            m_TurnChangedRead = true;
            return true;
        }
    }
    return false;
}

// 読んだ文字の数を数えている
int Stage1WallWritings::GetReadCount() const
{
    return static_cast<int>(std::count(m_Read.begin(), m_Read.end(), true));
}
