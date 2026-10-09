// ============================================================================
// ファイルの役割: 驚かせる瞬間などに、画面の上へ漂う埃の粒と細かい走査線を短い時間だけ重ねている（Object名は "CRTNoise"）。
// 主な技術: 画面全体に重ねる表現、シェーダーで作る粒の模様、アルファブレンド、時間で動く表現
// ============================================================================

#pragma once

#include <wrl/client.h>
#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

// 演出から強さと表示する秒数を指定して使っている。時間が過ぎたら自動で消える。
class ScreenDustOverlay : public Object
{
private:
    // シェーダーへ渡す値（定数バッファb0）：経過時間、強さ、16バイトにそろえる詰め物
    struct TimeBuffer
    {
        float time;
        float power;
        float dummy1;
        float dummy2;
    };

    // 画面全体を覆う四角形の頂点・インデックスとGPUのバッファ、シェーダー、マテリアル
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;

    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;

    Shader m_Shader;
    std::unique_ptr<Material> m_Material;

    // 時間と強さを渡す定数バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_TimeBuffer;

    // 経過時間、強さ（0〜1）
    float m_Time = 0.0f;
    float m_Power = 0.7f;

    // 表示しているか、消えるまでの残り秒数
    bool m_IsActive = false;
    float m_Timer = 0.0f;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;
    // 画面全体へ重ねるノイズなので、監視カメラなど別の視点の描画には含めていない。
    bool DrawsInAuxiliaryView() const override { return false; }

    // 強さ・表示するか・表示する秒数を決めている
    void SetPower(float power)
    {
        m_Power = power;
    }

    void SetActive(bool active)
    {
        m_IsActive = active;
    }

    void SetTimer(float timer)
    {
        m_Timer = timer;
    }
};
