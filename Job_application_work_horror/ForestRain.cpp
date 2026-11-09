// ============================================================================
// ファイルの役割: 外の森に降る雨の描画（説明は ForestRain.h を参照）。
// ============================================================================

#include "ForestRain.h"

#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "Renderer.h"
#include "Wall.h"
#include "utility.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX::SimpleMath;

namespace
{
    // shader/rainVS.hlsl の RainDrawBuffer と同じ並び（b9）
    struct RainDrawParams
    {
        Vector3 CameraPosition;
        float Time;
        Vector3 BoxSize;
        float GroundY;
        Vector2 Wind;
        float FallSpeed;
        float StreakLength;
        float StreakWidth;
        std::uint32_t DropCount;
        float PixelScale;
        float Intensity;
        float SplashRadius;
        std::uint32_t OccluderCount;
        float Padding[2];
        Vector4 Occluders[8];
        float OccluderBottoms[8];
    };
    static_assert(sizeof(RainDrawParams) == 80 + 16 * 8 + 32, "RainDrawParams must match HLSL layout");

    // 雨の大きさ（目の高さが40なので、1メートル ≒ 24）。秒速およそ9メートルで落ち、北東の風で少し斜めに流れる
    constexpr float FallSpeed = 210.0f;
    const Vector2 Wind(26.0f, 14.0f);
    constexpr float StreakLength = 3.8f;
    constexpr float StreakWidth = 0.07f;
    // カメラを囲む、雨粒を置く箱（横140・高さ130）。遠い雨は霧で見えないので、この中だけで十分
    const Vector3 BoxSize(140.0f, 130.0f, 140.0f);
    // しぶきを出す、カメラのまわりの範囲（半分の一辺）
    constexpr float SplashRadius = 45.0f;
}

void ForestRain::Init()
{
    // 頂点は頂点シェーダーが番号から作るので、入力レイアウトは作らない
    m_Shader.Create("shader/rainVS.hlsl", "shader/rainPS.hlsl", true);

    ID3D11Device* device = Renderer::GetDevice();
    D3D11_BUFFER_DESC paramsDesc{};
    paramsDesc.Usage = D3D11_USAGE_DEFAULT;
    paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    paramsDesc.ByteWidth = sizeof(RainDrawParams);
    if (FAILED(device->CreateBuffer(&paramsDesc, nullptr, m_DrawParams.ReleaseAndGetAddressOf())))
    {
        utility::ReportFatalError("雨の定数バッファの作成に失敗しました。");
    }
    // 雨粒の四角形は、カメラの向きによって表と裏が入れ替わるので、両面を描いている
    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, m_TwoSidedRasterizer.ReleaseAndGetAddressOf())))
    {
        utility::ReportFatalError("雨のラスタライザーの設定の作成に失敗しました。");
    }
    m_Time = 0.0f;
    m_ShelterTimer = 0.0f;
    m_ShelterCount = 0;
    // 同じ森で雨の負荷だけを比べるための起動時の切り替え（草の SIGNAL_LOST_DISABLE_FOREST_GRASS と同じ使い方）。通常は降らせる
    wchar_t disabled[2]{};
    if (GetEnvironmentVariableW(L"SIGNAL_LOST_DISABLE_FOREST_RAIN", disabled, 2) == 1 && disabled[0] == L'1')
    {
        m_Intensity = 0.0f;
    }
}

void ForestRain::CollectShelters()
{
    // 屋根・ひさし：当たり判定があり、地面から15以上浮いていて、上から見て広い板（研究所の屋根・玄関のひさしなど）。
    // 広い順に、上限の数まで使っている。回っている物は、0度か90度のものだけを使う（森の建物はどれもそう置いている）
    struct Candidate
    {
        Vector4 Rect;
        float Bottom;
        float Area;
    };
    std::vector<Candidate> candidates;
    for (const Wall* wall : Core::Game::GetInstance()->GetObjects<Wall>())
    {
        if (wall->IsDestroy() || !wall->IsCollisionEnabled())
        {
            continue;
        }
        const Vector3 position = wall->GetPosition();
        const Vector3 scale = wall->GetScale();
        const float bottom = position.y - scale.y * 0.5f;
        const float area = scale.x * scale.z;
        const float yaw = std::fmod(std::fabs(wall->GetRotation().y), DirectX::XM_PI);
        const bool straight = yaw < 0.01f || yaw > DirectX::XM_PI - 0.01f;
        const bool quarter = std::fabs(yaw - DirectX::XM_PIDIV2) < 0.01f;
        if (bottom < m_GroundY + 15.0f || area < 150.0f || (!straight && !quarter))
        {
            continue;
        }
        const float halfX = (quarter ? scale.z : scale.x) * 0.5f;
        const float halfZ = (quarter ? scale.x : scale.z) * 0.5f;
        candidates.push_back(Candidate{
            Vector4(position.x - halfX, position.z - halfZ, position.x + halfX, position.z + halfZ),
            bottom, area });
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.Area > b.Area; });
    m_ShelterCount = (std::min)(static_cast<int>(candidates.size()), MaxShelters);
    for (int index = 0; index < m_ShelterCount; ++index)
    {
        m_ShelterRects[index] = candidates[static_cast<std::size_t>(index)].Rect;
        m_ShelterBottoms[index] = candidates[static_cast<std::size_t>(index)].Bottom;
    }
}

void ForestRain::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    m_Time += deltaTime;
    // 長く続けても、時間の値の細かさが落ちないよう、雨の見え方が変わらない周期で戻している
    if (m_Time > 3600.0f)
    {
        m_Time -= 3600.0f;
    }
    m_ShelterTimer -= deltaTime;
    if (m_ShelterTimer <= 0.0f)
    {
        CollectShelters();
        m_ShelterTimer = 2.0f;
    }
}

void ForestRain::Draw(Camera* camera)
{
    if (camera == nullptr || m_Intensity <= 0.0f)
    {
        return;
    }
    camera->SetCamera();
    Matrix world = Matrix::Identity;
    Renderer::SetWorldMatrix(&world);
    ID3D11DeviceContext* context = Renderer::GetDeviceContext();

    RainDrawParams params{};
    params.CameraPosition = camera->GetPosition();
    params.Time = m_Time;
    params.BoxSize = BoxSize;
    params.GroundY = m_GroundY;
    params.Wind = Wind;
    params.FallSpeed = FallSpeed;
    params.StreakLength = StreakLength;
    params.StreakWidth = StreakWidth;
    params.DropCount = DropCount;
    // 距離1あたりの画素の数（遠くの筋を1画素の幅まで広げるのに使う）
    Matrix view;
    Matrix projection;
    camera->GetMainMatrices(view, projection);
    UINT viewportCount = 1;
    D3D11_VIEWPORT viewport{};
    context->RSGetViewports(&viewportCount, &viewport);
    params.PixelScale = projection._22 * viewport.Height * 0.5f;
    params.Intensity = m_Intensity;
    params.SplashRadius = SplashRadius;
    params.OccluderCount = static_cast<std::uint32_t>(m_ShelterCount);
    for (int index = 0; index < m_ShelterCount; ++index)
    {
        params.Occluders[index] = m_ShelterRects[index];
        params.OccluderBottoms[index] = m_ShelterBottoms[index];
    }
    context->UpdateSubresource(m_DrawParams.Get(), 0, nullptr, &params, 0, 0);

    m_Shader.SetGPU();
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11Buffer* drawBuffer = m_DrawParams.Get();
    context->VSSetConstantBuffers(9, 1, &drawBuffer);

    Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
    context->RSGetState(previousRasterizer.GetAddressOf());
    context->RSSetState(m_TwoSidedRasterizer.Get());
    // 加算合成で、深度の判定はするが書き込まない（木や建物の奥の雨は隠れ、雨どうしは重なって足される）
    Renderer::SetBlendState(BS_ADDITIVE);
    Renderer::SetDepthEnable(false);
    // 雨粒1つ・しぶき1つ = 4頂点の四角形1枚
    context->DrawInstanced(4, DropCount + SplashCount, 0, 0);
    Renderer::SetDepthEnable(true);
    Renderer::SetBlendState(BS_NONE);
    context->RSSetState(previousRasterizer.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ForestRain::Uninit()
{
    m_DrawParams.Reset();
    m_TwoSidedRasterizer.Reset();
}
