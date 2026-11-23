// ============================================================================
// ファイルの役割: 雨粒の着水点を求め、水面の波のシミュレーションに入力する。
// ============================================================================

#include "Ground.h"
#include "Game.h"
#include "MathUtility.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

namespace
{
	// 雨の輪：範囲の端に残す余白、一辺96の正方形あたり1秒に落とす粒の数（強い雨）と、その正方形の半分の大きさ、
	// ためておく粒の数の上限、1フレームに出す輪の数の上限
	constexpr float RainAreaMargin = 2.0f;
	constexpr float RainDropsPerReferenceArea = 1600.0f;
	constexpr float RainReferenceHalfSize = 48.0f;
	constexpr float MaxPendingRainDrops = 400.0f;
	constexpr int MaxRainRingsPerFrame = 12;
	// 遠くの雨の輪を太く強くする：この距離までは近くと同じ、この幅で最大になる。最大のときの半径と強さの増し方
	constexpr float RainFarStart = 40.0f;
	constexpr float RainFarRange = 200.0f;
	constexpr float RainFarRadiusBoost = 1.4f;
	constexpr float RainFarStrengthBoost = 2.0f;
	// 1粒の輪の半径（最低＋乱数の幅）と強さ（最低＋乱数の幅。負は押し下げる）
	constexpr float RainRingRadius = 0.8f;
	constexpr float RainRingRadiusRange = 0.3f;
	constexpr float RainRingStrength = 0.3f;
	constexpr float RainRingStrengthRange = 0.25f;
	// 乱数（xorshift）の下位16ビットを 0〜1 にする割る数
	// 雨の乱数から下位16ビットを取り出し、同じ最大値で0〜1に変換する。
	constexpr unsigned int RainRandomMask = 0xFFFFu;
}

//=======================================
// 雨：カメラのまわりの地面に雨粒を落とし、水たまりに落ちたものだけを波のおもりにしている。
// 輪は波の式で広がるので、近くに落ちた輪どうしが重なって干渉し、岸で跳ね返る（作り物の輪の模様ではない）。
// 1フレームに足せるおもりの数には上限（16）があるので、足音の分を残して12個までにし、残りは次のフレームへ持ち越している
//=======================================
void Ground::AddRainRipples(float deltaTime)
{
	if (m_RainIntensity <= 0.0f)
	{
		m_PendingRainDrops = 0.0f;
		return;
	}
	// 雨粒を落とす範囲（カメラから縦横にこの距離まで）。波を計算している範囲（外の森は一辺576）の全体に落とし、
	// 範囲の端の水たまりにも、近くと同じように雨の輪を出している（端の2は、範囲の外へはみ出したおもりを捨てないための余白）
	const float DropArea = m_RippleSimulation.GetWorldSize() * 0.5f - RainAreaMargin;
	// 1秒に落とす雨粒の数。水たまりの外に落ちた分は捨てるので、輪になるのは水たまりの広さの割合だけになる。
	// 一辺96の正方形あたり1600粒（強い雨）を基準にし、範囲の面積に比例させて、1平方あたりの輪の数を一定にしている
	const float DropsPerSecond = RainDropsPerReferenceArea * (DropArea / RainReferenceHalfSize) * (DropArea / RainReferenceHalfSize);
	constexpr int MaxRingsPerFrame = MaxRainRingsPerFrame;
	// まだ落としていない雨粒の数をためている。重いフレームで一度に大量に落とさないよう、上限を400にしている
	m_PendingRainDrops = (std::min)(m_PendingRainDrops + DropsPerSecond * m_RainIntensity * deltaTime, MaxPendingRainDrops);
	const DirectX::SimpleMath::Vector3 eye = Core::Game::GetInstance()->GetCamera()->GetPosition();
	auto nextRandom = [this]()
	{
		// 簡単な乱数（xorshift）。見た目だけに使うので、決まった並びで十分
		MathUtility::NextXorShift32(m_RainSeed);
		return static_cast<float>(m_RainSeed & RainRandomMask) / static_cast<float>(RainRandomMask);
	};
	int rings = 0;
	while (m_PendingRainDrops >= 1.0f && rings < MaxRingsPerFrame)
	{
		m_PendingRainDrops -= 1.0f;
		const DirectX::SimpleMath::Vector3 drop(
			eye.x + (nextRandom() * 2.0f - 1.0f) * DropArea,
			m_Position.y,
			eye.z + (nextRandom() * 2.0f - 1.0f) * DropArea);
		if (!m_WaterEffects.IsInsidePuddle(drop))
		{
			continue;
		}
		// 水たまりに入った雨粒を、波のシミュレーションのおもりとして足している（水面を押し下げ、そこから輪が広がる）。
		// 足音（半径1.4・強さ1.2）より小さく弱い、細かい輪にしている（波のマス目が0.75なので、半径はそれより少し大きくしている）。
		// 1粒の強さは雨の強さで変えず、弱い雨は輪の数だけを減らしている（強さまで下げると、少し離れた所の輪が見えなくなるため）。
		// カメラから離れた雨粒ほど、輪を太く強くしている。細い輪のままだと、離れると1画素より細くなって見えなくなるため
		// （距離40までは近くと同じ、距離240より遠くで半径が2.4倍・強さが3倍。森は1メートル ≒ 24）
		const float distance = DirectX::SimpleMath::Vector2(drop.x - eye.x, drop.z - eye.z).Length();
		const float farRate = std::clamp((distance - RainFarStart) / RainFarRange, 0.0f, 1.0f);
		m_RippleSimulation.AddImpulse(DirectX::SimpleMath::Vector2(drop.x, drop.z),
			(RainRingRadius + nextRandom() * RainRingRadiusRange) * (1.0f + farRate * RainFarRadiusBoost),
			-(RainRingStrength + nextRandom() * RainRingStrengthRange) * (1.0f + farRate * RainFarStrengthBoost));
		++rings;
	}
}
