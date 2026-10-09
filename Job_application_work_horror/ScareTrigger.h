// ============================================================================
// ファイルの役割: プレイヤーが範囲に入ったとき、一度だけ起きる驚かせる演出の条件と実行を管理している。
// 主な技術: 箱の範囲（AABB）に入ったかの判定、出来事の発生、二度起きないようにする仕組み
// ============================================================================

#pragma once

#include "Object.h"

// 見た目のない箱の範囲。プレイヤーが入ると影を出し、音・画面の乱れ・振動で驚かせている（一度きり）。
class ScareTrigger : public Object
{
private:
    // 範囲の大きさ（中心は位置）、影を出す位置、電力が戻った後にだけ起きるか、もう起きたか
    DirectX::SimpleMath::Vector3 m_Size =
        DirectX::SimpleMath::Vector3(10.0f, 10.0f, 10.0f);
    DirectX::SimpleMath::Vector3 m_ShadowPosition =
        DirectX::SimpleMath::Vector3::Zero;
    bool m_RequiresPower = false;
    bool m_HasTriggered = false;

    // プレイヤーが範囲の中にいるか
    bool IsPlayerInside() const;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    void Uninit() override;

    // 範囲の中心・大きさ、影を出す位置、電力が戻った後だけにするかを決めている
    void SetPosition(const DirectX::SimpleMath::Vector3& position)
    {
        m_Position = position;
    }

    void SetSize(const DirectX::SimpleMath::Vector3& size)
    {
        m_Size = size;
    }

    void SetShadowPosition(const DirectX::SimpleMath::Vector3& position)
    {
        m_ShadowPosition = position;
    }

    void SetRequiresPower(bool requiresPower)
    {
        m_RequiresPower = requiresPower;
    }
};
