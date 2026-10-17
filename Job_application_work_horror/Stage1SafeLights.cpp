// ============================================================================
// ファイルの役割: 1面の「光の安全地帯」（停電しても消えない、暖かい色の電球の下）にいるかと、その効き目を管理している。
// 主な技術: 範囲の判定、なめらかに上がり下がりする落ち着きの度合い、描画・入力に依存しない状態クラス
// （説明はStage1SafeLights.hを参照）
// ============================================================================

#include "Stage1SafeLights.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

namespace
{
    // 落ち着きが上がりきるまでの秒数と、下がりきるまでの秒数
    constexpr float CalmRiseSeconds = 1.5f;
    constexpr float CalmFallSeconds = 0.8f;
    // 初めて入ったときの知らせを出す秒数
    constexpr float NoticeSeconds = 4.5f;
}

void Stage1SafeLights::Init(const std::vector<Zone>& zones)
{
    m_Zones = zones;
    m_Calm = 0.0f;
    m_Inside = false;
    m_NoticeShown = false;
    m_NoticeTimer = 0.0f;
}

float Stage1SafeLights::Update(float deltaTime, const Vector3& playerPosition,
    bool flashlightOn, float battery)
{
    m_NoticeTimer = (std::max)(0.0f, m_NoticeTimer - deltaTime);

    // どれかの灯りの円の中にいるか（高さは見ず、床の上の距離だけで判定している）
    m_Inside = std::any_of(m_Zones.begin(), m_Zones.end(), [&playerPosition](const Zone& zone)
    {
        const float x = playerPosition.x - zone.Center.x;
        const float z = playerPosition.z - zone.Center.z;
        return x * x + z * z <= zone.Radius * zone.Radius;
    });

    // 入るとゆっくり落ち着き、出るとそれより速く元に戻る
    m_Calm = m_Inside
        ? (std::min)(1.0f, m_Calm + deltaTime / CalmRiseSeconds)
        : (std::max)(0.0f, m_Calm - deltaTime / CalmFallSeconds);

    if (m_Inside && !m_NoticeShown)
    {
        m_NoticeShown = true;
        m_NoticeTimer = NoticeSeconds;
    }

    // 落ち着いた状態でライトを消して休んでいる間だけ、上限まで電池を戻している
    if (!m_Inside || flashlightOn || m_Calm < 1.0f || battery >= RechargeLimit)
    {
        return 0.0f;
    }
    return (std::min)(RechargePerSecond * deltaTime, RechargeLimit - battery);
}

std::string_view Stage1SafeLights::GetNoticeText() const
{
    if (m_NoticeTimer > 0.0f)
    {
        return "暖かい灯りの下は落ち着く ライトを消して休むと電池が戻る";
    }
    return {};
}
