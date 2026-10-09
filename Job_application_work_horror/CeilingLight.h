// ============================================================================
// ファイルの役割: 天井照明の形・点灯の状態・故障したときのちらつきを管理している。
// 主な技術: 点光源の登録、自己発光（エミッシブ）の表現、器具ごとにずらした周期、時間で変わる蛍光灯の演出
// ============================================================================

#pragma once

#include <algorithm>

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

// 天井に付いた蛍光灯。電力の状態に合わせて点灯・明滅し、点光源として部屋を照らしている。
class CeilingLight : public Object
{
private:
    // 頂点・インデックスとGPUのバッファ、本体と発光パネルのマテリアル
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_BodyMaterial;
    std::unique_ptr<Material> m_LightMaterial;

    // 本体のインデックス数（その後ろが発光パネル）、経過時間、器具ごとに周期をずらす値、今の明るさ
    size_t m_BodyIndexCount = 0;
    float m_Time = 0.0f;
    float m_FlickerOffset = 0.0f;
    float m_Brightness = 0.0f;
    // 電力が復旧してからの秒数
    float m_PowerOnTimer = 0.0f;
    // イベント用の明滅の残り秒数・長さ・強さ
    float m_EventFlickerTimer = 0.0f;
    float m_EventFlickerDuration = 0.0f;
    float m_EventFlickerStrength = 0.0f;
    // 停電中の非常灯か、故障した灯か、演出で強制的に消しているか、前フレームで電力が戻っていたか
    bool m_IsEmergencyLight = false;
    bool m_IsFaulted = false;
    bool m_IsForcedOff = false;
    bool m_WasPowerRestored = false;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;

    // 今の明るさ（0〜1程度）を返している
    float GetBrightness() const { return m_Brightness; }
    // 明るさが残っていれば、点光源として登録している
    void CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const override;
    // 非常灯か、故障しているか、強制的に消しているかを返している
    bool IsEmergencyLight() const { return m_IsEmergencyLight; }
    bool IsFaulted() const { return m_IsFaulted; }
    bool IsForcedOff() const { return m_IsForcedOff; }

    // 位置と大きさを設定している
    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

    void SetScale(float x, float y, float z)
    {
        m_Scale = DirectX::SimpleMath::Vector3(x, y, z);
    }

    // 非常灯にするかどうかと、器具ごとに周期をずらす値を設定している
    void SetEmergencyLight(bool emergency, float flickerOffset)
    {
        m_IsEmergencyLight = emergency;
        m_FlickerOffset = flickerOffset;
    }

    // 故障した灯にしている（電力が戻っても不規則に消える）
    void SetFaulted(bool faulted)
    {
        m_IsFaulted = faulted;
    }

    // 演出のために強制的に消している
    void SetForcedOff(bool forcedOff)
    {
        m_IsForcedOff = forcedOff;
    }

    // イベント用の明滅を始めている（長さは0.05秒以上、強さは0〜1に制限している）
    void TriggerEventFlicker(float duration, float strength)
    {
        m_EventFlickerDuration = (std::max)(duration, 0.05f);
        m_EventFlickerTimer = m_EventFlickerDuration;
        m_EventFlickerStrength = (std::clamp)(strength, 0.0f, 1.0f);
    }
};
