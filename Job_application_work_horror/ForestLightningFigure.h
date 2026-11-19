// ============================================================================
// ファイルの役割: 外の森で、稲光が光った一瞬だけ、木の間に浮かぶ黒い人影。
// 主な技術: 稲光を背にした輪郭の光（リムライト）、光が消えるとノイズで崩れて消える表現（shader/lightningFigurePS.hlsl）
//
// 人影の形は、1面の人影（ShadowMan）と同じモデル（assets/model/props/shadow_man.fbx）。
// 自分では動かず、出す場所・見え方・稲光の強さは ForestScene が毎フレーム決めている。
// 懐中電灯で照らしても崩れない（稲光の光でしか見えない、手の届かない存在にしている）。
// ============================================================================

#pragma once

#include <memory>
#include <vector>
#include <wrl/client.h>

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

class ForestLightningFigure final : public Object
{
public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    bool UsesCameraCulling() const override { return true; }
    void Uninit() override;

    // 人影を出している。footPosition は足もと、facing はプレイヤーの方を向く角度（ラジアン）
    void Show(const DirectX::SimpleMath::Vector3& footPosition, float facing);
    // 人影を消している
    void Hide() { m_Visible = false; }
    bool IsShown() const { return m_Visible; }
    // 毎フレームの見え方（0〜1。下がるとノイズで崩れる）と、背にした稲光の位置・色・強さ（0〜1）
    void SetLightning(float visibility, const DirectX::SimpleMath::Vector3& lightningPosition,
        const DirectX::SimpleMath::Vector3& lightningColor, float backlight);

private:
    // shader/lightningFigurePS.hlsl の LightningFigureBuffer と同じ並び（b7 を描く間だけ借りる）
    struct LightningFigureBuffer
    {
        float Time;
        float Visibility;
        float Backlight;
        float Padding;
        DirectX::SimpleMath::Vector4 LightningPosition;
        DirectX::SimpleMath::Vector4 LightningColor;
    };
    static_assert(sizeof(LightningFigureBuffer) == 48, "LightningFigureBuffer must match HLSL layout");

    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_FigureBuffer;

    // 出しているか、出してからの秒数（崩れる模様を動かす）、見え方、稲光
    bool m_Visible = false;
    float m_Time = 0.0f;
    float m_Visibility = 0.0f;
    float m_Backlight = 0.0f;
    DirectX::SimpleMath::Vector3 m_LightningPosition = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_LightningColor = DirectX::SimpleMath::Vector3::One;
};
