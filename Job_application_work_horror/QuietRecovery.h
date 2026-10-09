// ============================================================================
// ファイルの役割: 2面で、消灯して止まっていると影の気配を抑えられる「息を潜める」操作の進み具合を管理している。
// 主な技術: 続けた時間の計測、使った後の待ち時間（クールダウン）、成功の知らせ
// ============================================================================

// 描画や入力のAPIに依存しないため、単体で確かめられる。
#pragma once
#include <algorithm>

struct QuietRecovery
{
    // 2秒続ければ成功し、成功した後は10秒間使えない
    static constexpr float RequiredSeconds = 2.0f;
    static constexpr float CooldownSeconds = 10.0f;
    // 続けた秒数、次に使えるまでの残り秒数、成功の知らせの残り秒数、影が近すぎるか
    float progress = 0.0f;
    float cooldown = 0.0f;
    float successNotice = 0.0f;
    bool tooClose = false;

    // 最初の状態に戻している
    void Reset() { *this = QuietRecovery{}; }

    // 1フレーム分進めている。使える状況・止まっている・ライトが消えている・影が近くない、のすべてを満たす間だけ時間をため、
    // 1つでも崩れたら最初からやり直しにしている。成功したフレームだけtrueを返している
    bool Update(float deltaTime, bool eligible, bool stationary,
        bool lightOff, bool nearbyThreat)
    {
        // 止まっていた後の大きな経過時間で一気に進まないよう、0.1秒までにしている
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
