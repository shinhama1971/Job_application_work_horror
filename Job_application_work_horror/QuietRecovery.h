// 2面の「息を潜める」判定。描画や入力APIに依存しないため単体で検証できます。
#pragma once
#include <algorithm>

struct QuietRecovery
{
    static constexpr float RequiredSeconds = 2.0f;
    static constexpr float CooldownSeconds = 10.0f;
    float progress = 0.0f;
    float cooldown = 0.0f;
    float successNotice = 0.0f;
    bool tooClose = false;

    void Reset() { *this = QuietRecovery{}; }

    bool Update(float deltaTime, bool eligible, bool stationary,
        bool lightOff, bool nearbyThreat)
    {
        const float dt = (std::clamp)(deltaTime, 0.0f, 0.1f);
        cooldown = (std::max)(0.0f, cooldown - dt);
        successNotice = (std::max)(0.0f, successNotice - dt);
        tooClose = eligible && nearbyThreat;
        if (!eligible || !stationary || !lightOff || nearbyThreat || cooldown > 0.0f)
        {
            progress = 0.0f;
            return false;
        }
        progress = (std::min)(RequiredSeconds, progress + dt);
        if (progress < RequiredSeconds) return false;
        progress = 0.0f;
        cooldown = CooldownSeconds;
        successNotice = 2.5f;
        return true;
    }
};
