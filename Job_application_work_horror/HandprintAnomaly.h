// ============================================================================
// ファイルの役割: 2面の「血の手形」の異変に固有の、手形がいくつ現れたか・照らした時間・見つけたかを管理している。
// 主な技術: 有限状態機械（1つ照らすと次が現れる）、照らし続けた時間の計測、少しずつ浮かぶ見え方の度合い
// 照らしているかの判定と、壁のシェーダーへの手形の受け渡し（Renderer::SetWallHandprints）は Stage2Scene が担当している。
//
// ・この周回では、廊下の壁に血の手形が1つだけ浮かんでいる。懐中電灯で照らすと、奥の反対側の壁に次の手形が現れる。
//   左右の壁を交互にたどり、4つ目まで照らすと異変を見つけたことになる。
// ・ほかの異変（照らして目を離す・ライトを消して見る・見つめ続ける・音を探す）と違い、「光で跡をたどる」操作にしている。
// ・手形は、1面の壁の血の跡と同じ形の式（horrorMarks.hlsli の HorrorHandprint）で描いている。
// ============================================================================

#pragma once

#include <SimpleMath.h>

#include <algorithm>
#include <array>

class HandprintAnomaly final
{
public:
    // 手形の数と、次の手形が現れるまでに照らし続ける秒数、現れるのにかかる秒数
    static constexpr int PrintCount = 4;
    static constexpr float RequiredLitSeconds = 0.5f;
    static constexpr float AppearSeconds = 0.8f;

    // 手形の置き場所（壁の表面の中心）。左右の壁を交互に、奥の出口へ向かって並べている。
    // x は壁の表面（左 -40、右 40）、y は高さ、z は廊下の奥行き。
    static constexpr std::array<DirectX::XMFLOAT3, PrintCount> Positions =
    { {
        { -40.0f, -70.0f, -95.0f },
        { 40.0f, -74.0f, -50.0f },
        { -40.0f, -68.0f, 0.0f },
        { 40.0f, -71.0f, 88.0f },
    } };

    // Updateの結果（何も起きていない・次の手形が現れた・最後の手形を照らして見つけた）
    enum class Event
    {
        None,
        NextAppeared,
        Found
    };

private:
    // 現れている手形の数、手形ごとの見え方（0〜1）、今の手形を照らし続けている秒数、見つけたか、通知の残り秒数
    int m_RevealedCount = 0;
    std::array<float, PrintCount> m_Visibility{};
    float m_LitSeconds = 0.0f;
    bool m_Found = false;
    float m_NoticeTimer = 0.0f;

public:
    // 何も起きていない状態に戻している
    void Reset() noexcept
    {
        ResetProgressForLoop();
        m_NoticeTimer = 0.0f;
    }

    // 周回の始まりに、手形をすべて消して最初からにしている
    void ResetProgressForLoop() noexcept
    {
        m_RevealedCount = 0;
        m_Visibility.fill(0.0f);
        m_LitSeconds = 0.0f;
        m_Found = false;
    }

    // 1フレーム分進めている。activeはこの周回が手形の異変か、litLatestは最後に現れた手形を照らしているか
    Event Update(float deltaTime, bool active, bool litLatest) noexcept
    {
        m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);
        if (!active)
        {
            return Event::None;
        }
        // 周回が始まったら、最初の手形を浮かべている
        if (m_RevealedCount == 0)
        {
            m_RevealedCount = 1;
        }
        for (int index = 0; index < m_RevealedCount; ++index)
        {
            float& visibility = m_Visibility[static_cast<std::size_t>(index)];
            visibility = (std::min)(1.0f, visibility + deltaTime / AppearSeconds);
        }
        if (m_Found)
        {
            return Event::None;
        }

        // 最後に現れた手形が浮かびきってから照らし続けると、次の手形が現れる（4つ目なら見つけた）
        const float latestVisibility = m_Visibility[static_cast<std::size_t>(m_RevealedCount - 1)];
        m_LitSeconds = (litLatest && latestVisibility >= 0.8f) ? m_LitSeconds + deltaTime : 0.0f;
        if (m_LitSeconds < RequiredLitSeconds)
        {
            return Event::None;
        }
        m_LitSeconds = 0.0f;
        if (m_RevealedCount < PrintCount)
        {
            ++m_RevealedCount;
            return Event::NextAppeared;
        }
        m_Found = true;
        m_NoticeTimer = 2.6f;
        return Event::Found;
    }

    // 現れている手形の数、手形の見え方（0〜1）、最後に現れた手形の位置、見つけたか、手形をたどっている途中か、通知の残り秒数
    int GetRevealedCount() const noexcept { return m_RevealedCount; }
    float GetVisibility(int index) const noexcept { return m_Visibility[static_cast<std::size_t>(index)]; }
    DirectX::SimpleMath::Vector3 GetLatestPosition() const noexcept
    {
        const DirectX::XMFLOAT3& position =
            Positions[static_cast<std::size_t>((std::max)(m_RevealedCount, 1) - 1)];
        return DirectX::SimpleMath::Vector3(position.x, position.y, position.z);
    }
    bool WasFound() const noexcept { return m_Found; }
    bool IsFollowing() const noexcept { return m_RevealedCount >= 2 && !m_Found; }
    float GetNoticeTimer() const noexcept { return m_NoticeTimer; }
};
