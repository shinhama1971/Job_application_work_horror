// ============================================================================
// ファイルの役割: 1面で、電力が戻った後に一度だけ起きる「天井裏の足音が、照明を消しながら迫ってくる」演出の進行。
// 主な技術: 有限状態機械（待機 → 迫る → 追いつかれた暗闇 → 照明が戻る）、天井裏を一直線に進む足音、
//           足音が通り過ぎた照明を消す合図、プレイヤーとの距離から求める脅威の度合い
//
// ・施設の南北の廊下（中央の廊下・中央ホール・北の角の天井の照明が一直線に並ぶ所）を歩いていると、
//   背後の天井裏から重い足音が近づいてくる。足音が真下を通り過ぎた照明は、カチッと音を立てて消える。
// ・足音は歩くより少し速いので、歩いたままだと追いつかれる。走るか、廊下から横の部屋へ逃げれば追いつかれない。
// ・追いつかれると、廊下の照明がすべて消え、真上で大きな音がする（失敗にはしない。驚かせるだけ）。
// ・足音が廊下の端まで行くか、追いつかれた後しばらくすると、消えた照明が1つずつ点き直す。
// 描画・音・照明に依存せず、「いつ・どこで・何をするか」の合図（Cue）だけを返している。照明の操作と音は StageScene がしている。
// ============================================================================

#pragma once

#include <SimpleMath.h>
#include "Stage1CeilingGeometry.h"

#include <vector>

class Stage1CeilingStalker final
{
public:
    // 足音が通る線の上にある照明（CeilingLightAt の番号と、置き場所）
    struct Fixture
    {
        int Number = 0;
        DirectX::SimpleMath::Vector3 Position;
    };

    // StageScene へ渡す合図の種類：足音を1歩鳴らす・照明を消す・追いつかれた・照明を点け直す
    enum class CueType
    {
        Step,
        LightOff,
        Caught,
        LightOn
    };

    struct Cue
    {
        CueType Type = CueType::Step;
        int LightNumber = 0;
        DirectX::SimpleMath::Vector3 Position;
        float Pitch = 1.0f;
    };

    // 足音の高さ（天井板のすぐ上。1面の物音の天井裏の足音と同じ）と、天井裏の足音の音量・こもらせ方
    static constexpr float CeilingVoidY = Stage1CeilingGeometry::VoidY;
    static constexpr float StepVolume = 2.1f;
    static constexpr float StepMinimumOcclusion = 0.72f;

    // 最初の状態に戻している（もう一度起きるようになる）
    void Reset();
    // 照明の線の上にいて、背後に足音を置けるなら始めている（始めたら true）。一度起きた後は始めない
    bool TryStart(const DirectX::SimpleMath::Vector3& playerPosition, const DirectX::SimpleMath::Vector3& forward,
        const std::vector<Fixture>& fixtures);
    // 1フレーム分進め、このフレームの合図を cues へ足している
    void Update(float deltaTime, const DirectX::SimpleMath::Vector3& playerPosition, std::vector<Cue>& cues);

    // 演出の途中か（迫っている間・暗闇の間・照明が戻るまで）。この間は、ほかの物音を鳴らさない
    bool IsActive() const { return m_State != State::Idle && m_State != State::Finished; }
    // 足音が迫っている、または追いつかれた直後で、心拍と恐怖の画面効果を強めるときの度合い（0〜1）
    float GetThreat() const;

private:
    enum class State
    {
        Idle,
        Approaching,
        Blackout,
        Waiting,
        Restoring,
        Finished
    };

    struct TrackedFixture
    {
        Fixture Light;
        bool Off = false;
    };

    void TurnOffAll(std::vector<Cue>& cues);

    State m_State = State::Idle;
    std::vector<TrackedFixture> m_Fixtures;
    // 足音の線の x、足音の今の位置（z）と進む向き（+1 か -1）、線の両端の z
    float m_AxisX = 0.0f;
    float m_StepZ = 0.0f;
    float m_Direction = 1.0f;
    float m_ChainMinZ = 0.0f;
    float m_ChainMaxZ = 0.0f;
    // 次の1歩までの秒数、何歩目か、今の段階の残り秒数、プレイヤーとの距離
    float m_StepTimer = 0.0f;
    int m_StepCount = 0;
    float m_StateTimer = 0.0f;
    float m_PlayerDistance = 0.0f;
};
