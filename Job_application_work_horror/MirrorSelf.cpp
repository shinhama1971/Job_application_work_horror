// ============================================================================
// ファイルの役割: 2面の壁の鏡にだけ映る「自分の姿」の位置・向き・手足の振りを決め、鏡のシェーダーへ渡している。説明は MirrorSelf.h を参照。
// 主な技術: 過去の姿勢の輪のバッファ（遅れて動く）、歩く速さから決める手足の振り
// ============================================================================

#include "MirrorSelf.h"

#include "Application.h"
#include "Camera.h"
#include "Game.h"
#include "Player.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 1歩で進む距離の2倍（脚が1往復する距離）
    constexpr float StrideCycleDistance = 34.0f;
}

void MirrorSelf::Init()
{
    m_HistoryHead = 0;
    m_HistoryFilled = 0;
    m_SampleTimer = 0.0f;
    m_WalkPhase = 0.0f;
    m_WalkAmount = 0.0f;
}

const MirrorSelf::PoseSample& MirrorSelf::GetSample(int samplesAgo) const
{
    const int clamped = (std::clamp)(samplesAgo, 0, (std::max)(m_HistoryFilled - 1, 0));
    return m_History[static_cast<std::size_t>((m_HistoryHead - clamped + HistoryCount) % HistoryCount)];
}

// プレイヤーの位置とカメラの向きを1秒に60回記録し、遅らせた姿勢と、その時の歩く速さから手足の振りを決めて、
// 鏡のシェーダーへ渡している（隠れている間は映さない）
void MirrorSelf::Update()
{
    Core::Game* game = Core::Game::GetInstance();
    if (m_Player == nullptr)
    {
        m_Player = game->GetObj<Player>("Player");
    }
    if (m_Player == nullptr)
    {
        return;
    }

    const float deltaTime = Application::GetDeltaTime();
    PoseSample current;
    current.Position = m_Player->GetPosition();
    current.Yaw = game->GetCamera()->GetCameraDirection();
    m_SampleTimer += deltaTime;
    if (m_HistoryFilled == 0 || m_SampleTimer >= SampleInterval)
    {
        m_SampleTimer = (std::clamp)(m_SampleTimer - SampleInterval, 0.0f, SampleInterval);
        m_HistoryHead = (m_HistoryHead + 1) % HistoryCount;
        m_History[static_cast<std::size_t>(m_HistoryHead)] = current;
        m_HistoryFilled = (std::min)(m_HistoryFilled + 1, HistoryCount);
    }

    // 遅らせる秒数の分だけ前の記録を選んでいる（記録が足りなければ、一番古いもの）
    const int delaySamples = static_cast<int>(std::lround(m_DelaySeconds / SampleInterval));
    const PoseSample& pose = GetSample(delaySamples);
    m_Position = pose.Position;
    m_Rotation = Vector3(0.0f, pose.Yaw, 0.0f);

    // 歩く速さ（0.1秒前との差）から、手足を振る大きさと速さを決めている
    const PoseSample& earlier = GetSample(delaySamples + 6);
    Vector3 moved = pose.Position - earlier.Position;
    moved.y = 0.0f;
    const float speed = moved.Length() / (6.0f * SampleInterval);
    const float targetAmount = (std::clamp)(speed / 35.0f, 0.0f, 1.0f);
    m_WalkAmount += (targetAmount - m_WalkAmount) * (std::min)(deltaTime * 8.0f, 1.0f);
    m_WalkPhase += speed / StrideCycleDistance * DirectX::XM_2PI * deltaTime;
    if (m_WalkPhase > DirectX::XM_2PI * 64.0f)
    {
        m_WalkPhase -= DirectX::XM_2PI * 64.0f;
    }

    // 体の右と前の向き（体の空間の+xと+zを、向きの回転で回したもの）
    const Matrix rotation = Matrix::CreateFromYawPitchRoll(m_Rotation.y, 0.0f, 0.0f);
    const Vector3 right = Vector3::TransformNormal(Vector3(1.0f, 0.0f, 0.0f), rotation);
    const Vector3 forward = Vector3::TransformNormal(Vector3(0.0f, 0.0f, 1.0f), rotation);
    Renderer::SetMirrorSelf(m_Position, right, forward, std::sin(m_WalkPhase) * m_WalkAmount, !m_Player->IsHiding());
}

// 場面を出るときに、鏡に映す体を消している
void MirrorSelf::Uninit()
{
    Renderer::SetMirrorSelf(Vector3::Zero, Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), 0.0f, false);
    m_Player = nullptr;
}
