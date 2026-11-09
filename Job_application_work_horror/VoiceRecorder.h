// ============================================================================
// ファイルの役割: 1面に落ちているボイスレコーダー（研究員たちの録音）の見た目と、調べたときの処理を管理している。
// 主な技術: Interactableインターフェース、箱を組み合わせた形、マテリアルの使い分け、点滅するランプと小さな点光源
//
// ・机や木箱の上に置かれた小さな録音機。上の赤いランプがゆっくり点滅し、近くの面をかすかに赤く照らすので、暗い部屋でも見つけられる。
// ・調べると再生ボタンを押した音がして、録音が字幕で流れ始める（字幕を流すのは Stage1Transmissions）。
//   聞いた後はランプが緑に変わり、もう調べる対象にはならない。
// ・ヒューズや電池のように回ったり浮いたりはさせず、置かれた物として見せている。
// ============================================================================

#pragma once

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Interactable.h"

class VoiceRecorder : public Object, public Interactable
{
private:
    // 頂点とインデックス（BuildGeometryで作り、GPUのバッファへ送っている）
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;

    // 本体・スピーカーの網・ボタン・ランプのマテリアル
    std::unique_ptr<Material> m_BodyMaterial;
    std::unique_ptr<Material> m_GrilleMaterial;
    std::unique_ptr<Material> m_ButtonMaterial;
    std::unique_ptr<Material> m_LampMaterial;

    // 部位ごとに描くインデックスの範囲（並びは Part と同じ）
    enum Part { Body, Grille, Button, Lamp, PartCount };
    unsigned int m_PartStart[PartCount] = {};
    unsigned int m_PartCount[PartCount] = {};

    // 録音の番号（0〜3）、聞いたか、調べられたがまだ Scene が受け取っていないか、ランプの点滅の時間
    int m_RecordingIndex = 0;
    bool m_Played = false;
    bool m_PlayRequested = false;
    float m_BlinkTime = 0.0f;

    // 箱を組み合わせて録音機の形を作っている
    void BuildGeometry();
    // 箱を1つ足している（中心と大きさはローカル座標。部位ごとに続けて足すと、インデックスの範囲がつながる）
    void AppendBox(const DirectX::SimpleMath::Vector3& center, const DirectX::SimpleMath::Vector3& size);
    // ランプの明るさ（0〜1）。聞く前はゆっくり点滅し、聞いた後は緑で弱く点いたまま
    float GetLampLevel() const;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    bool ContributesToReflection() const override { return true; }
    void CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const override;
    void Uninit() override;

    // 聞く前だけ、調べる対象になっている
    bool IsInteractionEnabled() const override { return !m_Played; }
    DirectX::SimpleMath::Vector3 GetInteractionPosition() const override { return m_Position; }
    const char* GetInteractionPrompt() const override { return "録音を聞く"; }
    void Interact(Player& player) override;

    // 録音の番号（StoryText::Recordings の並び）
    void SetRecordingIndex(int index) { m_RecordingIndex = index; }
    int GetRecordingIndex() const { return m_RecordingIndex; }
    bool IsPlayed() const { return m_Played; }
    // 調べられたフレームの後、Scene が一度だけ true を受け取り、字幕を流し始めている
    bool TakePlayRequest()
    {
        const bool requested = m_PlayRequested;
        m_PlayRequested = false;
        return requested;
    }
};
