// ============================================================================
// ファイルの役割: 外の森の地面に生える草（数十万本の草の葉）を、GPU で揺らして描いている。
// 主な技術: Compute Shader による葉ごとのばねの計算（風・プレイヤーが押し分ける力）、
//           頂点バッファを使わず SV_VertexID・SV_InstanceID から葉の形を作る頂点シェーダー（3次ベジェ曲線）、
//           区画ごとの視錐台カリングと、遠い区画ほど本数と節の数を減らす LOD
//
// 授業資料「Compute Shader 実践 Part.9（草）」の流れに沿っている。
// ・CPU は起動時（と配置を読み直したとき）に、葉の根元の位置・高さ・幅・向き・色を決めて、GPU のバッファへ一度だけ送る。
//   群れて生えるように、ノイズで濃い所と薄い所を作り、小道・研究所・木の幹など、当たり判定のある物の下には生やさない。
// ・毎フレーム、shader/grassCS.hlsl が葉の先の位置を、風とプレイヤーに押される力に向かうばねで動かす（葉1本 = 1スレッド）。
// ・shader/grassVS.hlsl が、根元・先端と2つの制御点から3次ベジェ曲線を作り、葉を細い三角形の帯にしている。
//   根元を少し細く、先へ行くほど細くし、先端は1点にとがらせている。
// ・shader/grassPS.hlsl が、懐中電灯（影つき）・点光源・環境光で照らし、他の面と同じ霧と色調を掛けている。
//   葉は薄いので、裏から当たった光も少し通している。
//
// 置き場所は配置ファイルの「place ForestGrass x y z size 幅 1 奥行き」で、その範囲（地面の高さ y）に生やしている。
// ============================================================================

#pragma once

#include "ComputeShader.h"
#include "Object.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

class ForestGrass final : public Object
{
public:
    // 葉の本数の上限（範囲が広いときは、この本数に収まるよう密度を下げている）
    static constexpr std::uint32_t MaxBlades = 300000;

    void Init() override;
    void Update() override;
    void RunGpuSimulation() override;
    void Draw(Camera* camera) override;
    void Uninit() override;

    // 監視カメラなどの別の視点には描かない（数十万本を2回描かないため）
    bool DrawsInAuxiliaryView() const override { return false; }

    // 葉を植え直している（置き場所が変わったときや、配置を読み直して木や岩が動いたとき）
    void Rebuild();
    // デバッグ画面に出す数：植えた本数、前の描画で描いた本数、描いた区画の数
    std::uint32_t GetBladeCount() const { return static_cast<std::uint32_t>(m_BladeCount); }
    std::uint32_t GetDrawnBladeCount() const { return m_DrawnBlades; }
    std::uint32_t GetDrawnChunkCount() const { return m_DrawnChunks; }

private:
    // shader/grassCS.hlsl・grassVS.hlsl の GrassBlade と同じ並び（48バイト）。葉ごとに変わらない値
    struct Blade
    {
        DirectX::SimpleMath::Vector3 Root;  // 根元の位置（ワールド）
        float Height;                       // 葉の長さ
        DirectX::SimpleMath::Vector2 Facing; // 葉の幅の向き（xz の単位ベクトル）
        float Width;                        // 根元近くの幅
        float Lean;                         // 何もないときの傾き（0 = まっすぐ）
        DirectX::SimpleMath::Vector3 Color; // 葉先の色
        float Stiffness;                    // ばねの強さ（硬い葉ほど早く戻る）
    };
    // shader/grassCS.hlsl の GrassState と同じ並び（32バイト）。毎フレーム GPU が書き換える値
    struct BladeState
    {
        DirectX::SimpleMath::Vector4 Tip;      // xyz = 根元から葉先までのずれ
        DirectX::SimpleMath::Vector4 Velocity; // xyz = 葉先の速さ
    };
    // 草を区切った1区画（上から見た正方形）。区画ごとに見えるかを調べ、遠さで本数と節の数を変えて描いている
    struct Chunk
    {
        DirectX::SimpleMath::Vector3 Center;
        float Radius = 0.0f;
        std::uint32_t First = 0;
        std::uint32_t Count = 0;
    };

    // 範囲の中に葉を植え、区画ごとに並べ替えている（区画の中の順番はばらばらにし、先頭から何割かを描けば全体が薄くなるようにしている）
    void PlantBlades(std::vector<Blade>& blades, std::vector<BladeState>& states);
    // GPU のバッファを作り直している
    void CreateBuffers(const std::vector<Blade>& blades, const std::vector<BladeState>& states);
    void ReleaseBuffers();

    ComputeShader m_SimulationShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_BladeBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_BladeView;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_StateBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_StateView;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_StateAccess;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SimulationParams;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DrawParams;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_TwoSidedRasterizer;

    std::vector<Chunk> m_Chunks;
    std::size_t m_BladeCount = 0;
    // 植えたときの位置と大きさ（変わったら植え直す）
    DirectX::SimpleMath::Vector3 m_PlantedPosition;
    DirectX::SimpleMath::Vector3 m_PlantedScale;
    bool m_NeedsRebuild = true;
    bool m_Disabled = false;

    // 風を動かす時間と、まだ計算していない経過時間（Update で足し、RunGpuSimulation で使う）
    float m_Time = 0.0f;
    float m_PendingSeconds = 0.0f;
    std::uint32_t m_DrawnBlades = 0;
    std::uint32_t m_DrawnChunks = 0;
};
