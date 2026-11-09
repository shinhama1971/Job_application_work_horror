// ============================================================================
// ファイルの役割: タイトル画面や導入（受信記録）の後ろに映す、3Dの外の森。
// 主な技術: 森の場面と同じ作り方（ForestScene::BuildWorld）、入力を読まないプレイヤー（Player::SetAttractMode）、
//           場面の表（始めと終わりの位置・向き・秒数）をなめらかに補間して動かすカメラ、森の演出（ForestEvents）
//
// ・場面の表を渡すと、ゆっくり進むカメラで順に映す。loop が true なら最後の場面の後に最初へ戻る（タイトル）。
//   false なら最後の場面の終わりで止まる（導入）。
// ・場面が変わるたびに森の演出を作り直すので、街灯が消える・窓の明かりが消える・草むらを何かが通る、がまた起きる。
// ・場面が変わった瞬間の「乱れの強さ」（GetCutNoise）を、使う側が題名や画面の電波障害に使っている。
// ・Uninit で、森の画面効果・視点の操作を普段に戻し、作ったObjectを破棄している。
// ============================================================================

#pragma once

#include "ForestEvents.h"
#include "PropLayoutFile.h"

#include <SimpleMath.h>

#include <string>
#include <vector>

class Player;
class ForestGrass;

class ForestBackdrop
{
public:
    // 映す場面1つ分：足元の位置・水平の向き（+Z が0）・上下の向きを、始めから終わりへ、なめらかに動かしている
    struct Shot
    {
        DirectX::SimpleMath::Vector3 From;
        DirectX::SimpleMath::Vector3 To;
        float YawFrom;
        float YawTo;
        float PitchFrom;
        float PitchTo;
        float Seconds;
    };

    // 森を作り、最初の場面のカメラに合わせている
    void Init(const std::vector<Shot>& shots, bool loop);
    // カメラを動かし、森の演出と草むらを通る「見えない何か」を進めている
    void Update(float deltaTime);
    // 森の画面効果・視点の操作・画面の乱れを普段に戻し、森を破棄している
    void Uninit();

    // 場面が変わった瞬間の乱れの強さ（1から0.6秒で0へ弱める）
    float GetCutNoise() const { return m_CutNoise; }

private:
    std::vector<Shot> m_Shots;
    bool m_Loop = true;
    // 作ったObjectの名前（終わるときに破棄する）、プレイヤー（カメラと懐中電灯）、草、配置ファイルから作った小物、森の演出
    std::vector<std::string> m_ObjectNames;
    Player* m_Player = nullptr;
    ForestGrass* m_Grass = nullptr;
    PropLayoutFile::SpawnedProps m_Props;
    ForestEvents m_Events;
    // 今映している場面の番号と、その場面に入ってからの秒数、全体の経過秒（手持ちカメラの揺れ）、場面の切り替えの乱れ
    int m_ShotIndex = 0;
    float m_ShotTime = 0.0f;
    float m_Time = 0.0f;
    float m_CutNoise = 1.0f;

    // 今の場面のカメラの位置と向きを決めている
    void ApplyCamera();
};
