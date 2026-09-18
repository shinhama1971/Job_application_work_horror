// ============================================================================
// ファイルの役割: カリング、影、水面反射、本描画の順序を管理します。
// 主な技術: マルチパス描画、シャドウマップ、視錐台カリング、ポストプロセス
// ============================================================================

#include "Game.h"
#include "Scene.h"
#include "Renderer.h"
#include "Texture2D.h"
#include "ScreenDustOverlay.h"
#include "Player.h"
#include "DebugUI.h"

#include <algorithm>
#include <cmath>

namespace
{
    WorldBoundingSphere GetConservativeCullingBounds(const Object& object)
    {
        if (object.HasModelBounds())
        {
            WorldBoundingSphere bounds = object.GetWorldBoundingSphere();
            // 既存の画面端余白を維持し、実モデルBoundsでも急な消失を防ぎます。
            bounds.Radius += 3.0f;
            return bounds;
        }

        const DirectX::SimpleMath::Vector3 scale = object.GetScale();
        WorldBoundingSphere bounds;
        bounds.Center = object.GetPosition();
        // コード生成メッシュは従来どおりScaleの半対角と余白を使います。
        bounds.Radius = 0.5f * std::sqrt(
            scale.x * scale.x + scale.y * scale.y + scale.z * scale.z) + 3.0f;
        return bounds;
    }

    bool IsVisibleToCamera(
        const Object& object,
        const Camera& camera,
        bool testVertical)
    {
        if (!object.UsesCameraCulling())
        {
            return true;
        }

        const WorldBoundingSphere bounds =
            GetConservativeCullingBounds(object);
        return camera.IsSphereVisible(
            bounds.Center, bounds.Radius, testVertical);
    }

    bool IsRelevantToShadowMap(const Object& object, const Camera& camera)
    {
        const WorldBoundingSphere bounds =
            GetConservativeCullingBounds(object);
        const float radius = bounds.Radius;
        const DirectX::SimpleMath::Vector3 offset =
            bounds.Center - camera.GetPosition();
        // 投影far=280の境界に大きな物体の一部が掛かる場合を残すため、
        // 中心距離の判定には半径と固定余白を加えます。
        const float shadowRange = 292.0f + radius;
        const float distanceSquared =
            offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
        return distanceSquared <= shadowRange * shadowRange &&
            camera.IsSphereVisible(bounds.Center, radius + 18.0f, false);
    }

    bool IsNearEnoughForReflection(const Object& object, const Camera& camera)
    {
        const WorldBoundingSphere bounds =
            GetConservativeCullingBounds(object);
        const float radius = bounds.Radius;
        const DirectX::SimpleMath::Vector3 offset =
            bounds.Center - camera.GetPosition();
        // 1/6解像度では判別できない遠景を省きます。大型物だけはradius分を
        // 判定距離へ加え、背景になる壁が欠けないようにします。
        const float reflectionRange = 230.0f + radius;
        return offset.x * offset.x + offset.y * offset.y + offset.z * offset.z <=
            reflectionRange * reflectionRange;
    }

}

namespace Core
{
    // 影・反射などの事前パスを必要なフレームだけ更新し、
    // 本描画をPostProcessへ取り込んでからHUDとデバッグUIを重ねます。
    void Game::Draw()
    {
        Debug::UI::BeginFrame();
        ID3D11DeviceContext* context = Renderer::GetDeviceContext();
        m_Instance->m_GpuTimer.BeginFrame(context);
        unsigned int mainDrawn = 0;
        unsigned int mainCulled = 0;
        unsigned int shadowDrawn = 0;
        unsigned int shadowCulled = 0;
        unsigned int reflectionDrawn = 0;
        unsigned int reflectionCulled = 0;
        bool reflectionSkipped = false;
        const unsigned int shadowInterval =
            m_Instance->m_Settings.GetEffectLevel() >= 2
                ? 1u
                : (m_Instance->m_Settings.GetEffectLevel() == 1 ? 2u : 3u);
        const bool updateShadow =
            (m_Instance->m_ShadowFrameIndex++ % shadowInterval) == 0u;
        if (updateShadow)
        {
            m_Instance->m_GpuTimer.BeginPass(GpuPass::Shadow, context);
            m_Instance->m_ShadowMap.Begin(m_Instance->m_Camera);
            for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
            {
                if (o->IsDestroy() || !o->CastsShadow())
                {
                    continue;
                }

                if (!IsRelevantToShadowMap(*o, m_Instance->m_Camera))
                {
                    ++shadowCulled;
                    continue;
                }

                ++shadowDrawn;
                o->DrawShadow();
            }
            m_Instance->m_ShadowMap.End();
            m_Instance->m_GpuTimer.EndPass(GpuPass::Shadow, context);
        }
        else
        {
            m_Instance->m_GpuTimer.SkipPass(GpuPass::Shadow);
            m_Instance->m_ShadowMap.Bind();
        }

        if (m_Instance->m_CurrentScene == SceneName::Stage)
        {
            bool reflectionVisible = false;
            for (const auto& object :
                m_Instance->m_ObjectManager.GetAllObjects())
            {
                if (!object->IsDestroy() &&
                    object->IsPlanarReflectionSurfaceVisible(
                        m_Instance->m_Camera))
                {
                    reflectionVisible = true;
                    break;
                }
            }

            // 移動中は反射カメラも動くため毎フレーム更新します。静止中は反射像を
            // 再利用し、ドアや照明の変化を拾うためだけに低頻度で更新します。
            const DirectX::SimpleMath::Vector3 reflectionCameraPosition =
                m_Instance->m_Camera.GetPosition();
            const DirectX::SimpleMath::Vector3 reflectionCameraForward =
                m_Instance->m_Camera.GetForward();
            const DirectX::SimpleMath::Vector3 reflectionPositionDelta =
                reflectionCameraPosition -
                m_Instance->m_LastReflectionCameraPosition;
            const bool reflectionCameraMoved =
                !m_Instance->m_HasReflectionCameraPose ||
                reflectionPositionDelta.LengthSquared() > 0.03f * 0.03f ||
                reflectionCameraForward.Dot(
                    m_Instance->m_LastReflectionCameraForward) < 0.99998f;
            const unsigned int minimumReflectionInterval =
                m_Instance->m_Settings.GetEffectLevel() == 0 ? 2u : 1u;
            const unsigned int movingReflectionInterval = (std::max)(
                Debug::UI::GetReflectionUpdateInterval(),
                minimumReflectionInterval);
            const unsigned int idleReflectionInterval =
                m_Instance->m_Settings.GetEffectLevel() >= 2
                    ? 3u
                    : (m_Instance->m_Settings.GetEffectLevel() == 1 ? 6u : 8u);
            const unsigned int reflectionInterval = reflectionCameraMoved
                ? movingReflectionInterval
                : (std::max)(movingReflectionInterval, idleReflectionInterval);
            // 水面が画面へ入った最初のフレームは直ちに更新し、古い反射を見せません。
            const bool updateReflection = reflectionVisible &&
                (!m_Instance->m_WasReflectionVisible ||
                    (m_Instance->m_ReflectionFrameIndex++ %
                        reflectionInterval) == 0u);
            if (updateReflection)
            {
                m_Instance->m_GpuTimer.BeginPass(
                    GpuPass::Reflection, context);
                m_Instance->m_PlanarReflection.Begin(
                    m_Instance->m_Camera,
                    -99.5f);

                for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
                {
                    if (o->IsDestroy() ||
                        !o->ContributesToPlanarReflection())
                    {
                        continue;
                    }

                    // 反射カメラは上下が反転するため、左右・前後・距離だけを判定します。
                    // これにより縦方向の誤判定を避けつつ廊下後方を大きく削減できます。
                    if (!IsVisibleToCamera(*o, m_Instance->m_Camera, false) ||
                        !IsNearEnoughForReflection(*o, m_Instance->m_Camera))
                    {
                        ++reflectionCulled;
                        continue;
                    }

                    ++reflectionDrawn;
                    o->Draw(&m_Instance->m_Camera);
                }

                m_Instance->m_PlanarReflection.End(
                    m_Instance->m_Camera);
                m_Instance->m_GpuTimer.EndPass(
                    GpuPass::Reflection, context);
                m_Instance->m_LastReflectionCameraPosition =
                    reflectionCameraPosition;
                m_Instance->m_LastReflectionCameraForward =
                    reflectionCameraForward;
                m_Instance->m_HasReflectionCameraPose = true;
            }
            else
            {
                m_Instance->m_GpuTimer.SkipPass(GpuPass::Reflection);
                m_Instance->m_PlanarReflection.Bind();
                reflectionSkipped = !reflectionVisible;
            }
            m_Instance->m_WasReflectionVisible = reflectionVisible;
        }
        else
        {
            m_Instance->m_GpuTimer.SkipPass(GpuPass::Reflection);
            m_Instance->m_WasReflectionVisible = false;
            m_Instance->m_HasReflectionCameraPose = false;
        }

        m_Instance->m_GpuTimer.BeginPass(GpuPass::MainScene, context);
        Renderer::DrawStart();

        for (auto& o : m_Instance->m_ObjectManager.GetAllObjects())
        {
            if (!o->IsDestroy())
            {
                if (!IsVisibleToCamera(*o, m_Instance->m_Camera, true))
                {
                    ++mainCulled;
                    continue;
                }

                ++mainDrawn;
                o->Draw(&m_Instance->m_Camera);
            }
        }
        m_Instance->m_GpuTimer.EndPass(GpuPass::MainScene, context);

        Debug::UI::SetCullingStats(
            mainDrawn,
            mainCulled,
            shadowDrawn,
            shadowCulled,
            reflectionDrawn,
            reflectionCulled,
            reflectionSkipped);

        m_Instance->m_GpuTimer.BeginPass(GpuPass::PostProcess, context);
        m_Instance->m_PostProcess.CaptureBackBuffer();
        m_Instance->m_PostProcess.Draw(&m_Instance->m_GpuTimer);
        m_Instance->m_GpuTimer.EndPass(GpuPass::PostProcess, context);

        // ブルーム後にHUDと画面表示を描き、文字の輪郭がぼけないようにします。
        if (m_Instance->m_Scene)
        {
            m_Instance->m_Scene->Draw(&m_Instance->m_Camera);
        }
        Debug::UI::Draw(m_Instance->m_PostProcess);

        m_Instance->m_GpuTimer.EndFrame(context);
        Renderer::DrawEnd();
    }
}
