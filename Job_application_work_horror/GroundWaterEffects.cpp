// ============================================================================
// ファイルの役割: 水たまりの位置、天井から落ちる水滴、着水と足音の波紋の作成と描画を管理している。
// 主な技術: 決まった数の使い回し（オブジェクトプール）、距離順の並べ替え、アルファ合成・加算合成、時間で薄れる表現
// ============================================================================

#include "Ground.h"
#include "MathUtility.h"

#include "Game.h"
#include "PostProcess.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
	// 水滴を落とす水たまりを探す範囲（プレイヤーのいるマス目から前後左右に何マスまで）
	constexpr int DropSearchCellRadius = 4;
	// --- 高さ：水滴が落ち始める天井の下の高さ（ここから乱数の分だけ下げる）、水滴が床に着く高さ、着水のしぶきの高さ、波紋を描く水面の高さ
	constexpr float DropStartY = -52.0f;
	constexpr float DropLandY = -99.35f;
	constexpr float SplashBaseY = -99.72f;
	constexpr float RippleSurfaceY = -99.96f;
	// 水たまりに落ちる水滴：数の上限、落ち始める高さのばらつき（最初・落ち直し）、水たまりの中心からずらす幅、
	// 落ちる速さ（最低＋乱数の幅）、最初に待つ秒数の幅、落ち直すまでに待つ秒数（最低＋乱数の幅）
	constexpr std::size_t MaxPuddleDrops = 24;
	constexpr float DropStartHeightRange = 22.0f;
	constexpr float DropRestartHeightRange = 14.0f;
	constexpr float DropSpread = 9.0f;
	constexpr float DropSpeed = 19.0f;
	constexpr float DropSpeedRange = 13.0f;
	constexpr float DropFirstWaitRange = 1.4f;
	constexpr float DropWaitSeconds = 0.65f;
	constexpr float DropWaitRange = 1.9f;
	// 遊ぶ範囲の中心（z が正の方へ寄っている）。水滴を落とす水たまりを、ここから近い順に選ぶ
	constexpr float GameplayCenterZ = 70.0f;
	// 着水の音：雨漏りの水滴はこの距離まで毎回、ほかの水滴はこの距離までを、間を空けて鳴らす
	constexpr float LeakDropHearDistance = 115.0f;
	constexpr float PuddleDropHearDistance = 70.0f;
	constexpr float DropSoundInterval = 0.25f;
	constexpr float DropSoundIntervalRange = 0.25f;
	// 着水：波紋を描く秒数、しぶきを飛ばす秒数、しぶきが広がる距離と跳ねる高さ
	constexpr float DropImpactSeconds = 0.55f;
	constexpr float SplashSeconds = 0.20f;
	constexpr float SplashSpread = 0.95f;
	constexpr float SplashHeight = 1.15f;
	// 足音の波紋：消えるまでの秒数・広がる半径・次に出せるまでの秒数（走る・歩く）、残す数の上限
	constexpr float SprintRippleSeconds = 1.35f;
	constexpr float WalkRippleSeconds = 1.15f;
	constexpr float SprintRippleRadius = 4.4f;
	constexpr float WalkRippleRadius = 3.2f;
	constexpr float SprintRippleInterval = 0.22f;
	constexpr float WalkRippleInterval = 0.36f;
	constexpr std::size_t MaxFootstepRipples = 8;
	// 走って水を蹴ったとき、画面にしぶきを付ける強さ・乾くまでの秒数、次に付けられるまでの秒数
	constexpr float LensSplashStrength = 0.85f;
	constexpr float LensSplashSeconds = 2.6f;
	constexpr float LensSplashInterval = 2.8f;
	// 雨漏りの水たまり：最初からある小さな濡れ跡の半径。雨漏り1か所の水滴の数
	constexpr float LeakStartRadius = 1.2f;
	constexpr int DropsPerLeak = 2;
	// 雨漏りの水滴：器具ごとの乱数（中心の x・z に掛ける倍率、2粒目をずらす量）、2粒目の位置のずらし（x・z）、
	// 落ち始める高さのばらつき、落ちる速さ（最低＋乱数の幅）、最初に待つ秒数（最低＋粒ごとにずらす秒数）
	constexpr float LeakSeedScaleX = 0.137f;
	constexpr float LeakSeedScaleZ = 0.311f;
	constexpr float LeakSeedStep = 0.47f;
	constexpr float LeakSecondDropOffsetX = 0.6f;
	constexpr float LeakSecondDropOffsetZ = -0.4f;
	constexpr float LeakDropHeightRange = 4.0f;
	constexpr float LeakDropSpeed = 24.0f;
	constexpr float LeakDropSpeedRange = 6.0f;
	constexpr float LeakDropFirstWait = 0.3f;
	constexpr float LeakDropWaitStep = 0.9f;

	// fastNoise.hlsli の FAST_HASH_COORDINATE_SCALE・FAST_HASH_DOT_OFFSET と同じ値。岸の形を CPU 判定と描画で揃える。
	constexpr float NoiseHashScale = 0.1031f;
	constexpr float NoiseHashShift = 33.33f;
	// wetFloorPS.hlsl の WATER_HASH21_FREQUENCY / WATER_HASH21_AMPLITUDE と同じ係数で、水滴用の乱数を作る。
	constexpr float Hash21FrequencyX = 127.1f;
	constexpr float Hash21FrequencyY = 311.7f;
	constexpr float HashAmplitude = 43758.5453f;
	// wetFloorPS.hlsl の WATER_HASH22_OFFSET_A / WATER_HASH22_OFFSET_B と同じずらしで、2つの乱数を作る。
	const Vector2 Hash22OffsetA(17.3f, 41.7f);
	const Vector2 Hash22OffsetB(93.1f, 11.8f);
	// マスに水たまりがあるかを決める乱数のずらし、水滴の種のずらし
	const Vector2 PuddlePresenceOffset(53.4f, 27.9f);
	const Vector2 DropSeedOffset(9.7f, 71.3f);
	// 水たまりの岸のゆがみ（puddleMask.hlsli の PUDDLE_SHORE_* と同じ）：ノイズの細かさとずらし、ゆがみの大きさ、
	// 角度に沿った2つの波（数・種の倍率・大きさ）
	constexpr float ShoreNoiseScale = 0.052f;
	constexpr float ShoreNoiseCellScale = 1.73f;
	constexpr float ShoreNoiseWarp = 0.22f;
	constexpr float ShoreWaveCountA = 5.0f;
	constexpr float ShoreWaveSeedA = 9.0f;
	constexpr float ShoreWaveAmountA = 0.045f;
	constexpr float ShoreWaveCountB = 9.0f;
	constexpr float ShoreWaveSeedB = 13.0f;
	constexpr float ShoreWaveAmountB = 0.022f;
	// 雨漏りの水たまりの岸（wetFloorPS.hlsl と同じ）：調べる範囲（半径の倍率）、ノイズの細かさとゆがみ、水たまりとみなす境
	constexpr float LeakShoreSearchScale = 1.075f;
	constexpr float LeakShoreNoiseScale = 0.35f;
	constexpr float LeakShoreWarp = 0.35f;
	constexpr float LeakShoreEdge = 0.90f;
	// 0で割らないための、半径と秒数の下限
	constexpr float MinimumRadius = 0.01f;
	constexpr float MinimumGrowSeconds = 0.01f;
	// この秒数より短い待ちなら、最初から落ちている
	constexpr float ImmediateDropWait = 0.01f;

	// --- 水滴の形（先が細い板の十字）：色（薄い青の半透明）、細い先・太い元の半分の幅、半分の高さ
	const Color DropVertexColor(0.62f, 0.76f, 0.80f, 0.80f);
	constexpr float DropTailWidth = 0.025f;
	constexpr float DropHeadWidth = 0.14f;
	constexpr float DropHalfHeight = 0.58f;
	// 水滴のマテリアル：薄い青の半透明で、光沢を強くしている
	const Color DropDiffuse(0.64f, 0.78f, 0.82f, 0.82f);
	const Color DropAmbient(0.32f, 0.42f, 0.46f, 0.82f);
	const Color DropEmission(0.065f, 0.10f, 0.115f, 0.0f);
	const Color DropSpecular(0.92f, 0.98f, 1.00f, 0.82f);
	constexpr float DropShininess = 42.0f;
	// 波紋の形：頂点の色、分割数、内側の半径（外側は1）
	const Color RippleVertexColor(0.62f, 0.78f, 0.82f, 0.72f);
	constexpr int RippleSegments = 32;
	constexpr float RippleInnerRadius = 0.76f;
	// 波紋のマテリアル（作るとき）
	const Color RippleInitDiffuse(0.34f, 0.48f, 0.52f, 0.70f);
	const Color RippleInitEmission(0.035f, 0.065f, 0.075f, 0.0f);
	const Color RippleInitSpecular(0.80f, 0.90f, 0.94f, 0.70f);
	constexpr float RippleInitShininess = 64.0f;
	// 描くときの波紋の色（薄れ具合を掛ける）と光沢
	const Vector3 RippleDiffuse(0.20f, 0.34f, 0.38f);
	const Vector3 RippleEmission(0.025f, 0.055f, 0.065f);
	const Vector3 RippleSpecular(0.65f, 0.78f, 0.82f);
	constexpr float RippleShininess = 72.0f;
	// しぶきの粒の大きさ
	const Vector3 SplashDropScale(0.28f, 0.34f, 0.28f);
	// 画面に映るかを調べる球の半径（落ちている水滴・しぶき・着水の波紋）と、足音の波紋に足すゆとり
	constexpr float FallingDropCullRadius = 2.0f;
	constexpr float SplashCullRadius = 3.0f;
	constexpr float ImpactRippleCullRadius = 4.0f;
	constexpr float FootstepRippleCullMargin = 1.0f;
	// 着水の波紋：始めの半径と広がる幅、強さ。足音の波紋：始めの半径と強さ
	constexpr float ImpactRippleStartRadius = 0.35f;
	constexpr float ImpactRippleGrowth = 2.5f;
	constexpr float ImpactRippleStrength = 0.82f;
	constexpr float FootstepRippleStartRadius = 0.55f;
	constexpr float FootstepRippleStrength = 0.35f;

	// --- 波のシミュレーションへ渡すおもり（半径と強さ。負は押し下げる）：水滴、走る足・歩く足
	constexpr float DropImpulseRadius = 0.8f;
	constexpr float DropImpulseStrength = -0.45f;
	constexpr float SprintImpulseRadius = 1.7f;
	constexpr float SprintImpulseStrength = -1.7f;
	constexpr float WalkImpulseRadius = 1.4f;
	constexpr float WalkImpulseStrength = -1.2f;
	// 着水の音（高さは 最低＋種の幅、音量）：雨漏りの水滴・水たまりに落ちるほかの水滴
	constexpr float LeakDropPitch = 0.94f;
	constexpr float LeakDropPitchRange = 0.12f;
	constexpr float LeakDropVolume = 0.75f;
	constexpr float PuddleDropPitch = 0.88f;
	constexpr float PuddleDropPitchRange = 0.30f;
	constexpr float PuddleDropVolume = 0.55f;
	// fastNoise.hlsli と同じ式で、足元でも水面の岸のゆがみを求める。
	float PuddleEdgeNoise(const Vector2& position)
	{
		const auto fraction = [](float value) { return value - std::floor(value); };
		const auto hash = [&fraction](const Vector2& cell)
		{
			Vector3 p(fraction(cell.x * NoiseHashScale), fraction(cell.y * NoiseHashScale), fraction(cell.x * NoiseHashScale));
			const float shift = p.Dot(Vector3(p.y, p.z, p.x) + Vector3(NoiseHashShift, NoiseHashShift, NoiseHashShift));
			p += Vector3(shift, shift, shift);
			return fraction((p.x + p.y) * p.z);
		};
		const Vector2 cell(std::floor(position.x), std::floor(position.y));
		const Vector2 fractionInCell = position - cell;
		const float x = MathUtility::SmoothCurve(fractionInCell.x);
		const float y = MathUtility::SmoothCurve(fractionInCell.y);
		return std::lerp(
			std::lerp(hash(cell), hash(cell + Vector2(1.0f, 0.0f)), x),
			std::lerp(hash(cell + Vector2(0.0f, 1.0f)), hash(cell + Vector2(1.0f, 1.0f)), x), y);
	}
}

// 水滴と波紋の形・マテリアルを作り、シェーダーと同じ計算で水たまりの位置を求め、水滴を落とす場所を決めている
void WaterEffectSystem::Init()
{
	m_FallingDrops.clear();
	m_Leaks.clear();
	m_DropVertices.clear();
	m_RippleVertices.clear();
	m_FallingDrops.reserve(MaxPuddleDrops);
	m_FootstepRipples.reserve(MaxFootstepRipples);

	// 先が細い板を十字に組み、どの角度から見ても小さな水滴に見える形にしている。
	const Color dropColor = DropVertexColor;
	const float tailWidth = DropTailWidth;
	const float headWidth = DropHeadWidth;
	const float halfHeight = DropHalfHeight;
	// 4頂点で四角形（三角形2つ）を追加している
	const auto addQuad = [this, &dropColor](
		const Vector3& a, const Vector3& b,
		const Vector3& c, const Vector3& d,
		const Vector3& normal)
	{
		const Vector3 positions[] = { a, b, c, c, b, d };
		for (const Vector3& position : positions)
		{
			VERTEX_3D vertex{};
			vertex.position = position;
			vertex.normal = normal;
			vertex.color = dropColor;
			m_DropVertices.push_back(vertex);
		}
	};

	addQuad(
		Vector3(-headWidth, -halfHeight, 0.0f),
		Vector3(-tailWidth, halfHeight, 0.0f),
		Vector3(headWidth, -halfHeight, 0.0f),
		Vector3(tailWidth, halfHeight, 0.0f),
		Vector3(0.0f, 0.0f, 1.0f));
	addQuad(
		Vector3(0.0f, -halfHeight, -headWidth),
		Vector3(0.0f, halfHeight, -tailWidth),
		Vector3(0.0f, -halfHeight, headWidth),
		Vector3(0.0f, halfHeight, tailWidth),
		Vector3(1.0f, 0.0f, 0.0f));

	m_DropVertexBuffer.Create(m_DropVertices);
	m_DropShader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	// 水滴のマテリアル：薄い青の半透明で、光沢を強くしている
	MATERIAL material{};
	material.Diffuse = DropDiffuse;
	material.Ambient = DropAmbient;
	material.Emission = DropEmission;
	material.Specular = DropSpecular;
	material.Shininess = DropShininess;
	material.TextureEnable = FALSE;
	m_DropMaterial = std::make_unique<Material>();
	m_DropMaterial->CreateRequired(material);

	// 波紋の形：内側の半径0.76・外側の半径1の細い輪を32分割で作っている（表と裏の両面）
	const Color rippleColor = RippleVertexColor;
	constexpr int rippleSegments = RippleSegments;
	constexpr float innerRadius = RippleInnerRadius;
	for (int segment = 0; segment < rippleSegments; ++segment)
	{
		const float angle0 = DirectX::XM_2PI *
			static_cast<float>(segment) / static_cast<float>(rippleSegments);
		const float angle1 = DirectX::XM_2PI *
			static_cast<float>(segment + 1) / static_cast<float>(rippleSegments);
		const Vector3 inner0(std::cos(angle0) * innerRadius, 0.0f,
			std::sin(angle0) * innerRadius);
		const Vector3 outer0(std::cos(angle0), 0.0f, std::sin(angle0));
		const Vector3 inner1(std::cos(angle1) * innerRadius, 0.0f,
			std::sin(angle1) * innerRadius);
		const Vector3 outer1(std::cos(angle1), 0.0f, std::sin(angle1));
		const Vector3 positions[] =
		{
			inner0, outer0, inner1, inner1, outer0, outer1,
			inner1, outer0, inner0, outer1, outer0, inner1
		};
		for (const Vector3& position : positions)
		{
			VERTEX_3D vertex{};
			vertex.position = position;
			vertex.normal = Vector3::Up;
			vertex.color = rippleColor;
			m_RippleVertices.push_back(vertex);
		}
	}
	m_RippleVertexBuffer.Create(m_RippleVertices);
	// 波紋のマテリアル（色の濃さは描くときに薄れ具合で毎回変えている）
	MATERIAL rippleMaterial{};
	rippleMaterial.Diffuse = RippleInitDiffuse;
	rippleMaterial.Emission = RippleInitEmission;
	rippleMaterial.Specular = RippleInitSpecular;
	rippleMaterial.Shininess = RippleInitShininess;
	rippleMaterial.TextureEnable = FALSE;
	m_RippleMaterial = std::make_unique<Material>();
	m_RippleMaterial->CreateRequired(rippleMaterial);

	// 水たまりは、床全体のマス目に乱数で置いている（1面）。2面は Ground::SetPuddleLayout で置き直している
	BuildPuddles(PuddleLayout{});
}

// 足元の判定とシェーダーで共有する水たまりの表、水滴を落とす場所を作る。
void WaterEffectSystem::BuildPuddles(const PuddleLayout& layout)
{
	m_FallingDrops.clear();
	m_PuddleCellData = PuddleCellBufferData{};
	m_PuddleCellData.GridOffset = Vector4(layout.GridOffset.x, layout.GridOffset.y, 0.0f, 0.0f);

	// 0〜1の疑似乱数。wetFloorPS.hlslの水たまりと同じ式を使い、見た目と判定の位置をそろえている
	const auto fraction = [](float value)
	{
		return value - std::floor(value);
	};
	const auto hash21 = [&fraction](float x, float y)
	{
		return fraction(std::sin(x * Hash21FrequencyX + y * Hash21FrequencyY) * HashAmplitude);
	};
	const auto hash22 = [&hash21](float x, float y)
	{
		return Vector2(
			hash21(x + Hash22OffsetA.x, y + Hash22OffsetA.y),
			hash21(x + Hash22OffsetB.x, y + Hash22OffsetB.y));
	};

	// 水滴を落とす場所の候補（水たまりがあるマス）
	struct DropCandidate
	{
		int CellX;
		int CellZ;
		Vector2 RandomValue;
		Vector2 PuddleCenter;
		float DistanceSquared;
	};

	// 描画する水滴の候補を1つ足している。中心はシェーダーと同じ式で求める。
	constexpr float cellSize = PuddleCellSize;
	std::vector<DropCandidate> candidates;
	const auto addPuddle = [&](int cellX, int cellZ, const Vector2& randomValue)
	{
		const Vector2 puddleCenter =
			(Vector2(static_cast<float>(cellX), static_cast<float>(cellZ)) +
			 Vector2(0.5f, 0.5f) +
			 (randomValue - Vector2(0.5f, 0.5f)) * PuddleCenterJitter) * cellSize - layout.GridOffset;
		// 遊ぶ範囲はZの正の方へ少し寄っているため、候補をその中心からの距離順に並べている。
		// 限られた数の水滴が、マップの端のマスだけで埋まらないようにしている。
		const Vector2 fromGameplayCenter =
			puddleCenter - Vector2(0.0f, GameplayCenterZ);
		candidates.push_back({
			cellX,
			cellZ,
			randomValue,
			puddleCenter,
			fromGameplayCenter.x * fromGameplayCenter.x +
			fromGameplayCenter.y * fromGameplayCenter.y });
	};

	if (layout.Cells.empty())
	{
		// 床全体のマス目について、水たまりがあるかと形の乱数を表にし、シェーダーへ渡している（Ground::Init）
		for (int row = 0; row < PuddleCellCount; ++row)
		{
			for (int column = 0; column < PuddleCellCount; ++column)
			{
				const float cellX = static_cast<float>(PuddleCellMin + column);
				const float cellZ = static_cast<float>(PuddleCellMin + row);
				const bool hasPuddle = hash21(cellX + PuddlePresenceOffset.x, cellZ + PuddlePresenceOffset.y) >= PuddleChance;
				const Vector2 randomValue = hash22(cellX, cellZ);
				m_PuddleCellData.Cells[row * PuddleCellCount + column] = Vector4(
					hasPuddle ? 1.0f : 0.0f, randomValue.x, randomValue.y, 0.0f);
			}
		}

		// 水滴はプレイヤーが通る範囲の水たまりから選ぶ。
		for (int cellZ = -DropSearchCellRadius; cellZ <= DropSearchCellRadius; ++cellZ)
		{
			for (int cellX = -DropSearchCellRadius; cellX <= DropSearchCellRadius; ++cellX)
			{
				const float puddleSeed = hash21(
					static_cast<float>(cellX) + PuddlePresenceOffset.x,
					static_cast<float>(cellZ) + PuddlePresenceOffset.y);
				if (puddleSeed < PuddleChance)
				{
					continue;
				}
				const Vector2 randomValue = hash22(
					static_cast<float>(cellX),
					static_cast<float>(cellZ));
				addPuddle(cellX, cellZ, randomValue);
			}
		}
	}
	else
	{
		// 決まったマス目にだけ水たまりを置いている（2面）。
		for (const Vector4& cell : layout.Cells)
		{
			const int cellX = static_cast<int>(cell.x);
			const int cellZ = static_cast<int>(cell.y);
			const int column = cellX - PuddleCellMin;
			const int row = cellZ - PuddleCellMin;
			if (column < 0 || column >= PuddleCellCount || row < 0 || row >= PuddleCellCount)
			{
				continue;
			}
			const Vector2 randomValue(cell.z, cell.w);
			m_PuddleCellData.Cells[row * PuddleCellCount + column] =
				Vector4(1.0f, randomValue.x, randomValue.y, 0.0f);
			addPuddle(cellX, cellZ, randomValue);
		}
	}

	std::sort(
		candidates.begin(),
		candidates.end(),
		[](const DropCandidate& left, const DropCandidate& right)
		{
			return left.DistanceSquared < right.DistanceSquared;
		});

	// 近い順に最大24個の水滴を、水たまりの真上から落としている（落ちる速さと待ち時間はマスごとに変えている）
	const size_t dropCount = (std::min)(candidates.size(), MaxPuddleDrops);
	for (size_t candidateIndex = 0; candidateIndex < dropCount; ++candidateIndex)
	{
		const DropCandidate& candidate = candidates[candidateIndex];
		const float cellX = static_cast<float>(candidate.CellX);
		const float cellZ = static_cast<float>(candidate.CellZ);
		const Vector2& randomValue = candidate.RandomValue;
		const Vector2& puddleCenter = candidate.PuddleCenter;

		FallingDrop drop{};
		drop.Seed = hash21(cellX + DropSeedOffset.x, cellZ + DropSeedOffset.y);
		drop.Position = Vector3(
			puddleCenter.x + (drop.Seed - 0.5f) * DropSpread,
			DropStartY - randomValue.y * DropStartHeightRange,
			puddleCenter.y + (randomValue.x - 0.5f) * DropSpread);
		drop.Speed = DropSpeed + drop.Seed * DropSpeedRange;
		drop.WaitTimer = randomValue.x * DropFirstWaitRange;
		drop.Active = drop.WaitTimer <= ImmediateDropWait;
		m_FallingDrops.push_back(drop);
	}
	// 雨漏りの水滴は、水たまりを置き直しても残している
	for (const Leak& leak : m_Leaks)
	{
		AddLeakDrops(leak);
	}
}

// 水滴を落とし、床に着いたら着水の時間を始め、少し待ってからまた上から落としている
void WaterEffectSystem::UpdateFallingDrops(float deltaTime)
{
	Core::Game* game = Core::Game::GetInstance();
	const Vector3 listener = game->GetCamera()->GetPosition();
	m_DropSoundCooldown = (std::max)(0.0f, m_DropSoundCooldown - deltaTime);
	for (FallingDrop& drop : m_FallingDrops)
	{
		drop.ImpactTimer = (std::max)(0.0f, drop.ImpactTimer - deltaTime);

		if (!drop.Active)
		{
			drop.WaitTimer -= deltaTime;
			if (drop.WaitTimer <= 0.0f)
			{
				drop.Active = true;
				drop.Position.y = DropStartY - drop.Seed * DropRestartHeightRange;
			}
			continue;
		}

		drop.Position.y -= drop.Speed * deltaTime;
		if (drop.Position.y <= DropLandY)
		{
			drop.Position.y = DropLandY;
			drop.Active = false;
			// 水たまりに落ちた水滴は、小さなおもりとして波を立てる
			if (IsInsidePuddle(drop.Position))
			{
				m_RippleImpulses.emplace_back(drop.Position.x, drop.Position.z, DropImpulseRadius, DropImpulseStrength);
				const float distanceSquared = Vector3::DistanceSquared(listener, drop.Position);
				if (drop.Audible && distanceSquared <= LeakDropHearDistance * LeakDropHearDistance)
				{
					// 雨漏りの水滴（雨漏り1か所につき1粒）：毎回、着水と同時に、着水した位置から鳴らしている
					game->PlayAudioCueAt(SOUND_CUE_WATER_DROP, drop.Position, LeakDropPitch + drop.Seed * LeakDropPitchRange, LeakDropVolume);
				}
				else if (!drop.Audible && m_DropSoundCooldown <= 0.0f && distanceSquared <= PuddleDropHearDistance * PuddleDropHearDistance)
				{
					// 水たまりに落ちるほかの水滴（近い水たまりの上から落ちる最大24粒）：距離70以内に落ちたものだけを、
					// 0.25〜0.5秒の間を空けて鳴らしている。位置付きの音の枠は16しかないので、続けて鳴らしすぎると、
					// 足音や影の音の枠を奪ってしまうため。
					// 雨漏りより少し小さく、粒ごとに高さを変えて、同じ音の繰り返しに聞こえないようにしている
					game->PlayAudioCueAt(SOUND_CUE_WATER_DROP, drop.Position, PuddleDropPitch + drop.Seed * PuddleDropPitchRange, PuddleDropVolume);
					m_DropSoundCooldown = DropSoundInterval + drop.Seed * DropSoundIntervalRange;
				}
			}
			drop.ImpactTimer = DropImpactSeconds;
			drop.WaitTimer = DropWaitSeconds + drop.Seed * DropWaitRange;
		}
	}
}

// その位置が水たまり（楕円）か、水に浸かった範囲の中かを返している
bool WaterEffectSystem::IsInsidePuddle(const Vector3& position) const
{
	if (IsInsideFloodRegion(position))
	{
		return true;
	}
	const Vector2 world(position.x, position.z);
	for (const Leak& leak : m_Leaks)
	{
		const float distance = (world - leak.Center).Length();
		if (distance <= leak.Radius * LeakShoreSearchScale &&
			distance / (std::max)(leak.Radius, MinimumRadius) +
				(PuddleEdgeNoise(world * LeakShoreNoiseScale + leak.Center) - 0.5f) * LeakShoreWarp <= LeakShoreEdge)
		{
			return true;
		}
	}
	// puddleMask.hlsli と同じ表、向き、岸のゆがみで、水面の濃さが半分以上の場所を水たまりとする。
	const Vector2 grid = (world + Vector2(m_PuddleCellData.GridOffset.x, m_PuddleCellData.GridOffset.y)) / PuddleCellSize;
	const Vector2 cell(std::floor(grid.x), std::floor(grid.y));
	const int column = static_cast<int>(cell.x) - PuddleCellMin;
	const int row = static_cast<int>(cell.y) - PuddleCellMin;
	if (column < 0 || row < 0 || column >= PuddleCellCount || row >= PuddleCellCount)
	{
		return false;
	}
	const Vector4& entry = m_PuddleCellData.Cells[row * PuddleCellCount + column];
	if (entry.x < PuddleCellPresentThreshold)
	{
		return false;
	}
	const Vector2 local = grid - cell - Vector2(0.5f, 0.5f) - Vector2(entry.y - 0.5f, entry.z - 0.5f) * PuddleCenterJitter;
	const float angle = entry.y * DirectX::XM_2PI;
	const float cosine = std::cos(angle);
	const float sine = std::sin(angle);
	const Vector2 rotated(local.x * cosine - local.y * sine, local.x * sine + local.y * cosine);
	const Vector2 normalized(rotated.x / (PuddleAxisX + entry.y * PuddleAxisXRange), rotated.y / (PuddleAxisZ + entry.z * PuddleAxisZRange));
	const float radius = normalized.Length();
	// 岸のノイズと波形の振れ幅の上限を使い、岸から離れたところではノイズを計算しない。
	if (std::abs(radius - PuddleShoreRadius) > PuddleShoreWarpMax)
	{
		return radius < PuddleShoreRadius;
	}
	const float polar = std::atan2(rotated.y, rotated.x);
	const float edgeWarp = (PuddleEdgeNoise(world * ShoreNoiseScale + cell * ShoreNoiseCellScale) - 0.5f) * ShoreNoiseWarp +
		std::sin(polar * ShoreWaveCountA + entry.y * ShoreWaveSeedA) * ShoreWaveAmountA +
		std::sin(polar * ShoreWaveCountB + entry.z * ShoreWaveSeedB) * ShoreWaveAmountB;
	return radius + edgeWarp <= PuddleShoreRadius;
}

// 足音の波紋を古くし、消える時間を過ぎたものを取り除いている
void WaterEffectSystem::UpdateFootstepRipples(float deltaTime)
{
	m_FootstepRippleCooldown = (std::max)(
		0.0f, m_FootstepRippleCooldown - deltaTime);
	m_LensSplashCooldown = (std::max)(
		0.0f, m_LensSplashCooldown - deltaTime);
	for (WaterRipple& ripple : m_FootstepRipples)
	{
		ripple.Age += deltaTime;
	}
	std::erase_if(m_FootstepRipples, [](const WaterRipple& ripple)
	{
		return ripple.Age >= ripple.Duration;
	});

	// 波紋を作るのは、Playerの足音のタイミングから呼んでいる。
	// これにより、壁へ向かって歩いた場合やフレームが落ちたときも、音と波紋がずれない。
}

// 足元が水の中なら波紋を1つ作り、たまにレンズに水しぶきが付いた効果を出している。
// 間隔を空けるために作らなかったときも、水の中ならtrueを返している（足音を水音にするため）
bool WaterEffectSystem::TriggerFootstepRipple(
	const Vector3& playerPosition,
	bool sprinting)
{
	const bool wet = IsInsidePuddle(playerPosition);
	if (m_FootstepRippleCooldown > 0.0f || !wet)
	{
		return wet;
	}

	WaterRipple ripple{};
	ripple.Position = Vector3(playerPosition.x, RippleSurfaceY, playerPosition.z);
	ripple.Duration = sprinting ? SprintRippleSeconds : WalkRippleSeconds;
	ripple.MaxRadius = sprinting ? SprintRippleRadius : WalkRippleRadius;
	m_FootstepRipples.push_back(ripple);
	// 足音の波紋は最大8個まで（古いものから消している）
	if (m_FootstepRipples.size() > MaxFootstepRipples)
	{
		m_FootstepRipples.erase(m_FootstepRipples.begin());
	}
	m_FootstepRippleCooldown = sprinting ? SprintRippleInterval : WalkRippleInterval;
	// 波のシミュレーションへ、足を下ろした所を押し下げるおもりを渡している（走っているほど大きく強い）。
	// おもりを小さく鋭くし、太い1本の波ではなく、細い輪が何重にも広がるようにしている（P.T. の水面を参考）
	m_RippleImpulses.emplace_back(playerPosition.x, playerPosition.z,
		sprinting ? SprintImpulseRadius : WalkImpulseRadius, sprinting ? SprintImpulseStrength : WalkImpulseStrength);

	// 走って水を強く蹴ったときだけ、跳ねた水が画面の下の方に小さく付く（歩いて踏んだ水は目の高さまで届かないので出さない）。
	// 毎歩では付けず、間隔を空けている。しぶきは crtOverlayPS.hlsl で、毎回違う位置に付き、数秒で流れ落ちて乾く
	if (sprinting && m_LensSplashCooldown <= 0.0f)
	{
		Core::Game::GetInstance()->GetPostProcess()->TriggerLensMoisture(LensSplashStrength, LensSplashSeconds);
		m_LensSplashCooldown = LensSplashInterval;
	}
	return true;
}

// 落ちている水滴を、半透明で描いている（画面に映っているものだけ）
void WaterEffectSystem::DrawFallingDrops(Camera* camera)
{
	if (m_DropVertices.empty() || m_DropMaterial == nullptr)
	{
		return;
	}

	camera->SetCamera();
	ID3D11DeviceContext* context = Renderer::GetDeviceContext();
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Renderer::SetBlendState(BS_ALPHABLEND);
	m_DropShader.SetGPU();
	m_DropVertexBuffer.SetGPU();
	m_DropMaterial->SetGPU();

	for (const FallingDrop& drop : m_FallingDrops)
	{
		if (!drop.Active ||
			!camera->IsSphereVisible(drop.Position, FallingDropCullRadius))
		{
			continue;
		}

		Matrix world = Matrix::CreateTranslation(drop.Position);
		Renderer::SetWorldMatrix(&world);
		context->Draw(static_cast<UINT>(m_DropVertices.size()), 0);
	}

	Renderer::SetBlendState(BS_NONE);
}

// 着水したところの、小さなしぶきと広がる波紋を描いている
void WaterEffectSystem::DrawWaterRipples(Camera* camera)
{
	if (m_RippleVertices.empty() || m_RippleMaterial == nullptr ||
		m_DropMaterial == nullptr)
	{
		return;
	}

	camera->SetCamera();
	ID3D11DeviceContext* context = Renderer::GetDeviceContext();
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_DropShader.SetGPU();

	Renderer::SetBlendState(BS_ALPHABLEND);
	m_DropVertexBuffer.SetGPU();
	m_DropMaterial->SetGPU();
	// 着水から0.2秒の間、4方向へ小さなしぶきを飛ばしている（水滴の形を小さくして使い回している）
	for (const FallingDrop& drop : m_FallingDrops)
	{
		const float impactAge = DropImpactSeconds - drop.ImpactTimer;
		if (drop.ImpactTimer <= 0.0f || impactAge > SplashSeconds ||
			!camera->IsSphereVisible(drop.Position, SplashCullRadius))
		{
			continue;
		}

		const float progress = impactAge / SplashSeconds;
		const float spread = progress * SplashSpread;
		const float height = std::sin(progress * DirectX::XM_PI) * SplashHeight;
		const Vector2 directions[] =
		{
			Vector2(1.0f, 0.0f), Vector2(-1.0f, 0.0f),
			Vector2(0.0f, 1.0f), Vector2(0.0f, -1.0f)
		};
		for (const Vector2& direction : directions)
		{
			const Vector3 splashPosition(
				drop.Position.x + direction.x * spread,
				SplashBaseY + height,
				drop.Position.z + direction.y * spread);
			Matrix world = Matrix::CreateScale(SplashDropScale) *
				Matrix::CreateTranslation(splashPosition);
			Renderer::SetWorldMatrix(&world);
			context->Draw(static_cast<UINT>(m_DropVertices.size()), 0);
		}
	}

	// 波紋は加算合成で、光が当たったように明るく重ねている
	Renderer::SetBlendState(BS_ADDITIVE);
	m_RippleVertexBuffer.SetGPU();
	// 1つの波紋を、広がり具合（progress）に合わせて大きく・薄くして描いている
	const auto drawRipple = [this, context](
		const Vector3& position,
		float progress,
		float radius,
		float strength)
	{
		const float fade = (1.0f - progress) * strength;
		MATERIAL material{};
		material.Diffuse = Color(
			RippleDiffuse.x * fade, RippleDiffuse.y * fade, RippleDiffuse.z * fade, fade);
		material.Emission = Color(
			RippleEmission.x * fade, RippleEmission.y * fade, RippleEmission.z * fade, 0.0f);
		material.Specular = Color(
			RippleSpecular.x * fade, RippleSpecular.y * fade, RippleSpecular.z * fade, fade);
		material.Shininess = RippleShininess;
		material.TextureEnable = FALSE;
		m_RippleMaterial->SetMaterial(material);
		m_RippleMaterial->SetGPU();

		Matrix world = Matrix::CreateScale(radius, 1.0f, radius) *
			Matrix::CreateTranslation(position);
		Renderer::SetWorldMatrix(&world);
		context->Draw(static_cast<UINT>(m_RippleVertices.size()), 0);
	};

	// 水滴が着水したところの波紋
	for (const FallingDrop& drop : m_FallingDrops)
	{
		if (drop.ImpactTimer <= 0.0f ||
			!camera->IsSphereVisible(drop.Position, ImpactRippleCullRadius))
		{
			continue;
		}
		const float progress = 1.0f - drop.ImpactTimer / DropImpactSeconds;
		drawRipple(
			Vector3(drop.Position.x, RippleSurfaceY, drop.Position.z),
			progress,
			ImpactRippleStartRadius + progress * ImpactRippleGrowth,
			ImpactRippleStrength);
	}

	// 足音の波紋（足を下ろした瞬間の小さな輪。そこから広がる波は、波のシミュレーションが描いている）
	for (const WaterRipple& ripple : m_FootstepRipples)
	{
		if (!camera->IsSphereVisible(ripple.Position, ripple.MaxRadius + FootstepRippleCullMargin))
		{
			continue;
		}
		const float progress = ripple.Age / ripple.Duration;
		drawRipple(
			ripple.Position,
			progress,
			FootstepRippleStartRadius + progress * ripple.MaxRadius,
			FootstepRippleStrength);
	}

	Renderer::SetBlendState(BS_NONE);
}

// 水滴と足音の波紋を更新している
void WaterEffectSystem::Update(float deltaTime)
{
	// 雨漏りの水たまりを、広がりきるまで少しずつ広げている（最初から小さな濡れ跡がある）
	for (Leak& leak : m_Leaks)
	{
		leak.Age += deltaTime;
		const float growth = (std::clamp)(leak.Age / (std::max)(leak.GrowSeconds, MinimumGrowSeconds), 0.0f, 1.0f);
		leak.Radius = LeakStartRadius + (leak.MaxRadius - LeakStartRadius) * MathUtility::SmoothCurve(growth);
	}
	UpdateFallingDrops(deltaTime);
	UpdateFootstepRipples(deltaTime);
}

// 水滴と波紋を描いている
void WaterEffectSystem::Draw(Camera* camera)
{
	DrawFallingDrops(camera);
	DrawWaterRipples(camera);
}

// 作ったものをすべて解放している
void WaterEffectSystem::Uninit()
{
	m_FallingDrops.clear();
	m_DropVertices.clear();
	m_DropMaterial.reset();
	m_RippleVertices.clear();
	m_FootstepRipples.clear();
	m_RippleMaterial.reset();
}

// Groundの関数は、水の効果の処理をそのまま呼んでいる
bool Ground::TriggerFootstepRipple(
	const Vector3& position,
	bool sprinting)
{
	// 水の中の一歩なら、その後の数歩に濡れた靴の跡を残している
	RecordFootstep(position);
	return m_WaterEffects.TriggerFootstepRipple(position, sprinting);
}

void Ground::SetPuddleLayout(const WaterEffectSystem::PuddleLayout& layout)
{
	m_WaterEffects.SetPuddleLayout(layout);
	// 水たまりのメッシュも、新しい置き方で作り直す
	m_PuddleSurfacesDirty = true;
	if (m_PuddleCellBuffer)
	{
		Renderer::GetDeviceContext()->UpdateSubresource(
			m_PuddleCellBuffer.Get(), 0, nullptr, &m_WaterEffects.GetPuddleCellData(), 0, 0);
	}
}

bool Ground::IsInsidePuddle(const Vector3& position) const
{
	return m_WaterEffects.IsInsidePuddle(position);
}

// 見えている水面（BuildPuddleSurfaces と同じ楕円）から margin 以内かを返している。
// 岸のノイズまで求める足元の判定と違い、草が水面に重ならないよう、メッシュ全体を覆う楕円を使う。
// 楕円の中心はマス目の中心から少しずれ、半径もマスの6割ほどあるため、まわりの3×3マスを調べている
bool Ground::IsNearVisiblePuddle(const Vector3& position, float margin) const
{
	const WaterEffectSystem::PuddleCellBufferData& data = m_WaterEffects.GetPuddleCellData();
	constexpr float cellSize = WaterEffectSystem::PuddleCellSize;
	constexpr float coverRadius = WaterEffectSystem::PuddleCoverRadius;
	const Vector2 cellPosition = (Vector2(position.x, position.z) + Vector2(data.GridOffset.x, data.GridOffset.y)) / cellSize;
	const int baseColumn = static_cast<int>(std::floor(cellPosition.x)) - WaterEffectSystem::PuddleCellMin;
	const int baseRow = static_cast<int>(std::floor(cellPosition.y)) - WaterEffectSystem::PuddleCellMin;
	for (int row = baseRow - 1; row <= baseRow + 1; ++row)
	{
		for (int column = baseColumn - 1; column <= baseColumn + 1; ++column)
		{
			if (row < 0 || column < 0 || row >= WaterEffectSystem::PuddleCellCount || column >= WaterEffectSystem::PuddleCellCount)
			{
				continue;
			}
			const Vector4& cell = data.Cells[static_cast<std::size_t>(row * WaterEffectSystem::PuddleCellCount + column)];
			if (cell.x < WaterEffectSystem::PuddleCellPresentThreshold)
			{
				continue;
			}
			const Vector2 randomValue(cell.y, cell.z);
			const float angle = randomValue.x * DirectX::XM_2PI;
			const Vector2 axes = Vector2(WaterEffectSystem::PuddleAxisX + randomValue.x * WaterEffectSystem::PuddleAxisXRange,
				WaterEffectSystem::PuddleAxisZ + randomValue.y * WaterEffectSystem::PuddleAxisZRange) * coverRadius;
			const Vector2 shift = (randomValue - Vector2(0.5f, 0.5f)) * WaterEffectSystem::PuddleCenterJitter;
			const Vector2 cellCenter(
				static_cast<float>(WaterEffectSystem::PuddleCellMin + column) + 0.5f,
				static_cast<float>(WaterEffectSystem::PuddleCellMin + row) + 0.5f);
			// マス目の単位の位置を、BuildPuddleSurfaces の回転の逆に回して、楕円の軸にそろえている
			const Vector2 local = cellPosition - (cellCenter + shift);
			const float cosine = std::cos(angle);
			const float sine = std::sin(angle);
			const float alongX = local.x * cosine - local.y * sine;
			const float alongY = local.x * sine + local.y * cosine;
			const float marginCells = margin / cellSize;
			const float nx = alongX / (axes.x + marginCells);
			const float ny = alongY / (axes.y + marginCells);
			if (nx * nx + ny * ny <= 1.0f)
			{
				return true;
			}
		}
	}
	return false;
}

void WaterEffectSystem::AddLeak(const Vector2& center, float maxRadius, float growSeconds)
{
	if (static_cast<int>(m_Leaks.size()) >= MaxLeakCount)
	{
		return;
	}
	Leak leak;
	leak.Center = center;
	leak.MaxRadius = maxRadius;
	leak.GrowSeconds = growSeconds;
	leak.Radius = LeakStartRadius;
	m_Leaks.push_back(leak);
	AddLeakDrops(leak);
}

// 雨漏りの真上（天井のすぐ下）から、ずらした間隔で落ち続ける水滴を2つ足している
void WaterEffectSystem::AddLeakDrops(const Leak& leak)
{
	for (int index = 0; index < DropsPerLeak; ++index)
	{
		FallingDrop drop{};
		drop.Seed = std::fmod(std::abs(leak.Center.x * LeakSeedScaleX + leak.Center.y * LeakSeedScaleZ) + index * LeakSeedStep, 1.0f);
		drop.Position = Vector3(
			leak.Center.x + (index == 0 ? 0.0f : LeakSecondDropOffsetX),
			DropStartY - drop.Seed * LeakDropHeightRange,
			leak.Center.y + (index == 0 ? 0.0f : LeakSecondDropOffsetZ));
		drop.Speed = LeakDropSpeed + drop.Seed * LeakDropSpeedRange;
		drop.WaitTimer = LeakDropFirstWait + index * LeakDropWaitStep;
		drop.Active = false;
		drop.Audible = index == 0;
		m_FallingDrops.push_back(drop);
	}
}

void Ground::AddLeak(const Vector2& center, float maxRadius, float growSeconds)
{
	m_WaterEffects.AddLeak(center, maxRadius, growSeconds);
}
