// ============================================================================
// ファイルの役割: 1面のループ廊下で、振り返ると廊下が伸びて見える演出の進行。
// （説明はStage1CorridorStretch.hを参照）
// ============================================================================

#include "Stage1CorridorStretch.h"

#include "MathUtility.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 伸びる場所：南北にまっすぐな廊下（両側の壁は x=±45、南のドアは z=37.5、北の角は z=235〜275）の中。
    // 南のドアからこの距離より奥にいて、北の角の手前にいるとき
    constexpr float CorridorHalfWidth = 40.0f;
    constexpr float CorridorMinZ = 90.0f;
    constexpr float CorridorMaxZ = 232.0f;
    // 振り返っているとみなす向き（視線の z が、これより小さい＝南のドアの方を向いている）
    constexpr float LookBackFacingZ = -0.75f;
    // 振り返っている間に、最後まで伸びるまでの秒数と、目を離してから元に戻るまでの秒数（戻る方を遅くしている）
    constexpr float GrowSeconds = 4.5f;
    constexpr float RelaxSeconds = 7.0f;
    // 周回ごとに伸ばせる量（倍率から1を引いた値。1周目・2周目・3周目以降）
    constexpr float FirstLoopAmount = 0.6f;
    constexpr float SecondLoopAmount = 1.0f;
    constexpr float LaterLoopAmount = 1.5f;
    // この伸び具合より小さいところから伸び始めたら、伸び始めの合図を出す
    constexpr float StartCueProgress = 0.02f;
}

void Stage1CorridorStretch::Reset()
{
    m_MaxAmount = 0.0f;
    m_Progress = 0.0f;
    m_Growing = false;
}

void Stage1CorridorStretch::SetLoopCount(int loopCount)
{
    if (loopCount <= 0)
    {
        m_MaxAmount = 0.0f;
    }
    else if (loopCount == 1)
    {
        m_MaxAmount = FirstLoopAmount;
    }
    else if (loopCount == 2)
    {
        m_MaxAmount = SecondLoopAmount;
    }
    else
    {
        m_MaxAmount = LaterLoopAmount;
    }
}

bool Stage1CorridorStretch::Update(float deltaTime, const Vector3& playerPosition, const Vector3& forward, bool allowed)
{
    const bool inCorridor =
        std::fabs(playerPosition.x) <= CorridorHalfWidth &&
        playerPosition.z >= CorridorMinZ && playerPosition.z <= CorridorMaxZ;
    const bool growing = allowed && m_MaxAmount > 0.0f && inCorridor && forward.z < LookBackFacingZ;

    const bool startCue = growing && !m_Growing && m_Progress < StartCueProgress;
    m_Growing = growing;
    if (growing)
    {
        m_Progress = (std::min)(1.0f, m_Progress + deltaTime / GrowSeconds);
    }
    else
    {
        m_Progress = (std::max)(0.0f, m_Progress - deltaTime / RelaxSeconds);
    }
    return startCue;
}

float Stage1CorridorStretch::GetStretchZ() const
{
    // 伸び始めと伸び終わりをなめらかにしている
    return 1.0f + m_MaxAmount * MathUtility::SmoothStep(0.0f, 1.0f, m_Progress);
}
