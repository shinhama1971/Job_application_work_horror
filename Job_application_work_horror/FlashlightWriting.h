// ============================================================================
// ファイルの役割: 懐中電灯で照らした部分だけ浮かび上がる、壁に書かれた文字。
// 主な技術: 半透明の板ポリゴン、懐中電灯の配光を使うピクセルシェーダー（flashlightRevealPS）、
//           視線・距離・さえぎる物による「読んでいるか」の判定
// 文字の画像は tools/generate-wall-writings.ps1 で生成した自作の画像を使っている。
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

// 壁に貼る文字の板。照らすと浮かび、別の文字への書き換えや、じわりと消える・現れる動きもできる。
class FlashlightWriting : public Object
{
public:
    // 板の頂点を作る・濃さを変化させる・描く・解放する
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;
    // 浮かび上がる文字は監視映像に映さない。印刷物は普通の小物と同じように映す。
    bool DrawsInAuxiliaryView() const override { return m_PrintedSurface; }

    // 板を、周囲の照明や懐中電灯で照らせる印刷物として描く。
    void SetPrintedSurface();

    // 文字の画像を読み込んでいる。alternatePathを指定すると、ShowAlternateで書き換えられるようになる。
    void SetTextures(const std::string& primaryPath, const std::string& alternatePath = "");

    // 壁の表面の中心と、壁から外へ向く法線で置き場所を決めている。
    // 壁と重なって見えなくならないよう、法線方向へ少し浮かせている。
    void Place(
        const DirectX::SimpleMath::Vector3& surfaceCenter,
        const DirectX::SimpleMath::Vector3& outwardNormal,
        float width,
        float height);

    // 文字のインクの色を決めている
    void SetInkColor(const DirectX::SimpleMath::Color& color) { m_InkColor = color; }

    // 別の画像に替える。浮かび上がる文字はじわりと現れ、印刷物はその場で切り替わる。
    void ShowAlternate();
    // 書き換えた後の文字を表示しているか
    bool IsShowingAlternate() const { return m_ShowingAlternate; }

    // 0で消え、1で見える。指定した値へ向かってゆっくり変化させている。
    void SetTargetPresence(float presence) { m_TargetPresence = presence; }
    // 濃さを、ゆっくり変えずにすぐ決めている（最初は隠しておく文字に使っている）
    void SetPresenceImmediately(float presence) { m_Presence = presence; m_TargetPresence = presence; }

    // 懐中電灯の中心付近で、さえぎる物なく照らして読める状態かどうかを返している。
    bool IsBeingRead(
        const DirectX::SimpleMath::Vector3& cameraPosition,
        const DirectX::SimpleMath::Vector3& cameraForward,
        bool flashlightOn) const;

    // 文字が向いている方向（壁から外向き）
    const DirectX::SimpleMath::Vector3& GetNormal() const { return m_Normal; }

private:
    // 壁から浮かせる距離
    static constexpr float SurfaceOffset = 0.25f;
    static constexpr float PresenceFadeSpeed = 0.55f;   // 1秒あたりの濃さの変化量
    // 読める距離
    static constexpr float ReadDistance = 150.0f;
    static constexpr float ReadAlignment = 0.94f;       // 視線との内積（約20度以内なら読める）
    static constexpr float ReadFacing = 0.25f;          // 真横から見た文字は読めない

    // 板の頂点・インデックスとGPUのバッファ、マテリアル、書き換え後の文字の画像、書き換えがあるか、書き換えた後か
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;
    Texture m_AlternateTexture;
    bool m_HasAlternate = false;
    bool m_ShowingAlternate = false;
    bool m_PrintedSurface = false;

    // 文字が向いている方向、インクの色、今の濃さと目標の濃さ、経過時間（シェーダーで、文字が現れる境目をゆっくり揺らすのに使っている）
    DirectX::SimpleMath::Vector3 m_Normal = DirectX::SimpleMath::Vector3(0.0f, 0.0f, -1.0f);
    DirectX::SimpleMath::Color m_InkColor = DirectX::SimpleMath::Color(0.30f, 0.030f, 0.022f, 1.0f);
    float m_Presence = 1.0f;
    float m_TargetPresence = 1.0f;
    float m_Time = 0.0f;
};
