// ============================================================================
// ファイルの役割: 2面の「壁の向こうのノック」異変に固有の状態（音の出どころ、ノックの間隔、
//                 聞き当てた時間、通知）を管理します。
// 主な技術: 乱数による出どころと間隔の決定、時間差で鳴らす音の予約、入力や描画に依存しない状態クラス
// 音を鳴らす処理と、プレイヤーの位置・向きの判定は Stage2Scene が担当します。
//
// 目で見て探す他の3つの異変と違い、立体音響だけを頼りに音の出どころを探します。
// 出どころは壁の裏にあるため、壁越しにこもった音になります（Game::ComputeSoundOcclusion）。
// ============================================================================

#pragma once

#include <algorithm>
#include <random>

class KnockingAnomaly final
{
public:
    // 音の出どころの候補数です。位置はStage2Sceneが持ちます（Stage2KnockSpots）。
    static constexpr int SpotCount = 4;
    // 出どころの壁の前で、壁の方を向いてこの秒数聞いていると見つけたことになります。
    static constexpr float RequiredListenSeconds = 1.0f;

private:
    // 周回が始まってから最初のノックまでの秒数です。
    static constexpr float FirstKnockDelay = 3.0f;

    std::mt19937 m_Random{ std::random_device{}() };
    int m_Spot = 0;
    int m_KnocksLeftInBurst = 0;
    float m_KnockTimer = FirstKnockDelay;
    float m_ListenSeconds = 0.0f;
    float m_NoticeTimer = 0.0f;
    bool m_Found = false;

    float RandomRange(float minimum, float maximum)
    {
        std::uniform_real_distribution<float> distribution(minimum, maximum);
        return distribution(m_Random);
    }

public:
    void Reset() noexcept
    {
        m_Spot = 0;
        m_KnocksLeftInBurst = 0;
        m_KnockTimer = FirstKnockDelay;
        m_ListenSeconds = 0.0f;
        m_NoticeTimer = 0.0f;
        m_Found = false;
    }

    // 周回の開始時に、音の出どころを選び直します。
    void ResetProgressForLoop()
    {
        std::uniform_int_distribution<int> spotDistribution(0, SpotCount - 1);
        m_Spot = spotDistribution(m_Random);
        m_KnocksLeftInBurst = 0;
        m_KnockTimer = FirstKnockDelay;
        m_ListenSeconds = 0.0f;
        m_Found = false;
    }

    // 時間を進め、このフレームにノックを1回鳴らすならtrueを返します。
    // 2〜3回続けて叩き、少し間を空けてまた叩く、を繰り返します。
    bool UpdateKnock(float deltaTime)
    {
        if (m_Found)
        {
            return false;
        }
        m_KnockTimer -= deltaTime;
        if (m_KnockTimer > 0.0f)
        {
            return false;
        }
        if (m_KnocksLeftInBurst <= 0)
        {
            m_KnocksLeftInBurst = 2 + static_cast<int>(RandomRange(0.0f, 1.99f));
        }
        --m_KnocksLeftInBurst;
        m_KnockTimer = m_KnocksLeftInBurst > 0
            ? RandomRange(0.30f, 0.46f)
            : RandomRange(2.2f, 3.6f);
        return true;
    }

    // 出どころの壁の前で耳を澄ませている間は時間をため、離れると最初からやり直しです。
    // 必要な時間に届いたフレームだけtrueを返します。
    bool UpdateListen(bool listening, float deltaTime) noexcept
    {
        if (m_Found)
        {
            return false;
        }
        m_ListenSeconds = listening ? m_ListenSeconds + deltaTime : 0.0f;
        if (m_ListenSeconds < RequiredListenSeconds)
        {
            return false;
        }
        m_Found = true;
        m_NoticeTimer = 2.6f;
        return true;
    }

    void UpdateNoticeTimer(float deltaTime) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
    }

    int GetSpot() const noexcept { return m_Spot; }
    bool IsListening() const noexcept { return m_ListenSeconds > 0.0f; }
    bool WasFound() const noexcept { return m_Found; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
