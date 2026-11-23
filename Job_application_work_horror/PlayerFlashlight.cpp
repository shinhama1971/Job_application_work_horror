// ============================================================================
// ファイルの役割: 懐中電灯の電力・配光と、床や壁に当たった光の照り返しを更新する。
// ============================================================================

#include "Player.h"
#include "MathUtility.h"
#include "Camera.h"
#include "Game.h"
#include "Input.h"
#include "PostProcess.h"
#include "Wall.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 照り返しの光がこれより弱ければ出さない
    constexpr float MinimumBounceIntensity = 0.01f;
    // --- 懐中電灯
    // 照らす距離と、光の強さ
    constexpr float FlashlightRange = 275.0f;
    constexpr float FlashlightIntensity = 1.50f;
    // 光の形：内側の角度までは全部の明るさ、外側の角度で消え、その間はこの乗数で暗くする
    constexpr float FlashlightInnerConeDegrees = 14.0f;
    constexpr float FlashlightOuterConeDegrees = 32.0f;
    constexpr float FlashlightConeFalloff = 1.8f;
    // 近い面を探す一番遠い距離と、近い面で光を弱め始める距離・弱めきる距離までの幅
    constexpr float FlashlightSurfaceSearchDistance = 70.0f;
    constexpr float FlashlightNearSurfaceDistance = 12.0f;
    constexpr float FlashlightNearSurfaceRange = 42.0f;
    // 近い面で弱める明るさの割合（最大）と、縮める照らす距離
    constexpr float FlashlightNearSurfaceDim = 0.20f;
    constexpr float FlashlightNearSurfaceRangeCut = 48.0f;
    constexpr float FlashlightWeakDim = 0.30f;
    constexpr float FlashlightWeakRangeCut = 0.18f;
    // 開発用の電池の回復量（Debug と 3面の確認中の B キー）
    constexpr float DebugBatteryRefill = 50.0f;
    // --- 懐中電灯のスイッチの音の高さ（点ける・消す）
    constexpr float FlashlightOnClickPitch = 1.08f;
    constexpr float FlashlightOffClickPitch = 0.94f;
    // 電池の残りが少なくなったときの知らせ（20%を切った・10%を切った＝一番強い知らせの段階）
    constexpr float BatteryLowPulseStrength = 0.12f;
    constexpr float BatteryLowPulseSeconds = 0.22f;
    constexpr int BatteryLowRumbleFrames = 4;
    constexpr float BatteryLowRumbleStrength = 0.09f;
    constexpr float BatteryCriticalPulseStrength = 0.26f;
    constexpr float BatteryCriticalPulseSeconds = 0.34f;
    constexpr int BatteryCriticalRumbleFrames = 8;
    constexpr float BatteryCriticalRumbleStrength = 0.18f;
    // 電圧が一瞬落ちたときの知らせ（基本＋電池の弱り1のときに足す分）
    constexpr float VoltageDropPulseStrengthBase = 0.055f;
    constexpr float VoltageDropPulseStrengthStress = 0.095f;
    constexpr float VoltageDropPulseSecondsBase = 0.14f;
    constexpr float VoltageDropPulseSecondsStress = 0.08f;
    constexpr int VoltageDropRumbleFramesBase = 2;
    constexpr float VoltageDropRumbleFramesStress = 4.0f;
    constexpr float VoltageDropRumbleStrengthBase = 0.035f;
    constexpr float VoltageDropRumbleStrengthStress = 0.065f;
    // --- 懐中電灯の光線：照り返しの点光源を置く、光が最初に当たる面までの一番遠い距離。下を向いているとみなす傾き
    constexpr float FlashlightBounceReach = 180.0f;
    constexpr float FlashlightFloorRayMinSlope = 0.001f;
    // なめらかに近づける割合は「60fpsの1フレームでこの割合だけ近づく」で決め、フレームの長さに合わせて直している
    constexpr float ReferenceFramesPerSecond = 60.0f;
    constexpr float FlashlightNearSurfaceFollowPerFrame = 0.18f;
    // 電池の弱りで光の色を暖かくする量（赤を少し上げ、緑と青を下げる。Stress は電圧の揺れ、Weak は残量の少なさ）
    constexpr float FlashlightStressRedGain = 0.04f;
    constexpr float FlashlightStressGreenLoss = 0.06f;
    constexpr float FlashlightWeakGreenLoss = 0.10f;
    constexpr float FlashlightStressBlueLoss = 0.18f;
    constexpr float FlashlightWeakBlueLoss = 0.32f;
    // 懐中電灯の外の環境光：点けている間は光の外を暗く沈め、消すと照明の光だまりの外はほとんど闇にする。電力が戻ると少し明るい
    const Color FlashlightOnAmbient(0.070f, 0.072f, 0.072f, 1.0f);
    const Color FlashlightOffAmbient(0.045f, 0.047f, 0.050f, 1.0f);
    const Color PoweredFlashlightOnAmbient(0.095f, 0.097f, 0.100f, 1.0f);
    const Color PoweredFlashlightOffAmbient(0.070f, 0.072f, 0.076f, 1.0f);
    // --- 懐中電灯の照り返し：明るさ（懐中電灯の明るさに対する割合、一番遠いときの割合）、面から手前に置く距離と一番近い距離、
    // 置ける一番高い所、届く範囲（基本＋面までの距離に比例）、跳ね返った光の色、位置と明るさを近づける割合
    constexpr float BounceIntensityScale = 0.42f;
    constexpr float BounceFarIntensity = 0.25f;
    constexpr float BounceSurfaceOffset = 10.0f;
    constexpr float BounceMinDistance = 2.0f;
    constexpr float BounceMaxY = -59.0f;
    constexpr float BounceRangeBase = 75.0f;
    constexpr float BounceRangePerDistance = 0.45f;
    constexpr float BounceGreenTint = 0.94f;
    constexpr float BounceBlueTint = 0.84f;
    constexpr float BouncePositionFollowPerFrame = 0.25f;
    constexpr float BounceIntensityFollowPerFrame = 0.20f;
}

// 20%・10%を切った瞬間と、電圧が一瞬落ちたときに画面と振動で知らせている。このフレームの光の状態を返している
FlashlightSystem::FrameState Player::UpdateFlashlightPower(float deltaTime)
{
    if (!m_IsHiding && !m_AttractMode && (Input::GetKeyTrigger(VK_F) ||
        Input::GetButtonTrigger(XINPUT_Y)))
    {
        if (m_FlashlightBlocked)
        {
            // 点かない間は、スイッチの音だけが空しく鳴る
            Core::Game::GetInstance()->PlayAudioCue(SOUND_CUE_FLASHLIGHT, FlashlightOffClickPitch);
        }
        else if (m_Flashlight.Toggle())
        {
            m_FlashlightToggledThisFrame = true;
            Core::Game::GetInstance()->PlayAudioCue(
                SOUND_CUE_FLASHLIGHT,
                m_Flashlight.IsOn() ? FlashlightOnClickPitch : FlashlightOffClickPitch);
        }
    }

    const auto warningLevel = m_Flashlight.UpdateBattery(deltaTime);
    if (warningLevel != FlashlightSystem::BatteryWarning::None)
    {
        const bool critical = warningLevel == FlashlightSystem::BatteryWarning::Critical;
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            critical ? BatteryCriticalPulseStrength : BatteryLowPulseStrength,
            critical ? BatteryCriticalPulseSeconds : BatteryLowPulseSeconds);
        Input::SetVibration(
            critical ? BatteryCriticalRumbleFrames : BatteryLowRumbleFrames,
            critical ? BatteryCriticalRumbleStrength : BatteryLowRumbleStrength);
    }
    // このフレームの光の強さと、一瞬暗くなる状態を受け取っている
    const FlashlightSystem::FrameState flashlightState =
        m_Flashlight.UpdateFrameState(deltaTime);
    if (flashlightState.voltageDropStarted)
    {
        const float batteryStress = flashlightState.batteryStress;
        Core::Game::GetInstance()->GetPostProcess()->TriggerHorrorPulse(
            VoltageDropPulseStrengthBase + batteryStress * VoltageDropPulseStrengthStress,
            VoltageDropPulseSecondsBase + batteryStress * VoltageDropPulseSecondsStress);
        Input::SetVibration(
            VoltageDropRumbleFramesBase + static_cast<int>(batteryStress * VoltageDropRumbleFramesStress),
            VoltageDropRumbleStrengthBase + batteryStress * VoltageDropRumbleStrengthStress);
    }
    return flashlightState;
}

// 目の位置から視線の方向へ線を伸ばし、光が最初に当たる壁・床までの距離を求めている
// （FlashlightSurfaceSearchDistance までは近い面で光を弱めるのに、FlashlightBounceReach までは照り返しの位置に使っている）
Player::FlashlightBeam Player::TraceFlashlightBeam(const Camera& camera, float yaw,
    const std::vector<Wall*>& walls, bool visibleLight) const
{
    FlashlightBeam beam;
    beam.Origin = m_Position;
    beam.Origin.y += m_CameraHeightOffset;
    beam.HitDistance = FlashlightBounceReach;
    beam.ClosestSurfaceDistance = FlashlightSurfaceSearchDistance;
    if (!visibleLight)
    {
        return beam;
    }

    const float cameraPitch = camera.GetCameraPitch();
    const float pitchCos = cosf(cameraPitch);
    beam.Direction = Vector3(
        sinf(yaw) * pitchCos,
        sinf(cameraPitch),
        cosf(yaw) * pitchCos);
    beam.Direction.Normalize();
    const Vector3 beamEnd =
        beam.Origin + beam.Direction * FlashlightBounceReach;

    for (const Wall* wall : walls)
    {
        if (wall == nullptr)
        {
            continue;
        }

        float hitDistance = 0.0f;
        if (wall->IntersectsInteractionSegment(
            beam.Origin, beamEnd, hitDistance))
        {
            beam.HitDistance = (std::min)(beam.HitDistance, hitDistance);
        }
    }

    // 床はWallとは別に描いているため、下を向いたときは床の高さの平面との距離も計算している。
    if (beam.Direction.y < -FlashlightFloorRayMinSlope)
    {
        const float floorDistance =
            (MIN_Y_POSITION - beam.Origin.y) / beam.Direction.y;
        if (floorDistance >= 0.0f)
        {
            beam.HitDistance = (std::min)(beam.HitDistance, floorDistance);
        }
    }
    beam.ClosestSurfaceDistance = (std::min)(beam.ClosestSurfaceDistance, beam.HitDistance);
    return beam;
}

// シェーダーへ渡す懐中電灯の光（スポットライト）と、懐中電灯の外の環境光を作っている。
// 壁や床が近いときは、懐中電灯の明るさを下げている（明るさが一定だと、近くの面が白く飛んで、材質の細かい模様が消えるため）
LIGHT Player::BuildFlashlightLight(const FlashlightSystem::FrameState& state, float closestSurfaceDistance,
    float batteryWeakness, float deltaTime)
{
    const bool renderFlashlight = state.render;
    const float batteryStress = state.batteryStress;

    // 近い面までの距離が12〜54の間で、近いほど光を弱め、照らす距離も短くしている（急に変わらないよう、なめらかに近づけている）
    const float nearSurfaceTarget = state.visible
        ? MathUtility::Proximity(closestSurfaceDistance, FlashlightNearSurfaceDistance, FlashlightNearSurfaceRange)
        : 0.0f;
    const float nearSurfaceResponse = 1.0f - std::pow(
        1.0f - FlashlightNearSurfaceFollowPerFrame, deltaTime * ReferenceFramesPerSecond);
    m_FlashlightNearSurfaceBlend +=
        (nearSurfaceTarget - m_FlashlightNearSurfaceBlend) *
        nearSurfaceResponse;

    LIGHT light{};

    light.Enable = TRUE;
    light.FlashlightEnabled = renderFlashlight ? TRUE : FALSE;
    // 近い面で弱める量は最大2割。以前は4割強だったが、壁・小物の懐中電灯の拡散光に上限を付けて（flashlightLighting.hlsli の
    // GetFlashlightDiffuseResponse）近くの壁が白く飛ばなくなったため、弱めすぎて「壁に近づくと何も見えない」状態になっていた
    const float proximityExposure =
        1.0f - m_FlashlightNearSurfaceBlend * FlashlightNearSurfaceDim;
    light.Intensity = renderFlashlight
        ? FlashlightIntensity * state.output * proximityExposure *
            state.powerBlend *
            (1.0f - batteryWeakness * FlashlightWeakDim)
        : 0.0f;
    light.Range = (FlashlightRange - m_FlashlightNearSurfaceBlend * FlashlightNearSurfaceRangeCut) * (1.0f - batteryWeakness * FlashlightWeakRangeCut);
    // 手で持ったライトの小さな揺れを加えている。電池が少ないときは視点は動かさず、
    // 光だけに電気的・機械的な不安定さを足している。
    constexpr float FlashlightSwayBase = 0.0035f;
    constexpr float FlashlightSwayBatteryStress = 0.0065f;
    constexpr float FlashlightSwayHorizontalRadiansPerSecond = 1.37f;
    constexpr float FlashlightSwayVerticalRadiansPerSecond = 1.91f;
    constexpr float FlashlightSwayVerticalPhase = 1.2f;
    constexpr float FlashlightSwayVerticalScale = 0.72f;
    const float flashlightSway = renderFlashlight
        ? FlashlightSwayBase + batteryStress * FlashlightSwayBatteryStress
        : 0.0f;
    Vector3 flashlightDirection(
        sinf(m_AmbienceTimer * FlashlightSwayHorizontalRadiansPerSecond) * flashlightSway,
        sinf(m_AmbienceTimer * FlashlightSwayVerticalRadiansPerSecond + FlashlightSwayVerticalPhase) *
            flashlightSway * FlashlightSwayVerticalScale,
        1.0f);
    flashlightDirection.Normalize();
    light.Direction = Vector4(
        flashlightDirection.x,
        flashlightDirection.y,
        flashlightDirection.z,
        0.0f);
    // スポットライトの形：14度の内側は全部の明るさ、32度の外側で消え、その間は1.8乗で暗くしている（照らした輪の外がすぐ闇になる）。
    // 審査会で「暗くて見えない」と言われ、11度・26度から広げた（輪の外の闇は残し、照らせる範囲だけを広げている）
    light.SpotParams = Vector4(
        cosf(DirectX::XMConvertToRadians(FlashlightInnerConeDegrees)),
        cosf(DirectX::XMConvertToRadians(FlashlightOuterConeDegrees)),
        FlashlightConeFalloff,
        batteryWeakness
    );

    if (renderFlashlight)
    {
        // 電池が弱るほど光を暖かい色（弱った電球のような黄色）へ寄せ、残りが少ないことを色でも伝えている。
        light.Diffuse = Color(
            m_LightDiffuseR * (1.0f + batteryStress * FlashlightStressRedGain),
            m_LightDiffuseG * (1.0f - batteryStress * FlashlightStressGreenLoss - batteryWeakness * FlashlightWeakGreenLoss),
            m_LightDiffuseB * (1.0f - batteryStress * FlashlightStressBlueLoss - batteryWeakness * FlashlightWeakBlueLoss),
            1.0f);
        // 懐中電灯を点けている間は、光の外を暗く沈め、照らした所（と天井の照明の光だまり）だけが見えるようにしている
        light.Ambient = FlashlightOnAmbient;
    }
    else
    {
        light.Diffuse = Color(0.0f, 0.0f, 0.0f, 1.0f);
        // 懐中電灯を消すと、照明の光だまりの外はほとんど闇（壁の輪郭がかすかに分かる程度）
        light.Ambient = FlashlightOffAmbient;
    }

    // 電力が戻ったら、環境光を少し明るくしている（それでも照明の間には暗がりが残る程度にしている）
    if (Core::Game::GetInstance()->IsPowerRestored())
    {
        light.Ambient = light.FlashlightEnabled ? PoweredFlashlightOnAmbient : PoweredFlashlightOffAmbient;
    }
    return light;
}

// 懐中電灯の照り返し：光が最初に当たった面から少し手前に、弱い点光源を置いている。
// 本物の懐中電灯は、照らした壁や床で跳ね返った光が部屋全体をぼんやり明るくする（近い面ほど強い）。
// 面から少し手前に置き、光の筋（horrorMist.hlsli。高さ-58より上の光源だけ）が出ないよう、高さは BounceMaxY までにしている
void Player::UpdateFlashlightBounce(const LIGHT& light, bool renderFlashlight, const FlashlightBeam& beam, float deltaTime)
{
    float targetIntensity = 0.0f;
    if (renderFlashlight && beam.HitDistance < FlashlightBounceReach)
    {
        const float closeness = 1.0f - beam.HitDistance / FlashlightBounceReach;
        targetIntensity = light.Intensity * BounceIntensityScale *
            (BounceFarIntensity + (1.0f - BounceFarIntensity) * closeness * closeness);
        Vector3 bouncePosition = beam.Origin + beam.Direction * (std::max)(beam.HitDistance - BounceSurfaceOffset, BounceMinDistance);
        bouncePosition.y = (std::min)(bouncePosition.y, BounceMaxY);
        const float follow = m_HasBouncePosition
            ? 1.0f - std::pow(1.0f - BouncePositionFollowPerFrame, deltaTime * ReferenceFramesPerSecond)
            : 1.0f;
        m_BouncePosition += (bouncePosition - m_BouncePosition) * follow;
        m_HasBouncePosition = true;
        m_BounceRange = BounceRangeBase + beam.HitDistance * BounceRangePerDistance;
        // 跳ね返った光は、壁や床の色を少し帯びて暖かくなる
        m_BounceColor = Vector3(light.Diffuse.x, light.Diffuse.y * BounceGreenTint, light.Diffuse.z * BounceBlueTint);
    }
    const float response = 1.0f - std::pow(1.0f - BounceIntensityFollowPerFrame, deltaTime * ReferenceFramesPerSecond);
    m_BounceIntensity += (targetIntensity - m_BounceIntensity) * response;
}

// 開発用の電池の回復（Debug版の B キー。3面は開発用プレビューなので、Releaseでも B キーとコントローラーの BACK で使える）
void Player::HandleDebugBatteryKey()
{
    const bool stage3Preview = Core::Game::GetInstance()->GetCurrentSceneName() == SceneName::Stage3;
#ifdef _DEBUG
    constexpr bool allowBatteryKey = true;
#else
    const bool allowBatteryKey = stage3Preview;
#endif
    if ((allowBatteryKey && Input::GetKeyTrigger(VK_B)) ||
        (stage3Preview && Input::GetButtonTrigger(XINPUT_BACK)))
    {
        AddBattery(DebugBatteryRefill);
    }
}

// 一人称のときは懐中電灯を持った右手だけを、デバッグ用の三人称のときはメッシュを描いている。
void Player::CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
{
    if (m_BounceIntensity < MinimumBounceIntensity || !m_HasBouncePosition)
    {
        return;
    }
    ENVIRONMENT_POINT_LIGHT bounce{};
    bounce.PositionRange = Vector4(m_BouncePosition.x, m_BouncePosition.y, m_BouncePosition.z, m_BounceRange);
    bounce.ColorIntensity = Vector4(m_BounceColor.x, m_BounceColor.y, m_BounceColor.z, m_BounceIntensity);
    lights.push_back(bounce);
}
