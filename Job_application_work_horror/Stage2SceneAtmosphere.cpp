// ============================================================================
// ファイルの役割: 2面の廊下の画面効果と扉の表示灯を更新する。
// ============================================================================

#include "Stage2Scene.h"
#include "Stage2SceneConstants.h"
#include "CeilingLight.h"
#include "Door.h"
#include "Game.h"
#include "Player.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "ShadowMan.h"
#include "Wall.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // --- 廊下の画面効果（UpdateCorridorAtmosphere）。各値は「基本＋周回の進み具合1・暗さ1・危険度1のときに足す分」
    // 周回の色調が追いつく速さ（毎秒）、照明の明るさが届く廊下の向きの距離、緊張のゆっくりした揺れ（速さ・大きさ）
    constexpr float LoopDepthFollowSpeed = 2.5f;
    constexpr float FixtureLightReach = 105.0f;
    constexpr float TensionPulseSpeed = 1.7f;
    constexpr float TensionPulseAmount = 0.025f;
    constexpr float TensionBase = 0.38f;
    constexpr float TensionPerLoop = 0.42f;
    constexpr float TensionPerThreat = 0.14f;
    constexpr float TensionMax = 0.92f;
    constexpr float NoiseBase = 0.20f;
    constexpr float NoisePerLoop = 0.10f;
    constexpr float NoisePerDarkness = 0.018f;
    constexpr float NoisePerThreat = 0.025f;
    constexpr float VignetteBase = 0.62f;
    constexpr float VignettePerLoop = 0.12f;
    constexpr float VignettePerDarkness = 0.035f;
    constexpr float VignettePerThreat = 0.045f;
    // 目が慣れたような露出（ライトを点けているとき・消しているとき）
    constexpr float FlashlightOnExposure = 1.00f;
    constexpr float FlashlightOnExposurePerDarkness = 0.035f;
    constexpr float FlashlightOffExposure = 1.055f;
    constexpr float FlashlightOffExposurePerDarkness = 0.090f;
    constexpr float DistortionBase = 0.20f;
    constexpr float DistortionPerLoop = 0.34f;
    constexpr float FilmGradeBase = 0.58f;
    constexpr float FilmGradePerLoop = 0.20f;
    constexpr float LensDirtBase = 0.14f;
    constexpr float LensDirtPerLoop = 0.08f;
    // 2面の始めの露出（電力が戻った、少し暗い廊下）と、2面を出るときに戻すレンズの歪み（作った直後と同じ強さ）
    constexpr float StartExposure = 0.95f;
    // 信号の乱れ：信号盤の数、足音の影が近いときに乱れ始める危険度と強まり方と上限、信号盤パズルの間の乱れ
    constexpr float StalkerInterferenceStartThreat = 0.52f;
    constexpr float StalkerInterferencePerThreat = 0.72f;
    constexpr float StalkerInterferenceMax = 0.30f;
    constexpr float SignalInterferenceBase = 0.10f;
    constexpr float SignalInterferencePerThreat = 0.62f;
    constexpr float SignalInterferenceUnresolved = 0.16f;
    constexpr float SignalInterferenceMax = 0.88f;
    constexpr float VolumetricBase = 0.38f;
    constexpr float VolumetricPerLoop = 0.20f;
    constexpr float VolumetricPerDarkness = 0.055f;
    // --- 扉のランプ：鍵がかかっている間は赤くゆっくり、外れたら緑で速く点滅させる（点滅の速さ、色、光沢）
    constexpr float LockedIndicatorPulseSpeed = 3.4f;
    constexpr float OpenIndicatorPulseSpeed = 6.2f;
    const Color LockedIndicatorDiffuse(0.24f, 0.012f, 0.008f, 1.0f);
    constexpr float LockedIndicatorRedBase = 0.13f;
    constexpr float LockedIndicatorRedPulse = 0.08f;
    constexpr float LockedIndicatorGreen = 0.001f;
    constexpr float LockedIndicatorShininess = 24.0f;
    const Color OpenIndicatorDiffuse(0.04f, 0.18f, 0.035f, 1.0f);
    constexpr float OpenIndicatorRed = 0.015f;
    constexpr float OpenIndicatorGreenBase = 0.16f;
    constexpr float OpenIndicatorGreenPulse = 0.10f;
    constexpr float OpenIndicatorBlue = 0.008f;
    constexpr float OpenIndicatorShininess = 28.0f;
}

void Stage2Scene::InitializeCorridorAtmosphere()
{
    Core::Game* game = Core::Game::GetInstance();
    game->GetPostProcess()->SetCorridorTension(TensionBase);
    game->GetPostProcess()->SetExposure(StartExposure);
    game->GetPostProcess()->SetAtmosphere(NoiseBase, VignetteBase);
    game->GetPostProcess()->SetLensDistortionStrength(DistortionBase);
    game->GetPostProcess()->SetFilmGradeStrength(FilmGradeBase);
    game->GetPostProcess()->SetSignalInterference(0.0f);
    game->GetPostProcess()->SetVolumetricLight(true);
    game->GetPostProcess()->SetVolumetricIntensity(VolumetricBase);
}

// 画面効果を、周回の進み具合・近くの照明の明るさ・危険度に合わせて変えている
void Stage2Scene::UpdateCorridorAtmosphere(Player& player, float deltaTime)
{
    Core::Game* game = Core::Game::GetInstance();
    const float loopRate = static_cast<float>(m_LoopCount) / static_cast<float>(Stage2FinalLoopCount);
    const float threat = m_NoiseThreatSystem.GetThreat();
    // P.T. のように、周回が進むほど廊下の色調を赤黒く沈め、霧を濃くしている（扉を抜けた直後の暗転の間に、約1秒で変わる）
    m_LoopDepth += (loopRate - m_LoopDepth) * (std::min)(deltaTime * LoopDepthFollowSpeed, 1.0f);
    Renderer::SetLoopDepth(m_LoopDepth);
    // 一番明るく照らしている近くの照明（廊下の向きの距離で弱める）から、今いる所の暗さを求めている
    float localFixtureLight = 0.0f;
    for (CeilingLight* fixture : m_Objects.lights)
    {
        if (fixture == nullptr)
        {
            continue;
        }

        const float longitudinalDistance = std::fabs(
            fixture->GetPosition().z - player.GetPosition().z);
        const float proximity = 1.0f - (std::min)(
            longitudinalDistance / FixtureLightReach, 1.0f);
        localFixtureLight = (std::max)(
            localFixtureLight,
            fixture->GetBrightness() * proximity);
    }
    const float localDarkness =
        1.0f - (std::clamp)(localFixtureLight, 0.0f, 1.0f);
    const float pulse = std::sin(m_VisualTimer * TensionPulseSpeed) * TensionPulseAmount;
    game->GetPostProcess()->SetCorridorTension(
        (std::clamp)(TensionBase + loopRate * TensionPerLoop +
            threat * TensionPerThreat + pulse, 0.0f, TensionMax));
    game->GetPostProcess()->SetAtmosphere(
        NoiseBase + loopRate * NoisePerLoop + localDarkness * NoisePerDarkness + threat * NoisePerThreat,
        VignetteBase + loopRate * VignettePerLoop + localDarkness * VignettePerDarkness + threat * VignettePerThreat);
    // ライトを消しているときと暗い場所では、目が慣れたように露出を上げている
    const float adaptedExposure = player.IsFlashlightOn()
        ? FlashlightOnExposure + localDarkness * FlashlightOnExposurePerDarkness
        : FlashlightOffExposure + localDarkness * FlashlightOffExposurePerDarkness;
    game->GetPostProcess()->SetExposure(adaptedExposure);
    game->GetPostProcess()->SetLensDistortionStrength(DistortionBase + loopRate * DistortionPerLoop);
    game->GetPostProcess()->SetFilmGradeStrength(FilmGradeBase + loopRate * FilmGradePerLoop);
    game->GetPostProcess()->SetLensDirtStrength(LensDirtBase + loopRate * LensDirtPerLoop);
    // 信号盤パズルの間と、足音の影が近いときは、監視映像のような信号の乱れを出している
    const bool signalRestorationActive =
        m_LoopCount >= Stage2FinalLoopCount && !m_SignalPuzzle.IsComplete();
    const float unresolvedSignalRate =
        1.0f - static_cast<float>(m_SignalPuzzle.GetStep()) / static_cast<float>(Stage2SignalTerminalCount);
    ShadowMan* activeNoiseShadow = m_Objects.noiseShadow;
    const float stalkerInterference =
        activeNoiseShadow != nullptr && activeNoiseShadow->IsActive()
            ? (std::clamp)((threat - StalkerInterferenceStartThreat) * StalkerInterferencePerThreat,
                0.0f, StalkerInterferenceMax)
            : 0.0f;
    game->GetPostProcess()->SetSignalInterference(
        signalRestorationActive
            ? (std::clamp)(SignalInterferenceBase + threat * SignalInterferencePerThreat +
                unresolvedSignalRate * SignalInterferenceUnresolved, 0.0f, SignalInterferenceMax)
            : stalkerInterference);
    game->GetPostProcess()->SetVolumetricLight(player.IsFlashlightOn());
    game->GetPostProcess()->SetVolumetricIntensity(
        VolumetricBase + loopRate * VolumetricPerLoop + localDarkness * VolumetricPerDarkness);
}

// 扉のランプ：鍵がかかっている間は赤く、外れたら緑でゆっくり点滅させている
void Stage2Scene::UpdateDoorIndicator(Door* corridorDoor)
{
    Wall* doorIndicator = m_Objects.doorIndicator;
    if (doorIndicator == nullptr || m_FinalDoorReady)
    {
        return;
    }
    const bool locked = corridorDoor != nullptr && corridorDoor->IsLocked();
    const float indicatorPulse = std::sin(
        m_VisualTimer * (locked ? LockedIndicatorPulseSpeed : OpenIndicatorPulseSpeed)) * 0.5f + 0.5f;
    if (locked)
    {
        doorIndicator->SetAppearance(
            LockedIndicatorDiffuse,
            Color(LockedIndicatorRedBase + indicatorPulse * LockedIndicatorRedPulse,
                LockedIndicatorGreen, 0.0f, 1.0f), LockedIndicatorShininess);
    }
    else
    {
        doorIndicator->SetAppearance(
            OpenIndicatorDiffuse,
            Color(OpenIndicatorRed, OpenIndicatorGreenBase + indicatorPulse * OpenIndicatorGreenPulse,
                OpenIndicatorBlue, 1.0f), OpenIndicatorShininess);
    }
}
