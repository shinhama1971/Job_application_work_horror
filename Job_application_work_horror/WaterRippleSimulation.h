// ============================================================================
// ファイルの役割: 水たまりの水面の波を、GPUの波の式（Compute Shader）で計算している。
// 主な技術: 2次元の波動方程式、2枚のテクスチャを交互に読み書きする方法（ピンポン）、
//           決まった間隔で進める計算（フレームレートに左右されない）、範囲をプレイヤーに合わせて動かす仕組み
//
// ・プレイヤーのまわりの一辺192の正方形を256x256マスに分け、各マスの水面の高さを1秒に60歩ずつ進めている
//   （外の森は広いので、同じマス目の大きさのまま一辺576・768x768マスにしている）。
// ・足音や水滴を「おもり」として受け取り、そこから輪が広がる。輪は重なって干渉し、水たまりの岸と、床に立った柱や脚で跳ね返る。
// ・できた高さのテクスチャは、濡れた床（反射を波打たせる）と壁（波が集めた光の輪を映す）が読んでいる。
// ・ゲームの部品（Object・Game）に依存せず、技術デモからもそのまま使えるようにしている。
// ============================================================================

#pragma once

#include "ComputeShader.h"
#include "RenderTexture.h"

#include <SimpleMath.h>
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

class WaterRippleSimulation final
{
public:
    // 普段のマス目の数と範囲の一辺の長さ（1面・2面）、1回に足せるおもりの数
    static constexpr int DefaultResolution = 256;
    static constexpr float DefaultWorldSize = 192.0f;
    static constexpr int MaxImpulses = 16;
    // 波を跳ね返す物（床に立っている柱・棚の脚など）の最大数
    static constexpr int MaxObstacles = 32;
    // 1歩の秒数（1秒に60歩）と、1フレームに進める歩数の上限（重いフレームで歩数が増えすぎないように）
    static constexpr float StepSeconds = 1.0f / 60.0f;
    static constexpr int MaxStepsPerFrame = 3;

    // テクスチャ・シェーダー・定数バッファを作っている。
    // resolutionはマス目の数（16の倍数）、worldSizeは計算する範囲の一辺の長さ。
    // 広い外の森では、マス目の大きさ（worldSize ÷ resolution）を変えずに数を増やし、遠くの水たまりまで波を計算している
    void Init(int resolution = DefaultResolution, float worldSize = DefaultWorldSize);
    // 計算する範囲の一辺の長さ
    float GetWorldSize() const { return m_WorldSize; }
    void Uninit();

    // 足音や水滴を、波のおもりとして予約している（次のUpdateで足す）。strengthが負なら水面を押し下げる
    void AddImpulse(const DirectX::SimpleMath::Vector2& positionXZ, float radius, float strength);
    // 経過時間ぶん波を進めている。centerXZのまわりを計算し、水がある場所はpuddleCellBuffer（b13）とfloodRectで決めている。
    // obstaclesは、波を跳ね返す床に立った物を上から見た長方形（x・z = 中心、z・w = 半分の幅と奥行き）。範囲の外の物は使わない
    void Update(float deltaTime, const DirectX::SimpleMath::Vector2& centerXZ,
        ID3D11Buffer* puddleCellBuffer, const DirectX::SimpleMath::Vector4& floodRect,
        const std::vector<DirectX::SimpleMath::Vector4>& obstacles);

    // 今の高さのテクスチャ（x = 今の高さ、y = 1つ前の高さ）と、範囲の最小のx・z
    ID3D11ShaderResourceView* GetHeightSRV();
    DirectX::SimpleMath::Vector2 GetOrigin() const { return m_Origin; }

private:
    // 交互に読み書きする2枚のテクスチャと、今の高さが入っている方の番号
    Graphics::RenderTexture m_States[2];
    int m_CurrentState = 0;
    // マス目の数と、範囲の一辺の長さ（Initで決める）
    int m_Resolution = DefaultResolution;
    float m_WorldSize = DefaultWorldSize;
    ComputeShader m_Shader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ParamsBuffer;
    // 範囲の最小のx・z（マス目の単位にそろえている）、まだ範囲を決めていないか
    DirectX::SimpleMath::Vector2 m_Origin;
    bool m_HasOrigin = false;
    // まだ進めていない時間と、予約されたおもり（xy = x・z、z = 半径、w = 強さ）
    float m_Accumulator = 0.0f;
    std::vector<DirectX::SimpleMath::Vector4> m_PendingImpulses;
};
