// ============================================================================
// ファイルの役割: 人影の出現時間、プレイヤーを追う動き、見られたときに崩れて消える演出を管理している。
// 主な技術: 簡単な敵の状態管理、プレイヤーへ近づく動き、黒い人影の表現、ディゾルブ（崩れて消える表現）
// ============================================================================

#pragma once

#include <algorithm>
#include <functional>
#include <utility>
#include <wrl/client.h>
#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"

// 黒い人の形をした影。決まった時間だけ現れ、懐中電灯で照らして見つめると崩れて消える。追ってくる設定にもできる。
class ShadowMan : public Object
{
private:
    // 頂点・インデックスとGPUのバッファ、マテリアル
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::unique_ptr<Material> m_Material;

    // 崩れて消える表現のシェーダーへ渡す値（b7）：経過時間、見えている度合い（0〜1）、崩れる縁の幅、詰め物
    struct DissolveBuffer
    {
        float Time;
        float Visibility;
        float EdgeWidth;
        float Padding;
    };

    // 定数バッファ、現れてからの秒数、見つめられた度合い（0〜1）、見られたときの反応を済ませたか
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DissolveBuffer;
    float m_Age = 0.0f;
    float m_ObservedAmount = 0.0f;
    bool m_ReactedToGaze = false;
    // 見られたら強く驚かせるか、追ってくるか、表示しているか、時間切れで消さずに非表示にするか
    bool m_GazeScareEnabled = false;
    bool m_ChaseEnabled = false;
    bool m_IsActive = true;
    bool m_DeactivateOnExpire = false;
    // 見られたときに呼ぶ処理（Sceneが演出を足すのに使っている）
    std::function<void()> m_OnObserved;
    // 最初のUpdateで名前で探して、その後は覚えておいている。PlayerとShadowManは同じSceneで作られ、
    // Sceneを切り替えるときにまとめて破棄されるため、Playerだけが先に無効になることはない。
    class Player* m_Player = nullptr;

    // 残りの表示時間、追ってくる速さ、追うのをやめる距離
    float m_LifeTime = 2.0f;
    float m_ChaseSpeed = 0.0f;
    float m_ChaseStopDistance = 28.0f;
    // 追ってくる間、実際に進んだ距離に合わせて、自分の位置から足音を鳴らしている。
    float m_ChaseStepDistance = 0.0f;
    bool m_ChaseStepLeft = false;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* camera) override;
    bool UsesCameraCulling() const override { return true; }
    bool ContributesToPlanarReflection() const override { return true; }
    void Uninit() override;

    // 置き場所を決めている
    void SetPosition(float x, float y, float z)
    {
        m_Position = DirectX::SimpleMath::Vector3(x, y, z);
    }

    // 見られたら強く驚かせる設定にし、表示する時間を決めている（0.5秒以上）
    void EnableGazeScare(float lifetimeSeconds = 6.0f)
    {
        m_GazeScareEnabled = true;
        m_LifeTime = (std::max)(lifetimeSeconds, 0.5f);
    }

    // 見られたときに呼ぶ処理を登録している
    void SetOnObserved(std::function<void()> callback)
    {
        m_OnObserved = std::move(callback);
    }

    // 表示・非表示を切り替えている。非表示から表示に戻すときは、状態を全部最初に戻している
    void SetActive(bool active)
    {
        if (active && !m_IsActive)
        {
            m_Age = 0.0f;
            m_ObservedAmount = 0.0f;
            m_ReactedToGaze = false;
            m_GazeScareEnabled = false;
            m_ChaseEnabled = false;
            m_ChaseSpeed = 0.0f;
            m_ChaseStepDistance = 0.0f;
            m_LifeTime = 2.0f;
            m_OnObserved = nullptr;
        }
        if (!active)
        {
            m_ChaseEnabled = false;
        }
        m_IsActive = active;
    }

    // 表示しているか
    bool IsActive() const
    {
        return m_IsActive;
    }

    // プレイヤーを追う設定にしている（stopDistanceまで近づいたら止まる。最低8）
    void EnableChase(float speed, float stopDistance)
    {
        m_ChaseEnabled = true;
        m_ChaseSpeed = (std::max)(speed, 0.0f);
        m_ChaseStopDistance = (std::max)(stopDistance, 8.0f);
    }

    // 時間切れのとき、破棄せずに非表示にするか（何度も使い回す影で使っている）
    void SetDeactivateOnExpire(bool deactivate)
    {
        m_DeactivateOnExpire = deactivate;
    }
};
