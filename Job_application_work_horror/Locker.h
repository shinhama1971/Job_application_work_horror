// ============================================================================
// ファイルの役割: 中に隠れられるロッカーの扉（見た目と「隠れる」操作）を管理します。
// 主な技術: Interactableインターフェース、箱を組み合わせたプロシージャルメッシュ、使用可否の切り替え
// ロッカーの本体（当たり判定のある箱）はWallで作り、このクラスは正面の扉だけを担当します。
// 隠れている間の移動・視点の制限はPlayer、影の振る舞いはSceneが担当します。
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Interactable.h"

class Locker : public Object, public Interactable
{
private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;

    // 扉の正面の向き（ヨー）。+X向きを0とし、扉はこの向きの壁面に付きます。
    float m_Facing = 0.0f;
    DirectX::SimpleMath::Vector3 m_HidePosition;
    DirectX::SimpleMath::Vector3 m_ExitPosition;
    bool m_Usable = true;
    Player* m_Occupant = nullptr;

    void BuildGeometry();

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    bool UsesCameraCulling() const override { return true; }
    bool CastsShadow() const override { return false; }
    void Uninit() override;

    bool IsInteractionEnabled() const override { return m_Usable && m_Occupant == nullptr; }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override;
    const char* GetInteractionPrompt() const override { return "ロッカーに隠れる"; }
    void Interact(Player& player) override;

    // 扉の中心の位置と、扉が向く方向（ヨー）で置き場所を決めます。
    // 隠れる位置と出たときに立つ位置は、扉の前後に自動で決めます。
    void Place(const DirectX::SimpleMath::Vector3& doorCenter, float facing);

    // 追跡の最中など、隠れさせたくない場面ではfalseにします。
    void SetUsable(bool usable) { m_Usable = usable; }
    bool IsOccupied() const { return m_Occupant != nullptr; }
};
