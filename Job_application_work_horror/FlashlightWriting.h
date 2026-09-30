// ============================================================================
// ファイルの役割: 懐中電灯で照らした部分だけ浮かび上がる、壁に書かれた文字です。
// 主な技術: 半透明の板ポリゴン、懐中電灯の配光を使うピクセルシェーダー（flashlightRevealPS）、
//           視線・距離・遮蔽による「読んでいるか」の判定
// 文字の画像は tools/generate-wall-writings.ps1 で生成した自作の画像です。
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Material.h"

#include <SimpleMath.h>
#include <memory>
#include <string>
#include <vector>

class FlashlightWriting : public Object
{
public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;
    // 懐中電灯の光で見えるものなので、監視カメラの映像には映しません。
    bool DrawsInAuxiliaryView() const override { return false; }

    // 文字の画像を読み込みます。alternatePathを指定すると、ShowAlternateで書き換えられます。
    void SetTextures(const std::string& primaryPath, const std::string& alternatePath = "");

    // 壁の表面の中心と、壁から外へ向く法線で置き場所を決めます。
    // 壁と重なって見えなくならないよう、法線方向へ少し浮かせます。
    void Place(
        const DirectX::SimpleMath::Vector3& surfaceCenter,
        const DirectX::SimpleMath::Vector3& outwardNormal,
        float width,
        float height);

    void SetInkColor(const DirectX::SimpleMath::Color& color) { m_InkColor = color; }

    // 文字を書き換えます。いったん消してから、じわりと現します。
    void ShowAlternate();
    bool IsShowingAlternate() const { return m_ShowingAlternate; }

    // 0で消え、1で見えます。値へ向かってゆっくり変化します。
    void SetTargetPresence(float presence) { m_TargetPresence = presence; }

    // 懐中電灯の中心付近で、遮るものなく照らして読める状態かどうか。
    bool IsBeingRead(
        const DirectX::SimpleMath::Vector3& cameraPosition,
        const DirectX::SimpleMath::Vector3& cameraForward,
        bool flashlightOn) const;

    const DirectX::SimpleMath::Vector3& GetNormal() const { return m_Normal; }

private:
    static constexpr float SurfaceOffset = 0.25f;
    static constexpr float PresenceFadeSpeed = 0.55f;   // 1秒あたりの濃さの変化量
    static constexpr float ReadDistance = 150.0f;
    static constexpr float ReadAlignment = 0.94f;       // 視線との内積（約20度以内）
    static constexpr float ReadFacing = 0.25f;          // 真横から見た文字は読めません

    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;
    Texture m_AlternateTexture;
    bool m_HasAlternate = false;
    bool m_ShowingAlternate = false;

    DirectX::SimpleMath::Vector3 m_Normal = DirectX::SimpleMath::Vector3(0.0f, 0.0f, -1.0f);
    DirectX::SimpleMath::Color m_InkColor = DirectX::SimpleMath::Color(0.30f, 0.030f, 0.022f, 1.0f);
    float m_Presence = 1.0f;
    float m_TargetPresence = 1.0f;
    float m_Time = 0.0f;
};
