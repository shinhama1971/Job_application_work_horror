// ============================================================================
// ファイルの役割: 外の森の灯りの演出（説明は ForestEvents.h を参照）。
// ============================================================================

#include "ForestEvents.h"
#include "MathUtility.h"

#include "Game.h"
#include "Wall.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 北の街灯（z = -60）をこの距離だけ北へ過ぎたら、後ろの街灯が消え始める
    constexpr float LampPassDistance = 30.0f;
    // 2つ目の街灯は、1つ目が消えてから少し遅れて消える
    constexpr float SecondLampDelay = 1.4f;
    // 研究所の窓が消える所（研究所の正面の壁は z = 298。ここから先で、窓が正面に見えている）と、消えるまでの間
    constexpr float WindowTriggerZ = 140.0f;
    constexpr float WindowDelay = 0.7f;
    // 研究所の玄関の灯りが不安定になり始める所
    constexpr float PorchUnstableZ = 200.0f;
    // 草むらを通る「見えない何か」：歩き始める所（小道のこの z より北）、プレイヤーの何歩先を横切るか、横切る秒数、足音の間隔
    constexpr float WalkerTriggerZ = 40.0f;
    constexpr float WalkerAhead = 60.0f;
    constexpr float WalkerSeconds = 6.0f;
    constexpr float WalkerStepInterval = 0.62f;
    // 「見えない何か」の通り道：プレイヤーから西（-x）へこの距離の所から、東へこの距離の所まで、
    // 北へ（WalkerAhead から）この分だけずらして少し斜めに歩く。蛇行の回数と幅、歩き始めと終わりに押す力を弱める割合
    constexpr float WalkerStartWest = 110.0f;
    constexpr float WalkerEndEast = 130.0f;
    constexpr float WalkerStartNorthShift = -8.0f;
    constexpr float WalkerEndNorthShift = 12.0f;
    constexpr float WalkerWiggleCount = 9.0f;
    constexpr float WalkerWiggleWidth = 4.0f;
    constexpr float WalkerFadeRate = 8.0f;
    // 街灯：消える瞬間まで激しくまたたく秒数と、1秒あたりのまたたきの回数
    constexpr float LampDyingSeconds = 0.55f;
    constexpr float LampDyingFlickersPerSecond = 22.0f;
    // 街灯がこの距離より近いと不安定になり始め、この幅で最も不安定になる
    constexpr float LampUnstableDistance = 90.0f;
    constexpr float LampUnstableRange = 55.0f;
    // 研究所の玄関の灯りが、PorchUnstableZ からこの距離で最も不安定になる
    constexpr float PorchUnstableRange = 40.0f;
    // 恐怖の画面効果：窓が消えたときに強く出し、この秒数で消す。「見えない何か」が通っている間の強さ
    constexpr float FearFadeSeconds = 3.0f;
    constexpr float WalkerFear = 0.30f;

    // sin を使う決まった式で 0〜1 の値を作る係数
    constexpr float HashFrequency = 12.9898f;
    constexpr float HashAmplitude = 43758.5453f;
    // 不安定な時間：来る速さ（1秒あたり）と灯ごとのずらし、来る確率（最低＋不安定さの幅）
    constexpr float BurstRate = 0.6f;
    constexpr float BurstSeedScale = 3.1f;
    constexpr float BurstChance = 0.25f;
    constexpr float BurstChanceRange = 0.45f;
    // 電球の小さなうなり（中心・幅・速さ・灯ごとのずらし）
    constexpr float HumCenter = 0.94f;
    constexpr float HumDepth = 0.06f;
    constexpr float HumRate = 47.0f;
    constexpr float HumSeedScale = 5.0f;
    // 不安定な時間の中で、途切れるかを決める回数（1秒あたり）と灯ごとのずらし、途切れる確率の倍率
    constexpr float DropoutStepsPerSecond = 14.0f;
    constexpr float DropoutSeedScale = 7.0f;
    constexpr float DropoutHashScale = 1.37f;
    constexpr float DropoutChanceScale = 0.55f;
    // 途切れたときに残る光（最低＋乱数の幅）、途切れないときの明るさ（最低＋乱数の幅）。乱数を引くときのずらし
    constexpr float DropoutGlow = 0.06f;
    constexpr float DropoutGlowRange = 0.12f;
    constexpr float SteadyGlow = 0.75f;
    constexpr float SteadyGlowRange = 0.25f;
    constexpr float DropoutGlowHashOffset = 0.5f;
    constexpr float SteadyGlowHashOffset = 2.0f;
    // 消える前の激しいまたたき：乱数の倍率、消えている間の明るさ
    constexpr float DyingHashScale = 2.3f;
    constexpr float DyingOffBrightness = 0.05f;
    // 灯が消える瞬間の「カチッ」（懐中電灯のスイッチの音を低くして使う。高さと音量）：街灯、窓の明かり
    constexpr float LampClickPitch = 0.55f;
    constexpr float LampClickVolume = 0.9f;
    constexpr float WindowClickPitch = 0.70f;
    constexpr float WindowClickVolume = 1.0f;
    // 近づいたときの街灯の不安定さ（最低＋近さの幅）
    constexpr float LampMinInstability = 0.15f;
    constexpr float LampInstabilityRange = 0.85f;
    // 窓の明かりの乱数の種、玄関の灯の乱数の種と不安定さ（最低＋近さの幅）
    constexpr float WindowSeed = 9.1f;
    constexpr float PorchSeed = 17.0f;
    constexpr float PorchMinInstability = 0.3f;
    constexpr float PorchInstabilityRange = 0.6f;
    // 「見えない何か」の足音（高さと音量）
    constexpr float WalkerStepPitch = 0.70f;
    constexpr float WalkerStepVolume = 1.0f;
    // 灯が消えた後の恐怖の強さ（2乗に掛ける割合）
    constexpr float LampFearScale = 0.55f;

    float Hash(float value)
    {
        const float s = std::sin(value * HashFrequency) * HashAmplitude;
        return s - std::floor(s);
    }
}

Wall* ForestEvents::Find(const PropLayoutFile::SpawnedProps& props, const char* name)
{
    for (const auto& [propName, wall] : props)
    {
        if (propName == name)
        {
            return wall;
        }
    }
    return nullptr;
}

float ForestEvents::Flicker(float time, float seed, float instability)
{
    // ちらつきは、ときどき来る「不安定な時間」の中だけで起きる（ずっとまたたくと、ただの点滅に見えるため）
    const float burstWindow = std::floor(time * BurstRate + seed * BurstSeedScale);
    const bool burst = Hash(burstWindow + seed) < BurstChance + instability * BurstChanceRange;
    // 電球の小さなうなり（ほとんど分からない程度の明るさの揺れ）
    const float hum = HumCenter + HumDepth * std::sin(time * HumRate + seed * HumSeedScale);
    if (!burst)
    {
        return hum;
    }
    // 不安定な時間の中では、1秒に14回、乱数で途切れるかを決めている（途切れたときも少しだけ光が残る）
    const float step = std::floor(time * DropoutStepsPerSecond + seed * DropoutSeedScale);
    const bool dropout = Hash(step * DropoutHashScale + seed) < instability * DropoutChanceScale;
    return dropout
        ? DropoutGlow + Hash(step + DropoutGlowHashOffset) * DropoutGlowRange
        : hum * (SteadyGlow + SteadyGlowRange * Hash(step + SteadyGlowHashOffset));
}

float ForestEvents::DyingBrightness(float elapsed, float seed)
{
    // 0.5秒ほど激しくまたたいた後に消える
    if (elapsed >= LampDyingSeconds)
    {
        return 0.0f;
    }
    const float step = std::floor(elapsed * LampDyingFlickersPerSecond);
    return Hash(step * DyingHashScale + seed) < 0.5f ? DyingOffBrightness : 1.0f;
}

float ForestEvents::Update(float deltaTime, const Vector3& playerPosition, const PropLayoutFile::SpawnedProps& props)
{
    m_Time += deltaTime;
    Core::Game* game = Core::Game::GetInstance();

    // --- 小道の街灯：近いほど不安定にまたたき、通り過ぎると後ろから消える
    Wall* northLamp = Find(props, m_Lamps[1].Name);
    if (northLamp != nullptr && m_Lamps[0].StartTime < 0.0f &&
        playerPosition.z > northLamp->GetPosition().z + LampPassDistance)
    {
        m_Lamps[0].StartTime = m_Time;
        m_Lamps[1].StartTime = m_Time + SecondLampDelay;
    }
    for (int index = 0; index < 2; ++index)
    {
        DyingLamp& lamp = m_Lamps[index];
        Wall* wall = Find(props, lamp.Name);
        if (wall == nullptr)
        {
            continue;
        }
        const float seed = static_cast<float>(index) * 4.7f + 1.3f;
        float brightness = 1.0f;
        if (lamp.StartTime >= 0.0f && m_Time >= lamp.StartTime)
        {
            const float elapsed = m_Time - lamp.StartTime;
            brightness = DyingBrightness(elapsed, seed);
            // 消える瞬間に、電球の位置から「カチッ」と鳴らしている（懐中電灯のスイッチの音を低くして使っている）
            if (!lamp.ClickPlayed && elapsed >= LampDyingSeconds)
            {
                lamp.ClickPlayed = true;
                game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, wall->GetPosition(), LampClickPitch, LampClickVolume);
            }
        }
        else
        {
            // 近づくほど不安定にしている（遠くからは、ほぼ普通の灯りに見える）
            const Vector2 offset(playerPosition.x - wall->GetPosition().x, playerPosition.z - wall->GetPosition().z);
            const float t = std::clamp((LampUnstableDistance - offset.Length()) / LampUnstableRange, 0.0f, 1.0f);
            const float instability = LampMinInstability + LampInstabilityRange * MathUtility::SmoothCurve(t);
            brightness = Flicker(m_Time, seed, instability);
        }
        wall->SetEmissionScale(brightness);
    }

    // --- 研究所の2階の窓：研究所が見えてくると、またたいて消える
    if (m_WindowOffTimer < 0.0f && playerPosition.z > WindowTriggerZ)
    {
        m_WindowOffTimer = 0.0f;
    }
    if (Wall* window = Find(props, "LabWindow2F4"))
    {
        float brightness = 1.0f;
        if (m_WindowOffTimer >= 0.0f)
        {
            m_WindowOffTimer += deltaTime;
            const float elapsed = m_WindowOffTimer - WindowDelay;
            if (elapsed >= 0.0f)
            {
                brightness = DyingBrightness(elapsed, WindowSeed);
                if (!m_WindowClickPlayed && elapsed >= LampDyingSeconds)
                {
                    m_WindowClickPlayed = true;
                    game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, window->GetPosition(), WindowClickPitch, WindowClickVolume);
                    m_Fear = 1.0f;
                }
            }
        }
        window->SetEmissionScale(brightness);
    }

    // --- 研究所の玄関の灯り：正面まで来ると不安定になる
    if (Wall* porch = Find(props, "LabPorchLight"))
    {
        const float instability = std::clamp((playerPosition.z - PorchUnstableZ) / PorchUnstableRange, 0.0f, 1.0f);
        porch->SetEmissionScale(instability > 0.0f
            ? Flicker(m_Time, PorchSeed, PorchMinInstability + instability * PorchInstabilityRange) : 1.0f);
    }

    // --- 草むらを通る「見えない何か」：前方の草むらを、左から右へ横切る
    float walkerFear = 0.0f;
    if (m_WalkerTime < 0.0f && playerPosition.z > WalkerTriggerZ)
    {
        m_WalkerTime = 0.0f;
        m_NextWalkerStep = 0.0f;
        // 小道を北へ向いて歩いているので、北（+z）の少し先を、西（-x）から東（+x）へ、少し斜めに横切らせている
        m_WalkerStart = Vector2(playerPosition.x - WalkerStartWest, playerPosition.z + WalkerAhead + WalkerStartNorthShift);
        m_WalkerEnd = Vector2(playerPosition.x + WalkerEndEast, playerPosition.z + WalkerAhead + WalkerEndNorthShift);
    }
    m_WalkerStrength = 0.0f;
    if (m_WalkerTime >= 0.0f && m_WalkerTime < WalkerSeconds)
    {
        m_WalkerTime += deltaTime;
        const float progress = std::clamp(m_WalkerTime / WalkerSeconds, 0.0f, 1.0f);
        // 少し蛇行しながら歩いている（まっすぐだと機械的に見えるため）
        const Vector2 along = m_WalkerEnd - m_WalkerStart;
        Vector2 side(-along.y, along.x);
        side.Normalize();
        m_WalkerPosition = m_WalkerStart + along * progress + side * (std::sin(progress * WalkerWiggleCount) * WalkerWiggleWidth);
        // 歩き始めと終わりは、押す力を弱めている（急に草が倒れ始めないように）
        m_WalkerStrength = std::clamp(progress * WalkerFadeRate, 0.0f, 1.0f) * std::clamp((1.0f - progress) * WalkerFadeRate, 0.0f, 1.0f);
        // 重い足音（濡れた道の足音を低くして使っている）を、その足元から鳴らしている
        m_NextWalkerStep -= deltaTime;
        if (m_NextWalkerStep <= 0.0f)
        {
            m_NextWalkerStep += WalkerStepInterval;
            game->PlayAudioCueAt(SOUND_CUE_FOREST_STEP, Vector3(m_WalkerPosition.x, playerPosition.y, m_WalkerPosition.y),
                WalkerStepPitch, WalkerStepVolume);
        }
        walkerFear = WalkerFear * m_WalkerStrength;
    }

    // 恐怖の画面効果は、窓が消えた瞬間に強く出し、3秒ほどで消している
    m_Fear = (std::max)(0.0f, m_Fear - deltaTime / FearFadeSeconds);
    return (std::max)(m_Fear * m_Fear * LampFearScale, walkerFear);
}
