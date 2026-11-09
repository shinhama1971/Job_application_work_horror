// ============================================================================
// ファイルの役割: 外の森の、小道から研究所の入口までの小さな演出（灯りのちらつき・消える灯り）。
// 主な技術: プレイヤーの位置で起きる一度きりの出来事、時間から決まるちらつきの式、灯りの強さの倍率（Wall::SetEmissionScale）
//
// ・小道の街灯：近づくと、ときどき途切れるように明滅する。小道を北へ進んで街灯を通り過ぎると、
//   後ろの街灯が「カチッ」という音とともに1つずつ消える（振り返ると、来た道が暗くなっている）。
// ・研究所の2階の窓：研究所が見えてくる所まで来ると、ただ1つ明かりのついていた窓が、少しまたたいて消える
//   （中に誰かがいて、明かりを消したように見せる）。そのとき、恐怖で視界が狭まる画面効果を短く出す。
// ・研究所の玄関の灯り：研究所の正面まで来ると、不安定に明滅し始める。
// 灯りは配置ファイル（forest_props.txt）の名前で探しているので、Unity で動かしても演出はそのまま付いてくる。
// ============================================================================

#pragma once

#include "PropLayoutFile.h"

#include <SimpleMath.h>

#include <string>

class Wall;

class ForestEvents
{
public:
    // 毎フレーム呼ぶ。playerPosition はプレイヤーの足元。演出で画面効果を出す強さ（0〜1）を返している
    float Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition,
        const PropLayoutFile::SpawnedProps& props);

    // 演出の段階（デバッグ表示と確認用）
    int GetLampsTurnedOff() const { return m_LampsTurnedOff; }
    bool IsWindowOff() const { return m_WindowOffTimer >= 0.0f; }
    // 草むらを通る「見えない何か」の足元（x・z）と、押し倒す強さ（0 なら通っていない）。ForestScene が草（ForestGrass）へ渡している
    DirectX::SimpleMath::Vector2 GetWalkerPosition() const { return m_WalkerPosition; }
    float GetWalkerStrength() const { return m_WalkerStrength; }

private:
    // 消えていく街灯1つ分（いつ消え始めたか。まだなら負）
    struct DyingLamp
    {
        const char* Name;
        float StartTime = -1.0f;
        bool ClickPlayed = false;
    };

    // 配置ファイルの名前から、その灯りを探している（なければ nullptr）
    static Wall* Find(const PropLayoutFile::SpawnedProps& props, const char* name);
    // 時間 time での、ちらつきの明るさ（0〜1）。instability が大きいほど、途切れる回数が増える
    static float Flicker(float time, float seed, float instability);
    // 消えていく途中の明るさ（消え始めてからの秒数から。速くまたたいた後、0 になる）
    static float DyingBrightness(float elapsed, float seed);

    float m_Time = 0.0f;
    // 街灯：南の街灯（小道で先に通る）と北の街灯。北の街灯を通り過ぎると、南から順に消える
    DyingLamp m_Lamps[2] = { { "PathLampBulb_1" }, { "PathLampBulb" } };
    int m_LampsTurnedOff = 0;
    // 研究所の窓が消え始めてからの秒数（まだなら負）と、その音を鳴らしたか
    float m_WindowOffTimer = -1.0f;
    bool m_WindowClickPlayed = false;
    // 草むらを通る「見えない何か」：歩き始めてからの秒数（まだなら負）、歩き始めと終わりの位置、今の足元と強さ、次の足音までの秒数
    float m_WalkerTime = -1.0f;
    DirectX::SimpleMath::Vector2 m_WalkerStart;
    DirectX::SimpleMath::Vector2 m_WalkerEnd;
    DirectX::SimpleMath::Vector2 m_WalkerPosition;
    float m_WalkerStrength = 0.0f;
    float m_NextWalkerStep = 0.0f;
    // 恐怖の画面効果の残り（窓が消えたときに 1 にし、だんだん弱めている）
    float m_Fear = 0.0f;
};
