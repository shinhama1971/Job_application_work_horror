// ============================================================================
// ファイルの役割: 1面の西棟の排水バルブ（壁の配管に付いたハンドル）の、回した量と見た目を管理している。
// 主な技術: Interactableインターフェース、長押しで進む操作、回転する部品だけ別の行列で描く描画
//
// ・Eキー（コントローラーはA）を押し続けている間だけ回り、離しても回した分は残る。
// ・回す・回し終えたときの音、水を抜く処理は Stage1WestWing が担当し、このクラスは回した量と見た目だけを持っている。
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"
#include "Interactable.h"

class DrainValve : public Object, public Interactable
{
public:
    // 回し終えるまでの秒数（押し続けた時間の合計）
    static constexpr float TurnSeconds = 3.2f;

private:
    // 頂点・インデックスとGPUのバッファ、マテリアル
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;
    // インデックスのうち、動かない部品（台座・配管）の数。残りは回るハンドル
    unsigned int m_FixedIndexCount = 0;

    // 回した割合（0〜1）、ハンドルの角度（ラジアン）、今調べられるか
    float m_Progress = 0.0f;
    float m_WheelAngle = 0.0f;
    bool m_InteractionAllowed = false;

    // 台座・配管と、ハンドルの形を作っている
    void BuildGeometry();
    // 拡大・回転・移動の行列（ハンドルの面が、ヨーの向きの手前を向く）
    DirectX::SimpleMath::Matrix MakeWorldMatrix() const;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;

    // 回し終える前で、回してよい段階のときだけ調べる対象になっている
    bool IsInteractionEnabled() const override
    {
        return m_InteractionAllowed && !IsOpened();
    }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override;
    const char* GetInteractionPrompt() const override
    {
        return m_Progress > 0.0f ? "バルブを回し続ける（長押し）" : "バルブを回す（長押し）";
    }
    // 押した瞬間には何もしない（押し続けた時間で回すため、Stage1WestWingがTurnを呼んでいる）
    void Interact(Player& player) override { (void)player; }

    // deltaTime秒だけ回している。回し終えた瞬間だけtrueを返している
    bool Turn(float deltaTime);
    // 回してよい段階かどうかを切り替えている
    void SetInteractionAllowed(bool allowed) { m_InteractionAllowed = allowed; }
    // 回した割合（0〜1）と、回し終えたか
    float GetProgress() const { return m_Progress; }
    bool IsOpened() const { return m_Progress >= 1.0f; }

    // 置き場所を決めている（壁の表面の位置。ハンドルはそこから手前へ出る）
    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }
};
