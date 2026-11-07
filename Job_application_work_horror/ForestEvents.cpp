// ============================================================================
// ファイルの役割: 外の森（試作）の灯りの演出（説明は ForestEvents.h を参照）。
// ============================================================================

#include "ForestEvents.h"

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

    float Hash(float value)
    {
        const float s = std::sin(value * 12.9898f) * 43758.5453f;
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
    const float burstWindow = std::floor(time * 0.6f + seed * 3.1f);
    const bool burst = Hash(burstWindow + seed) < 0.25f + instability * 0.45f;
    // 電球の小さなうなり（ほとんど分からない程度の明るさの揺れ）
    const float hum = 0.94f + 0.06f * std::sin(time * 47.0f + seed * 5.0f);
    if (!burst)
    {
        return hum;
    }
    // 不安定な時間の中では、1秒に14回、乱数で途切れるかを決めている（途切れたときも少しだけ光が残る）
    const float step = std::floor(time * 14.0f + seed * 7.0f);
    const bool dropout = Hash(step * 1.37f + seed) < instability * 0.55f;
    return dropout ? 0.06f + Hash(step + 0.5f) * 0.12f : hum * (0.75f + 0.25f * Hash(step + 2.0f));
}

float ForestEvents::DyingBrightness(float elapsed, float seed)
{
    // 0.5秒ほど激しくまたたいた後に消える
    if (elapsed >= 0.55f)
    {
        return 0.0f;
    }
    const float step = std::floor(elapsed * 22.0f);
    return Hash(step * 2.3f + seed) < 0.5f ? 0.05f : 1.0f;
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
    m_LampsTurnedOff = 0;
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
            if (!lamp.ClickPlayed && elapsed >= 0.55f)
            {
                lamp.ClickPlayed = true;
                game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, wall->GetPosition(), 0.55f, 0.9f);
            }
            if (elapsed >= 0.55f)
            {
                ++m_LampsTurnedOff;
            }
        }
        else
        {
            // 近づくほど不安定にしている（遠くからは、ほぼ普通の灯りに見える）
            const Vector2 offset(playerPosition.x - wall->GetPosition().x, playerPosition.z - wall->GetPosition().z);
            const float t = std::clamp((90.0f - offset.Length()) / 55.0f, 0.0f, 1.0f);
            const float instability = 0.15f + 0.85f * t * t * (3.0f - 2.0f * t);
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
                brightness = DyingBrightness(elapsed, 9.1f);
                if (!m_WindowClickPlayed && elapsed >= 0.55f)
                {
                    m_WindowClickPlayed = true;
                    game->PlayAudioCueAt(SOUND_CUE_FLASHLIGHT, window->GetPosition(), 0.70f, 1.0f);
                    m_Fear = 1.0f;
                }
            }
        }
        window->SetEmissionScale(brightness);
    }

    // --- 研究所の玄関の灯り：正面まで来ると不安定になる
    if (Wall* porch = Find(props, "LabPorchLight"))
    {
        const float instability = std::clamp((playerPosition.z - PorchUnstableZ) / 40.0f, 0.0f, 1.0f);
        porch->SetEmissionScale(instability > 0.0f ? Flicker(m_Time, 17.0f, 0.3f + instability * 0.6f) : 1.0f);
    }

    // --- 草むらを通る「見えない何か」：前方の草むらを、左から右へ横切る
    float walkerFear = 0.0f;
    if (m_WalkerTime < 0.0f && playerPosition.z > WalkerTriggerZ)
    {
        m_WalkerTime = 0.0f;
        m_NextWalkerStep = 0.0f;
        // 小道を北へ向いて歩いているので、北（+z）の少し先を、西（-x）から東（+x）へ、少し斜めに横切らせている
        m_WalkerStart = Vector2(playerPosition.x - 110.0f, playerPosition.z + WalkerAhead - 8.0f);
        m_WalkerEnd = Vector2(playerPosition.x + 130.0f, playerPosition.z + WalkerAhead + 12.0f);
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
        m_WalkerPosition = m_WalkerStart + along * progress + side * (std::sin(progress * 9.0f) * 4.0f);
        // 歩き始めと終わりは、押す力を弱めている（急に草が倒れ始めないように）
        m_WalkerStrength = std::clamp(progress * 8.0f, 0.0f, 1.0f) * std::clamp((1.0f - progress) * 8.0f, 0.0f, 1.0f);
        // 重い足音（足音を低くして使っている）を、その足元から鳴らしている
        m_NextWalkerStep -= deltaTime;
        if (m_NextWalkerStep <= 0.0f)
        {
            m_NextWalkerStep += WalkerStepInterval;
            game->PlayAudioCueAt(SOUND_CUE_FOOTSTEP, Vector3(m_WalkerPosition.x, playerPosition.y, m_WalkerPosition.y), 0.62f, 1.0f);
        }
        walkerFear = 0.30f * m_WalkerStrength;
    }

    // 恐怖の画面効果は、窓が消えた瞬間に強く出し、3秒ほどで消している
    m_Fear = (std::max)(0.0f, m_Fear - deltaTime / 3.0f);
    return (std::max)(m_Fear * m_Fear * 0.55f, walkerFear);
}
